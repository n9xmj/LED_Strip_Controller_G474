/******************************************************************************
 * fs_shell_hrn.h
 *
 * Test-harness 'R' op — remote filesystem shell API (host-fs-shell plan W9/I1).
 *
 *   R                enter persistent fileops REPL until Q/EXIT/0xA5
 *   R <MN> [args]    one-shot mnemonic (HIL); returns to outer harness
 *
 * Mnemonics: LS ST RM MD RN MO UM FM PU GT NOP. Scalar paths only.
 * Bulk PU/GT: xmodem-spirited binary after text ready.
 * Note: harness 'F' is TX flush — do not reuse it.
 ******************************************************************************/

#ifndef FS_SHELL_HRN_H
#define FS_SHELL_HRN_H

/**
 * @brief Harness 'R' entry. Empty @p pc_arg → fileops REPL; else one-shot.
 */
extern void v_fs_shell_hrn_op(const char *pc_arg);

#endif /* FS_SHELL_HRN_H */
