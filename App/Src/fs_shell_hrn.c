/******************************************************************************
 * fs_shell_hrn.c
 *
 * Harness 'R' — remote FS shell (host-fs-shell plan).
 *
 * Modes:
 *   R <MN> args…   one-shot op (HIL / scripts), then return to harness
 *   R              enter persistent fileops REPL until Q / EXIT / 0xA5
 *
 * Note: harness letter F is TX flush — do not reuse for FS.
 ******************************************************************************/

#include "fs_shell_hrn.h"

#include "vfs.h"
#include "filesystem.h"
#include "crc32.h"
#include "platform.h"
#include "utils.h"
#include "test_harness.h"   /* HARNESS_EXIT */
#include "uart_stream.h"    /* binary xfer must not use getchar (0 = empty) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>

/* Debug console stream — binary path uses this, not newlib getchar/putchar. */
extern uart_stream_h_t x_app_debug_console_handle(void);

/* ---- binary chunk protocol (xmodem-spirit; not XModem-compatible) -------- */
#define R_SOH           0x82u   /* STX | 0x80 */
#define R_ACK           0x06u
#define R_NAK           0x15u
#define R_CAN           0x18u
#define R_CHUNK_MAX     256u
#define R_PKT_TIMEOUT_MS  2000u
#define R_MAX_RETRIES     8u
/* Fileops inner-REPL idle (ms). Long: host may sit at its own prompt. */
#define R_FS_IDLE_TIMEOUT_MS  600000u
#define R_FS_LINE_MAX         160u

/* ---- helpers ------------------------------------------------------------ */

static const char *pc_next_tok(const char *pc, char *pc_buf, uint32_t u32_bufsz)
{
    uint32_t u32_i = 0u;

    while ((*pc == ' ') || (*pc == '\t')) { pc++; }
    while ((*pc != '\0') && (*pc != ' ') && (*pc != '\t'))
    {
        if (u32_i < (u32_bufsz - 1u)) { pc_buf[u32_i++] = *pc; }
        pc++;
    }
    pc_buf[u32_i] = '\0';
    return pc;
}

static void v_ensure_vfs(void)
{
    (void)x_fs_device_init();
    v_vfs_attach(p_x_fs_table());
}

/*
 * Binary RX/TX must use uart_stream, not getchar/putchar:
 *   __io_getchar() returns 0 when the RX ring is empty — same as a 0x00 data
 *   byte — and i_ch > 0 would drop NULs and mis-handle the binary stream.
 */

/* Cooperative single-byte read with timeout. Returns 0..255 or -1 on timeout. */
static int i_read_byte_to(uint32_t u32_timeout_ms)
{
    uart_stream_h_t h = x_app_debug_console_handle();
    uint32_t        u32_t0 = HAL_GetTick();

    for (;;)
    {
        int16_t i16_ch;

        v_app_polling_task();
        i16_ch = i16_uart_stream_rx_byte(h);
        if (i16_ch >= 0)
        {
            return (int)(uint8_t)i16_ch;
        }
        if (ELAPSED_TIME(u32_t0) >= u32_timeout_ms)
        {
            return -1;
        }
    }
}

static void v_write_byte(uint8_t u8)
{
    uart_stream_h_t h = x_app_debug_console_handle();
    /* Drain any pending printf text first so ACK is not stuck behind stdio. */
    (void)fflush(stdout);
    v_uart_stream_tx_byte_blocking(h, u8);
}

static void v_write_bytes(const uint8_t *pu8, uint32_t u32_n)
{
    uart_stream_h_t h = x_app_debug_console_handle();
    (void)fflush(stdout);
    v_uart_stream_tx_multi_blocking(h, pu8, (uint16_t)u32_n);
}

/* Read exactly n bytes or fail. Returns 0 OK, -1 timeout/CAN. */
static int i_read_exact(uint8_t *pu8, uint32_t u32_n)
{
    uint32_t i;
    for (i = 0u; i < u32_n; i++)
    {
        int i_ch = i_read_byte_to(R_PKT_TIMEOUT_MS);
        if (i_ch < 0) { return -1; }
        if ((uint8_t)i_ch == R_CAN) { return -1; }
        pu8[i] = (uint8_t)i_ch;
    }
    return 0;
}

