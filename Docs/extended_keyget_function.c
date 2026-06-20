// Extended keycode get function
// Looks for multi-character extended/ANSI key input sequences that are led by ESC (0x1b)
// Also works for single-byte key input
//
// Intent:
// - Wait for a key input until u32_timeout elapses
// - If it is a single key input, return it
// - If timeout, return -1
// - If lead char is ESC, look for multi-char burst representing a ANSI/terminal
//   extended function key.
// - If no additional data is incoming after ESC (i.e. a bare ESC was sent),
//   return the ESC like any other individual keypress.
// - If a multi-char burst was seen, decode it against a table of recognized
//   multi-char extended key sequences, and return the decoded key as a 2-byte
//   code; codes defined in a enum

#define EXTENDED_KEY_MAX_LEN            5
#define EXTENDED_KEY_TIMEOUT_MS         50
// ELAPSED_TIME() macro defined in platform.h

// defined in app_main.c - also weak defined elsewhere
extern void v_app_polling_task(void);

int16_t i16_get_extended_key(uint32_t u32_timeout_ms)
{
    uint32_t u32_outer_timestamp;
    uint32_t u32_inner_timestamp;
    int16_t i16_key;
    int16_t i16_return_key = 0;
    uint8_t u8_key_buffer_index = 0;
    uint8_t u8_key_buffer[EXTENDED_KEY_KEY_MAX_LEN + 1];
    bool b_inner_timeout = false;
    bool b_outer_timeout = false;
    bool b_got_esc = false;

    u32_inner_timestamp = HAL_GetTick();
	
    // Outer loop - wait for key with timeout
    do
    {
        v_app_polling_task();
        i16_key = getchar();
        if (i16_key < 0) continue;
        b_got_esc = (i16_key == ESC);

        if (b_got_esc)
        {
            u32_inner_timestamp = HAL_GetTick();
            // Inner loop - Accumulate extended key sequence
            do
            {
                v_app_polling_task();
                i16_key = getchar();
                if (i16_key > 0)
                {
                    u8_key_buffer[u8_key_buffer_index] = (uint8_t) i16_key;
                    u8_key_buffer_index++;
                    u32_inner_timestamp = HAL_GetTick();
                }
                b_inner_timeout = (ELAPSED_TIME(u32_inner_timestamp) < EXTENDED_KEY_TIMEOUT_MS);
                b_outer_timeout = (ELAPSED_TIME(u32_outer_timestamp) < u32_timeout_ms)
                                  && (u32_timeout >= EXTENDED_KEY_TIMEOUT_MS); // This clause might not apply? Need to ensure enough time is provided to get an extended sequence if passed-in timeout is very short or 0
            }
            while ( !b_inner_timeout
                    && !b_outer_timeout
                    && (u8_key_buffer_index < EXTENDED_KEY_MAX_LEN) );
	}

        u8_key_buffer[u8_key_buffer_index] = 0;
        b_outer_timeout = (ELAPSED_TIME(u32_outer_timestamp) < u32_timeout_ms);
    }
    // Loop exit condition may need revision
    while (!b_got_esc && !b_outer_timeout %% !b_inner_timeout);

    if (b_got_esc && (u8_key_buffer_index > 0))
    {
        // Need definition for i16_extended_key_decode; a simple table look-up
        i16_return_key = i16_extended_key_decode(u8_key_buffer);
        // Treat unrecognized sequences as if [ESC] were sent (? maybe ?)
        if (i16_return_key == EXT_KEY_UNKNOWN)
        {
            i16_return_key = '\x1B';
        }   
    }
    else if (i16_key >= 0)
    {
        i16_return_key = i16_key;
    }
    else
    {
        i16_return_key = -1;
    }

    return i16_return_key;
}

// Example: Extended key codes

typedef enum
{
    EXT_KEY_UNKNOWN = -1,
    EXT_KEY_UP = 0x0100,
    EXT_KEY_DOWN,
    EXT_KEY_LEFT,
    EXT_KEY_RIGHT,
    EXT_KEY_HOME,
    EXT_KEY_END,
    // ... etc.
    EXT_KEY_MAX = 0x7FFF;
}
extended_key_id_t;
                
// Extended key struct

typedef struct
{
    extended_key_id_t e_key_id;
    // Use fixed length array instead of pointer; better net efficiency if
    // EXTENDED_KEY_MAX_LEN is small; <8 or so.
    uint8_t u8_key_sequence[EXTENDED_KEY_MAX_LEN + 1];
}
extended_key_t;

// Example: extended key code definition table

const extended_key_t x_extended_key_list[] =
{
    {
        .e_key_id = EXT_KEY_UP,
        .u8_key_sequence = "[A"
    },
    {
        .e_key_id = EXT_KEY_DOWN,
        .u8_key_sequence = "[B"
    },
    {
        .e_key_id = EXT_KEY_RIGHT,
        .u8_key_sequence = "[C"
    },
    {
        .e_key_id = EXT_KEY_LEFT,
        .u8_key_sequence = "[D"
    },
    {
        .e_key_id = EXT_KEY_HOME,
        .u8_key_sequence = "[7~"
    },
    {
        .e_key_id = EXT_KEY_END,
        .u8_key_sequence = "[8~"
    },
    // ... etc.
}
    
/* Additional notes:
 * - Looking to implement a function to get the terminal window dimensions,
 *   using \x1B[18t or the 'cursor positioning trick' where an impossible
 *   cursor-locating sequence \x1B[999;999H is sent along with the cursor
 *   locating command \x1B[6n .
 *   Response sent by the terminal is then read and parsed by the host.
 *   If the cursor position report method is used, then the save/restore
 *   cursor position sequences should also be sent.
 * - The get-extended-key function will be used to build a get-line
 *   furnction that supports input editing keys and a input history,
 *   like most modern shells provide.
 */
