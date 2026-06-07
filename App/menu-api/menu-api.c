/******************************************************************************
 * menu-api.c
 ******************************************************************************/

#include "menu-api.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>


/******************************************************************************
 *
 ******************************************************************************/

char *pc_char_to_str(char c_ch, char *p_c_str)
{
    if (p_c_str == NULL)
    {
        return NULL;
    }

    c_ch &= 0x7F;
    if (c_ch == '\r')
    {
        strcpy(p_c_str, "Ent");
    }
    else if (c_ch == 0x1B)
    {
        strcpy(p_c_str, "ESC");
    }
    else if (c_ch < 0x20)
    {
        p_c_str[0] = '^';
        p_c_str[1] = (char) (c_ch + '@');
        p_c_str[2] = 0;
    }
    else
    {
        p_c_str[0] = c_ch;
        p_c_str[1] = 0;
    }

    return p_c_str;
}

/******************************************************************************
 *
 ******************************************************************************/

static const char * const p_c_no_description = "--- no description ---";

void v_menu_init(menu_control_t *p_x_menu_control,
                 const menu_item_t *p_x_home_menu,
                 void *p_v_menu_stack,
                 uint8_t u8_stack_depth)
{
    if ( (p_x_menu_control == NULL) ||
         (p_x_home_menu == NULL) )
    {
        return;
    }

    if (u8_stack_depth == 0)
    {
        u8_stack_depth = 1;
    }
    if (p_v_menu_stack == NULL)
    {
        p_v_menu_stack = calloc(u8_stack_depth, sizeof(void *));
    }

    memset(p_x_menu_control, 0, sizeof(menu_control_t));
    p_x_menu_control->u8_stack_index = 0;
    p_x_menu_control->pap_x_menu = (const menu_item_t **) p_v_menu_stack;
    p_x_menu_control->pap_x_menu[0] = p_x_home_menu;
    p_x_menu_control->u8_stack_depth = u8_stack_depth;
}

/******************************************************************************
 *
 ******************************************************************************/