static uint32_t u32_le(const uint8_t *pu8)
{
    return ((uint32_t)pu8[0])
         | ((uint32_t)pu8[1] << 8)
         | ((uint32_t)pu8[2] << 16)
         | ((uint32_t)pu8[3] << 24);
}

static void v_store_le32(uint8_t *pu8, uint32_t u32)
{
    pu8[0] = (uint8_t)(u32 & 0xFFu);
    pu8[1] = (uint8_t)((u32 >> 8) & 0xFFu);
    pu8[2] = (uint8_t)((u32 >> 16) & 0xFFu);
    pu8[3] = (uint8_t)((u32 >> 24) & 0xFFu);
}

/* Receive one data packet (payload into @p pu8_pay, max R_CHUNK_MAX).
 * *pu8_seq out, *pu32_len out. Returns 0 OK, 1 EOT (len 0), -1 fatal/cancel. */
static int i_recv_data_pkt(uint8_t *pu8_seq, uint8_t *pu8_pay, uint32_t *pu32_len)
{
    uint8_t  au8_hdr[4];
    uint8_t  au8_crc[4];
    uint32_t u32_len;
    uint32_t u32_crc_rx;
    uint32_t u32_crc;
    int      i_ch;
    uint32_t u32_try;

    for (u32_try = 0u; u32_try < R_MAX_RETRIES; u32_try++)
    {
        /* Resync: wait for SOH */
        do
        {
            i_ch = i_read_byte_to(R_PKT_TIMEOUT_MS);
            if (i_ch < 0)
            {
                v_write_byte(R_NAK);
                break;
            }
            if ((uint8_t)i_ch == R_CAN) { return -1; }
        } while ((uint8_t)i_ch != R_SOH);

        if (i_ch < 0) { continue; }

        if (i_read_exact(au8_hdr, 3u) != 0)
        {
            v_write_byte(R_NAK);
            continue;
        }
        *pu8_seq = au8_hdr[0];
        u32_len  = (uint32_t)au8_hdr[1] | ((uint32_t)au8_hdr[2] << 8);
        if (u32_len > R_CHUNK_MAX)
        {
            v_write_byte(R_NAK);
            continue;
        }
        if (u32_len > 0u)
        {
            if (i_read_exact(pu8_pay, u32_len) != 0)
            {
                v_write_byte(R_NAK);
                continue;
            }
        }
        if (i_read_exact(au8_crc, 4u) != 0)
        {
            v_write_byte(R_NAK);
            continue;
        }
        u32_crc_rx = u32_le(au8_crc);
        v_crc32_init();
        u32_crc = (u32_len > 0u) ? u32_crc32(pu8_pay, u32_len) : u32_crc32(au8_hdr, 0u);
        if (u32_crc != u32_crc_rx)
        {
            v_write_byte(R_NAK);
            continue;
        }
        *pu32_len = u32_len;
        v_write_byte(R_ACK);
        return (u32_len == 0u) ? 1 : 0;
    }
    return -1;
}

/* Send one data packet; wait for ACK. Returns 0 OK, -1 fail/cancel. */
static int i_send_data_pkt(uint8_t u8_seq, const uint8_t *pu8_pay, uint32_t u32_len)
{
    uint8_t  au8_hdr[4];
    uint8_t  au8_crc[4];
    uint32_t u32_crc;
    uint32_t u32_try;

    if (u32_len > R_CHUNK_MAX) { return -1; }

    au8_hdr[0] = R_SOH;
    au8_hdr[1] = u8_seq;
    au8_hdr[2] = (uint8_t)(u32_len & 0xFFu);
    au8_hdr[3] = (uint8_t)((u32_len >> 8) & 0xFFu);

    v_crc32_init();
    u32_crc = (u32_len > 0u) ? u32_crc32(pu8_pay, u32_len) : u32_crc32(au8_hdr, 0u);
    v_store_le32(au8_crc, u32_crc);

    for (u32_try = 0u; u32_try < R_MAX_RETRIES; u32_try++)
    {
        int i_ch;

        v_write_bytes(au8_hdr, 4u);
        if (u32_len > 0u) { v_write_bytes(pu8_pay, u32_len); }
        v_write_bytes(au8_crc, 4u);

        i_ch = i_read_byte_to(R_PKT_TIMEOUT_MS);
        if (i_ch < 0) { continue; }
        if ((uint8_t)i_ch == R_ACK) { return 0; }
        if ((uint8_t)i_ch == R_CAN) { return -1; }
        /* NAK or garbage -> retry */
    }
    return -1;
}

