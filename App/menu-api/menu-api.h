/******************************************************************************
 * menu-api.h
 ******************************************************************************/

#pragma once

#include <stdint.h>

// MENU_API_PROMPT will be output after every menu command execution attempt.
// #define externally to "" for no prompt.

#ifndef MENU_API_PROMPT
#define MENU_API_PROMPT       "{Ready}:"
#endif

//------------------------------------------------------------------------------

typedef enum __attribute__((packed))
{
    MENU_ITEM_END_OF_LIST,          // This should be 1st enum listed (0)
    MENU_ITEM_IGNORE,
    MENU_ITEM_HELP_TEXT_FIXED,
    MENU_ITEM_HELP_TEXT_VARIABLE,
    MENU_ITEM_HELP,
    MENU_ITEM_HELP_HIDDEN,
    MENU_ITEM_FUNCTION,
    MENU_ITEM_KEY_FUNCTION,
    MENU_ITEM_KEY_LIST_FUNCTION,
    MENU_ITEM_GOTO_MENU,
    MENU_ITEM_CALL_MENU,
    MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
    MENU_ITEM_RETURN_TO_HOME_MENU,
    MENU_ITEM_MAX_VALUE_FOR_SIZEOF_1 = 0xFF
}
menu_item_type_t;

typedef struct menu_item_s menu_item_t;

struct menu_item_s
{
    menu_item_type_t    x_type;
    char                c_key;
    uint8_t             b_not_implemented;
    uint8_t             b_no_newline;
    union
    {
        const char      *p_c_text;
        const char      *p_c_key_list;
    };
    union
    {
        void (*pfn_function)(void);
        void (*pfn_key_function)(char);
        void (*pfn_key_list_function)(char, uint8_t);
        void (*pfn_help_text_function)(void);
        const menu_item_t *p_x_menu;
    };
};

typedef struct
{
    const menu_item_t   **pap_x_menu;
    uint8_t             u8_stack_depth;
    uint8_t             u8_stack_index;
    uint8_t             u8_reserved1;
    uint8_t             u8_reserved2;
}
menu_control_t;

//------------------------------------------------------------------------------

extern char *pc_char_to_str(char c_ch, char *p_c_str);

extern void v_menu_init(menu_control_t *p_x_menu_control,
                        const menu_item_t *p_x_home_menu,
                        void *p_v_menu_stack,
                        uint8_t u8_stack_depth);
extern void v_menu_exec(menu_control_t *p_x_menu_control, char c_key);