bool b_menu_key_conflict_check(const menu_item_t *menu)
{
    uint16_t u16_outer_index;
    uint16_t u16_inner_index;
    bool b_key_conflict = false;
    const char *p_c_text_outer;
    const char *p_c_text_inner;

    u16_outer_index = 0;
    while (menu[u16_outer_index].x_type != MENU_ITEM_END_OF_LIST)
    {
        if (menu[u16_outer_index].c_key != 0)
        {
            u16_inner_index = u16_outer_index + 1;
            while (menu[u16_inner_index].x_type != MENU_ITEM_END_OF_LIST)
            {
                if (menu[u16_inner_index].c_key != 0)
                {
                    if (menu[u16_outer_index].c_key == menu[u16_inner_index].c_key)
                    {
                        b_key_conflict = true;
                        p_c_text_outer = menu[u16_outer_index].p_c_text;
                        if (p_c_text_outer == NULL)
                        {
                            p_c_text_outer = p_c_no_description;
                        }
                        p_c_text_inner = menu[u16_inner_index].p_c_text;
                        if (p_c_text_inner == NULL)
                        {
                            p_c_text_inner = p_c_no_description;
                        }
                        printf("WARNING: Menu items share the same key definition [%c]:\r\n"
                               "%s\r\n"
                               "%s\r\n",
                               menu[u16_outer_index].c_key,
                               p_c_text_outer,
                               p_c_text_inner);
                    }
                }
                u16_inner_index++;
            }
        }
        u16_outer_index++;
    }

    return b_key_conflict;
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_newline(uint8_t u8_no_newline)
{
    if (!u8_no_newline)
    {
        putchar('\r');
        putchar('\n');
    }
}

void v_menu_help(const menu_item_t *p_x_menu_list)
{
    const menu_item_t *p_x_entry;
    const char *p_c_text;
    bool b_print_entry;
    char ac_key[4];

    if (p_x_menu_list == NULL)
    {
        return;
    }

    p_x_entry = p_x_menu_list;
    while (p_x_entry->x_type != MENU_ITEM_END_OF_LIST)
    {
        p_c_text = p_x_entry->p_c_text;
        pc_char_to_str(p_x_entry->c_key, ac_key);
        b_print_entry = true;

        switch (p_x_entry->x_type)
        {
            case MENU_ITEM_HELP_TEXT_FIXED:
                b_print_entry = false;
                if (p_x_entry->p_c_text != NULL)
                {
                    printf("%s", p_c_text);
                    v_newline(p_x_entry->b_no_newline);
                }
                break;

            case MENU_ITEM_HELP_TEXT_VARIABLE:
                b_print_entry = false;
                if (p_x_entry->p_c_text != NULL)
                {
                    printf("%s", p_c_text);
                    v_newline(p_x_entry->b_no_newline);
                }
                if (p_x_entry->pfn_help_text_function != NULL)
                {
                    p_x_entry->pfn_help_text_function();
                }
                break;

            case MENU_ITEM_HELP:
                if (p_x_entry->p_c_text == NULL)
                {
                    p_c_text = "Help - show this menu";
                }
                break;

            case MENU_ITEM_HELP_HIDDEN:
                if (p_x_entry->p_c_text == NULL)
                {
                    b_print_entry = false;
                }
                break;

            case MENU_ITEM_FUNCTION:
            case MENU_ITEM_KEY_FUNCTION:
                if (p_x_entry->p_c_text == NULL)
                {
                    b_print_entry = false;
                }
                break;

            case MENU_ITEM_KEY_LIST_FUNCTION:
                b_print_entry = false;
                break;

            case MENU_ITEM_GOTO_MENU:
                if (p_x_entry->p_c_text == NULL)
                {
                    p_c_text = "Go to another menu";
                }
                break;

            case MENU_ITEM_CALL_MENU:
                if (p_x_entry->p_c_text == NULL)
                {
                    p_c_text = "Go to submenu";
                }
                break;

            case MENU_ITEM_RETURN_TO_PREVIOUS_MENU:
                if (p_x_entry->p_c_text == NULL)
                {
                    p_c_text = "Return to previous menu";
                }
                break;

            case MENU_ITEM_RETURN_TO_HOME_MENU:
                if (p_x_entry->p_c_text == NULL)
                {
                    p_c_text = "Return to home (main) menu";
                }
                break;

            case MENU_ITEM_IGNORE:
            default:
                b_print_entry = false;
                break;
        }

        if (b_print_entry)
        {
            printf("[%s] %s", ac_key, p_c_text);
            v_newline(p_x_entry->b_no_newline);
        }

        p_x_entry++;
    }

    b_menu_key_conflict_check(p_x_menu_list);
    printf("\r\n");
}

/******************************************************************************
 *
 ******************************************************************************/

void v_menu_exec(menu_control_t *p_x_menu_control, char c_key)
{
    const menu_item_t *p_x_current_menu;
    const menu_item_t *p_x_entry;
    bool b_found_match = false;
    bool b_report_not_implemented = false;
    uint8_t u8_index;

    if (p_x_menu_control == NULL)
    {
        return;
    }

    p_x_current_menu = p_x_menu_control->pap_x_menu[p_x_menu_control->u8_stack_index];

    if (p_x_current_menu == NULL)
    {
        return;
    }

    if (c_key == 0xFF)
    {
        v_menu_help(p_x_current_menu);
        goto POST_EXEC_PROMPT;
    }

    c_key &= 0x7F;
    p_x_entry = p_x_current_menu;

    while ((p_x_entry->x_type != MENU_ITEM_END_OF_LIST) && !b_found_match)
    {
        // Special handling for MENU_ITEM_KEY_LIST_FUNCTION
        // Scan p_x_entry->p_c_text (aka p_c_key_list) string for match with c_key
        if ( (p_x_entry->x_type == MENU_ITEM_KEY_LIST_FUNCTION)
             && (p_x_entry->p_c_key_list != NULL) )
        {
            u8_index = 0;
            while ((u8_index < 0xFF) && (p_x_entry->p_c_key_list[u8_index] != 0))
            {
                if (p_x_entry->p_c_key_list[u8_index] == c_key)
                {
                    b_found_match = true;
                    break;
                }
                u8_index++;
            }
            if (b_found_match)
            {
                if (p_x_entry->b_not_implemented
                    || (p_x_entry->pfn_key_list_function == NULL))
                {
                    b_report_not_implemented = true;
                }
                else
                {
                    p_x_entry->pfn_key_list_function(c_key, u8_index);
                }
            }
        }

        if ((p_x_entry->c_key != 0) && (p_x_entry->c_key == c_key))
        {
            b_found_match = true;
            switch (p_x_entry->x_type)
            {
                case MENU_ITEM_HELP:
                case MENU_ITEM_HELP_HIDDEN:
                    v_menu_help(p_x_current_menu);
                    break;

                case MENU_ITEM_FUNCTION:
                    if (p_x_entry->b_not_implemented || (p_x_entry->pfn_function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_entry->pfn_function();
                    }
                    break;

                case MENU_ITEM_KEY_FUNCTION:
                    if (p_x_entry->b_not_implemented || (p_x_entry->pfn_key_function == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_entry->pfn_key_function(c_key);
                    }
                    break;

                case MENU_ITEM_KEY_LIST_FUNCTION:
                    break;

                case MENU_ITEM_GOTO_MENU:
                    if (p_x_entry->b_not_implemented || (p_x_entry->p_x_menu == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        p_x_menu_control->pap_x_menu[p_x_menu_control->u8_stack_index] = p_x_entry->p_x_menu;
                        v_menu_help(p_x_entry->p_x_menu);
                    }
                    break;

                case MENU_ITEM_CALL_MENU:
                    if (p_x_entry->b_not_implemented || (p_x_entry->p_x_menu == NULL))
                    {
                        b_report_not_implemented = true;
                    }
                    else
                    {
                        if (p_x_menu_control->u8_stack_index < (p_x_menu_control->u8_stack_depth - 1))
                        {
                            p_x_menu_control->u8_stack_index++;
                        }
                        else
                        {
                            printf("WARNING: Menu stack full\r\n");
                        }
                        p_x_menu_control->pap_x_menu[p_x_menu_control->u8_stack_index] = p_x_entry->p_x_menu;
                        v_menu_help(p_x_entry->p_x_menu);
                    }
                    break;

                case MENU_ITEM_RETURN_TO_PREVIOUS_MENU:
                    if (p_x_menu_control->u8_stack_index > 0)
                    {
                        p_x_menu_control->u8_stack_index--;
                    }
                    else
                    {
                        printf("WARNING: Menu stack empty\r\n");
                    }
                    // Optional cleanup / exit callback (e.g. stop active synth on leaving i submenu).
                    // The pfn_function slot is unused by RETURN items (GOTO/CALL use p_x_menu),
                    // so we repurpose it here without changing struct size.
                    if (p_x_entry->pfn_function != NULL)
                    {
                        p_x_entry->pfn_function();
                    }
                    v_menu_help(p_x_menu_control->pap_x_menu[p_x_menu_control->u8_stack_index]);
                    break;

                case MENU_ITEM_RETURN_TO_HOME_MENU:
                    p_x_menu_control->u8_stack_index = 0;
                    // Optional cleanup / exit callback (see comment above).
                    if (p_x_entry->pfn_function != NULL)
                    {
                        p_x_entry->pfn_function();
                    }
                    v_menu_help(p_x_menu_control->pap_x_menu[0]);
                    break;

                case MENU_ITEM_IGNORE:
                case MENU_ITEM_HELP_TEXT_FIXED:
                case MENU_ITEM_HELP_TEXT_VARIABLE:
                default:
                    // Do nothing
                    break;
            }

            // Found matching entry for key and executed command
            // Exit from loop
            break;
        }

        p_x_entry++;
    }

    if (! b_found_match)
    {
        char ac_key[4];
        pc_char_to_str(c_key, ac_key);
        printf("Selection [%s] not recognized\r\n", ac_key);
    }
    else if (b_report_not_implemented)
    {
        printf("Not implemented yet\r\n");
    }
    else
    {
        // No action
    }

POST_EXEC_PROMPT:
#ifdef MENU_API_PROMPT
    printf("%s", MENU_API_PROMPT);
#endif
}