/* ---- binary PUT: host -> device ----------------------------------------- */

static void v_op_put(const char *pc_path, uint32_t u32_size)
{
    static uint8_t au8_chunk[R_CHUNK_MAX];
    int            i_fd;
    uint8_t        u8_expect = 0u;
    uint32_t       u32_got   = 0u;
    int            i_rc;

    i_fd = i_vfs_open(pc_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (i_fd < 0)
    {
        printf("<HRN R PU path=%s rc=%d err=open>\r\n", pc_path, i_fd);
        return;
    }

    printf("<HRN R PU ready path=%s size=%lu>\r\n", pc_path, (unsigned long)u32_size);
    (void)fflush(stdout);

    /* Wait for host GO (ACK) so host can drain the ready line first. */
    {
        int i_go = i_read_byte_to(R_PKT_TIMEOUT_MS);
        if ((i_go < 0) || ((uint8_t)i_go == R_CAN))
        {
            (void)i_vfs_close(i_fd);
            printf("<HRN R PU path=%s rc=-1 err=nogo>\r\n", pc_path);
            return;
        }
        /* Accept ACK as GO; ignore one stray if needed */
        if ((uint8_t)i_go != R_ACK)
        {
            i_go = i_read_byte_to(R_PKT_TIMEOUT_MS);
            if ((i_go < 0) || ((uint8_t)i_go != R_ACK))
            {
                (void)i_vfs_close(i_fd);
                printf("<HRN R PU path=%s rc=-1 err=nogo>\r\n", pc_path);
                return;
            }
        }
    }

    for (;;)
    {
        uint8_t  u8_seq;
        uint32_t u32_len;
        int      i_pk;

        i_pk = i_recv_data_pkt(&u8_seq, au8_chunk, &u32_len);
        if (i_pk < 0)
        {
            (void)i_vfs_close(i_fd);
            printf("<HRN R PU path=%s rc=-1 err=xfer>\r\n", pc_path);
            return;
        }
        if (i_pk == 1)
        {
            /* EOT */
            break;
        }
        if (u8_seq != u8_expect)
        {
            /* Wrong seq: ACK already sent for good CRC; still reject stream */
            (void)i_vfs_close(i_fd);
            printf("<HRN R PU path=%s rc=-1 err=seq>\r\n", pc_path);
            return;
        }
        if ((u32_size > 0u) && ((u32_got + u32_len) > u32_size))
        {
            (void)i_vfs_close(i_fd);
            printf("<HRN R PU path=%s rc=-1 err=overflow>\r\n", pc_path);
            return;
        }
        {
            lfs_ssize_t i_w = z_vfs_write(i_fd, au8_chunk, u32_len);
            if (i_w < 0 || (uint32_t)i_w != u32_len)
            {
                (void)i_vfs_close(i_fd);
                printf("<HRN R PU path=%s rc=%ld err=write>\r\n", pc_path, (long)i_w);
                return;
            }
        }
        u32_got += u32_len;
        u8_expect++;
    }

    i_rc = i_vfs_close(i_fd);
    if ((u32_size > 0u) && (u32_got != u32_size))
    {
        printf("<HRN R PU path=%s rc=-1 err=short got=%lu want=%lu>\r\n",
               pc_path, (unsigned long)u32_got, (unsigned long)u32_size);
        return;
    }
    printf("<HRN R PU path=%s rc=%d n=%lu>\r\n",
           pc_path, i_rc, (unsigned long)u32_got);
}

/* ---- binary GET: device -> host ----------------------------------------- */

static void v_op_get(const char *pc_path)
{
    static uint8_t au8_chunk[R_CHUNK_MAX];
    int            i_fd;
    lfs_soff_t     i_size;
    uint8_t        u8_seq = 0u;
    uint32_t       u32_left;
    int            i_go;

    i_fd = i_vfs_open(pc_path, O_RDONLY);
    if (i_fd < 0)
    {
        printf("<HRN R GT path=%s rc=%d err=open>\r\n", pc_path, i_fd);
        return;
    }
    i_size = z_vfs_fsize(i_fd);
    if (i_size < 0)
    {
        (void)i_vfs_close(i_fd);
        printf("<HRN R GT path=%s rc=%ld err=size>\r\n", pc_path, (long)i_size);
        return;
    }

    printf("<HRN R GT ready path=%s size=%ld>\r\n", pc_path, (long)i_size);
    (void)fflush(stdout);

    /* Host may send a go byte (ACK) before we stream; wait briefly. */
    i_go = i_read_byte_to(R_PKT_TIMEOUT_MS);
    if (i_go < 0)
    {
        (void)i_vfs_close(i_fd);
        printf("<HRN R GT path=%s rc=-1 err=nogo>\r\n", pc_path);
        return;
    }
    if ((uint8_t)i_go == R_CAN)
    {
        (void)i_vfs_close(i_fd);
        printf("<HRN R GT path=%s rc=-1 err=can>\r\n", pc_path);
        return;
    }

    u32_left = (uint32_t)i_size;
    while (u32_left > 0u)
    {
        uint32_t    u32_n = (u32_left > R_CHUNK_MAX) ? R_CHUNK_MAX : u32_left;
        lfs_ssize_t i_r   = z_vfs_read(i_fd, au8_chunk, u32_n);

        if (i_r < 0 || (uint32_t)i_r != u32_n)
        {
            v_write_byte(R_CAN);
            (void)i_vfs_close(i_fd);
            printf("<HRN R GT path=%s rc=%ld err=read>\r\n", pc_path, (long)i_r);
            return;
        }
        if (i_send_data_pkt(u8_seq, au8_chunk, u32_n) != 0)
        {
            (void)i_vfs_close(i_fd);
            printf("<HRN R GT path=%s rc=-1 err=xfer>\r\n", pc_path);
            return;
        }
        u32_left -= u32_n;
        u8_seq++;
    }

    /* EOT */
    if (i_send_data_pkt(u8_seq, NULL, 0u) != 0)
    {
        (void)i_vfs_close(i_fd);
        printf("<HRN R GT path=%s rc=-1 err=eot>\r\n", pc_path);
        return;
    }

    {
        int i_rc = i_vfs_close(i_fd);
        printf("<HRN R GT path=%s rc=%d n=%ld>\r\n", pc_path, i_rc, (long)i_size);
    }
}

/* ---- text ops ----------------------------------------------------------- */

static void v_op_ls(const char *pc_path)
{
    const char    *pc_rel = "/";
    lfs_t         *p_x_lfs;
    lfs_dir_t      x_dir;
    struct lfs_info x_info;
    int            i_rc;

    p_x_lfs = p_x_vfs_resolve(pc_path, &pc_rel);
    if (p_x_lfs == NULL)
    {
        printf("<HRN R LS path=%s rc=%d end>\r\n", pc_path, (int)LFS_ERR_NOENT);
        return;
    }
    i_rc = lfs_dir_open(p_x_lfs, &x_dir, pc_rel);
    if (i_rc == 0)
    {
        while (lfs_dir_read(p_x_lfs, &x_dir, &x_info) > 0)
        {
            printf("<HRN R ENT path=%s name=%s type=%d size=%lu>\r\n",
                   pc_path, x_info.name, (int)x_info.type,
                   (unsigned long)x_info.size);
        }
        (void)lfs_dir_close(p_x_lfs, &x_dir);
    }
    printf("<HRN R LS path=%s rc=%d end>\r\n", pc_path, i_rc);
}

static void v_op_st(const char *pc_path)
{
    struct lfs_info x_info;
    int             i_rc = i_vfs_stat(pc_path, &x_info);

    if (i_rc == 0)
    {
        printf("<HRN R ST path=%s rc=0 type=%d size=%lu name=%s>\r\n",
               pc_path, (int)x_info.type, (unsigned long)x_info.size, x_info.name);
    }
    else
    {
        printf("<HRN R ST path=%s rc=%d>\r\n", pc_path, i_rc);
    }
}

static void v_op_rm(const char *pc_path)
{
    int i_rc = i_vfs_remove(pc_path);
    printf("<HRN R RM path=%s rc=%d>\r\n", pc_path, i_rc);
}

static void v_op_md(const char *pc_path)
{
    int i_rc = i_vfs_mkdir(pc_path);
    printf("<HRN R MD path=%s rc=%d>\r\n", pc_path, i_rc);
}

static void v_op_rn(const char *pc_old, const char *pc_new)
{
    int i_rc = i_vfs_rename(pc_old, pc_new);
    printf("<HRN R RN old=%s new=%s rc=%d>\r\n", pc_old, pc_new, i_rc);
}

static void v_op_mo(const char *pc_lab)
{
    int i_rc = i_vfs_mount(pc_lab, false);
    printf("<HRN R MO label=%s rc=%d>\r\n", pc_lab, i_rc);
}

static void v_op_um(const char *pc_lab)
{
    int i_rc = i_vfs_unmount(pc_lab);
    printf("<HRN R UM label=%s rc=%d>\r\n", pc_lab, i_rc);
}

static void v_op_fm(const char *pc_lab)
{
    int i_rc = i_vfs_format(pc_lab);
    printf("<HRN R FM label=%s rc=%d>\r\n", pc_lab, i_rc);
}

/* ---- dispatch one command line (mnemonics, no leading 'R') -------------- */

/** @return true if caller should leave the fileops REPL. */
static bool b_dispatch_line(const char *pc_line)
{
    char        ac_mn[8];
    char        ac_a[96];
    char        ac_b[96];
    const char *pc;

    if (pc_line == NULL) { pc_line = ""; }

    pc = pc_next_tok(pc_line, ac_mn, (uint32_t)sizeof(ac_mn));
    if (ac_mn[0] == '\0')
    {
        return false;   /* empty line — ignore */
    }

    {
        char *p;
        for (p = ac_mn; *p != '\0'; p++)
        {
            *p = (char)toupper((unsigned char)*p);
        }
    }

    /* Leave fileops REPL (back to outer harness) */
    if ((strcmp(ac_mn, "Q") == 0)
        || (strcmp(ac_mn, "QUIT") == 0)
        || (strcmp(ac_mn, "EXIT") == 0)
        || (strcmp(ac_mn, "BYE") == 0))
    {
        printf("<HRN R FSEND>\r\n");
        return true;
    }

    /* Keepalive / probe recognized by host startup */
    if ((strcmp(ac_mn, "NOP") == 0) || (strcmp(ac_mn, "PING") == 0))
    {
        printf("<HRN R NOP rc=0>\r\n");
        return false;
    }

    if (strcmp(ac_mn, "LS") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        if (ac_a[0] == '\0') { printf("<HRN R LS rc=-1 err=nopath>\r\n"); return false; }
        v_op_ls(ac_a);
    }
    else if (strcmp(ac_mn, "ST") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        if (ac_a[0] == '\0') { printf("<HRN R ST rc=-1 err=nopath>\r\n"); return false; }
        v_op_st(ac_a);
    }
    else if (strcmp(ac_mn, "RM") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        if (ac_a[0] == '\0') { printf("<HRN R RM rc=-1 err=nopath>\r\n"); return false; }
        v_op_rm(ac_a);
    }
    else if (strcmp(ac_mn, "MD") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        if (ac_a[0] == '\0') { printf("<HRN R MD rc=-1 err=nopath>\r\n"); return false; }
        v_op_md(ac_a);
    }
    else if (strcmp(ac_mn, "RN") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        pc = pc_next_tok(pc, ac_b, (uint32_t)sizeof(ac_b));
        if ((ac_a[0] == '\0') || (ac_b[0] == '\0'))
        {
            printf("<HRN R RN rc=-1 err=args>\r\n");
            return false;
        }
        v_op_rn(ac_a, ac_b);
    }
    else if (strcmp(ac_mn, "MO") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        if (ac_a[0] == '\0') { printf("<HRN R MO rc=-1 err=nolabel>\r\n"); return false; }
        v_op_mo(ac_a);
    }
    else if (strcmp(ac_mn, "UM") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        if (ac_a[0] == '\0') { printf("<HRN R UM rc=-1 err=nolabel>\r\n"); return false; }
        v_op_um(ac_a);
    }
    else if (strcmp(ac_mn, "FM") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        if (ac_a[0] == '\0') { printf("<HRN R FM rc=-1 err=nolabel>\r\n"); return false; }
        v_op_fm(ac_a);
    }
    else if (strcmp(ac_mn, "PU") == 0)
    {
        char ac_sz[16];
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        pc = pc_next_tok(pc, ac_sz, (uint32_t)sizeof(ac_sz));
        if (ac_a[0] == '\0')
        {
            printf("<HRN R PU rc=-1 err=nopath>\r\n");
            return false;
        }
        {
            uint32_t u32_sz = (ac_sz[0] != '\0')
                              ? (uint32_t)strtoul(ac_sz, NULL, 10)
                              : 0u;
            v_op_put(ac_a, u32_sz);
        }
    }
    else if (strcmp(ac_mn, "GT") == 0)
    {
        pc = pc_next_tok(pc, ac_a, (uint32_t)sizeof(ac_a));
        if (ac_a[0] == '\0') { printf("<HRN R GT rc=-1 err=nopath>\r\n"); return false; }
        v_op_get(ac_a);
    }
    else
    {
        printf("<HRN R ERR verb=%s>\r\n", ac_mn);
    }
    return false;
}

/* ---- persistent fileops REPL -------------------------------------------- */

typedef enum
{
    R_LINE_OK = 0,
    R_LINE_QUIT,
    R_LINE_TIMEOUT
}
r_line_t;

static r_line_t x_fs_read_line(char *pc_buf, uint16_t u16_max)
{
    uint16_t u16_len = 0u;
    uint32_t u32_t0  = HAL_GetTick();

    for (;;)
    {
        int i_ch;

        v_app_polling_task();
        i_ch = getchar();

        if (i_ch <= 0)
        {
            if (ELAPSED_TIME(u32_t0) >= R_FS_IDLE_TIMEOUT_MS)
            {
                return R_LINE_TIMEOUT;
            }
            continue;
        }

        u32_t0 = HAL_GetTick();

        if ((uint8_t)i_ch == HARNESS_EXIT)
        {
            return R_LINE_QUIT;
        }

        if ((i_ch == '\r') || (i_ch == '\n'))
        {
            pc_buf[u16_len] = '\0';
            return R_LINE_OK;
        }

        if (u16_len < (u16_max - 1u))
        {
            pc_buf[u16_len++] = (char)i_ch;
        }
    }
}

static void v_fs_repl(void)
{
    static char s_ac_line[R_FS_LINE_MAX];

    printf("<HRN R FS>\r\n");
    (void)fflush(stdout);

    for (;;)
    {
        r_line_t x_ln = x_fs_read_line(s_ac_line, (uint16_t)sizeof(s_ac_line));

        if (x_ln == R_LINE_QUIT)
        {
            printf("<HRN R FSEND reason=exit>\r\n");
            break;
        }
        if (x_ln == R_LINE_TIMEOUT)
        {
            printf("<HRN R FSEND reason=timeout>\r\n");
            break;
        }
        if (b_dispatch_line(s_ac_line))
        {
            break;
        }
        /* Ready for next line — host may key off this */
        printf("<HRN R FS>\r\n");
        (void)fflush(stdout);
    }
}

/* ---- harness entry: one-shot or enter REPL ------------------------------ */

void v_fs_shell_hrn_op(const char *pc_arg)
{
    char ac_mn[8];

    if (pc_arg == NULL) { pc_arg = ""; }

    v_ensure_vfs();

    (void)pc_next_tok(pc_arg, ac_mn, (uint32_t)sizeof(ac_mn));

    /* Bare 'R' / empty → persistent fileops REPL */
    if (ac_mn[0] == '\0')
    {
        v_fs_repl();
        return;
    }

    /* One-shot still supported: R LS /lfs0 */
    (void)b_dispatch_line(pc_arg);
}
