/*
 * wee-command.c - WeeChat core commands
 *
 * Copyright (C) 2003-2023 Sébastien Helleu <flashcode@flashtux.org>
 * Copyright (C) 2005-2006 Emmanuel Bouthenot <kolter@openics.org>
 *
 * This file is part of WeeChat, the extensible chat client.
 *
 * WeeChat is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * WeeChat is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WeeChat.  If not, see <https://www.gnu.org/licenses/>.
 */

/* this define is needed for strptime() (not on OpenBSD/Sun) */
#if !defined(__OpenBSD__) && !defined(__sun)
#define _XOPEN_SOURCE 700
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "weechat.h"
#include "wee-command.h"
#include "wee-arraylist.h"
#include "wee-config.h"
#include "wee-config-file.h"
#include "wee-debug.h"
#include "wee-dir.h"
#include "wee-eval.h"
#include "wee-hashtable.h"
#include "wee-hdata.h"
#include "wee-hook.h"
#include "wee-input.h"
#include "wee-list.h"
#include "wee-log.h"
#include "wee-network.h"
#include "wee-proxy.h"
#include "wee-secure.h"
#include "wee-secure-buffer.h"
#include "wee-secure-config.h"
#include "wee-string.h"
#include "wee-upgrade.h"
#include "wee-utf8.h"
#include "wee-util.h"
#include "wee-version.h"
#include "../gui/gui-bar.h"
#include "../gui/gui-bar-item.h"
#include "../gui/gui-bar-item-custom.h"
#include "../gui/gui-buffer.h"
#include "../gui/gui-chat.h"
#include "../gui/gui-color.h"
#include "../gui/gui-cursor.h"
#include "../gui/gui-filter.h"
#include "../gui/gui-history.h"
#include "../gui/gui-hotlist.h"
#include "../gui/gui-input.h"
#include "../gui/gui-key.h"
#include "../gui/gui-layout.h"
#include "../gui/gui-line.h"
#include "../gui/gui-main.h"
#include "../gui/gui-mouse.h"
#include "../gui/gui-window.h"
#include "../plugins/plugin.h"
#include "../plugins/plugin-config.h"


extern char **environ;


/*
 * Callback for command "/allbuf": executes a command on all buffers.
 */

COMMAND_CALLBACK(allbuf)
{
    struct t_arraylist *all_buffers;
    struct t_gui_buffer *ptr_buffer;
    int i, list_size;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    COMMAND_MIN_ARGS(2, "");

    all_buffers = arraylist_new (gui_buffers_count, 0, 0,
                                 NULL, NULL, NULL, NULL);
    if (!all_buffers)
        COMMAND_ERROR;

    for (ptr_buffer = gui_buffers; ptr_buffer;
         ptr_buffer = ptr_buffer->next_buffer)
    {
        arraylist_add (all_buffers, ptr_buffer);
    }

    list_size = arraylist_size (all_buffers);
    for (i = 0; i < list_size; i++)
    {
        ptr_buffer = (struct t_gui_buffer *)arraylist_get (all_buffers, i);
        if (gui_buffer_valid (ptr_buffer))
            (void) input_data (ptr_buffer, argv_eol[1], NULL);
    }

    arraylist_free (all_buffers);

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/away".
 *
 * The command /away in core does nothing, so this function is empty.
 * Plugins that need /away command can use hook_command_run() to do something
 * when user issues the /away command.
 */

COMMAND_EMPTY(away)

/*
 * Displays a list of bars.
 */

void
command_bar_list (int full)
{
    struct t_gui_bar *ptr_bar;
    char str_size[16];

    if (gui_bars)
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, _("List of bars:"));
        for (ptr_bar = gui_bars; ptr_bar;
             ptr_bar = ptr_bar->next_bar)
        {
            snprintf (str_size, sizeof (str_size),
                      "%d", CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_SIZE]));
            if (full)
            {
                gui_chat_printf (NULL,
                                 /* TRANSLATORS: the last thing displayed is "width:" or "height:" with its value */
                                 _("  %s%s%s: %s%s%s (conditions: %s), %s, "
                                   "filling: %s(top/bottom)/%s(left/right), "
                                   "%s: %s"),
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 ptr_bar->name,
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 (CONFIG_BOOLEAN(ptr_bar->options[GUI_BAR_OPTION_HIDDEN])) ? _("(hidden)") : "",
                                 (CONFIG_BOOLEAN(ptr_bar->options[GUI_BAR_OPTION_HIDDEN])) ? " " : "",
                                 gui_bar_type_string[CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_TYPE])],
                                 (CONFIG_STRING(ptr_bar->options[GUI_BAR_OPTION_CONDITIONS])
                                  && CONFIG_STRING(ptr_bar->options[GUI_BAR_OPTION_CONDITIONS])[0]) ?
                                 CONFIG_STRING(ptr_bar->options[GUI_BAR_OPTION_CONDITIONS]) : "-",
                                 gui_bar_position_string[CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_POSITION])],
                                 gui_bar_filling_string[CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_FILLING_TOP_BOTTOM])],
                                 gui_bar_filling_string[CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_FILLING_LEFT_RIGHT])],
                                 ((CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_POSITION]) == GUI_BAR_POSITION_BOTTOM)
                                  || (CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_POSITION]) == GUI_BAR_POSITION_TOP)) ?
                                 _("height") : _("width"),
                                 (CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_SIZE]) == 0) ? _("auto") : str_size);
                gui_chat_printf (NULL,
                                 _("    priority: %d, fg: %s, bg: %s, bg_inactive: %s, items: %s%s"),
                                 CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_PRIORITY]),
                                 gui_color_get_name (CONFIG_COLOR(ptr_bar->options[GUI_BAR_OPTION_COLOR_FG])),
                                 gui_color_get_name (CONFIG_COLOR(ptr_bar->options[GUI_BAR_OPTION_COLOR_BG])),
                                 gui_color_get_name (CONFIG_COLOR(ptr_bar->options[GUI_BAR_OPTION_COLOR_BG_INACTIVE])),
                                 (CONFIG_STRING(ptr_bar->options[GUI_BAR_OPTION_ITEMS])
                                  && CONFIG_STRING(ptr_bar->options[GUI_BAR_OPTION_ITEMS])[0]) ?
                                 CONFIG_STRING(ptr_bar->options[GUI_BAR_OPTION_ITEMS]) : "-",
                                 (CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_SEPARATOR])) ?
                                 _(", with separator") : "");
            }
            else
            {
                gui_chat_printf (NULL,
                                 "  %s%s%s: %s%s%s, %s, %s: %s",
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 ptr_bar->name,
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 (CONFIG_BOOLEAN(ptr_bar->options[GUI_BAR_OPTION_HIDDEN])) ? _("(hidden)") : "",
                                 (CONFIG_BOOLEAN(ptr_bar->options[GUI_BAR_OPTION_HIDDEN])) ? " " : "",
                                 gui_bar_type_string[CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_TYPE])],
                                 gui_bar_position_string[CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_POSITION])],
                                 ((CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_POSITION]) == GUI_BAR_POSITION_BOTTOM)
                                  || (CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_POSITION]) == GUI_BAR_POSITION_TOP)) ?
                                 _("height") : _("width"),
                                 (CONFIG_INTEGER(ptr_bar->options[GUI_BAR_OPTION_SIZE]) == 0) ? _("auto") : str_size);
            }
        }
    }
    else
        gui_chat_printf (NULL, _("No bar defined"));
}

/*
 * Callback for command "/bar": manages bars.
 */

COMMAND_CALLBACK(bar)
{
    int i, type, position, number;
    long value;
    char *error, *str_type, *pos_condition, *name;
    struct t_gui_bar *ptr_bar;
    struct t_gui_bar_item *ptr_item;
    struct t_gui_window *ptr_window;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    /* list of bars */
    if ((argc == 1) || (string_strcmp (argv[1], "list") == 0))
    {
        command_bar_list (0);
        return WEECHAT_RC_OK;
    }

    /* full list of bars */
    if (string_strcmp (argv[1], "listfull") == 0)
    {
        command_bar_list (1);
        return WEECHAT_RC_OK;
    }

    /* list of bar items */
    if (string_strcmp (argv[1], "listitems") == 0)
    {
        if (gui_bar_items)
        {
            gui_chat_printf (NULL, "");
            gui_chat_printf (NULL, _("List of bar items:"));
            for (ptr_item = gui_bar_items; ptr_item;
                 ptr_item = ptr_item->next_item)
            {
                gui_chat_printf (NULL,
                                 _("  %s (plugin: %s)"),
                                 ptr_item->name,
                                 (ptr_item->plugin) ? ptr_item->plugin->name : "-");
            }
        }
        else
        {
            gui_chat_printf (NULL, _("No bar item defined"));
        }
        return WEECHAT_RC_OK;
    }

    /* add a new bar */
    if (string_strcmp (argv[1], "add") == 0)
    {
        COMMAND_MIN_ARGS(8, "add");
        pos_condition = strchr (argv[3], ',');
        if (pos_condition)
        {
            str_type = string_strndup (argv[3], pos_condition - argv[3]);
            pos_condition++;
        }
        else
        {
            str_type = strdup (argv[3]);
        }
        if (!str_type)
        {
            gui_chat_printf (NULL,
                             _("%sNot enough memory (%s)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             "/bar");
            return WEECHAT_RC_OK;
        }
        type = gui_bar_search_type (str_type);
        if (type < 0)
        {
            gui_chat_printf (NULL,
                             _("%sInvalid type \"%s\" for bar \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             str_type, argv[2]);
            free (str_type);
            return WEECHAT_RC_OK;
        }
        position = gui_bar_search_position (argv[4]);
        if (position < 0)
        {
            gui_chat_printf (NULL,
                             _("%sInvalid position \"%s\" for bar \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[4], argv[2]);
            free (str_type);
            return WEECHAT_RC_OK;
        }
        error = NULL;
        value = strtol (argv[5], &error, 10);
        (void) value;
        if (error && !error[0])
        {
            /* create bar */
            if (gui_bar_new (
                    argv[2],       /* name */
                    "0",           /* hidden */
                    "0",           /* priority */
                    str_type,      /* type */
                    (pos_condition) ? pos_condition : "",  /* conditions */
                    argv[4],       /* position */
                    "horizontal",  /* filling_top_bottom */
                    "vertical",    /* filling_left_right */
                    argv[5],       /* size */
                    "0",           /* size_max */
                    "default",     /* color fg */
                    "default",     /* color delim */
                    "default",     /* color bg */
                    "default",     /* color bg inactive */
                    argv[6],       /* separators */
                    argv_eol[7]))  /* items */
            {
                gui_chat_printf (NULL, _("Bar \"%s\" created"),
                                 argv[2]);
            }
            else
            {
                gui_chat_printf (NULL, _("%sFailed to create bar \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2]);
            }
        }
        else
        {
            gui_chat_printf (NULL,
                             _("%sInvalid size \"%s\" for bar \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[5], argv[2]);
            free (str_type);
            return WEECHAT_RC_OK;
        }
        free (str_type);

        return WEECHAT_RC_OK;
    }

    /* create default bars */
    if (string_strcmp (argv[1], "default") == 0)
    {
        if (argc > 2)
        {
            for (i = 2; i < argc; i++)
            {
                if (string_strcmp (argv[i], "input") == 0)
                    gui_bar_create_default_input ();
                else if (string_strcmp (argv[i], "title") == 0)
                    gui_bar_create_default_title ();
                else if (string_strcmp (argv[i], "status") == 0)
                    gui_bar_create_default_status ();
                else if (string_strcmp (argv[i], "nicklist") == 0)
                    gui_bar_create_default_nicklist ();
            }
        }
        else
            gui_bar_create_default ();
        return WEECHAT_RC_OK;
    }

    /* delete a bar */
    if (string_strcmp (argv[1], "del") == 0)
    {
        COMMAND_MIN_ARGS(3, "del");
        if (string_strcmp (argv[2], "-all") == 0)
        {
            gui_bar_free_all ();
            gui_chat_printf (NULL, _("All bars have been deleted"));
            gui_bar_create_default_input ();
        }
        else
        {
            ptr_bar = gui_bar_search (argv[2]);
            if (!ptr_bar)
            {
                gui_chat_printf (NULL,
                                 _("%sBar \"%s\" not found"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2]);
                return WEECHAT_RC_OK;
            }
            name = strdup (ptr_bar->name);
            gui_bar_free (ptr_bar);
            gui_chat_printf (NULL, _("Bar \"%s\" deleted"), name);
            if (name)
                free (name);
            gui_bar_create_default_input ();
        }
        return WEECHAT_RC_OK;
    }

    /* set a bar property */
    if (string_strcmp (argv[1], "set") == 0)
    {
        COMMAND_MIN_ARGS(5, "set");
        ptr_bar = gui_bar_search (argv[2]);
        if (!ptr_bar)
        {
            gui_chat_printf (NULL,
                             _("%sBar \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        if (!gui_bar_set (ptr_bar, argv[3], argv_eol[4]))
        {
            gui_chat_printf (NULL,
                             _("%sUnable to set option \"%s\" for bar \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[3], argv[2]);
            return WEECHAT_RC_OK;
        }
        return WEECHAT_RC_OK;
    }

    /* hide a bar */
    if (string_strcmp (argv[1], "hide") == 0)
    {
        COMMAND_MIN_ARGS(3, "hide");
        ptr_bar = gui_bar_search (argv[2]);
        if (!ptr_bar)
        {
            gui_chat_printf (NULL,
                             _("%sBar \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        if (!CONFIG_BOOLEAN(ptr_bar->options[GUI_BAR_OPTION_HIDDEN]))
            gui_bar_set (ptr_bar, "hidden", "1");
        return WEECHAT_RC_OK;
    }

    /* show a bar */
    if (string_strcmp (argv[1], "show") == 0)
    {
        COMMAND_MIN_ARGS(3, "show");
        ptr_bar = gui_bar_search (argv[2]);
        if (!ptr_bar)
        {
            gui_chat_printf (NULL,
                             _("%sBar \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        if (CONFIG_BOOLEAN(ptr_bar->options[GUI_BAR_OPTION_HIDDEN]))
            gui_bar_set (ptr_bar, "hidden", "0");
        return WEECHAT_RC_OK;
    }

    /* toggle a bar visible/hidden */
    if (string_strcmp (argv[1], "toggle") == 0)
    {
        COMMAND_MIN_ARGS(3, "toggle");
        ptr_bar = gui_bar_search (argv[2]);
        if (!ptr_bar)
        {
            gui_chat_printf (NULL,
                             _("%sBar \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        gui_bar_set (ptr_bar, "hidden",
                     CONFIG_BOOLEAN(ptr_bar->options[GUI_BAR_OPTION_HIDDEN]) ? "0" : "1");
        return WEECHAT_RC_OK;
    }

    /* scroll in a bar */
    if (string_strcmp (argv[1], "scroll") == 0)
    {
        COMMAND_MIN_ARGS(5, "scroll");
        ptr_bar = gui_bar_search (argv[2]);
        if (ptr_bar)
        {
            if (strcmp (argv[3], "*") == 0)
                ptr_window = gui_current_window;
            else
            {
                ptr_window = NULL;
                error = NULL;
                number = (int)strtol (argv[3], &error, 10);
                if (error && !error[0])
                    ptr_window = gui_window_search_by_number (number);
            }
            if (!ptr_window)
            {
                gui_chat_printf (NULL,
                                 _("%sWindow not found for \"%s\" command"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR], "bar");
                return WEECHAT_RC_OK;
            }
            if (!gui_bar_scroll (ptr_bar, ptr_window, argv_eol[4]))
            {
                gui_chat_printf (NULL,
                                 _("%sUnable to scroll bar \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2]);
                return WEECHAT_RC_OK;
            }
        }
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Checks if the buffer number is valid (in range 1 to GUI_BUFFER_NUMBER_MAX).
 *
 * If the number is not valid, a warning is displayed.
 *
 * Returns:
 *   1: buffer number is valid
 *   0: buffer number is invalid
 */

int
command_buffer_check_number (long number)
{
    if ((number < 1) || (number > GUI_BUFFER_NUMBER_MAX))
    {
        /* invalid number */
        gui_chat_printf (NULL,
                         _("%sBuffer number \"%d\" is out of range "
                           "(it must be between 1 and %d)"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                         number,
                         GUI_BUFFER_NUMBER_MAX);
        return 0;
    }

    /* number is OK */
    return 1;
}

/*
 * Displays a local variable for a buffer.
 */

void
command_buffer_display_localvar (void *data,
                                 struct t_hashtable *hashtable,
                                 const void *key, const void *value)
{
    /* make C compiler happy */
    (void) data;
    (void) hashtable;

    if (key)
    {
        if (value)
        {
            gui_chat_printf (NULL,
                             "  %s: \"%s\"",
                             (const char *)key,
                             (const char *)value);
        }
        else
        {
            gui_chat_printf (NULL,
                             "  %s: (null)",
                             (const char *)key);
        }
    }
}

/*
 * Callback for command "/buffer": manages buffers.
 */

COMMAND_CALLBACK(buffer)
{
    struct t_gui_buffer *ptr_buffer, *ptr_buffer1, *ptr_buffer2;
    struct t_gui_buffer *weechat_buffer;
    struct t_arraylist *buffers_to_close;
    long number, number1, number2, numbers[3];
    char *error, *value, *pos, *str_number1, *pos_number2;
    int i, count, prev_number, clear_number, list_size;
    int buffer_found, arg_name, type_free, switch_to_buffer;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    if ((argc == 1) || (string_strcmp (argv[1], "list") == 0))
    {
        /* list buffers */
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, _("Buffers list:"));

        for (ptr_buffer = gui_buffers; ptr_buffer;
             ptr_buffer = ptr_buffer->next_buffer)
        {
            gui_chat_printf (
                NULL,
                _("  %s[%s%d%s]%s %s%s.%s%s%s (notify: %s%s%s)%s%s"),
                GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                GUI_COLOR(GUI_COLOR_CHAT),
                ptr_buffer->number,
                GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                gui_buffer_get_plugin_name (ptr_buffer),
                GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                ptr_buffer->name,
                GUI_COLOR(GUI_COLOR_CHAT),
                GUI_COLOR(GUI_COLOR_CHAT_VALUE),
                gui_buffer_notify_string[ptr_buffer->notify],
                GUI_COLOR(GUI_COLOR_CHAT),
                (ptr_buffer->hidden) ? " " : "",
                /* TRANSLATORS: "hidden" is displayed in list of buffers */
                (ptr_buffer->hidden) ? _("(hidden)") : "");
        }

        return WEECHAT_RC_OK;
    }

    /* create a new buffer */
    if (string_strcmp (argv[1], "add") == 0)
    {
        COMMAND_MIN_ARGS(3, "add");
        arg_name = 2;
        type_free = 0;
        switch_to_buffer = 0;
        for (i = 2; i < argc; i++)
        {
            if (string_strcmp (argv[i], "-free") == 0)
                type_free = 1;
            else if (string_strcmp (argv[i], "-switch") == 0)
                switch_to_buffer = 1;
            else
                arg_name = i;
        }
        if (gui_buffer_is_reserved_name (argv[arg_name]))
        {
            gui_chat_printf (NULL,
                             _("%sBuffer name \"%s\" is reserved for WeeChat"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[arg_name]);
            return WEECHAT_RC_OK;
        }
        ptr_buffer = gui_buffer_search_by_name (PLUGIN_CORE, argv[arg_name]);
        if (!ptr_buffer)
        {
            ptr_buffer = gui_buffer_new_user (
                argv[arg_name],
                (type_free) ? GUI_BUFFER_TYPE_FREE : GUI_BUFFER_TYPE_DEFAULT);
        }
        if (ptr_buffer && switch_to_buffer)
            gui_window_switch_to_buffer (gui_current_window, ptr_buffer, 1);
        return WEECHAT_RC_OK;
    }

    /* clear content of buffer(s) */
    if (string_strcmp (argv[1], "clear") == 0)
    {
        if (argc > 2)
        {
            if (string_strcmp (argv[2], "-all") == 0)
                gui_buffer_clear_all ();
            else
            {
                for (i = 2; i < argc; i++)
                {
                    if (string_strcmp (argv[i], "-merged") == 0)
                    {
                        ptr_buffer = buffer;
                        clear_number = 1;
                    }
                    else
                    {
                        ptr_buffer = gui_buffer_search_by_number_or_name (argv[i]);
                        (void) strtol (argv[i], &error, 10);
                        clear_number = (error && !error[0]);
                    }
                    if (ptr_buffer)
                    {
                        if (clear_number)
                        {
                            for (ptr_buffer2 = gui_buffers; ptr_buffer2;
                                 ptr_buffer2 = ptr_buffer2->next_buffer)
                            {
                                if ((ptr_buffer2->number == ptr_buffer->number)
                                    && ptr_buffer2->clear)
                                {
                                    gui_buffer_clear (ptr_buffer2);
                                }
                            }
                        }
                        else
                        {
                            if (ptr_buffer->clear)
                                gui_buffer_clear (ptr_buffer);
                        }
                    }
                }
            }
        }
        else
        {
            if (buffer->clear)
                gui_buffer_clear (buffer);
        }

        return WEECHAT_RC_OK;
    }

    /* move buffer to another number in the list */
    if (string_strcmp (argv[1], "move") == 0)
    {
        COMMAND_MIN_ARGS(3, "move");
        if (strcmp (argv[2], "-") == 0)
        {
            gui_buffer_move_to_number (buffer, gui_buffers->number);
        }
        else if (strcmp (argv[2], "+") == 0)
        {
            number = last_gui_buffer->number + 1;
            if (command_buffer_check_number (number))
                gui_buffer_move_to_number (buffer, number);
        }
        else
        {
            error = NULL;
            number = strtol (((argv[2][0] == '+') || (argv[2][0] == '-')) ?
                             argv[2] + 1 : argv[2],
                             &error, 10);
            if (error && !error[0]
                && (number >= INT_MIN) && (number <= INT_MAX))
            {
                if (argv[2][0] == '+')
                    number = buffer->number + number;
                else if (argv[2][0] == '-')
                    number = buffer->number - number;
                number = (int)number;
                if (command_buffer_check_number (number))
                    gui_buffer_move_to_number (buffer, number);
            }
            else
            {
                /* invalid number */
                gui_chat_printf (NULL,
                                 _("%sInvalid buffer number: \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2]);
                return WEECHAT_RC_OK;
            }
        }

        return WEECHAT_RC_OK;
    }

    /* swap buffers */
    if (string_strcmp (argv[1], "swap") == 0)
    {
        COMMAND_MIN_ARGS(3, "swap");

        ptr_buffer = NULL;
        ptr_buffer2 = NULL;

        /* search buffers to swap */
        ptr_buffer = gui_buffer_search_by_number_or_name (argv[2]);
        if (!ptr_buffer)
        {
            /* invalid buffer name/number */
            gui_chat_printf (NULL,
                             _("%sBuffer \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        if (argc > 3)
        {
            ptr_buffer2 = gui_buffer_search_by_number_or_name (argv[3]);
            if (!ptr_buffer2)
            {
                /* invalid buffer name/number */
                gui_chat_printf (NULL,
                                 _("%sBuffer \"%s\" not found"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[3]);
                return WEECHAT_RC_OK;
            }
        }
        else
        {
            ptr_buffer2 = buffer;
        }

        gui_buffer_swap (ptr_buffer->number, ptr_buffer2->number);

        return WEECHAT_RC_OK;
    }

    /* cycle between a list of buffers */
    if (string_strcmp (argv[1], "cycle") == 0)
    {
        COMMAND_MIN_ARGS(3, "cycle");

        /* first buffer found different from current one */
        ptr_buffer1 = NULL;

        /* boolean to check if current buffer was found in list */
        buffer_found = 0;

        for (i = 2; i < argc; i++)
        {
            ptr_buffer = gui_buffer_search_by_number_or_name (argv[i]);
            if (!ptr_buffer)
                continue;
            if (ptr_buffer == buffer)
            {
                /* current buffer found */
                buffer_found = 1;
            }
            else
            {
                if (!ptr_buffer1)
                    ptr_buffer1 = ptr_buffer;
                if (buffer_found)
                {
                    /*
                     * we already found the current buffer in list,
                     * so let's jump to this buffer
                     */
                    gui_window_switch_to_buffer (gui_current_window,
                                                 ptr_buffer, 1);
                    return WEECHAT_RC_OK;
                }
            }
        }

        /* cycle on the first buffer found if no other buffer was found */
        if (ptr_buffer1)
        {
            gui_window_switch_to_buffer (gui_current_window,
                                         ptr_buffer1, 1);
        }

        return WEECHAT_RC_OK;
    }

    /* merge buffer with another buffer in the list */
    if (string_strcmp (argv[1], "merge") == 0)
    {
        COMMAND_MIN_ARGS(3, "merge");
        error = NULL;
        ptr_buffer = gui_buffer_search_by_number_or_name (argv[2]);
        if (!ptr_buffer)
        {
            gui_chat_printf (NULL,
                             _("%sBuffer \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        gui_buffer_merge (buffer, ptr_buffer);
        return WEECHAT_RC_OK;
    }

    /* unmerge buffer */
    if (string_strcmp (argv[1], "unmerge") == 0)
    {
        number = -1;
        if (argc >= 3)
        {
            if (string_strcmp (argv[2], "-all") == 0)
            {
                gui_buffer_unmerge_all ();
                return WEECHAT_RC_OK;
            }
            else
            {
                error = NULL;
                number = strtol (argv[2], &error, 10);
                if (!error || error[0])
                {
                    /* invalid number */
                    gui_chat_printf (NULL,
                                     _("%sInvalid buffer number: \"%s\""),
                                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                     argv[2]);
                    return WEECHAT_RC_OK;
                }
                if (!command_buffer_check_number ((int)number))
                    COMMAND_ERROR;
            }
        }
        gui_buffer_unmerge (buffer, (int)number);

        return WEECHAT_RC_OK;
    }

    /* hide buffer(s) */
    if (string_strcmp (argv[1], "hide") == 0)
    {
        if (argc > 2)
        {
            if (string_strcmp (argv[2], "-all") == 0)
                gui_buffer_hide_all ();
            else
            {
                for (i = 2; i < argc; i++)
                {
                    ptr_buffer = gui_buffer_search_by_number_or_name (argv[i]);
                    if (ptr_buffer)
                    {
                        (void) strtol (argv[i], &error, 10);
                        if (error && !error[0])
                        {
                            for (ptr_buffer2 = gui_buffers; ptr_buffer2;
                                 ptr_buffer2 = ptr_buffer2->next_buffer)
                            {
                                if (ptr_buffer2->number == ptr_buffer->number)
                                {
                                    gui_buffer_hide (ptr_buffer2);
                                }
                            }
                        }
                        else
                            gui_buffer_hide (ptr_buffer);
                    }
                }
            }
        }
        else
            gui_buffer_hide (buffer);

        return WEECHAT_RC_OK;
    }

    /* unhide buffer(s) */
    if (string_strcmp (argv[1], "unhide") == 0)
    {
        if (argc > 2)
        {
            if (string_strcmp (argv[2], "-all") == 0)
                gui_buffer_unhide_all ();
            else
            {
                for (i = 2; i < argc; i++)
                {
                    ptr_buffer = gui_buffer_search_by_number_or_name (argv[i]);
                    if (ptr_buffer)
                    {
                        (void) strtol (argv[i], &error, 10);
                        if (error && !error[0])
                        {
                            for (ptr_buffer2 = gui_buffers; ptr_buffer2;
                                 ptr_buffer2 = ptr_buffer2->next_buffer)
                            {
                                if (ptr_buffer2->number == ptr_buffer->number)
                                {
                                    gui_buffer_unhide (ptr_buffer2);
                                }
                            }
                        }
                        else
                            gui_buffer_unhide (ptr_buffer);
                    }
                }
            }
        }
        else
            gui_buffer_unhide (buffer);

        return WEECHAT_RC_OK;
    }

    /* switch to next/previous active buffer */
    if (string_strcmp (argv[1], "switch") == 0)
    {
        if ((argc > 2) && (string_strcmp (argv[2], "-previous") == 0))
            gui_buffer_switch_active_buffer_previous (buffer);
        else
            gui_buffer_switch_active_buffer (buffer);
        return WEECHAT_RC_OK;
    }

    /* zoom on merged buffer */
    if (string_strcmp (argv[1], "zoom") == 0)
    {
        gui_buffer_zoom (buffer);
        return WEECHAT_RC_OK;
    }

    /* renumber buffers */
    if (string_strcmp (argv[1], "renumber") == 0)
    {
        if (CONFIG_BOOLEAN(config_look_buffer_auto_renumber))
        {
            gui_chat_printf (NULL,
                             _("%sRenumbering is allowed only if option "
                               "weechat.look.buffer_auto_renumber is off"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
            return WEECHAT_RC_OK;
        }

        for (i = 0; i < 3; i++)
        {
            if (argc >= i + 3)
            {
                error = NULL;
                numbers[i] = strtol (argv[i + 2], &error, 10);
                if (!error || error[0])
                {
                    /* invalid number */
                    gui_chat_printf (NULL,
                                     _("%sInvalid buffer number: \"%s\""),
                                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                     argv[i + 2]);
                    return WEECHAT_RC_OK;
                }
                if ((i == 2) && !command_buffer_check_number ((int)numbers[i]))
                    return WEECHAT_RC_OK;
            }
            else
                numbers[i] = -1;
        }

        /*
         * renumber the buffers; if we are renumbering all buffers (no numbers
         * given), start at number 1
         */
        gui_buffer_renumber ((int)numbers[0], (int)numbers[1],
                             (argc == 2) ? 1 : (int)numbers[2]);

        return WEECHAT_RC_OK;
    }

    /* close buffer */
    if (string_strcmp (argv[1], "close") == 0)
    {
        buffers_to_close = arraylist_new (32, 0, 0, NULL, NULL, NULL, NULL);
        if (!buffers_to_close)
            COMMAND_ERROR;

        if (argc < 3)
        {
            arraylist_add (buffers_to_close, buffer);
        }
        else
        {
            for (i = 2; i < argc; i++)
            {
                if (isdigit ((unsigned char)argv[i][0]))
                {
                    number1 = -1;
                    number2 = -1;
                    pos = strchr (argv[i], '-');
                    if (pos)
                    {
                        str_number1 = string_strndup (argv[i],
                                                      pos - argv[i]);
                        pos_number2 = pos + 1;
                    }
                    else
                    {
                        str_number1 = strdup (argv[i]);
                        pos_number2 = NULL;
                    }
                    if (str_number1)
                    {
                        error = NULL;
                        number1 = strtol (str_number1, &error, 10);
                        if (error && !error[0])
                        {
                            if (pos_number2)
                            {
                                error = NULL;
                                number2 = strtol (pos_number2, &error, 10);
                                if (!error || error[0])
                                {
                                    free (str_number1);
                                    COMMAND_ERROR;
                                }
                            }
                            else
                                number2 = number1;
                        }
                        else
                        {
                            free (str_number1);
                            COMMAND_ERROR;
                        }
                        free (str_number1);
                    }
                    if ((number1 >= 1) && (number2 >= 1) && (number2 >= number1))
                    {
                        ptr_buffer = gui_buffers;
                        while (ptr_buffer && (ptr_buffer->number <= number2))
                        {
                            if (ptr_buffer->number >= number1)
                            {
                                arraylist_add (buffers_to_close,
                                               ptr_buffer);
                            }
                            ptr_buffer = ptr_buffer->next_buffer;
                        }
                    }
                }
                else
                {
                    ptr_buffer = gui_buffer_search_by_full_name (argv[i]);
                    if (!ptr_buffer)
                    {
                        ptr_buffer = gui_buffer_search_by_partial_name (
                            NULL, argv[i]);
                    }
                    if (ptr_buffer)
                        arraylist_add (buffers_to_close, ptr_buffer);
                }
            }
        }

        weechat_buffer = gui_buffer_search_main ();

        list_size = arraylist_size (buffers_to_close);
        for (i = 0; i < list_size; i++)
        {
            ptr_buffer = (struct t_gui_buffer *)arraylist_get (buffers_to_close,
                                                               i);
            if (!gui_buffer_valid (ptr_buffer))
                continue;
            if (ptr_buffer == weechat_buffer)
            {
                if (arraylist_size (buffers_to_close) == 1)
                {
                    /*
                     * display error for main buffer if it was the only
                     * buffer to close with matching number
                     */
                    gui_chat_printf (NULL,
                                     _("%sWeeChat main buffer can't be closed"),
                                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
                }
            }
            else
            {
                gui_buffer_close (ptr_buffer);
            }
        }

        arraylist_free (buffers_to_close);

        return WEECHAT_RC_OK;
    }

    /* display or set notify level */
    if (string_strcmp (argv[1], "notify") == 0)
    {
        if (argc < 3)
        {
            gui_chat_printf (NULL,
                             _("Notify for \"%s%s%s\": \"%s%s%s\""),
                             GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                             buffer->full_name,
                             GUI_COLOR(GUI_COLOR_CHAT),
                             GUI_COLOR(GUI_COLOR_CHAT_VALUE),
                             gui_buffer_notify_string[buffer->notify],
                             GUI_COLOR(GUI_COLOR_CHAT));
        }
        else
        {
            if (!config_weechat_notify_set (buffer, argv_eol[2]))
            {
                gui_chat_printf (NULL,
                                 _("%sUnable to set notify level \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv_eol[2]);
            }
        }
        return WEECHAT_RC_OK;
    }

    /*
     * display buffer local variables
     *
     * (note: option "localvar" has been replaced by "listvar" in WeeChat 3.1
     * but is still accepted for compatibility with WeeChat ≤ 3.0;
     * it is now deprecated and will be removed in a future version)
     */
    if ((string_strcmp (argv[1], "listvar") == 0)
        || (string_strcmp (argv[1], "localvar") == 0))
    {
        if (argc > 2)
            ptr_buffer = gui_buffer_search_by_number_or_name (argv[2]);
        else
            ptr_buffer = buffer;

        if (ptr_buffer)
        {
            if (ptr_buffer->local_variables
                && (ptr_buffer->local_variables->items_count > 0))
            {
                gui_chat_printf (NULL, "");
                gui_chat_printf (NULL,
                                 _("Local variables for buffer \"%s\":"),
                                 ptr_buffer->name);
                hashtable_map (ptr_buffer->local_variables,
                               &command_buffer_display_localvar, NULL);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("No local variable defined for buffer "
                                   "\"%s\""),
                                 ptr_buffer->name);
            }
        }

        return WEECHAT_RC_OK;
    }

    /* set a local variable in buffer */
    if (string_strcmp (argv[1], "setvar") == 0)
    {
        COMMAND_MIN_ARGS(3, "setvar");
        if (argc == 3)
        {
            gui_buffer_local_var_add (buffer, argv[2], "");
        }
        else
        {
            value = string_remove_quotes (argv_eol[3], "'\"");
            gui_buffer_local_var_add (buffer,
                                      argv[2],
                                      (value) ? value : argv_eol[3]);
            if (value)
                free (value);
        }
        return WEECHAT_RC_OK;
    }

    /* delete a local variable from a buffer */
    if (string_strcmp (argv[1], "delvar") == 0)
    {
        COMMAND_MIN_ARGS(3, "delvar");
        gui_buffer_local_var_remove (buffer, argv[2]);
        return WEECHAT_RC_OK;
    }

    /* set a property on buffer */
    if (string_strcmp (argv[1], "set") == 0)
    {
        COMMAND_MIN_ARGS(3, "set");
        if (argc == 3)
        {
            /*
             * default to empty value for valueless buffer "properties",
             * e.g. localvar_del_xxx
             */
            gui_buffer_set (buffer, argv[2], "");
        }
        else
        {
            value = string_remove_quotes (argv_eol[3], "'\"");
            gui_buffer_set (buffer, argv[2], (value) ? value : argv_eol[3]);
            if (value)
                free (value);
        }
        return WEECHAT_RC_OK;
    }

    /* get a buffer property */
    if (string_strcmp (argv[1], "get") == 0)
    {
        COMMAND_MIN_ARGS(3, "get");
        if (gui_buffer_property_in_list (gui_buffer_properties_get_integer,
                                         argv[2]))
        {
            gui_chat_printf (NULL, "%s%s%s: (int) %s = %d",
                             GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                             buffer->full_name,
                             GUI_COLOR(GUI_COLOR_CHAT),
                             argv[2],
                             gui_buffer_get_integer (buffer, argv[2]));
        }
        if (gui_buffer_property_in_list (gui_buffer_properties_get_string,
                                         argv[2])
            || (string_strncmp (argv[2], "localvar_", 9) == 0))
        {
            gui_chat_printf (NULL, "%s%s%s: (str) %s = %s",
                             GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                             buffer->full_name,
                             GUI_COLOR(GUI_COLOR_CHAT),
                             argv[2],
                             gui_buffer_get_string (buffer, argv[2]));
        }
        if (gui_buffer_property_in_list (gui_buffer_properties_get_pointer,
                                         argv[2]))
        {
            gui_chat_printf (NULL, "%s%s%s: (ptr) %s = 0x%lx",
                             GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                             buffer->full_name,
                             GUI_COLOR(GUI_COLOR_CHAT),
                             argv[2],
                             gui_buffer_get_pointer (buffer, argv[2]));
        }
        return WEECHAT_RC_OK;
    }

    /* jump to another buffer */
    if (string_strcmp (argv[1], "jump") == 0)
    {
        COMMAND_MIN_ARGS(3, "jump");
        if (string_strcmp (argv[2], "smart") == 0)
             gui_buffer_jump_smart (gui_current_window);
        else if (string_strcmp (argv[2], "last_displayed") == 0)
            gui_buffer_jump_last_buffer_displayed (gui_current_window);
        else if (string_strcmp (argv[2], "prev_visited") == 0)
            gui_buffer_jump_previously_visited_buffer (gui_current_window);
        else if (string_strcmp (argv[2], "next_visited") == 0)
            gui_buffer_jump_next_visited_buffer (gui_current_window);
        else
            COMMAND_ERROR;
        return WEECHAT_RC_OK;
    }

    /* relative jump '-' */
    if (argv[1][0] == '-')
    {
        if (strcmp (argv[1], "-") == 0)
        {
            /* search first non-hidden buffer */
            for (ptr_buffer = gui_buffers; ptr_buffer;
                 ptr_buffer = ptr_buffer->next_buffer)
            {
                if (!ptr_buffer->hidden)
                    break;
            }
            number = (ptr_buffer) ?
                ptr_buffer->number : ((gui_buffers) ? gui_buffers->number : -1);
            if (number > 0)
                gui_buffer_switch_by_number (gui_current_window, number);
        }
        else
        {
            error = NULL;
            number = strtol (argv[1] + 1, &error, 10);
            if (error && !error[0] && (number > 0))
            {
                count = 0;
                prev_number = gui_current_window->buffer->number;
                ptr_buffer = gui_current_window->buffer;
                while (1)
                {
                    ptr_buffer = ptr_buffer->prev_buffer;
                    if (!ptr_buffer)
                        ptr_buffer = last_gui_buffer;

                    /* if we have looped on all buffers, exit the loop */
                    if (ptr_buffer == gui_current_window->buffer)
                        break;

                    /* skip hidden buffers */
                    if (!ptr_buffer->hidden)
                    {
                        if ((ptr_buffer->number != gui_current_window->buffer->number)
                            && (ptr_buffer->number != prev_number))
                        {
                            /*
                             * increase count each time we discover a different
                             * number
                             */
                            count++;
                            if (count == number)
                            {
                                gui_buffer_switch_by_number (gui_current_window,
                                                             ptr_buffer->number);
                                break;
                            }
                        }
                        prev_number = ptr_buffer->number;
                    }
                }
            }
            else
            {
                /* invalid number */
                gui_chat_printf (NULL,
                                 _("%sInvalid buffer number: \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[1] + 1);
                return WEECHAT_RC_OK;
            }
        }
        return WEECHAT_RC_OK;
    }

    /* relative jump '+' */
    if (argv[1][0] == '+')
    {
        if (strcmp (argv[1], "+") == 0)
        {
            gui_buffer_jump_last_visible_number (gui_current_window);
        }
        else
        {
            error = NULL;
            number = strtol (argv[1] + 1, &error, 10);
            if (error && !error[0] && (number > 0))
            {
                count = 0;
                prev_number = gui_current_window->buffer->number;
                ptr_buffer = gui_current_window->buffer;
                while (1)
                {
                    ptr_buffer = ptr_buffer->next_buffer;
                    if (!ptr_buffer)
                        ptr_buffer = gui_buffers;

                    /* if we have looped on all buffers, exit the loop */
                    if (ptr_buffer == gui_current_window->buffer)
                        break;

                    /* skip hidden buffers */
                    if (!ptr_buffer->hidden)
                    {
                        if ((ptr_buffer->number != gui_current_window->buffer->number)
                            && (ptr_buffer->number != prev_number))
                        {
                            /*
                             * increase count each time we discover a different
                             * number
                             */
                            count++;
                            if (count == number)
                            {
                                gui_buffer_switch_by_number (gui_current_window,
                                                             ptr_buffer->number);
                                break;
                            }
                        }
                        prev_number = ptr_buffer->number;
                    }
                }
            }
            else
            {
                /* invalid number */
                gui_chat_printf (NULL,
                                 _("%sInvalid buffer number: \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[1] + 1);
                return WEECHAT_RC_OK;
            }
        }
        return WEECHAT_RC_OK;
    }

    /* smart jump (jump to previous buffer for current number) */
    if (argv[1][0] == '*')
    {
        error = NULL;
        number = strtol (argv[1] + 1, &error, 10);
        if (error && !error[0])
        {
            /* buffer is currently displayed ? then jump to previous buffer */
            if ((number == buffer->number)
                && (CONFIG_BOOLEAN(config_look_jump_current_to_previous_buffer))
                && gui_buffers_visited)
            {
                gui_buffer_jump_previously_visited_buffer (gui_current_window);
            }
            else
            {
                if (number != buffer->number)
                {
                    gui_buffer_switch_by_number (gui_current_window,
                                                 (int) number);
                }
            }
        }
        else
        {
            /* invalid number */
            gui_chat_printf (NULL,
                             _("%sInvalid buffer number: \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[1] + 1);
            return WEECHAT_RC_OK;
        }
        return WEECHAT_RC_OK;
    }

    /* jump to buffer by number or name */
    error = NULL;
    number = strtol (argv[1], &error, 10);
    if (error && !error[0])
    {
        gui_buffer_switch_by_number (gui_current_window,
                                     (int) number);
        return WEECHAT_RC_OK;
    }
    else
    {
        ptr_buffer = gui_buffer_search_by_full_name (argv_eol[1]);
        if (!ptr_buffer)
            ptr_buffer = gui_buffer_search_by_partial_name (NULL, argv_eol[1]);
        if (ptr_buffer)
        {
            gui_window_switch_to_buffer (gui_current_window, ptr_buffer, 1);
            return WEECHAT_RC_OK;
        }
    }

    COMMAND_ERROR;
}

/*
 * Callback for command "/color": defines custom colors and displays palette of
 * colors.
 */

COMMAND_CALLBACK(color)
{
    char *str_alias, *str_rgb, *pos, *error;
    char str_color[1024], str_command[2048];
    long number, limit;
    unsigned int rgb;
    int i;
    struct t_gui_color_palette *color_palette;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) argv_eol;

    if (argc == 1)
    {
        gui_color_buffer_open ();
        return WEECHAT_RC_OK;
    }

    /* send terminal/colors info to buffer */
    if (string_strcmp (argv[1], "-o") == 0)
    {
        gui_color_info_term_colors (str_color, sizeof (str_color));
        (void) input_data (buffer, str_color, NULL);
        return WEECHAT_RC_OK;
    }

    /* add a color alias */
    if (string_strcmp (argv[1], "alias") == 0)
    {
        COMMAND_MIN_ARGS(4, "alias");

        /* check color number */
        error = NULL;
        number = strtol (argv[2], &error, 10);
        if (error && !error[0])
        {
            if ((number < 0) || (number > gui_color_get_term_colors ()))
                number = -1;
        }
        else
        {
            number = -1;
        }
        if (number < 0)
        {
            gui_chat_printf (NULL,
                             _("%sInvalid color number \"%s\" (must be "
                               "between %d and %d)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2], 0, gui_color_get_term_colors ());
            return WEECHAT_RC_OK;
        }

        /* check other arguments */
        str_alias = NULL;
        str_rgb = NULL;
        for (i = 3; i < argc; i++)
        {
            pos = strchr (argv[i], '/');
            if (pos)
                str_rgb = argv[i];
            else
                str_alias = argv[i];
        }
        str_color[0] = '\0';
        if (str_alias)
        {
            strcat (str_color, ";");
            strcat (str_color, str_alias);
        }
        if (str_rgb)
        {
            strcat (str_color, ";");
            strcat (str_color, str_rgb);
        }

        /* add color alias */
        snprintf (str_command, sizeof (str_command),
                  "/set weechat.palette.%d \"%s\"",
                  (int)number,
                  (str_color[0]) ? str_color + 1 : "");
        (void) input_exec_command (buffer, 1, NULL, str_command, NULL);

        return WEECHAT_RC_OK;
    }

    /* delete a color alias */
    if (string_strcmp (argv[1], "unalias") == 0)
    {
        COMMAND_MIN_ARGS(3, "unalias");

        /* check color number */
        error = NULL;
        number = strtol (argv[2], &error, 10);
        if (error && !error[0])
        {
            if ((number < 0) || (number > gui_color_get_term_colors ()))
                number = -1;
        }
        else
        {
            number = -1;
        }
        if (number < 0)
        {
            gui_chat_printf (NULL,
                             _("%sInvalid color number \"%s\" (must be "
                               "between %d and %d)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2], 0, gui_color_get_term_colors ());
            return WEECHAT_RC_OK;
        }

        /* search color */
        color_palette = gui_color_palette_get ((int)number);
        if (!color_palette)
        {
            gui_chat_printf (NULL,
                             _("%sColor \"%s\" is not defined in palette"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }

        /* delete color alias */
        snprintf (str_command, sizeof (str_command),
                  "/unset weechat.palette.%d",
                  (int)number);
        (void) input_exec_command (buffer, 1, NULL, str_command, NULL);

        return WEECHAT_RC_OK;
    }

    /* reset color pairs */
    if (string_strcmp (argv[1], "reset") == 0)
    {
        gui_color_reset_pairs ();
        return WEECHAT_RC_OK;
    }

    /* switch WeeChat/terminal colors */
    if (string_strcmp (argv[1], "switch") == 0)
    {
        gui_color_switch_colors ();
        return WEECHAT_RC_OK;
    }

    /* convert terminal color to RGB color */
    if (string_strcmp (argv[1], "term2rgb") == 0)
    {
        COMMAND_MIN_ARGS(3, "term2rgb");
        error = NULL;
        number = strtol (argv[2], &error, 10);
        if (!error || error[0] || (number < 0) || (number > 255))
            COMMAND_ERROR;
        gui_chat_printf (NULL,
                         "%ld -> #%06x",
                         number,
                         gui_color_convert_term_to_rgb (number));
        return WEECHAT_RC_OK;
    }

    /* convert RGB color to terminal color */
    if (string_strcmp (argv[1], "rgb2term") == 0)
    {
        COMMAND_MIN_ARGS(3, "rgb2term");
        if (sscanf ((argv[2][0] == '#') ? argv[2] + 1 : argv[2], "%x", &rgb) != 1)
            COMMAND_ERROR;
        if (rgb > 0xFFFFFF)
            COMMAND_ERROR;
        limit = 256;
        if (argc > 3)
        {
            error = NULL;
            limit = strtol (argv[3], &error, 10);
            if (!error || error[0] || (limit < 1) || (limit > 256))
                COMMAND_ERROR;
        }
        gui_chat_printf (NULL,
                         "#%06x -> %d",
                         rgb,
                         gui_color_convert_rgb_to_term (rgb, limit));
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Callback for command "/command": launches explicit WeeChat or plugin command.
 */

COMMAND_CALLBACK(command)
{
    int length, index_args, any_plugin;
    char *command;
    struct t_weechat_plugin *ptr_plugin;
    struct t_gui_buffer *ptr_buffer;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    COMMAND_MIN_ARGS(3, "");

    ptr_buffer = buffer;
    index_args = 1;
    any_plugin = 0;
    ptr_plugin = NULL;

    if ((argc >= 5) && (string_strcmp (argv[1], "-buffer") == 0))
    {
        ptr_buffer = gui_buffer_search_by_full_name (argv[2]);
        if (!ptr_buffer)
        {
            gui_chat_printf (NULL,
                             _("%sBuffer \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        index_args = 3;
    }

    if (strcmp (argv[index_args], "*") == 0)
    {
        any_plugin = 1;
        ptr_plugin = ptr_buffer->plugin;
    }
    else if (string_strcmp (argv[index_args], PLUGIN_CORE) != 0)
    {
        ptr_plugin = plugin_search (argv[index_args]);
        if (!ptr_plugin)
        {
            gui_chat_printf (NULL, _("%sPlugin \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[index_args]);
            return WEECHAT_RC_OK;
        }
    }
    if (string_is_command_char (argv_eol[index_args + 1]))
    {
        (void) input_exec_command (ptr_buffer, any_plugin, ptr_plugin,
                                   argv_eol[index_args + 1], NULL);
    }
    else
    {
        length = strlen (argv_eol[index_args + 1]) + 2;
        command = malloc (length);
        if (command)
        {
            snprintf (command, length, "/%s", argv_eol[index_args + 1]);
            (void) input_exec_command (ptr_buffer, any_plugin, ptr_plugin,
                                       command, NULL);
            free (command);
        }
    }

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/cursor": free movement of cursor on screen.
 */

COMMAND_CALLBACK(cursor)
{
    char *pos, *str_x, *error;
    int x, y;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;
    (void) argv_eol;

    if (gui_window_bare_display)
        return WEECHAT_RC_OK;

    if (argc == 1)
    {
        gui_cursor_mode_toggle ();
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "go") == 0)
    {
        if (argc > 2)
        {
            pos = strchr (argv[2], ',');
            if (pos)
            {
                str_x = string_strndup (argv[2], pos - argv[2]);
                pos++;
                if (str_x)
                {
                    error = NULL;
                    x = (int) strtol (str_x, &error, 10);
                    if (error && !error[0])
                    {
                        error = NULL;
                        y = (int) strtol (pos, &error, 10);
                        if (error && !error[0])
                        {
                            gui_cursor_move_xy (x, y);
                        }
                    }
                    free (str_x);
                }
            }
            else
                gui_cursor_move_area (argv[2]);
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "move") == 0)
    {
        if (argc > 2)
        {
            if (string_strcmp (argv[2], "up") == 0)
                gui_cursor_move_add_xy (0, -1);
            else if (string_strcmp (argv[2], "down") == 0)
                gui_cursor_move_add_xy (0, 1);
            else if (string_strcmp (argv[2], "left") == 0)
                gui_cursor_move_add_xy (-1, 0);
            else if (string_strcmp (argv[2], "right") == 0)
                gui_cursor_move_add_xy (1, 0);
            else if (string_strcmp (argv[2], "area_up") == 0)
                gui_cursor_move_area_add_xy (0, -1);
            else if (string_strcmp (argv[2], "area_down") == 0)
                gui_cursor_move_area_add_xy (0, 1);
            else if (string_strcmp (argv[2], "area_left") == 0)
                gui_cursor_move_area_add_xy (-1, 0);
            else if (string_strcmp (argv[2], "area_right") == 0)
                gui_cursor_move_area_add_xy (1, 0);
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "stop") == 0)
    {
        gui_cursor_mode_stop ();
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Callback for command "/debug": controls debug for core/plugins.
 */

COMMAND_CALLBACK(debug)
{
    struct t_config_option *ptr_option;
    struct t_weechat_plugin *ptr_plugin;
    struct timeval time_start, time_end;
    char *result;
    int debug;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    if ((argc == 1) || (string_strcmp (argv[1], "list") == 0))
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, "Debug:");

        ptr_option = config_weechat_debug_get (PLUGIN_CORE);
        gui_chat_printf (NULL, "  %s: %d",
                         PLUGIN_CORE,
                         (ptr_option) ? CONFIG_INTEGER(ptr_option) : 0);
        for (ptr_plugin = weechat_plugins; ptr_plugin;
             ptr_plugin = ptr_plugin->next_plugin)
        {
            gui_chat_printf (NULL, "  %s: %d",
                             ptr_plugin->name,
                             ptr_plugin->debug);
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "buffer") == 0)
    {
        gui_buffer_dump_hexa (buffer);
        gui_chat_printf (NULL,
                         _("Raw content of buffers has been written in log "
                           "file"));
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "certs") == 0)
    {
        gui_chat_printf (NULL,
                         NG_("%d certificate loaded (system: %d, user: %d)",
                             "%d certificates loaded (system: %d, user: %d)",
                             network_num_certs),
                         network_num_certs,
                         network_num_certs_system,
                         network_num_certs_user);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "color") == 0)
    {
        gui_color_dump ();
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "cursor") == 0)
    {
        if (gui_cursor_debug)
            gui_cursor_debug_set (0);
        else
        {
            debug = ((argc > 2)
                     && (string_strcmp (argv[2], "verbose") == 0)) ? 2 : 1;
            gui_cursor_debug_set (debug);
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "dirs") == 0)
    {
        debug_directories ();
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "dump") == 0)
    {
        if (argc > 2)
            log_printf ("Dump request for plugin: \"%s\"", argv_eol[2]);
        else
            log_printf ("Dump request for WeeChat core and plugins");
        weechat_log_use_time = 0;
        (void) hook_signal_send ("debug_dump", WEECHAT_HOOK_SIGNAL_STRING,
                                 (argc > 2) ? argv_eol[2] : NULL);
        weechat_log_use_time = 1;
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "hdata") == 0)
    {
        if ((argc > 2) && (string_strcmp (argv[2], "free") == 0))
            hdata_free_all ();
        else
            debug_hdata ();
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "hooks") == 0)
    {
        if (argc > 2)
            debug_hooks_plugin (argv[2]);
        else
            debug_hooks ();
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "infolists") == 0)
    {
        debug_infolists ();
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "libs") == 0)
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, "Libs:");
        (void) hook_signal_send ("debug_libs", WEECHAT_HOOK_SIGNAL_STRING, NULL);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "memory") == 0)
    {
        debug_memory ();
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "mouse") == 0)
    {
        if (gui_mouse_debug)
            gui_mouse_debug_set (0);
        else
        {
            debug = ((argc > 2)
                     && (string_strcmp (argv[2], "verbose") == 0)) ? 2 : 1;
            gui_mouse_debug_set (debug);
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "set") == 0)
    {
        COMMAND_MIN_ARGS(4, "set");
        if (strcmp (argv[3], "0") == 0)
        {
            /* disable debug for a plugin */
            ptr_option = config_weechat_debug_get (argv[2]);
            if (ptr_option)
            {
                config_file_option_free (ptr_option, 1);
                config_weechat_debug_set_all ();
                gui_chat_printf (NULL, _("Debug disabled for \"%s\""),
                                 argv[2]);
            }
        }
        else
        {
            /* set debug level for a plugin */
            if (config_weechat_debug_set (argv[2], argv[3]) != WEECHAT_CONFIG_OPTION_SET_ERROR)
            {
                ptr_option = config_weechat_debug_get (argv[2]);
                if (ptr_option)
                {
                    gui_chat_printf (NULL, "%s: \"%s\" => %d",
                                     "debug", argv[2],
                                     CONFIG_INTEGER(ptr_option));
                }
            }
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "tags") == 0)
    {
        gui_chat_display_tags ^= 1;
        gui_window_ask_refresh (2);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "term") == 0)
    {
        gui_window_term_display_infos ();
        weechat_term_check ();
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "time") == 0)
    {
        COMMAND_MIN_ARGS(3, "time");
        gettimeofday (&time_start, NULL);
        (void) input_data (buffer, argv_eol[2], NULL);
        gettimeofday (&time_end, NULL);
        debug_display_time_elapsed (&time_start, &time_end, argv_eol[2], 1);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "unicode") == 0)
    {
        COMMAND_MIN_ARGS(3, "unicode");
        result = eval_expression (argv_eol[2], NULL, NULL, NULL);
        if (result)
        {
            debug_unicode (result);
            free (result);
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "windows") == 0)
    {
        debug_windows_tree ();
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Prints eval debug output.
 */

void
command_eval_print_debug (const char *debug)
{
    regex_t regex;
    char str_replace[1024], *string;

    string = NULL;

    if (string_regcomp (&regex, "(^|\n)( *)([0-9]+:)", REG_EXTENDED) == 0)
    {
        /* colorize debug ids and the following colon with delimiter color */
        snprintf (str_replace, sizeof (str_replace),
                  "$1$2%s$3%s",
                  GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                  GUI_COLOR(GUI_COLOR_CHAT));
        string = string_replace_regex (debug, &regex, str_replace, '$',
                                       NULL, NULL);
        regfree (&regex);
    }

    gui_chat_printf (NULL, "%s", (string) ? string : debug);

    if (string)
        free (string);
}

/*
 * Callback for command "/eval": evaluates an expression and sends result to
 * buffer.
 */

COMMAND_CALLBACK(eval)
{
    int i, print_only, split_command, condition, debug, error;
    char *result, *ptr_args, **commands, str_debug[32];
    const char *debug_output;
    struct t_hashtable *pointers, *options;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) argv;

    print_only = 0;
    split_command = 0;
    condition = 0;
    debug = 0;
    error = 0;

    COMMAND_MIN_ARGS(2, "");

    ptr_args = argv_eol[1];
    for (i = 1; i < argc; i++)
    {
        if (string_strcmp (argv[i], "-n") == 0)
        {
            print_only = 1;
            ptr_args = argv_eol[i + 1];
        }
        else if (string_strcmp (argv[i], "-s") == 0)
        {
            split_command = 1;
            ptr_args = argv_eol[i + 1];
        }
        else if (string_strcmp (argv[i], "-c") == 0)
        {
            condition = 1;
            ptr_args = argv_eol[i + 1];
        }
        else if (string_strcmp (argv[i], "-d") == 0)
        {
            debug++;
            ptr_args = argv_eol[i + 1];
        }
        else
        {
            ptr_args = argv_eol[i];
            break;
        }
    }

    if (ptr_args)
    {
        pointers = hashtable_new (32,
                                  WEECHAT_HASHTABLE_STRING,
                                  WEECHAT_HASHTABLE_POINTER,
                                  NULL,
                                  NULL);
        if (pointers)
        {
            hashtable_set (pointers, "window",
                           gui_window_search_with_buffer (buffer));
            hashtable_set (pointers, "buffer", buffer);
        }

        options = NULL;
        if (condition || debug)
        {
            options = hashtable_new (32,
                                     WEECHAT_HASHTABLE_STRING,
                                     WEECHAT_HASHTABLE_STRING,
                                     NULL,
                                     NULL);
            if (options)
            {
                if (condition)
                    hashtable_set (options, "type", "condition");
                if (debug > 0)
                {
                    snprintf (str_debug, sizeof (str_debug), "%d", debug);
                    hashtable_set (options, "debug", str_debug);
                }
            }
        }

        if (print_only)
        {
            result = eval_expression (ptr_args, pointers, NULL, options);
            gui_chat_printf_date_tags (NULL, 0, "no_log", "\t>> %s", ptr_args);
            if (result)
            {
                gui_chat_printf_date_tags (NULL, 0, "no_log", "\t== %s[%s%s%s]",
                                           GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                           GUI_COLOR(GUI_COLOR_CHAT),
                                           result,
                                           GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS));
                free (result);
            }
            else
            {
                gui_chat_printf_date_tags (NULL, 0, "no_log", "\t== %s<%s%s%s>",
                                           GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                           GUI_COLOR(GUI_COLOR_CHAT),
                                           _("error"),
                                           GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS));
            }
            if (options && debug)
            {
                debug_output = hashtable_get (options,
                                              "debug_output");
                if (debug_output)
                    command_eval_print_debug (debug_output);
            }
        }
        else
        {
            if (split_command)
            {
                commands = string_split_command (ptr_args, ';');
                if (commands)
                {
                    for (i = 0; commands[i]; i++)
                    {
                        result = eval_expression (commands[i], pointers, NULL,
                                                  options);
                        if (result)
                        {
                            (void) input_data (buffer, result, NULL);
                            free (result);
                        }
                        else
                        {
                            error = 1;
                        }
                        if (options && debug)
                        {
                            debug_output = hashtable_get (options,
                                                          "debug_output");
                            if (debug_output)
                                command_eval_print_debug (debug_output);
                        }
                    }
                    string_free_split_command (commands);
                }
            }
            else
            {
                result = eval_expression (ptr_args, pointers, NULL, options);
                if (result)
                {
                    (void) input_data (buffer, result, NULL);
                    free (result);
                }
                else
                {
                    error = 1;
                }
                if (options && debug)
                {
                    debug_output = hashtable_get (options,
                                                  "debug_output");
                    if (debug_output)
                        command_eval_print_debug (debug_output);
                }
            }
        }

        if (error)
        {
            gui_chat_printf (NULL,
                             _("%sError in expression to evaluate"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
        }

        if (pointers)
            hashtable_free (pointers);
        if (options)
            hashtable_free (options);
    }

    return WEECHAT_RC_OK;
}

/*
 * Displays one filter.
 */

void
command_filter_display (struct t_gui_filter *filter)
{
    gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                               _("  %s[%s%s%s]%s buffer: %s%s%s "
                                 "/ tags: %s / regex: %s %s"),
                               GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                               GUI_COLOR(GUI_COLOR_CHAT),
                               filter->name,
                               GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                               GUI_COLOR(GUI_COLOR_CHAT),
                               GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                               filter->buffer_name,
                               GUI_COLOR(GUI_COLOR_CHAT),
                               filter->tags,
                               filter->regex,
                               (filter->enabled) ?
                               "" : _("(disabled)"));
}

/*
 * Callback for command "/filter": manages message filters.
 */

COMMAND_CALLBACK(filter)
{
    struct t_gui_filter *ptr_filter;
    char str_command[4096], str_pos[16];
    int i, update;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    if ((argc == 1) || (string_strcmp (argv[1], "list") == 0))
    {
        /* display all filters */
        gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER, "");
        gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                   "%s",
                                   (gui_filters_enabled) ?
                                   _("Message filtering enabled") :
                                   _("Message filtering disabled"));

        if (gui_filters)
        {
            gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                       _("Message filters:"));
            for (ptr_filter = gui_filters; ptr_filter;
                 ptr_filter = ptr_filter->next_filter)
            {
                command_filter_display (ptr_filter);
            }
        }
        else
        {
            gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                       _("No message filter defined"));
        }

        return WEECHAT_RC_OK;
    }

    /* enable global filtering or a filter */
    if (string_strcmp (argv[1], "enable") == 0)
    {
        if (argc > 2)
        {
            if (strcmp (argv[2], "@") == 0)
            {
                /* enable filters in buffer */
                if (!buffer->filter)
                {
                    buffer->filter = 1;
                    gui_filter_buffer (buffer, NULL);
                    (void) hook_signal_send (
                        "buffer_filters_enabled",
                        WEECHAT_HOOK_SIGNAL_POINTER, buffer);
                }
            }
            else
            {
                /* enable a filter */
                ptr_filter = gui_filter_search_by_name (argv[2]);
                if (ptr_filter)
                {
                    if (!ptr_filter->enabled)
                    {
                        ptr_filter->enabled = 1;
                        gui_filter_all_buffers (ptr_filter);
                        gui_chat_printf_date_tags (NULL, 0,
                                                   GUI_FILTER_TAG_NO_FILTER,
                                                   _("Filter \"%s\" enabled"),
                                                   ptr_filter->name);
                    }
                }
                else
                {
                    gui_chat_printf_date_tags (NULL, 0,
                                               GUI_FILTER_TAG_NO_FILTER,
                                               _("%sFilter \"%s\" not found"),
                                               gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                               argv[2]);
                    return WEECHAT_RC_OK;
                }
            }
        }
        else
        {
            /* enable global filtering */
            if (!gui_filters_enabled)
            {
                gui_filter_global_enable ();
                gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                           _("Message filtering enabled"));
            }
        }
        return WEECHAT_RC_OK;
    }

    /* disable global filtering or a filter */
    if (string_strcmp (argv[1], "disable") == 0)
    {
        if (argc > 2)
        {
            if (strcmp (argv[2], "@") == 0)
            {
                /* disable filters in buffer */
                if (buffer->filter)
                {
                    buffer->filter = 0;
                    gui_filter_buffer (buffer, NULL);
                    (void) hook_signal_send (
                        "buffer_filters_disabled",
                        WEECHAT_HOOK_SIGNAL_POINTER, buffer);
                }
            }
            else
            {
                /* disable a filter */
                ptr_filter = gui_filter_search_by_name (argv[2]);
                if (ptr_filter)
                {
                    if (ptr_filter->enabled)
                    {
                        ptr_filter->enabled = 0;
                        gui_filter_all_buffers (ptr_filter);
                        gui_chat_printf_date_tags (NULL, 0,
                                                   GUI_FILTER_TAG_NO_FILTER,
                                                   _("Filter \"%s\" disabled"),
                                                   ptr_filter->name);
                    }
                }
                else
                {
                    gui_chat_printf_date_tags (NULL, 0,
                                               GUI_FILTER_TAG_NO_FILTER,
                                               _("%sFilter \"%s\" not found"),
                                               gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                               argv[2]);
                    return WEECHAT_RC_OK;
                }
            }
        }
        else
        {
            /* disable global filtering */
            if (gui_filters_enabled)
            {
                gui_filter_global_disable ();
                gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                           _("Message filtering disabled"));
            }
        }
        return WEECHAT_RC_OK;
    }

    /* toggle global filtering or a filter on/off */
    if (string_strcmp (argv[1], "toggle") == 0)
    {
        if (argc > 2)
        {
            if (strcmp (argv[2], "@") == 0)
            {
                /* toggle filters in buffer */
                buffer->filter ^= 1;
                gui_filter_buffer (buffer, NULL);
                (void) hook_signal_send (
                    (buffer->filter) ?
                    "buffer_filters_enabled" : "buffer_filters_disabled",
                    WEECHAT_HOOK_SIGNAL_POINTER, buffer);
            }
            else
            {
                /* toggle a filter */
                ptr_filter = gui_filter_search_by_name (argv[2]);
                if (ptr_filter)
                {
                    ptr_filter->enabled ^= 1;
                    gui_filter_all_buffers (ptr_filter);
                }
                else
                {
                    gui_chat_printf_date_tags (NULL, 0,
                                               GUI_FILTER_TAG_NO_FILTER,
                                               _("%sFilter \"%s\" not found"),
                                               gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                               argv[2]);
                    return WEECHAT_RC_OK;
                }
            }
        }
        else
        {
            if (gui_filters_enabled)
                gui_filter_global_disable ();
            else
                gui_filter_global_enable ();
        }
        return WEECHAT_RC_OK;
    }

    /* add (or add/replace) a filter */
    if ((string_strcmp (argv[1], "add") == 0)
        || (string_strcmp (argv[1], "addreplace") == 0))
    {
        COMMAND_MIN_ARGS(6, argv[1]);

        if ((strcmp (argv[4], "*") == 0) && (strcmp (argv_eol[5], "*") == 0))
        {
            gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                       _("%sYou must specify at least tags "
                                         "or regex for filter"),
                                       gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
            return WEECHAT_RC_OK;
        }

        update = 0;
        if (string_strcmp (argv[1], "addreplace") == 0)
        {
            ptr_filter = gui_filter_search_by_name (argv[2]);
            if (ptr_filter)
            {
                /* disable filter and apply before removing it */
                ptr_filter->enabled = 0;
                gui_filter_all_buffers (ptr_filter);
                gui_filter_free (ptr_filter);
                update = 1;
            }
        }

        ptr_filter = gui_filter_new (1, argv[2], argv[3], argv[4],
                                     argv_eol[5]);
        if (ptr_filter)
        {
            gui_filter_all_buffers (ptr_filter);
            gui_chat_printf (NULL, "");
            gui_chat_printf_date_tags (
                NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                (update) ? _("Filter \"%s\" updated:") : _("Filter \"%s\" added:"),
                argv[2]);
            command_filter_display (ptr_filter);
        }

        return WEECHAT_RC_OK;
    }

    /* recreate a filter */
    if (string_strcmp (argv[1], "recreate") == 0)
    {
        COMMAND_MIN_ARGS(3, "recreate");
        ptr_filter = gui_filter_search_by_name (argv[2]);
        if (ptr_filter)
        {
            snprintf (str_command, sizeof (str_command),
                      "/filter addreplace %s %s %s %s",
                      ptr_filter->name,
                      ptr_filter->buffer_name,
                      ptr_filter->tags,
                      ptr_filter->regex);
            gui_buffer_set (buffer, "input", str_command);
            snprintf (str_pos, sizeof (str_pos),
                      "%d", utf8_strlen (str_command));
            gui_buffer_set (buffer, "input_pos", str_pos);
        }
        else
        {
            gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                       _("%sFilter \"%s\" not found"),
                                       gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                       argv[2]);
        }
        return WEECHAT_RC_OK;
    }

    /* rename a filter */
    if (string_strcmp (argv[1], "rename") == 0)
    {
        COMMAND_MIN_ARGS(4, "rename");
        ptr_filter = gui_filter_search_by_name (argv[2]);
        if (ptr_filter)
        {
            if (gui_filter_rename (ptr_filter, argv[3]))
            {
                gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                           _("Filter \"%s\" renamed to \"%s\""),
                                           argv[2], argv[3]);
            }
            else
            {
                gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                           _("%sUnable to rename filter "
                                             "\"%s\" to \"%s\""),
                                           gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                           argv[2], argv[3]);
            }
        }
        else
        {
            gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                       _("%sFilter \"%s\" not found"),
                                       gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                       argv[2]);
        }
        return WEECHAT_RC_OK;
    }

    /* delete filter */
    if (string_strcmp (argv[1], "del") == 0)
    {
        COMMAND_MIN_ARGS(3, "del");
        if (string_strcmp (argv[2], "-all") == 0)
        {
            if (gui_filters)
            {
                gui_filter_free_all ();
                gui_filter_all_buffers (NULL);
                gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                           _("All filters have been deleted"));
            }
            else
            {
                gui_chat_printf_date_tags (NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                                           _("No message filter defined"));
            }
        }
        else
        {
            for (i = 2; i < argc; i++)
            {
                ptr_filter = gui_filter_search_by_name (argv[i]);
                if (ptr_filter)
                {
                    /* disable filter and apply before removing it */
                    ptr_filter->enabled = 0;
                    gui_filter_all_buffers (ptr_filter);
                    gui_filter_free (ptr_filter);
                    gui_chat_printf_date_tags (
                        NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                        _("Filter \"%s\" deleted"),
                        argv[i]);
                }
                else
                {
                    gui_chat_printf_date_tags (
                        NULL, 0, GUI_FILTER_TAG_NO_FILTER,
                        _("%sFilter \"%s\" not found"),
                        gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                        argv[i]);
                }
            }
        }
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Displays help for commands of a plugin (or core commands if plugin is NULL).
 */

void
command_help_list_plugin_commands (struct t_weechat_plugin *plugin,
                                   int verbose)
{
    struct t_hook *ptr_hook;
    struct t_weelist *list;
    struct t_weelist_item *item;
    struct t_gui_buffer *ptr_buffer;
    int command_found, length, max_length, list_size;
    int cols, lines, col, line, index;
    char str_format[64], str_command[256], str_line[2048];

    if (verbose)
    {
        command_found = 0;
        for (ptr_hook = weechat_hooks[HOOK_TYPE_COMMAND]; ptr_hook;
             ptr_hook = ptr_hook->next_hook)
        {
            if (!ptr_hook->deleted && (ptr_hook->plugin == plugin)
                && HOOK_COMMAND(ptr_hook, command)
                && HOOK_COMMAND(ptr_hook, command)[0])
            {
                if (!command_found)
                {
                    gui_chat_printf (NULL, "");
                    gui_chat_printf (NULL,
                                     "%s[%s%s%s]",
                                     GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                     GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                     plugin_get_name (plugin),
                                     GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS));
                    command_found = 1;
                }
                gui_chat_printf (NULL, "  %s%s%s%s%s",
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 HOOK_COMMAND(ptr_hook, command),
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 (HOOK_COMMAND(ptr_hook, description)
                                  && HOOK_COMMAND(ptr_hook, description)[0]) ?
                                 " - " : "",
                                 (HOOK_COMMAND(ptr_hook, description)
                                  && HOOK_COMMAND(ptr_hook, description)[0]) ?
                                 _(HOOK_COMMAND(ptr_hook, description)) : "");
            }
        }
    }
    else
    {
        ptr_buffer = gui_buffer_search_main ();
        if (!ptr_buffer)
            return;

        max_length = -1;
        list = weelist_new ();

        /*
         * build list of commands for plugin and save max length of command
         * names
         */
        for (ptr_hook = weechat_hooks[HOOK_TYPE_COMMAND]; ptr_hook;
             ptr_hook = ptr_hook->next_hook)
        {
            if (!ptr_hook->deleted && (ptr_hook->plugin == plugin)
                && HOOK_COMMAND(ptr_hook, command)
                && HOOK_COMMAND(ptr_hook, command)[0])
            {
                length = utf8_strlen_screen (HOOK_COMMAND(ptr_hook, command));
                if (length > max_length)
                    max_length = length;
                weelist_add (list, HOOK_COMMAND(ptr_hook, command),
                             WEECHAT_LIST_POS_SORT, NULL);
            }
        }

        /* use list to display commands, sorted by columns */
        list_size = weelist_size (list);
        if ((max_length > 0) && (list_size > 0))
        {
            gui_chat_printf (NULL, "");
            gui_chat_printf (NULL,
                             "%s[%s%s%s]",
                             GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                             GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                             plugin_get_name (plugin),
                             GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS));

            /* auto compute number of columns according to current chat width */
            cols = 1;
            length = gui_current_window->win_chat_width -
                (gui_chat_time_length + 1 +
                 ptr_buffer->lines->buffer_max_length + 1 +
                 ptr_buffer->lines->prefix_max_length + 1 +
                 gui_chat_strlen_screen (CONFIG_STRING(config_look_prefix_suffix)) + 1);
            if (length > 0)
            {
                cols = length / (max_length + 2);
                if (cols == 0)
                    cols = 1;
            }
            lines = ((list_size - 1) / cols) + 1;

            /* build format according to number of columns */
            if (lines == 1)
            {
                snprintf (str_format, sizeof (str_format), "  %%s");
            }
            else
            {
                snprintf (str_format, sizeof (str_format),
                          "  %%-%ds", max_length);
            }

            /* display lines with commands, in columns */
            for (line = 0; line < lines; line++)
            {
                str_line[0] = '\0';
                for (col = 0; col < cols; col++)
                {
                    index = (col * lines) + line;
                    if (index < list_size)
                    {
                        item = weelist_get (list, index);
                        if (item)
                        {
                            if (strlen (str_line) + strlen (weelist_string (item)) + 1 < (int)sizeof (str_line))
                            {
                                snprintf (str_command, sizeof (str_command),
                                          str_format, weelist_string (item));
                                strcat (str_line, str_command);
                            }
                        }
                    }
                }
                gui_chat_printf (NULL, "%s", str_line);
            }
        }

        weelist_free (list);
    }
}

/*
 * Displays help for commands.
 */

void
command_help_list_commands (int verbose)
{
    struct t_weechat_plugin *ptr_plugin;

    /* WeeChat commands */
    command_help_list_plugin_commands (NULL, verbose);

    /* plugins commands */
    for (ptr_plugin = weechat_plugins; ptr_plugin;
         ptr_plugin = ptr_plugin->next_plugin)
    {
        command_help_list_plugin_commands (ptr_plugin, verbose);
    }
}

/*
 * Callback for command "/help": displays help about commands and options.
 */

COMMAND_CALLBACK(help)
{
    struct t_hook *ptr_hook;
    struct t_weechat_plugin *ptr_plugin;
    struct t_config_option *ptr_option;
    int i, length, command_found, first_line_displayed, verbose;
    char *string, *ptr_string, *pos_double_pipe, *pos_end;
    char empty_string[1] = { '\0' }, str_format[64];

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;
    (void) argv_eol;

    /* display help for all commands */
    if ((argc == 1) || (string_strncmp (argv[1], "-list", 5) == 0))
    {
        verbose = ((argc > 1) && (string_strcmp (argv[1], "-listfull") == 0));
        if (argc > 2)
        {
            for (i = 2; i < argc; i++)
            {
                if (string_strcmp (argv[i], PLUGIN_CORE) == 0)
                    command_help_list_plugin_commands (NULL, verbose);
                else
                {
                    ptr_plugin = plugin_search (argv[i]);
                    if (ptr_plugin)
                        command_help_list_plugin_commands (ptr_plugin, verbose);
                }
            }
        }
        else
            command_help_list_commands (verbose);
        return WEECHAT_RC_OK;
    }

    /* look for command */
    command_found = 0;
    for (ptr_hook = weechat_hooks[HOOK_TYPE_COMMAND]; ptr_hook;
         ptr_hook = ptr_hook->next_hook)
    {
        if (!ptr_hook->deleted
            && HOOK_COMMAND(ptr_hook, command)
            && HOOK_COMMAND(ptr_hook, command)[0]
            && (string_strcmp (HOOK_COMMAND(ptr_hook, command), argv[1]) == 0))
        {
            command_found = 1;
            gui_chat_printf (NULL, "");
            length = utf8_strlen_screen (plugin_get_name (ptr_hook->plugin)) +
                ((ptr_hook->subplugin && ptr_hook->subplugin[0]) ? utf8_strlen_screen (ptr_hook->subplugin) + 1 : 0) +
                utf8_strlen_screen (HOOK_COMMAND(ptr_hook, command)) + 7;
            snprintf (str_format, sizeof (str_format),
                      "%%-%ds%%s", length);
            first_line_displayed = 0;
            ptr_string = (HOOK_COMMAND(ptr_hook, args) && HOOK_COMMAND(ptr_hook, args)[0]) ?
                _(HOOK_COMMAND(ptr_hook, args)) : empty_string;
            while (ptr_string)
            {
                string = NULL;
                pos_double_pipe = strstr (ptr_string, "||");
                if (pos_double_pipe)
                {
                    pos_end = pos_double_pipe - 1;
                    while ((pos_end > ptr_string) && (pos_end[0] == ' '))
                    {
                        pos_end--;
                    }
                    string = string_strndup (ptr_string,
                                             pos_end - ptr_string + 1);
                }
                if (first_line_displayed)
                {
                    gui_chat_printf (NULL, str_format,
                                     " ",
                                     (string) ? string : ptr_string);
                }
                else
                {
                    gui_chat_printf (NULL,
                                     "%s[%s%s%s%s%s%s%s]  %s/%s  %s%s",
                                     GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                     GUI_COLOR(GUI_COLOR_CHAT),
                                     plugin_get_name (ptr_hook->plugin),
                                     (ptr_hook->subplugin && ptr_hook->subplugin[0]) ?
                                     GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS) : "",
                                     (ptr_hook->subplugin && ptr_hook->subplugin[0]) ?
                                     "/" : "",
                                     (ptr_hook->subplugin && ptr_hook->subplugin[0]) ?
                                     GUI_COLOR(GUI_COLOR_CHAT) : "",
                                     (ptr_hook->subplugin && ptr_hook->subplugin[0]) ?
                                     ptr_hook->subplugin : "",
                                     GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                     GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                     HOOK_COMMAND(ptr_hook, command),
                                     GUI_COLOR(GUI_COLOR_CHAT),
                                     (string) ? string : ptr_string);
                    first_line_displayed = 1;
                }
                if (string)
                    free (string);

                if (pos_double_pipe)
                {
                    ptr_string = pos_double_pipe + 2;
                    while (ptr_string[0] == ' ')
                    {
                        ptr_string++;
                    }
                }
                else
                    ptr_string = NULL;
            }
            if (HOOK_COMMAND(ptr_hook, description)
                && HOOK_COMMAND(ptr_hook, description)[0])
            {
                gui_chat_printf (NULL, "");
                gui_chat_printf (NULL, "%s",
                                 _(HOOK_COMMAND(ptr_hook, description)));
            }
            if (HOOK_COMMAND(ptr_hook, args_description)
                && HOOK_COMMAND(ptr_hook, args_description)[0])
            {
                gui_chat_printf (NULL, "");
                gui_chat_printf (NULL, "%s",
                                 _(HOOK_COMMAND(ptr_hook, args_description)));
            }
        }
    }
    if (command_found)
        return WEECHAT_RC_OK;

    /* look for option */
    config_file_search_with_string (argv[1], NULL, NULL, &ptr_option, NULL);
    if (ptr_option)
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, _("Option \"%s%s%s\":"),
                         GUI_COLOR(GUI_COLOR_CHAT_CHANNEL),
                         argv[1],
                         GUI_COLOR(GUI_COLOR_CHAT));
        gui_chat_printf (NULL, "  %s: %s",
                         _("description"),
                         (ptr_option->description && ptr_option->description[0]) ?
                         _(ptr_option->description) : "");
        switch (ptr_option->type)
        {
            case CONFIG_OPTION_TYPE_BOOLEAN:
                gui_chat_printf (NULL, "  %s: %s",
                                 _("type"), _("boolean"));
                gui_chat_printf (NULL, "  %s: on, off",
                                 _("values"));
                if (ptr_option->default_value)
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("default value"),
                                     (CONFIG_BOOLEAN_DEFAULT(ptr_option) == CONFIG_BOOLEAN_TRUE) ?
                                     "on" : "off");
                }
                else
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("default value"),
                                     _("(undefined)"));
                }
                if (ptr_option->value)
                {
                    gui_chat_printf (NULL, "  %s: %s%s",
                                     _("current value"),
                                     GUI_COLOR(GUI_COLOR_CHAT_VALUE),
                                     (CONFIG_BOOLEAN(ptr_option) == CONFIG_BOOLEAN_TRUE) ?
                                     "on" : "off");
                }
                else
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("current value"),
                                     _("(undefined)"));
                }
                break;
            case CONFIG_OPTION_TYPE_INTEGER:
                if (ptr_option->string_values)
                {
                    length = 0;
                    i = 0;
                    while (ptr_option->string_values[i])
                    {
                        length += strlen (ptr_option->string_values[i]) + 5;
                        i++;
                    }
                    if (length > 0)
                    {
                        string = malloc (length);
                        if (string)
                        {
                            string[0] = '\0';
                            i = 0;
                            while (ptr_option->string_values[i])
                            {
                                strcat (string, "\"");
                                strcat (string, ptr_option->string_values[i]);
                                strcat (string, "\"");
                                if (ptr_option->string_values[i + 1])
                                    strcat (string, ", ");
                                i++;
                            }
                            gui_chat_printf (NULL, "  %s: %s",
                                             _("type"), _("string"));
                            gui_chat_printf (NULL, "  %s: %s",
                                             _("values"), string);
                            if (ptr_option->default_value)
                            {
                                gui_chat_printf (NULL, "  %s: \"%s\"",
                                                 _("default value"),
                                                 ptr_option->string_values[CONFIG_INTEGER_DEFAULT(ptr_option)]);
                            }
                            else
                            {
                                gui_chat_printf (NULL, "  %s: %s",
                                                 _("default value"),
                                                 _("(undefined)"));
                            }
                            if (ptr_option->value)
                            {
                                gui_chat_printf (NULL,
                                                 "  %s: \"%s%s%s\"",
                                                 _("current value"),
                                                 GUI_COLOR(GUI_COLOR_CHAT_VALUE),
                                                 ptr_option->string_values[CONFIG_INTEGER(ptr_option)],
                                                 GUI_COLOR(GUI_COLOR_CHAT));
                            }
                            else
                            {
                                gui_chat_printf (NULL,
                                                 "  %s: %s",
                                                 _("current value"),
                                                 _("(undefined)"));
                            }
                            free (string);
                        }
                    }
                }
                else
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("type"), _("integer"));
                    gui_chat_printf (NULL, "  %s: %d .. %d",
                                     _("values"),
                                     ptr_option->min, ptr_option->max);
                    if (ptr_option->default_value)
                    {
                        gui_chat_printf (NULL, "  %s: %d",
                                         _("default value"),
                                         CONFIG_INTEGER_DEFAULT(ptr_option));
                    }
                    else
                    {
                        gui_chat_printf (NULL, "  %s: %s",
                                         _("default value"),
                                         _("(undefined)"));
                    }
                    if (ptr_option->value)
                    {
                        gui_chat_printf (NULL, "  %s: %s%d",
                                         _("current value"),
                                         GUI_COLOR(GUI_COLOR_CHAT_VALUE),
                                         CONFIG_INTEGER(ptr_option));
                    }
                    else
                    {
                        gui_chat_printf (NULL, "  %s: %s",
                                         _("current value"),
                                         _("(undefined)"));
                    }
                }
                break;
            case CONFIG_OPTION_TYPE_STRING:
                switch (ptr_option->max)
                {
                    case 0:
                        gui_chat_printf (NULL, "  %s: %s",
                                         _("type"), _("string"));
                        gui_chat_printf (NULL, "  %s: %s",
                                         _("values"), _("any string"));
                        break;
                    case 1:
                        gui_chat_printf (NULL, "  %s: %s",
                                         _("type"), _("string"));
                        gui_chat_printf (NULL, "  %s: %s",
                                         _("values"), _("any char"));
                        break;
                    default:
                        gui_chat_printf (NULL, "  %s: %s",
                                         _("type"), _("string"));
                        gui_chat_printf (NULL, "  %s: %s (%s: %d)",
                                         _("values"), _("any string"),
                                         _("max chars"),
                                         ptr_option->max);
                        break;
                }
                if (ptr_option->default_value)
                {
                    gui_chat_printf (NULL, "  %s: \"%s\"",
                                     _("default value"),
                                     CONFIG_STRING_DEFAULT(ptr_option));
                }
                else
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("default value"),
                                     _("(undefined)"));
                }
                if (ptr_option->value)
                {
                    gui_chat_printf (NULL, "  %s: \"%s%s%s\"",
                                     _("current value"),
                                     GUI_COLOR(GUI_COLOR_CHAT_VALUE),
                                     CONFIG_STRING(ptr_option),
                                     GUI_COLOR(GUI_COLOR_CHAT));
                }
                else
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("current value"),
                                     _("(undefined)"));
                }
                break;
            case CONFIG_OPTION_TYPE_COLOR:
                gui_chat_printf (NULL, "  %s: %s",
                                 _("type"), _("color"));
                gui_chat_printf (NULL, "  %s: %s",
                                 _("values"),
                                 _("a WeeChat color name (default, black, "
                                   "(dark)gray, white, (light)red, (light)green, "
                                   "brown, yellow, (light)blue, (light)magenta, "
                                   "(light)cyan), a terminal color number or "
                                   "an alias; attributes are allowed before "
                                   "color (for text color only, not "
                                   "background): "
                                   "\"%\" for blink, "
                                   "\".\" for \"dim\" (half bright), "
                                   "\"*\" for bold, "
                                   "\"!\" for reverse, "
                                   "\"/\" for italic, "
                                   "\"_\" for underline"));
                if (ptr_option->default_value)
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("default value"),
                                     gui_color_get_name (CONFIG_COLOR_DEFAULT(ptr_option)));
                }
                else
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("default value"),
                                     _("(undefined)"));
                }
                if (ptr_option->value)
                {
                    gui_chat_printf (NULL, "  %s: %s%s",
                                     _("current value"),
                                     GUI_COLOR(GUI_COLOR_CHAT_VALUE),
                                     gui_color_get_name (CONFIG_COLOR(ptr_option)));
                }
                else
                {
                    gui_chat_printf (NULL, "  %s: %s",
                                     _("current value"),
                                     _("(undefined)"));
                }
                break;
            case CONFIG_NUM_OPTION_TYPES:
                break;
        }
        if (ptr_option->null_value_allowed)
        {
            gui_chat_printf (NULL, "  %s",
                             /* TRANSLATORS: please do not translate "(null)" */
                             _("undefined value allowed (null)"));
        }
        return WEECHAT_RC_OK;
    }

    gui_chat_printf (NULL,
                     _("%sNo help available, \"%s\" is not a command or an "
                       "option"),
                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                     argv[1]);

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/history": displays command history for current buffer.
 */

COMMAND_CALLBACK(history)
{
    struct t_gui_history *ptr_history;
    int n, n_total, n_user, displayed;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) argv_eol;

    n_user = CONFIG_INTEGER(config_history_display_default);

    if (argc == 2)
    {
        if (string_strcmp (argv[1], "clear") == 0)
        {
            gui_history_buffer_free (buffer);
            return WEECHAT_RC_OK;
        }
        else
            n_user = atoi (argv[1]);
    }

    if (buffer->history)
    {
        n_total = 1;
        for (ptr_history = buffer->history;
             ptr_history->next_history;
             ptr_history = ptr_history->next_history)
        {
            n_total++;
        }
        displayed = 0;
        for (n = 0; ptr_history; ptr_history = ptr_history->prev_history, n++)
        {
            if ((n_user > 0) && ((n_total - n_user) > n))
                continue;
            if (!displayed)
            {
                gui_chat_printf_date_tags (buffer, 0, "no_log,cmd_history", "");
                gui_chat_printf_date_tags (buffer, 0, "no_log,cmd_history",
                                           _("Buffer command history:"));
            }
            gui_chat_printf_date_tags (buffer, 0, "no_log,cmd_history",
                                       "%s", ptr_history->text);
            displayed = 1;
        }
    }

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/hotlist": manages hotlist.
 */

COMMAND_CALLBACK(hotlist)
{
    int priority;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) argv_eol;

    COMMAND_MIN_ARGS(2, "");

    if (string_strcmp (argv[1], "add") == 0)
    {
        priority = GUI_HOTLIST_LOW;
        if (argc > 2)
        {
            priority = gui_hotlist_search_priority (argv[2]);
            if (priority < 0)
                COMMAND_ERROR;
        }
        gui_hotlist_add (
            buffer,
            priority,
            NULL,  /* creation_time */
            0);  /* check_conditions */
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "clear") == 0)
    {
        gui_hotlist_clear_level_string (buffer, (argc > 2) ? argv[2] : NULL);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "remove") == 0)
    {
        gui_hotlist_remove_buffer (buffer, 1);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "restore") == 0)
    {
        if ((argc > 2) && (string_strcmp (argv[2], "-all") == 0))
            gui_hotlist_restore_all_buffers ();
        else
            gui_hotlist_restore_buffer (buffer);
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Callback for command "/input": input actions (used by key bindings).
 */

COMMAND_CALLBACK(input)
{
    /* make C compiler happy */
    (void) pointer;
    (void) data;

    COMMAND_MIN_ARGS(2, "");

    if (string_strcmp (argv[1], "clipboard_paste") == 0)
        gui_input_clipboard_paste (buffer);
    else if (string_strcmp (argv[1], "return") == 0)
        gui_input_return (buffer);
    else if (string_strcmp (argv[1], "complete_next") == 0)
        gui_input_complete_next (buffer);
    else if (string_strcmp (argv[1], "complete_previous") == 0)
        gui_input_complete_previous (buffer);
    else if (string_strcmp (argv[1], "search_text_here") == 0)
        gui_input_search_text_here (buffer);
    else if (string_strcmp (argv[1], "search_text") == 0)
        gui_input_search_text (buffer);
    else if (string_strcmp (argv[1], "search_previous") == 0)
        gui_input_search_previous (buffer);
    else if (string_strcmp (argv[1], "search_next") == 0)
        gui_input_search_next (buffer);
    else if (string_strcmp (argv[1], "search_switch_case") == 0)
        gui_input_search_switch_case (buffer);
    else if (string_strcmp (argv[1], "search_switch_regex") == 0)
        gui_input_search_switch_regex (buffer);
    else if (string_strcmp (argv[1], "search_switch_where") == 0)
        gui_input_search_switch_where (buffer);
    else if (string_strcmp (argv[1], "search_stop_here") == 0)
        gui_input_search_stop_here (buffer);
    else if (string_strcmp (argv[1], "search_stop") == 0)
        gui_input_search_stop (buffer);
    else if (string_strcmp (argv[1], "delete_previous_char") == 0)
        gui_input_delete_previous_char (buffer);
    else if (string_strcmp (argv[1], "delete_next_char") == 0)
        gui_input_delete_next_char (buffer);
    else if (string_strcmp (argv[1], "delete_previous_word") == 0)
        gui_input_delete_previous_word (buffer);
    else if (string_strcmp (argv[1], "delete_previous_word_whitespace") == 0)
        gui_input_delete_previous_word_whitespace (buffer);
    else if (string_strcmp (argv[1], "delete_next_word") == 0)
        gui_input_delete_next_word (buffer);
    else if (string_strcmp (argv[1], "delete_beginning_of_line") == 0)
        gui_input_delete_beginning_of_line (buffer);
    else if (string_strcmp (argv[1], "delete_end_of_line") == 0)
        gui_input_delete_end_of_line (buffer);
    else if (string_strcmp (argv[1], "delete_line") == 0)
        gui_input_delete_line (buffer);
    else if (string_strcmp (argv[1], "transpose_chars") == 0)
        gui_input_transpose_chars (buffer);
    else if (string_strcmp (argv[1], "move_beginning_of_line") == 0)
        gui_input_move_beginning_of_line (buffer);
    else if (string_strcmp (argv[1], "move_end_of_line") == 0)
        gui_input_move_end_of_line (buffer);
    else if (string_strcmp (argv[1], "move_previous_char") == 0)
        gui_input_move_previous_char (buffer);
    else if (string_strcmp (argv[1], "move_next_char") == 0)
        gui_input_move_next_char (buffer);
    else if (string_strcmp (argv[1], "move_previous_word") == 0)
        gui_input_move_previous_word (buffer);
    else if (string_strcmp (argv[1], "move_next_word") == 0)
        gui_input_move_next_word (buffer);
    else if (string_strcmp (argv[1], "history_previous") == 0)
        gui_input_history_local_previous (buffer);
    else if (string_strcmp (argv[1], "history_next") == 0)
        gui_input_history_local_next (buffer);
    else if (string_strcmp (argv[1], "history_global_previous") == 0)
        gui_input_history_global_previous (buffer);
    else if (string_strcmp (argv[1], "history_global_next") == 0)
        gui_input_history_global_next (buffer);
    else if (string_strcmp (argv[1], "grab_key") == 0)
        gui_input_grab_key (buffer, 0, (argc > 2) ? argv[2] : NULL);
    else if (string_strcmp (argv[1], "grab_key_command") == 0)
        gui_input_grab_key (buffer, 1, (argc > 2) ? argv[2] : NULL);
    else if (string_strcmp (argv[1], "grab_mouse") == 0)
        gui_input_grab_mouse (buffer, 0);
    else if (string_strcmp (argv[1], "grab_mouse_area") == 0)
        gui_input_grab_mouse (buffer, 1);
    else if (string_strcmp (argv[1], "insert") == 0)
    {
        if (argc > 2)
            gui_input_insert (buffer, argv_eol[2]);
    }
    else if (string_strcmp (argv[1], "send") == 0)
        (void) input_data (buffer, argv_eol[2], NULL);
    else if (string_strcmp (argv[1], "undo") == 0)
        gui_input_undo (buffer);
    else if (string_strcmp (argv[1], "redo") == 0)
        gui_input_redo (buffer);
    else if (string_strcmp (argv[1], "paste_start") == 0)
    {
        /* do nothing here */
    }
    else if (string_strcmp (argv[1], "paste_stop") == 0)
    {
        /* do nothing here */
    }
    else
    {
        /*
         * deprecated options kept for compatibility
         * (they may be removed in future)
         */

        /* since WeeChat 3.8: "/buffer jump smart" */
        if (string_strcmp (argv[1], "jump_smart") == 0)
            gui_buffer_jump_smart (gui_current_window);
        /* since WeeChat 1.0: "/buffer +" */
        else if (string_strcmp (argv[1], "jump_last_buffer") == 0)
            gui_buffer_jump_last_visible_number (gui_current_window);
        /* since WeeChat 3.8: "/buffer jump last_displayed" */
        else if (string_strcmp (argv[1], "jump_last_buffer_displayed") == 0)
            gui_buffer_jump_last_buffer_displayed (gui_current_window);
        /* since WeeChat 3.8: "/buffer jump prev_visited" */
        else if (string_strcmp (argv[1], "jump_previously_visited_buffer") == 0)
            gui_buffer_jump_previously_visited_buffer (gui_current_window);
        /* since WeeChat 3.8: "/buffer jump next_visited" */
        else if (string_strcmp (argv[1], "jump_next_visited_buffer") == 0)
            gui_buffer_jump_next_visited_buffer (gui_current_window);
        /* since WeeChat 3.8: "/hotlist clear" */
        else if (string_strcmp (argv[1], "hotlist_clear") == 0)
            gui_hotlist_clear_level_string (buffer, (argc > 2) ? argv[2] : NULL);
        /* since WeeChat 3.8: "/hotlist remove" */
        else if (string_strcmp (argv[1], "hotlist_remove_buffer") == 0)
            gui_hotlist_remove_buffer (buffer, 1);
        /* since WeeChat 3.8: "/hotlist restore" */
        else if (string_strcmp (argv[1], "hotlist_restore_buffer") == 0)
            gui_hotlist_restore_buffer (buffer);
        /* since WeeChat 3.8: "/hotlist restore -all" */
        else if (string_strcmp (argv[1], "hotlist_restore_all") == 0)
            gui_hotlist_restore_all_buffers ();
        /* since WeeChat 3.8: "/buffer set unread" */
        else if (string_strcmp (argv[1], "set_unread_current_buffer") == 0)
            (void) input_data (buffer, "/buffer set unread", NULL);
        /* since WeeChat 3.8: "/allbuf /buffer set unread" */
        else if (string_strcmp (argv[1], "set_unread") == 0)
            (void) input_data (buffer, "/allbuf /buffer set unread", NULL);
        /* since WeeChat 3.8: "/buffer switch" */
        else if (string_strcmp (argv[1], "switch_active_buffer") == 0)
            gui_buffer_switch_active_buffer (buffer);
        /* since WeeChat 3.8: "/buffer switch previous" */
        else if (string_strcmp (argv[1], "switch_active_buffer_previous") == 0)
            gui_buffer_switch_active_buffer_previous (buffer);
        /* since WeeChat 3.8: "/buffer zoom" */
        else if (string_strcmp (argv[1], "zoom_merged_buffer") == 0)
            gui_buffer_zoom (buffer);
        else
            COMMAND_ERROR;
    }

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/item": manages custom bar items
 */

COMMAND_CALLBACK(item)
{
    struct t_gui_bar_item_custom *ptr_bar_item_custom;
    char str_command[4096], str_pos[16], **sargv;
    int i, update, sargc;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    if ((argc == 1) || (string_strcmp (argv[1], "list") == 0))
    {
        /* display all custom bar items */
        if (gui_custom_bar_items)
        {
            gui_chat_printf (NULL, "");
            gui_chat_printf (NULL,
                             _("Custom bar items:"));
            for (ptr_bar_item_custom = gui_custom_bar_items; ptr_bar_item_custom;
                 ptr_bar_item_custom = ptr_bar_item_custom->next_item)
            {
                gui_chat_printf (
                    NULL, "  %s:", ptr_bar_item_custom->bar_item->name);
                gui_chat_printf (NULL, _("    conditions: %s\"%s%s%s\"%s"),
                                 GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 CONFIG_STRING(ptr_bar_item_custom->options[GUI_BAR_ITEM_CUSTOM_OPTION_CONDITIONS]),
                                 GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                 GUI_COLOR(GUI_COLOR_CHAT));
                gui_chat_printf (NULL, _("    content: %s\"%s%s%s\"%s"),
                                 GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 CONFIG_STRING(ptr_bar_item_custom->options[GUI_BAR_ITEM_CUSTOM_OPTION_CONTENT]),
                                 GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                 GUI_COLOR(GUI_COLOR_CHAT));
            }
        }
        else
        {
            gui_chat_printf (NULL, _("No custom bar item defined"));
        }

        return WEECHAT_RC_OK;
    }

    /* add (or add/replace) a custom bar item */
    if ((string_strcmp (argv[1], "add") == 0)
        || (string_strcmp (argv[1], "addreplace") == 0))
    {
        sargv = string_split_shell (argv_eol[2], &sargc);
        if (!sargv || (sargc < 3))
        {
            if (sargv)
                string_free_split (sargv);
            COMMAND_ERROR;
        }

        update = 0;
        if (string_strcmp (argv[1], "addreplace") == 0)
        {
            ptr_bar_item_custom = gui_bar_item_custom_search (sargv[0]);
            if (ptr_bar_item_custom)
            {
                gui_bar_item_custom_free (ptr_bar_item_custom);
                update = 1;
            }
        }

        ptr_bar_item_custom = gui_bar_item_custom_new (sargv[0], sargv[1],
                                                       sargv[2]);
        if (ptr_bar_item_custom)
        {
            gui_chat_printf (NULL,
                             (update) ?
                             _("Custom bar item \"%s\" updated") :
                             _("Custom bar item \"%s\" added"),
                             sargv[0]);
        }
        else
        {
            gui_chat_printf (NULL,
                             _("%sUnable to add custom bar item \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             sargv[0]);
        }

        string_free_split (sargv);

        return WEECHAT_RC_OK;
    }

    /* refresh bar items */
    if (string_strcmp (argv[1], "refresh") == 0)
    {
        for (i = 2; i < argc; i++)
        {
            gui_bar_item_update (argv[i]);
        }
        return WEECHAT_RC_OK;
    }

    /* recreate a custom bar item */
    if (string_strcmp (argv[1], "recreate") == 0)
    {
        COMMAND_MIN_ARGS(3, "recreate");
        ptr_bar_item_custom = gui_bar_item_custom_search (argv[2]);
        if (ptr_bar_item_custom)
        {
            snprintf (str_command, sizeof (str_command),
                      "/item addreplace %s \"%s\" \"%s\"",
                      ptr_bar_item_custom->bar_item->name,
                      CONFIG_STRING(ptr_bar_item_custom->options[GUI_BAR_ITEM_CUSTOM_OPTION_CONDITIONS]),
                      CONFIG_STRING(ptr_bar_item_custom->options[GUI_BAR_ITEM_CUSTOM_OPTION_CONTENT]));
            gui_buffer_set (buffer, "input", str_command);
            snprintf (str_pos, sizeof (str_pos),
                      "%d", utf8_strlen (str_command));
            gui_buffer_set (buffer, "input_pos", str_pos);
        }
        else
        {
            gui_chat_printf (NULL,
                             _("%sCustom bar item \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
        }
        return WEECHAT_RC_OK;
    }

    /* rename a custom bar item */
    if (string_strcmp (argv[1], "rename") == 0)
    {
        COMMAND_MIN_ARGS(4, "rename");
        ptr_bar_item_custom = gui_bar_item_custom_search (argv[2]);
        if (ptr_bar_item_custom)
        {
            if (gui_bar_item_custom_rename (ptr_bar_item_custom, argv[3]))
            {
                gui_chat_printf (NULL,
                                 _("Custom bar item \"%s\" renamed to \"%s\""),
                                 argv[2], argv[3]);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("%sUnable to rename custom bar item "
                                   "\"%s\" to \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2], argv[3]);
            }
        }
        else
        {
            gui_chat_printf (NULL,
                             _("%sCustom bar item \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
        }
        return WEECHAT_RC_OK;
    }

    /* delete a custom bar item */
    if (string_strcmp (argv[1], "del") == 0)
    {
        COMMAND_MIN_ARGS(3, "del");
        if (string_strcmp (argv[2], "-all") == 0)
        {
            if (gui_custom_bar_items)
            {
                gui_bar_item_custom_free_all ();
                gui_chat_printf (NULL,
                                 _("All custom bar items have been deleted"));
            }
            else
            {
                gui_chat_printf (NULL, _("No custom bar item defined"));
            }
        }
        else
        {
            for (i = 2; i < argc; i++)
            {
                ptr_bar_item_custom = gui_bar_item_custom_search (argv[i]);
                if (ptr_bar_item_custom)
                {
                    gui_bar_item_custom_free (ptr_bar_item_custom);
                    gui_chat_printf (NULL,
                                     _("Custom bar item \"%s\" deleted"),
                                     argv[i]);
                }
                else
                {
                    gui_chat_printf (NULL,
                                     _("%sCustom bar item \"%s\" not found"),
                                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                     argv[i]);
                }
            }
        }
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Displays a key binding.
 */

void
command_key_display (struct t_gui_key *key, struct t_gui_key *default_key)
{
    char *expanded_name;

    expanded_name = gui_key_get_expanded_name (key->key);

    if (default_key)
    {
        gui_chat_printf (NULL, "  %s%s => %s%s  %s(%s%s %s%s)",
                         (expanded_name) ? expanded_name : key->key,
                         GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                         GUI_COLOR(GUI_COLOR_CHAT),
                         key->command,
                         GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                         GUI_COLOR(GUI_COLOR_CHAT),
                         _("default command:"),
                         default_key->command,
                         GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS));
    }
    else
    {
        gui_chat_printf (NULL, "  %s%s => %s%s",
                         (expanded_name) ? expanded_name : key->key,
                         GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                         GUI_COLOR(GUI_COLOR_CHAT),
                         key->command);
    }

    if (expanded_name)
        free (expanded_name);
}

/*
 * Displays a list of keys.
 */

void
command_key_display_list (const char *message_no_key,
                          const char *message_keys,
                          int context,
                          struct t_gui_key *keys,
                          int keys_count)
{
    struct t_gui_key *ptr_key;

    if (keys_count == 0)
        gui_chat_printf (NULL, message_no_key,
                         gui_key_context_string[context]);
    else
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, message_keys,
                         keys_count, gui_key_context_string[context]);
        for (ptr_key = keys; ptr_key; ptr_key = ptr_key->next_key)
        {
            command_key_display (ptr_key, NULL);
        }
    }
}

/*
 * Displays differences between default and current keys (keys added, redefined
 * or removed).
 */

void
command_key_display_listdiff (int context)
{
    struct t_gui_key *ptr_key, *ptr_default_key;
    int count_added, count_deleted;

    /* list keys added or redefined */
    count_added = 0;
    for (ptr_key = gui_keys[context]; ptr_key; ptr_key = ptr_key->next_key)
    {
        ptr_default_key = gui_key_search (gui_default_keys[context],
                                          ptr_key->key);
        if (!ptr_default_key
            || (strcmp (ptr_default_key->command, ptr_key->command) != 0))
        {
            count_added++;
        }
    }
    if (count_added > 0)
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL,
                         /* TRANSLATORS: first "%d" is number of keys */
                         _("%d key bindings added or redefined for "
                           "context \"%s\":"),
                         count_added,
                         _(gui_key_context_string[context]));
        for (ptr_key = gui_keys[context]; ptr_key; ptr_key = ptr_key->next_key)
        {
            ptr_default_key = gui_key_search (gui_default_keys[context],
                                              ptr_key->key);
            if (!ptr_default_key
                || (strcmp (ptr_default_key->command, ptr_key->command) != 0))
            {
                command_key_display (ptr_key, ptr_default_key);
            }
        }
    }

    /* list keys deleted */
    count_deleted = 0;
    for (ptr_default_key = gui_default_keys[context]; ptr_default_key;
         ptr_default_key = ptr_default_key->next_key)
    {
        ptr_key = gui_key_search (gui_keys[context], ptr_default_key->key);
        if (!ptr_key)
            count_deleted++;
        }
    if (count_deleted > 0)
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL,
                         /* TRANSLATORS: first "%d" is number of keys */
                         _("%d key bindings deleted for context \"%s\":"),
                         count_deleted,
                         _(gui_key_context_string[context]));
        for (ptr_default_key = gui_default_keys[context]; ptr_default_key;
             ptr_default_key = ptr_default_key->next_key)
        {
            ptr_key = gui_key_search (gui_keys[context], ptr_default_key->key);
            if (!ptr_key)
            {
                command_key_display (ptr_default_key, NULL);
            }
        }
    }

    /* display a message if all key bindings are default bindings */
    if ((count_added == 0) && (count_deleted == 0))
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL,
                         _("No key binding added, redefined or removed "
                           "for context \"%s\""),
                         _(gui_key_context_string[context]));
    }
}

/*
 * Resets a key for a given context.
 */

int
command_key_reset (int context, const char *key)
{
    char *internal_code;
    struct t_gui_key *ptr_key, *ptr_default_key, *ptr_new_key;
    int rc;

    internal_code = gui_key_get_internal_code (key);
    if (!internal_code)
        return WEECHAT_RC_ERROR;

    ptr_key = gui_key_search (gui_keys[context],
                              internal_code);
    ptr_default_key = gui_key_search (gui_default_keys[context],
                                      internal_code);
    free (internal_code);

    if (ptr_key || ptr_default_key)
    {
        if (ptr_key && ptr_default_key)
        {
            if (strcmp (ptr_key->command, ptr_default_key->command) != 0)
            {
                gui_key_verbose = 1;
                ptr_new_key = gui_key_bind (NULL, context, key,
                                            ptr_default_key->command);
                gui_key_verbose = 0;
                if (!ptr_new_key)
                {
                    gui_chat_printf (NULL,
                                     _("%sUnable to bind key \"%s\""),
                                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                     key);
                    return WEECHAT_RC_OK;
                }
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("Key \"%s\" has already default "
                                   "value"),
                                 key);
            }
        }
        else if (ptr_key)
        {
            /* no default key, so just unbind key */
            gui_key_verbose = 1;
            rc = gui_key_unbind (NULL, context, key);
            gui_key_verbose = 0;
            if (!rc)
            {
                gui_chat_printf (NULL,
                                 _("%sUnable to unbind key \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 key);
                return WEECHAT_RC_OK;
            }
        }
        else
        {
            /* no key, but default key exists */
            gui_key_verbose = 1;
            ptr_new_key = gui_key_bind (NULL, context, key,
                                        ptr_default_key->command);
            gui_key_verbose = 0;
            if (!ptr_new_key)
            {
                gui_chat_printf (NULL,
                                 _("%sUnable to bind key \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 key);
                return WEECHAT_RC_OK;
            }
        }
    }
    else
    {
        gui_chat_printf (NULL, _("%sKey \"%s\" not found"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                         key);
    }
    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/key": binds/unbinds keys.
 */

COMMAND_CALLBACK(key)
{
    char *internal_code;
    struct t_gui_key *ptr_new_key;
    int old_keys_count, keys_added, i, context, rc;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    /* display all key bindings (current keys) */
    if ((argc == 1) || (string_strcmp (argv[1], "list") == 0))
    {
        for (i = 0; i < GUI_KEY_NUM_CONTEXTS; i++)
        {
            if ((argc < 3)
                || (string_strcmp (argv[2], gui_key_context_string[i]) == 0))
            {
                command_key_display_list (_("No key binding defined for "
                                            "context \"%s\""),
                                          /* TRANSLATORS: first "%d" is number of keys */
                                          _("%d key bindings for context "
                                            "\"%s\":"),
                                          i, gui_keys[i], gui_keys_count[i]);
            }
        }
        return WEECHAT_RC_OK;
    }

    /* display redefined or key bindings added */
    if (string_strcmp (argv[1], "listdiff") == 0)
    {
        for (i = 0; i < GUI_KEY_NUM_CONTEXTS; i++)
        {
            if ((argc < 3)
                || (string_strcmp (argv[2], gui_key_context_string[i]) == 0))
            {
                command_key_display_listdiff (i);
            }
        }
        return WEECHAT_RC_OK;
    }

    /* display default key bindings */
    if (string_strcmp (argv[1], "listdefault") == 0)
    {
        for (i = 0; i < GUI_KEY_NUM_CONTEXTS; i++)
        {
            if ((argc < 3)
                || (string_strcmp (argv[2], gui_key_context_string[i]) == 0))
            {
                command_key_display_list (_("No default key binding for "
                                            "context \"%s\""),
                                          /* TRANSLATORS: first "%d" is number of keys */
                                          _("%d default key bindings for "
                                            "context \"%s\":"),
                                          i,
                                          gui_default_keys[i],
                                          gui_default_keys_count[i]);
            }
        }
        return WEECHAT_RC_OK;
    }

    /* bind a key (or display binding) */
    if (string_strcmp (argv[1], "bind") == 0)
    {
        COMMAND_MIN_ARGS(3, "bind");

        /* display a key binding */
        if (argc == 3)
        {
            ptr_new_key = NULL;
            internal_code = gui_key_get_internal_code (argv[2]);
            if (internal_code)
            {
                ptr_new_key = gui_key_search (gui_keys[GUI_KEY_CONTEXT_DEFAULT],
                                              internal_code);
            }
            if (ptr_new_key)
            {
                gui_chat_printf (NULL, "");
                gui_chat_printf (NULL, _("Key:"));
                command_key_display (ptr_new_key, NULL);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("No key found"));
            }
            if (internal_code)
                free (internal_code);
            return WEECHAT_RC_OK;
        }

        /* bind new key */
        if (CONFIG_BOOLEAN(config_look_key_bind_safe)
            && !gui_key_is_safe (GUI_KEY_CONTEXT_DEFAULT, argv[2]))
        {
            gui_chat_printf (NULL,
                             _("%sIt is not safe to bind key \"%s\" because "
                               "it does not start with a ctrl or meta code "
                               "(tip: use alt-k to find key codes); if you "
                               "want to bind this key anyway, turn off option "
                               "weechat.look.key_bind_safe"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        gui_key_verbose = 1;
        ptr_new_key = gui_key_bind (NULL, GUI_KEY_CONTEXT_DEFAULT,
                                    argv[2], argv_eol[3]);
        gui_key_verbose = 0;
        if (!ptr_new_key)
        {
            gui_chat_printf (NULL,
                             _("%sUnable to bind key \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        return WEECHAT_RC_OK;
    }

    /* bind a key for given context (or display binding) */
    if (string_strcmp (argv[1], "bindctxt") == 0)
    {
        COMMAND_MIN_ARGS(4, "bindctxt");

        /* search context */
        context = gui_key_search_context (argv[2]);
        if (context < 0)
        {
            gui_chat_printf (NULL,
                             _("%sContext \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }

        /* display a key binding */
        if (argc == 4)
        {
            ptr_new_key = NULL;
            internal_code = gui_key_get_internal_code (argv[2]);
            if (internal_code)
                ptr_new_key = gui_key_search (gui_keys[context],
                                              internal_code);
            if (ptr_new_key)
            {
                gui_chat_printf (NULL, "");
                gui_chat_printf (NULL, _("Key:"));
                command_key_display (ptr_new_key, NULL);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("No key found"));
            }
            if (internal_code)
                free (internal_code);
            return WEECHAT_RC_OK;
        }

        /* bind new key */
        if (CONFIG_BOOLEAN(config_look_key_bind_safe)
            && !gui_key_is_safe (context, argv[3]))
        {
            gui_chat_printf (NULL,
                             _("%sIt is not safe to bind key \"%s\" because "
                               "it does not start with a ctrl or meta code "
                               "(tip: use alt-k to find key codes); if you "
                               "want to bind this key anyway, turn off option "
                               "weechat.look.key_bind_safe"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[3]);
            return WEECHAT_RC_OK;
        }

        gui_key_verbose = 1;
        ptr_new_key = gui_key_bind (NULL, context,
                                    argv[3], argv_eol[4]);
        gui_key_verbose = 0;
        if (!ptr_new_key)
        {
            gui_chat_printf (NULL,
                             _("%sUnable to bind key \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[3]);
            return WEECHAT_RC_OK;
        }

        return WEECHAT_RC_OK;
    }

    /* unbind a key */
    if (string_strcmp (argv[1], "unbind") == 0)
    {
        COMMAND_MIN_ARGS(3, "unbind");

        gui_key_verbose = 1;
        rc = gui_key_unbind (NULL, GUI_KEY_CONTEXT_DEFAULT, argv[2]);
        gui_key_verbose = 0;
        if (!rc)
        {
            gui_chat_printf (NULL,
                             _("%sUnable to unbind key \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }

        return WEECHAT_RC_OK;
    }

    /* unbind a key for a given context */
    if (string_strcmp (argv[1], "unbindctxt") == 0)
    {
        COMMAND_MIN_ARGS(4, "unbindctxt");

        /* search context */
        context = gui_key_search_context (argv[2]);
        if (context < 0)
        {
            gui_chat_printf (NULL,
                             _("%sContext \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }

        gui_key_verbose = 1;
        rc = gui_key_unbind (NULL, context, argv[3]);
        gui_key_verbose = 0;
        if (!rc)
        {
            gui_chat_printf (NULL,
                             _("%sUnable to unbind key \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[3]);
            return WEECHAT_RC_OK;
        }

        return WEECHAT_RC_OK;
    }

    /* reset a key to default binding */
    if (string_strcmp (argv[1], "reset") == 0)
    {
        COMMAND_MIN_ARGS(3, "reset");
        return command_key_reset (GUI_KEY_CONTEXT_DEFAULT, argv[2]);
    }

    /* reset a key to default binding for a given context */
    if (string_strcmp (argv[1], "resetctxt") == 0)
    {
        COMMAND_MIN_ARGS(4, "resetctxt");

        /* search context */
        context = gui_key_search_context (argv[2]);
        if (context < 0)
        {
            gui_chat_printf (NULL,
                             _("%sContext \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }

        return command_key_reset (context, argv[3]);
    }

    /* reset ALL keys (only with "-yes", for security reason) */
    if (string_strcmp (argv[1], "resetall") == 0)
    {
        if ((argc >= 3) && (string_strcmp (argv[2], "-yes") == 0))
        {
            for (i = 0; i < GUI_KEY_NUM_CONTEXTS; i++)
            {
                if ((argc < 4)
                    || (string_strcmp (argv[3], gui_key_context_string[i]) == 0))
                {
                    gui_key_free_all (&gui_keys[i], &last_gui_key[i],
                                      &gui_keys_count[i]);
                    gui_key_default_bindings (i);
                    gui_chat_printf (NULL,
                                     _("Default key bindings restored for "
                                       "context \"%s\""),
                                     gui_key_context_string[i]);
                }
            }
        }
        else
        {
            gui_chat_printf (NULL,
                             _("%sArgument \"-yes\" is required for "
                               "keys reset (security reason)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
            return WEECHAT_RC_OK;
        }
        return WEECHAT_RC_OK;
    }

    /* add missing keys */
    if (string_strcmp (argv[1], "missing") == 0)
    {
        for (i = 0; i < GUI_KEY_NUM_CONTEXTS; i++)
        {
            if ((argc < 3)
                || (string_strcmp (argv[2], gui_key_context_string[i]) == 0))
            {
                old_keys_count = gui_keys_count[i];
                gui_key_verbose = 1;
                gui_key_default_bindings (i);
                gui_key_verbose = 0;
                keys_added = (gui_keys_count[i] > old_keys_count) ?
                    gui_keys_count[i] - old_keys_count : 0;
                gui_chat_printf (NULL,
                                 NG_("%d new key added", "%d new keys added "
                                     "(context: \"%s\")", keys_added),
                                 keys_added,
                                 gui_key_context_string[i]);
            }
        }
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Displays a tree of windows.
 */

void
command_layout_display_tree (struct t_gui_layout_window *layout_window,
                             int indent)
{
    char format[128];

    if (layout_window)
    {
        if (layout_window->plugin_name)
        {
            /* leaf */
            snprintf (format, sizeof (format), "%%-%ds%s",
                      (indent + 1) * 2,
                      "|-- %s.%s");
            gui_chat_printf (NULL, format,
                             " ",
                             (layout_window->plugin_name) ? layout_window->plugin_name : "-",
                             (layout_window->buffer_name) ? layout_window->buffer_name : "-");
        }
        else
        {
            /* node */
            snprintf (format, sizeof (format), "%%-%ds%s",
                      (indent + 1) * 2,
                      "%s== %d%% (split%s)");
            gui_chat_printf (NULL, format,
                             " ",
                             (indent == 1) ? "\\" : "|",
                             layout_window->split_pct,
                             (layout_window->split_horiz) ? "h" : "v");
        }

        if (layout_window->child1)
            command_layout_display_tree (layout_window->child1, indent + 1);

        if (layout_window->child2)
            command_layout_display_tree (layout_window->child2, indent + 1);
    }
}

/*
 * Gets arguments for /layout command (if option is store/apply/del).
 */

void
command_layout_get_arguments (int argc, char **argv,
                              const char **layout_name,
                              struct t_gui_layout **ptr_layout,
                              int *flag_buffers, int *flag_windows)
{
    int i;

    *layout_name = NULL;
    *ptr_layout = NULL;
    *flag_buffers = 1;
    *flag_windows = 1;

    for (i = 2; i < argc; i++)
    {
        if (string_strcmp (argv[i], "buffers") == 0)
            *flag_windows = 0;
        else if (string_strcmp (argv[i], "windows") == 0)
            *flag_buffers = 0;
        else if (!*layout_name)
            *layout_name = argv[i];
    }

    if (*layout_name)
        *ptr_layout = gui_layout_search (*layout_name);
    else
    {
        *ptr_layout = gui_layout_current;
        if (!*ptr_layout)
            *ptr_layout = gui_layout_search (GUI_LAYOUT_DEFAULT_NAME);
    }
}

/*
 * Callback for command "/layout": manages layouts.
 */

COMMAND_CALLBACK(layout)
{
    struct t_gui_layout *ptr_layout, *ptr_layout2;
    struct t_gui_layout_buffer *ptr_layout_buffer;
    const char *layout_name;
    char *name;
    int flag_buffers, flag_windows, layout_is_current;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;
    (void) argv_eol;

    /* display all layouts */
    if (argc == 1)
    {
        /* display stored layouts */
        if (gui_layouts)
        {
            gui_chat_printf (NULL, "");
            gui_chat_printf (NULL, _("Stored layouts:"));
            for (ptr_layout = gui_layouts; ptr_layout;
                 ptr_layout = ptr_layout->next_layout)
            {
                gui_chat_printf (NULL, "  %s%s%s%s:",
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 ptr_layout->name,
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 (ptr_layout == gui_layout_current) ? _(" (current layout)") : "");
                for (ptr_layout_buffer = ptr_layout->layout_buffers;
                     ptr_layout_buffer;
                     ptr_layout_buffer = ptr_layout_buffer->next_layout)
                {
                    gui_chat_printf (NULL, "    %d. %s.%s",
                                     ptr_layout_buffer->number,
                                     ptr_layout_buffer->plugin_name,
                                     ptr_layout_buffer->buffer_name);
                }
                if (ptr_layout->layout_windows)
                    command_layout_display_tree (ptr_layout->layout_windows, 1);
            }
        }
        else
            gui_chat_printf (NULL, _("No stored layouts"));

        return WEECHAT_RC_OK;
    }

    /* store in a layout */
    if (string_strcmp (argv[1], "store") == 0)
    {
        command_layout_get_arguments (argc, argv, &layout_name, &ptr_layout,
                                      &flag_buffers, &flag_windows);
        if (!ptr_layout)
        {
            ptr_layout = gui_layout_alloc ((layout_name) ? layout_name : GUI_LAYOUT_DEFAULT_NAME);
            if (!ptr_layout)
                COMMAND_ERROR;
            gui_layout_add (ptr_layout);
        }
        if (flag_buffers)
            gui_layout_buffer_store (ptr_layout);
        if (flag_windows)
            gui_layout_window_store (ptr_layout);
        gui_layout_current = ptr_layout;
        gui_chat_printf (NULL,
                         /* TRANSLATORS: %s%s%s is "buffers" or "windows" or "buffers+windows" */
                         _("Layout of %s%s%s stored in \"%s\" (current layout: %s)"),
                         (flag_buffers) ? _("buffers") : "",
                         (flag_buffers && flag_windows) ? "+" : "",
                         (flag_windows) ? _("windows") : "",
                         ptr_layout->name,
                         ptr_layout->name);

        return WEECHAT_RC_OK;
    }

    /* apply layout */
    if (string_strcmp (argv[1], "apply") == 0)
    {
        command_layout_get_arguments (argc, argv, &layout_name, &ptr_layout,
                                      &flag_buffers, &flag_windows);
        if (ptr_layout)
        {
            if (flag_buffers)
                gui_layout_buffer_apply (ptr_layout);
            if (flag_windows)
                gui_layout_window_apply (ptr_layout, -1);
            gui_layout_current = ptr_layout;
        }

        return WEECHAT_RC_OK;
    }

    /* leave current layout */
    if (string_strcmp (argv[1], "leave") == 0)
    {
        gui_layout_buffer_reset ();
        gui_layout_window_reset ();

        gui_layout_current = NULL;

        gui_chat_printf (NULL,
                         _("Layout of buffers+windows reset (current layout: -)"));

        return WEECHAT_RC_OK;
    }

    /* delete layout */
    if (string_strcmp (argv[1], "del") == 0)
    {
        command_layout_get_arguments (argc, argv, &layout_name, &ptr_layout,
                                      &flag_buffers, &flag_windows);
        if (ptr_layout)
        {
            layout_is_current = (ptr_layout == gui_layout_current);
            if (flag_buffers && flag_windows)
            {
                name = strdup (ptr_layout->name);
                gui_layout_remove (ptr_layout);
                if (layout_is_current)
                {
                    gui_layout_buffer_reset ();
                    gui_layout_window_reset ();
                }
                gui_chat_printf (NULL,
                                 _("Layout \"%s\" deleted (current layout: %s)"),
                                 name,
                                 (gui_layout_current) ? gui_layout_current->name : "-");
                if (name)
                    free (name);
            }
            else
            {
                if (flag_buffers)
                {
                    gui_layout_buffer_remove_all (ptr_layout);
                    if (layout_is_current)
                        gui_layout_buffer_reset ();
                }
                else if (flag_windows)
                {
                    gui_layout_window_remove_all (ptr_layout);
                    if (layout_is_current)
                        gui_layout_window_reset ();
                }
                gui_chat_printf (NULL,
                                 /* TRANSLATORS: %s%s%s is "buffers" or "windows" or "buffers+windows" */
                                 _("Layout of %s%s%s reset in \"%s\""),
                                 (flag_buffers) ? _("buffers") : "",
                                 (flag_buffers && flag_windows) ? "+" : "",
                                 (flag_windows) ? _("windows") : "",
                                 ptr_layout->name);
            }
        }

        return WEECHAT_RC_OK;
    }

    /* rename layout */
    if (string_strcmp (argv[1], "rename") == 0)
    {
        COMMAND_MIN_ARGS(4, "rename");
        ptr_layout = gui_layout_search (argv[2]);
        if (!ptr_layout)
        {
            gui_chat_printf (NULL,
                             _("%sLayout \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        ptr_layout2 = gui_layout_search (argv[3]);
        if (ptr_layout2)
        {
            gui_chat_printf (NULL,
                             _("%sLayout \"%s\" already exists for "
                               "\"%s\" command"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[3], "layout rename");
            return WEECHAT_RC_OK;
        }
        gui_layout_rename (ptr_layout, argv[3]);
        gui_chat_printf (NULL, _("Layout \"%s\" has been renamed to \"%s\""),
                         argv[2], argv[3]);
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Callback for mouse timer.
 */

int
command_mouse_timer_cb (const void *pointer, void *data, int remaining_calls)
{
    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) remaining_calls;

    config_file_option_set (config_look_mouse,
                            (gui_mouse_enabled) ? "0" : "1",
                            1);

    return WEECHAT_RC_OK;
}

/*
 * Timer for toggling mouse.
 */

void
command_mouse_timer (const char *delay)
{
    long seconds;
    char *error;

    error = NULL;
    seconds = strtol (delay, &error, 10);
    if (error && !error[0] && (seconds > 0))
    {
        hook_timer (NULL, seconds * 1000, 0, 1,
                    &command_mouse_timer_cb, NULL, NULL);
    }
}

/*
 * Callback for command "/mouse": controls mouse.
 */

COMMAND_CALLBACK(mouse)
{
    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;
    (void) argv_eol;

    if (argc == 1)
    {
        gui_mouse_display_state ();
        return WEECHAT_RC_OK;
    }

    /* enable mouse */
    if (string_strcmp (argv[1], "enable") == 0)
    {
        config_file_option_set (config_look_mouse, "1", 1);
        gui_chat_printf (NULL, _("Mouse enabled"));
        if (argc > 2)
            command_mouse_timer (argv[2]);
        return WEECHAT_RC_OK;
    }

    /* disable mouse */
    if (string_strcmp (argv[1], "disable") == 0)
    {
        config_file_option_set (config_look_mouse, "0", 1);
        gui_chat_printf (NULL, _("Mouse disabled"));
        if (argc > 2)
            command_mouse_timer (argv[2]);
        return WEECHAT_RC_OK;
    }

    /* toggle mouse */
    if (string_strcmp (argv[1], "toggle") == 0)
    {
        if (gui_mouse_enabled)
        {
            config_file_option_set (config_look_mouse, "0", 1);
            gui_chat_printf (NULL, _("Mouse disabled"));
        }
        else
        {
            config_file_option_set (config_look_mouse, "1", 1);
            gui_chat_printf (NULL, _("Mouse enabled"));
        }
        if (argc > 2)
            command_mouse_timer (argv[2]);
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Callback for command "/mute": silently executes a command.
 */

COMMAND_CALLBACK(mute)
{
    int length, mute_mode, gui_chat_mute_old;
    char *command, *ptr_command;
    struct t_gui_buffer *mute_buffer, *ptr_buffer, *gui_chat_mute_buffer_old;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    if (argc < 2)
    {
        /* silently ignore missing arguments ("/mute" does nothing) */
        return WEECHAT_RC_OK;
    }

    gui_chat_mute_old = gui_chat_mute;
    gui_chat_mute_buffer_old = gui_chat_mute_buffer;

    mute_mode = GUI_CHAT_MUTE_ALL_BUFFERS;
    mute_buffer = NULL;

    ptr_command = argv_eol[1];

    if (string_strcmp (argv[1], "-core") == 0)
    {
        mute_mode = GUI_CHAT_MUTE_BUFFER;
        mute_buffer = gui_buffer_search_main ();
        ptr_command = argv_eol[2];
    }
    else if (string_strcmp (argv[1], "-current") == 0)
    {
        mute_mode = GUI_CHAT_MUTE_BUFFER;
        mute_buffer = buffer;
        ptr_command = argv_eol[2];
    }
    else if (string_strcmp (argv[1], "-buffer") == 0)
    {
        COMMAND_MIN_ARGS(3, "-buffer");
        ptr_buffer = gui_buffer_search_by_full_name (argv[2]);
        if (ptr_buffer)
        {
            mute_mode = GUI_CHAT_MUTE_BUFFER;
            mute_buffer = ptr_buffer;
        }
        ptr_command = argv_eol[3];
    }
    else if (string_strcmp (argv[1], "-all") == 0)
    {
        /*
         * action ignored in WeeChat >= 1.0 (mute on all buffers is default)
         * (kept for compatibility with old versions)
         */
        ptr_command = argv_eol[2];
    }

    if (ptr_command && ptr_command[0])
    {
        gui_chat_mute = mute_mode;
        gui_chat_mute_buffer = mute_buffer;

        if (string_is_command_char (ptr_command))
        {
            (void) input_exec_command (buffer, 1, NULL, ptr_command, NULL);
        }
        else
        {
            length = strlen (ptr_command) + 2;
            command = malloc (length);
            if (command)
            {
                snprintf (command, length, "/%s", ptr_command);
                (void) input_exec_command (buffer, 1, NULL, command, NULL);
                free (command);
            }
        }

        gui_chat_mute = gui_chat_mute_old;
        gui_chat_mute_buffer =
            (gui_chat_mute_buffer_old
             && gui_buffer_valid (gui_chat_mute_buffer_old)) ?
            gui_chat_mute_buffer_old : NULL;
    }

    return WEECHAT_RC_OK;
}

/*
 * Displays a list of loaded plugins.
 */

void
command_plugin_list (const char *name, int full)
{
    struct t_weechat_plugin *ptr_plugin;
    int plugins_found;

    if (!name)
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, _("Plugins loaded:"));
    }

    plugins_found = 0;

    for (ptr_plugin = weechat_plugins; ptr_plugin;
         ptr_plugin = ptr_plugin->next_plugin)
    {
        if (!name || (strstr (ptr_plugin->name, name)))
        {
            plugins_found++;

            if (full)
            {
                gui_chat_printf (NULL, "");

                /* plugin info */
                gui_chat_printf (NULL,
                                 "  %s%s %s[%sv%s%s]%s: %s (%s)",
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 ptr_plugin->name,
                                 GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 ptr_plugin->version,
                                 GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 (ptr_plugin->description && ptr_plugin->description[0]) ? _(ptr_plugin->description) : "",
                                 ptr_plugin->filename);

                /* second line of plugin info */
                gui_chat_printf (NULL,
                                 _("  written by \"%s\", license: %s"),
                                 ptr_plugin->author,
                                 ptr_plugin->license);
            }
            else
            {
                /* plugin info */
                gui_chat_printf (NULL,
                                 "  %s%s%s: %s",
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 ptr_plugin->name,
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 (ptr_plugin->description && ptr_plugin->description[0]) ? _(ptr_plugin->description) : "");
            }
        }
    }
    if (plugins_found == 0)
    {
        if (name)
            gui_chat_printf (NULL, _("No plugin found"));
        else
            gui_chat_printf (NULL, _("  (no plugin)"));
    }
}

/*
 * Lists loaded plugins in input.
 *
 * Sends input to buffer if send_to_buffer == 1.
 * String is translated if translated == 1 (otherwise it's English).
 */

void
command_plugin_list_input (struct t_gui_buffer *buffer,
                           int send_to_buffer, int translated)
{
    struct t_weechat_plugin *ptr_plugin;
    struct t_weelist *list;
    struct t_weelist_item *ptr_item;
    int length;
    char **buf, str_pos[16];

    buf = string_dyn_alloc (256);
    if (!buf)
        return;

    list = weelist_new ();
    if (!list)
    {
        string_dyn_free (buf, 1);
        return;
    }

    for (ptr_plugin = weechat_plugins; ptr_plugin;
         ptr_plugin = ptr_plugin->next_plugin)
    {
        weelist_add (list, ptr_plugin->name, WEECHAT_LIST_POS_SORT, NULL);
    }

    for (ptr_item = list->items; ptr_item;
         ptr_item = ptr_item->next_item)
    {
        if (*buf[0])
        {
            string_dyn_concat (buf, ", ", -1);
        }
        else
        {
            string_dyn_concat (
                buf,
                (translated) ? _("Plugins loaded:") : "Plugins loaded:",
                -1);
            string_dyn_concat (buf, " ", -1);
        }
        string_dyn_concat (buf, ptr_item->data, -1);
    }

    if (!*buf[0])
    {
        string_dyn_concat (
            buf,
            (translated) ? _("No plugins loaded") : "No plugins loaded",
            -1);
    }

    if (send_to_buffer)
    {
        (void) input_data (buffer, *buf, NULL);
    }
    else
    {
        gui_buffer_set (buffer, "input", *buf);
        length = strlen (*buf);
        snprintf (str_pos, sizeof (str_pos), "%d", length);
        gui_buffer_set (buffer, "input_pos", str_pos);
    }

    weelist_free (list);
    string_dyn_free (buf, 1);
}

/*
 * Callback for command "/plugin": lists/loads/unloads WeeChat plugins.
 */

COMMAND_CALLBACK(plugin)
{
    int plugin_argc;
    char **plugin_argv, *full_name;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    if (argc == 1)
    {
        /* list all plugins */
        command_plugin_list (NULL, 0);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "list") == 0)
    {
        if (argc > 2)
        {
            if (string_strcmp (argv[2], "-i") == 0)
                command_plugin_list_input (buffer, 0, 0);
            else if (string_strcmp (argv[2], "-il") == 0)
                command_plugin_list_input (buffer, 0, 1);
            else if (string_strcmp (argv[2], "-o") == 0)
                command_plugin_list_input (buffer, 1, 0);
            else if (string_strcmp (argv[2], "-ol") == 0)
                command_plugin_list_input (buffer, 1, 1);
            else
                command_plugin_list (argv[2], 0);
        }
        else
        {
            command_plugin_list (NULL, 0);
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "listfull") == 0)
    {
        command_plugin_list ((argc > 2) ? argv[2] : NULL, 1);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "autoload") == 0)
    {
        if (argc > 2)
        {
            plugin_argv = string_split (argv_eol[2], " ", NULL,
                                        WEECHAT_STRING_SPLIT_STRIP_LEFT
                                        | WEECHAT_STRING_SPLIT_STRIP_RIGHT
                                        | WEECHAT_STRING_SPLIT_COLLAPSE_SEPS,
                                        0, &plugin_argc);
            plugin_auto_load (NULL, 1, 1, 1, plugin_argc, plugin_argv);
        }
        else
            plugin_auto_load (NULL, 1, 1, 1, 0, NULL);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "load") == 0)
    {
        COMMAND_MIN_ARGS(3, "load");
        plugin_argv = NULL;
        plugin_argc = 0;
        if (argc > 3)
        {
            plugin_argv = string_split (argv_eol[3], " ", NULL,
                                        WEECHAT_STRING_SPLIT_STRIP_LEFT
                                        | WEECHAT_STRING_SPLIT_STRIP_RIGHT
                                        | WEECHAT_STRING_SPLIT_COLLAPSE_SEPS,
                                        0, &plugin_argc);
        }
        full_name = dir_search_full_lib_name (argv[2], "plugins");
        plugin_load (full_name, 1, plugin_argc, plugin_argv);
        if (full_name)
            free (full_name);
        if (plugin_argv)
            string_free_split (plugin_argv);
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "reload") == 0)
    {
        if (argc > 2)
        {
            if (argc > 3)
            {
                plugin_argv = string_split (
                    argv_eol[3], " ", NULL,
                    WEECHAT_STRING_SPLIT_STRIP_LEFT
                    | WEECHAT_STRING_SPLIT_STRIP_RIGHT
                    | WEECHAT_STRING_SPLIT_COLLAPSE_SEPS,
                    0, &plugin_argc);
                if (strcmp (argv[2], "*") == 0)
                {
                    plugin_unload_all ();
                    plugin_auto_load (NULL, 1, 1, 1, plugin_argc, plugin_argv);
                }
                else
                {
                    plugin_reload_name (argv[2], plugin_argc, plugin_argv);
                }
                if (plugin_argv)
                    string_free_split (plugin_argv);
            }
            else
                plugin_reload_name (argv[2], 0, NULL);
        }
        else
        {
            plugin_unload_all ();
            plugin_auto_load (NULL, 1, 1, 1, 0, NULL);
        }
        return WEECHAT_RC_OK;
    }

    if (string_strcmp (argv[1], "unload") == 0)
    {
        if (argc > 2)
            plugin_unload_name (argv[2]);
        else
            plugin_unload_all ();
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Callback for command "/print": display text on a buffer.
 */

COMMAND_CALLBACK(print)
{
    struct t_gui_buffer *ptr_buffer;
    int i, y, escape, to_stdout, to_stderr, arg_new_buffer_name;
    int new_buffer_type_free, free_content, switch_to_buffer;
    time_t date, date_now;
    struct tm tm_date;
    char *tags, *pos, *text, *text2, *error, empty_string[1] = { '\0' };
    const char *prefix, *ptr_text;
    long value;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    ptr_buffer = buffer;
    arg_new_buffer_name = -1;
    new_buffer_type_free = 0;
    switch_to_buffer = 0;
    y = -1;
    date = 0;
    tags = NULL;
    prefix = NULL;
    escape = 0;
    to_stdout = 0;
    to_stderr = 0;
    ptr_text = NULL;

    for (i = 1; i < argc; i++)
    {
        if (string_strcmp (argv[i], "-buffer") == 0)
        {
            if (i + 1 >= argc)
                COMMAND_ERROR;
            i++;
            ptr_buffer = gui_buffer_search_by_number_or_name (argv[i]);
            if (!ptr_buffer)
                COMMAND_ERROR;
        }
        else if (string_strcmp (argv[i], "-newbuffer") == 0)
        {
            if (i + 1 >= argc)
                COMMAND_ERROR;
            i++;
            arg_new_buffer_name = i;
        }
        else if (string_strcmp (argv[i], "-free") == 0)
        {
            new_buffer_type_free = 1;
        }
        else if (string_strcmp (argv[i], "-switch") == 0)
        {
            switch_to_buffer = 1;
        }
        else if (string_strcmp (argv[i], "-current") == 0)
        {
            ptr_buffer = (gui_current_window) ? gui_current_window->buffer : NULL;
        }
        else if (string_strcmp (argv[i], "-core") == 0)
        {
            ptr_buffer = NULL;
        }
        else if (string_strcmp (argv[i], "-escape") == 0)
        {
            escape = 1;
        }
        else if (string_strcmp (argv[i], "-y") == 0)
        {
            if (i + 1 >= argc)
                COMMAND_ERROR;
            i++;
            error = NULL;
            value = strtol (argv[i], &error, 10);
            if (!error || error[0])
                COMMAND_ERROR;
            y = (int)value;
        }
        else if (string_strcmp (argv[i], "-date") == 0)
        {
            if (i + 1 >= argc)
                COMMAND_ERROR;
            i++;
            if ((argv[i][0] == '-') || (argv[i][0] == '+'))
            {
                error = NULL;
                value = strtol (argv[i] + 1, &error, 10);
                if (!error || error[0])
                    COMMAND_ERROR;
                date = (argv[i][0] == '-') ?
                    time (NULL) - value : time (NULL) + value;
            }
            else
            {
                error = NULL;
                value = strtol (argv[i], &error, 10);
                if (error && !error[0])
                {
                    date = value;
                }
                else
                {
                    memset (&tm_date, 0, sizeof (struct tm));
                    if (strchr (argv[i], '-'))
                    {
                        /* ISO 8601 (date/time) */
                        if (strptime (argv[i], "%Y-%m-%dT%H:%M:%S", &tm_date))
                        {
                            if (tm_date.tm_year > 0)
                                date = mktime (&tm_date);
                        }
                    }
                    else
                    {
                        /* time only (use current date) */
                        date_now = time (NULL);
                        localtime_r (&date_now, &tm_date);
                        if (strptime (argv[i], "%H:%M:%S", &tm_date))
                            date = mktime (&tm_date);
                    }
                }
            }
        }
        else if (string_strcmp (argv[i], "-tags") == 0)
        {
            if (i + 1 >= argc)
                COMMAND_ERROR;
            i++;
            tags = argv[i];
        }
        else if (string_strcmp (argv[i], "-action") == 0)
        {
            prefix = gui_chat_prefix[GUI_CHAT_PREFIX_ACTION];
        }
        else if (string_strcmp (argv[i], "-error") == 0)
        {
            prefix = gui_chat_prefix[GUI_CHAT_PREFIX_ERROR];
        }
        else if (string_strcmp (argv[i], "-join") == 0)
        {
            prefix = gui_chat_prefix[GUI_CHAT_PREFIX_JOIN];
        }
        else if (string_strcmp (argv[i], "-network") == 0)
        {
            prefix = gui_chat_prefix[GUI_CHAT_PREFIX_NETWORK];
        }
        else if (string_strcmp (argv[i], "-quit") == 0)
        {
            prefix = gui_chat_prefix[GUI_CHAT_PREFIX_QUIT];
        }
        else if (string_strcmp (argv[i], "-stdout") == 0)
        {
            to_stdout = 1;
        }
        else if (string_strcmp (argv[i], "-stderr") == 0)
        {
            to_stderr = 1;
        }
        else if (string_strcmp (argv[i], "-beep") == 0)
        {
            fprintf (stderr, "\a");
            fflush (stderr);
            return WEECHAT_RC_OK;
        }
        else if (argv[i][0] == '-')
        {
            /* unknown argument starting with "-", exit */
            COMMAND_ERROR;
        }
        else
            break;
    }

    if (i < argc)
    {
        ptr_text = (strncmp (argv_eol[i], "\\-", 2) == 0) ?
            argv_eol[i] + 1 : argv_eol[i];
    }
    else
    {
        ptr_text = empty_string;
    }

    /* print to stdout or stderr */
    if (to_stdout || to_stderr)
    {
        text = string_convert_escaped_chars (ptr_text);
        if (text)
        {
            fprintf ((to_stdout) ? stdout : stderr, "%s", text);
            fflush ((to_stdout) ? stdout : stderr);
            free (text);
        }
        return WEECHAT_RC_OK;
    }

    if (arg_new_buffer_name >= 0)
    {
        /* print to new buffer */
        if (gui_buffer_is_reserved_name (argv[arg_new_buffer_name]))
        {
            gui_chat_printf (NULL,
                             _("%sBuffer name \"%s\" is reserved for WeeChat"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[arg_new_buffer_name]);
            return WEECHAT_RC_OK;
        }
        ptr_buffer = gui_buffer_search_by_name (PLUGIN_CORE,
                                                argv[arg_new_buffer_name]);
        if (!ptr_buffer)
        {
            ptr_buffer = gui_buffer_new_user (
                argv[arg_new_buffer_name],
                (new_buffer_type_free) ? GUI_BUFFER_TYPE_FREE : GUI_BUFFER_TYPE_DEFAULT);
        }
    }
    else
    {
        /* print to existing buffer */
        if (!ptr_buffer)
            ptr_buffer = gui_buffer_search_main ();
    }

    free_content = (ptr_buffer && (ptr_buffer->type == GUI_BUFFER_TYPE_FREE));

    text = strdup (ptr_text);
    if (text)
    {
        pos = NULL;
        if (!prefix)
        {
            pos = strstr (text, "\\t");
            if (pos)
            {
                pos[0] = (free_content) ? ' ' : '\t';
                memmove (pos + 1, pos + 2, strlen (pos + 2) + 1);
            }
        }
        text2 = (escape) ?
            string_convert_escaped_chars (text) : strdup (text);
        if (text2)
        {
            if (free_content)
            {
                gui_chat_printf_y_date_tags (ptr_buffer, y, date, tags,
                                             "%s%s",
                                             (prefix) ? prefix : "",
                                             text2);
            }
            else
            {
                gui_chat_printf_date_tags (
                    ptr_buffer, date, tags,
                    "%s%s",
                    (prefix) ? prefix : ((!prefix && !pos) ? "\t" : ""),
                    text2);
            }
            free (text2);
        }
        free (text);
    }

    if (ptr_buffer && switch_to_buffer)
        gui_window_switch_to_buffer (gui_current_window, ptr_buffer, 1);

    return WEECHAT_RC_OK;
}

/*
 * Displays a list of proxies.
 */

void
command_proxy_list ()
{
    struct t_proxy *ptr_proxy;

    if (weechat_proxies)
    {
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, _("List of proxies:"));
        for (ptr_proxy = weechat_proxies; ptr_proxy;
             ptr_proxy = ptr_proxy->next_proxy)
        {
            gui_chat_printf (NULL,
                             _("  %s%s%s: %s, %s/%d (%s), username: %s, "
                               "password: %s"),
                             GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                             ptr_proxy->name,
                             GUI_COLOR(GUI_COLOR_CHAT),
                             proxy_type_string[CONFIG_INTEGER(ptr_proxy->options[PROXY_OPTION_TYPE])],
                             CONFIG_STRING(ptr_proxy->options[PROXY_OPTION_ADDRESS]),
                             CONFIG_INTEGER(ptr_proxy->options[PROXY_OPTION_PORT]),
                             (CONFIG_INTEGER(ptr_proxy->options[PROXY_OPTION_IPV6])) ? "IPv6" : "IPv4",
                             (CONFIG_STRING(ptr_proxy->options[PROXY_OPTION_USERNAME]) &&
                              CONFIG_STRING(ptr_proxy->options[PROXY_OPTION_USERNAME])[0]) ?
                             CONFIG_STRING(ptr_proxy->options[PROXY_OPTION_USERNAME]) : _("(none)"),
                             (CONFIG_STRING(ptr_proxy->options[PROXY_OPTION_PASSWORD]) &&
                              CONFIG_STRING(ptr_proxy->options[PROXY_OPTION_PASSWORD])[0]) ?
                             CONFIG_STRING(ptr_proxy->options[PROXY_OPTION_PASSWORD]) : _("(none)"));
        }
    }
    else
        gui_chat_printf (NULL, _("No proxy defined"));
}

/*
 * Callback for command "/proxy": manages proxies.
 */

COMMAND_CALLBACK(proxy)
{
    int type;
    long value;
    char *error;
    struct t_proxy *ptr_proxy;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    /* list of bars */
    if ((argc == 1) || (string_strcmp (argv[1], "list") == 0))
    {
        command_proxy_list ();
        return WEECHAT_RC_OK;
    }

    /* add a new proxy */
    if (string_strcmp (argv[1], "add") == 0)
    {
        COMMAND_MIN_ARGS(6, "add");
        type = proxy_search_type (argv[3]);
        if (type < 0)
        {
            gui_chat_printf (NULL,
                             _("%sInvalid type \"%s\" for proxy \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[3], argv[2]);
            return WEECHAT_RC_OK;
        }
        error = NULL;
        value = strtol (argv[5], &error, 10);
        (void) value;
        if (error && !error[0])
        {
            /* add proxy */
            if (proxy_new (argv[2], argv[3], "off", argv[4], argv[5],
                           (argc >= 7) ? argv[6] : NULL,
                           (argc >= 8) ? argv_eol[7] : NULL))
            {
                gui_chat_printf (NULL, _("Proxy \"%s\" added"),
                                 argv[2]);
            }
            else
            {
                gui_chat_printf (NULL, _("%sFailed to add proxy \"%s\""),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2]);
            }
        }
        else
        {
            gui_chat_printf (NULL,
                             _("%sInvalid port \"%s\" for proxy \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[5], argv[2]);
            return WEECHAT_RC_OK;
        }
        return WEECHAT_RC_OK;
    }

    /* delete a proxy */
    if (string_strcmp (argv[1], "del") == 0)
    {
        COMMAND_MIN_ARGS(3, "del");
        if (string_strcmp (argv[2], "-all") == 0)
        {
            proxy_free_all ();
            gui_chat_printf (NULL, _("All proxies have been deleted"));
        }
        else
        {
            ptr_proxy = proxy_search (argv[2]);
            if (!ptr_proxy)
            {
                gui_chat_printf (NULL,
                                 _("%sProxy \"%s\" not found"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2]);
                return WEECHAT_RC_OK;
            }
            proxy_free (ptr_proxy);
            gui_chat_printf (NULL, _("Proxy deleted"));
        }
        return WEECHAT_RC_OK;
    }

    /* set a proxy property */
    if (string_strcmp (argv[1], "set") == 0)
    {
        COMMAND_MIN_ARGS(5, "set");
        ptr_proxy = proxy_search (argv[2]);
        if (!ptr_proxy)
        {
            gui_chat_printf (NULL,
                             _("%sProxy \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
            return WEECHAT_RC_OK;
        }
        if (!proxy_set (ptr_proxy, argv[3], argv_eol[4]))
        {
            gui_chat_printf (NULL,
                             _("%sUnable to set option \"%s\" for proxy \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[3], argv[2]);
            return WEECHAT_RC_OK;
        }
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Callback for command "/quit": quits WeeChat.
 */

COMMAND_CALLBACK(quit)
{
    int confirm_ok;
    char *pos_args;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    /* already quitting? just ignore the command */
    if (weechat_quit)
        return WEECHAT_RC_OK;

    confirm_ok = 0;
    pos_args = NULL;
    if (argc > 1)
    {
        if (string_strcmp (argv[1], "-yes") == 0)
        {
            confirm_ok = 1;
            if (argc > 2)
                pos_args = argv_eol[2];
        }
        else
            pos_args = argv_eol[1];
    }

    /* if confirmation is required, check that "-yes" is given */
    if (CONFIG_BOOLEAN(config_look_confirm_quit) && !confirm_ok)
    {
        gui_chat_printf (NULL,
                         _("%sYou must confirm /%s command with extra "
                           "argument \"-yes\" (see /help %s)"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                         "quit", "quit");
        return WEECHAT_RC_OK;
    }

    /*
     * send quit signal: some plugins like irc use this signal to disconnect
     * from servers
     */
    (void) hook_signal_send ("quit", WEECHAT_HOOK_SIGNAL_STRING, pos_args);

    /* force end of WeeChat main loop */
    weechat_quit = 1;

    return WEECHAT_RC_OK;
}

/*
 * Reloads a configuration file.
 */

void
command_reload_file (struct t_config_file *config_file)
{
    int rc;

    if (config_file->callback_reload)
        rc = (int) (config_file->callback_reload)
            (config_file->callback_reload_pointer,
             config_file->callback_reload_data,
             config_file);
    else
        rc = config_file_reload (config_file);

    if (rc == WEECHAT_RC_OK)
    {
        gui_chat_printf (NULL,
                         _("Options reloaded from %s"),
                         config_file->filename);
    }
    else
    {
        gui_chat_printf (NULL,
                         _("%sFailed to reload options from %s"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                         config_file->filename);
    }
}

/*
 * Callback for command "/reload": reloads a configuration file.
 */

COMMAND_CALLBACK(reload)
{
    struct t_config_file *ptr_config;
    struct t_arraylist *all_configs;
    int i, list_size;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;
    (void) argv_eol;

    if (argc > 1)
    {
        for (i = 1; i < argc; i++)
        {
            ptr_config = config_file_search (argv[i]);
            if (ptr_config)
            {
                command_reload_file (ptr_config);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("Unknown configuration file \"%s\""),
                                 argv[i]);
            }
        }
    }
    else
    {
        all_configs = config_file_get_configs_by_priority ();
        if (!all_configs)
            COMMAND_ERROR;
        list_size = arraylist_size (all_configs);
        for (i = 0; i < list_size; i++)
        {
            ptr_config = (struct t_config_file *)arraylist_get (all_configs, i);
            if (config_file_valid (ptr_config))
                command_reload_file (ptr_config);
        }
        arraylist_free (all_configs);
    }

    return WEECHAT_RC_OK;
}

/*
 * Callback for repeat timer.
 */

int
command_repeat_timer_cb (const void *pointer, void *data, int remaining_calls)
{
    char **repeat_args;
    int i;
    struct t_gui_buffer *ptr_buffer;

    /* make C compiler happy */
    (void) data;

    repeat_args = (char **)pointer;

    if (!repeat_args)
        return WEECHAT_RC_ERROR;

    if (repeat_args[0] && repeat_args[1])
    {
        /* search buffer, fallback to core buffer if not found */
        ptr_buffer = gui_buffer_search_by_full_name (repeat_args[0]);
        if (ptr_buffer)
            (void) input_data (ptr_buffer, repeat_args[1], repeat_args[2]);
    }

    if (remaining_calls == 0)
    {
        for (i = 0; i < 3; i++)
        {
            if (repeat_args[i])
                free (repeat_args[i]);
        }
        free (repeat_args);
    }

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/repeat": executes a command several times.
 */

COMMAND_CALLBACK(repeat)
{
    int arg_count, count, i;
    long interval;
    char *error, **repeat_args;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    COMMAND_MIN_ARGS(3, "");

    arg_count = 1;
    interval = 0;

    if ((argc >= 5) && (string_strcmp (argv[1], "-interval") == 0))
    {
        interval = util_parse_delay (argv[2], 1000);
        if (interval < 1)
            interval = 0;
        arg_count = 3;
    }

    error = NULL;
    count = (int)strtol (argv[arg_count], &error, 10);
    if (!error || error[0] || (count < 1))
    {
        /* invalid count */
        gui_chat_printf (NULL,
                         _("%sInvalid number: \"%s\""),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                         argv[arg_count]);
        return WEECHAT_RC_OK;
    }

    /* first execute command now */
    (void) input_data (buffer, argv_eol[arg_count + 1], NULL);

    /* repeat execution of command */
    if (count > 1)
    {
        if (interval == 0)
        {
            /* execute command multiple times now */
            for (i = 0; i < count - 1; i++)
            {
                (void) input_data (buffer, argv_eol[arg_count + 1], NULL);
            }
        }
        else
        {
            /* schedule execution of command in future */
            repeat_args = malloc (3 * sizeof (*repeat_args));
            if (!repeat_args)
            {
                gui_chat_printf (NULL,
                                 _("%sNot enough memory (%s)"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 "/repeat");
                return WEECHAT_RC_OK;
            }
            repeat_args[0] = strdup (buffer->full_name);
            repeat_args[1] = strdup (argv_eol[arg_count + 1]);
            repeat_args[2] = (input_commands_allowed) ?
                string_rebuild_split_string (
                    (const char **)input_commands_allowed, ",", 0, -1) : NULL;
            hook_timer (NULL, interval, 0, count - 1,
                        &command_repeat_timer_cb, repeat_args, NULL);
        }
    }

    return WEECHAT_RC_OK;
}

/*
 * Saves a configuration file to disk.
 */

void
command_save_file (struct t_config_file *config_file)
{
    if (config_file_write (config_file) == 0)
    {
        gui_chat_printf (NULL,
                         _("Options saved to %s"),
                         config_file->filename);
    }
    else
    {
        gui_chat_printf (NULL,
                         _("%sFailed to save options to %s"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                         config_file->filename);
    }
}

/*
 * Callback for command "/save": saves configuration files to disk.
 */

COMMAND_CALLBACK(save)
{
    struct t_config_file *ptr_config;
    struct t_arraylist *all_configs;
    int i, list_size;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;
    (void) argv_eol;

    if (argc > 1)
    {
        /* save configuration files asked by user */
        for (i = 1; i < argc; i++)
        {
            ptr_config = config_file_search (argv[i]);
            if (ptr_config)
            {
                command_save_file (ptr_config);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("Unknown configuration file \"%s\""),
                                 argv[i]);
            }
        }
    }
    else
    {
        /* save all configuration files */
        all_configs = config_file_get_configs_by_priority ();
        if (!all_configs)
            COMMAND_ERROR;
        list_size = arraylist_size (all_configs);
        for (i = 0; i < list_size; i++)
        {
            ptr_config = (struct t_config_file *)arraylist_get (all_configs, i);
            if (config_file_valid (ptr_config))
                command_save_file (ptr_config);
        }
        arraylist_free (all_configs);
    }

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/secure": manage secured data
 */

COMMAND_CALLBACK(secure)
{
    int passphrase_was_set, count_encrypted;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    /* list of secured data */
    if (argc == 1)
    {
        secure_buffer_open ();
        return WEECHAT_RC_OK;
    }

    count_encrypted = secure_hashtable_data_encrypted->items_count;

    /* decrypt data still encrypted */
    if (string_strcmp (argv[1], "decrypt") == 0)
    {
        COMMAND_MIN_ARGS(3, "decrypt");
        if (count_encrypted == 0)
        {
            gui_chat_printf (NULL, _("There is no encrypted data"));
            return WEECHAT_RC_OK;
        }
        if (strcmp (argv[2], "-discard") == 0)
        {
            hashtable_remove_all (secure_hashtable_data_encrypted);
            gui_chat_printf (NULL, _("All encrypted data has been deleted"));
            return WEECHAT_RC_OK;
        }
        if (secure_decrypt_data_not_decrypted (argv_eol[2]) > 0)
        {
            gui_chat_printf (NULL,
                             _("Encrypted data has been successfully decrypted"));
            if (secure_passphrase)
                free (secure_passphrase);
            secure_passphrase = strdup (argv_eol[2]);
        }
        else
        {
            gui_chat_printf (NULL,
                             _("%sFailed to decrypt data (wrong passphrase?)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
        }
        return WEECHAT_RC_OK;
    }

    if (count_encrypted > 0)
    {
        gui_chat_printf (NULL,
                         _("%sYou must decrypt data still encrypted before "
                           "doing any operation on secured data or passphrase"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
        return WEECHAT_RC_OK;
    }

    /* set the passphrase */
    if (string_strcmp (argv[1], "passphrase") == 0)
    {
        COMMAND_MIN_ARGS(3, "passphrase");
        if ((strcmp (argv[2], "-delete") != 0)
            && (strlen (argv_eol[2]) > SECURE_PASSPHRASE_MAX_LENGTH))
        {
            gui_chat_printf (NULL,
                             _("%sPassphrase is too long (max: %d chars)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             SECURE_PASSPHRASE_MAX_LENGTH);
            return WEECHAT_RC_OK;
        }
        passphrase_was_set = 0;
        if (secure_passphrase)
        {
            free (secure_passphrase);
            secure_passphrase = NULL;
            passphrase_was_set = 1;
        }
        if (strcmp (argv[2], "-delete") == 0)
        {
            gui_chat_printf (NULL,
                             (passphrase_was_set) ?
                             _("Passphrase deleted") : _("Passphrase is not set"));
            if (passphrase_was_set)
            {
                if (secure_hashtable_data->items_count > 0)
                    command_save_file (secure_config_file);
                secure_buffer_display ();
            }
        }
        else
        {
            secure_passphrase = strdup (argv_eol[2]);
            gui_chat_printf (NULL,
                             (passphrase_was_set) ?
                             _("Passphrase changed") : _("Passphrase added"));
            if (secure_hashtable_data->items_count > 0)
                command_save_file (secure_config_file);
            secure_buffer_display ();
            if (CONFIG_STRING(secure_config_crypt_passphrase_command)[0])
            {
                gui_chat_printf (
                    NULL,
                    _("Important: an external program is configured to read "
                      "the passphrase on startup "
                      "(option sec.crypt.passphrase_command); "
                      "you must ensure this program returns the new "
                      "passphrase you just defined"));
            }
        }
        return WEECHAT_RC_OK;
    }

    /* set a secured data */
    if (string_strcmp (argv[1], "set") == 0)
    {
        COMMAND_MIN_ARGS(4, "set");
        hashtable_set (secure_hashtable_data, argv[2], argv_eol[3]);
        gui_chat_printf (NULL, _("Secured data \"%s\" set"), argv[2]);
        command_save_file (secure_config_file);
        secure_buffer_display ();
        return WEECHAT_RC_OK;
    }

    /* delete a secured data */
    if (string_strcmp (argv[1], "del") == 0)
    {
        COMMAND_MIN_ARGS(3, "del");
        if (hashtable_has_key (secure_hashtable_data, argv[2]))
        {
            hashtable_remove (secure_hashtable_data, argv[2]);
            gui_chat_printf (NULL, _("Secured data \"%s\" deleted"), argv[2]);
            command_save_file (secure_config_file);
            secure_buffer_display ();
        }
        else
        {
            gui_chat_printf (NULL,
                             _("%sSecured data \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[2]);
        }
        return WEECHAT_RC_OK;
    }

    /* toggle values on secured data buffer */
    if (string_strcmp (argv[1], "toggle_values") == 0)
    {
        if (secure_buffer)
        {
            secure_buffer_display_values ^= 1;
            secure_buffer_display ();
        }
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Displays a configuration section.
 */

void
command_set_display_section (struct t_config_file *config_file,
                             struct t_config_section *section)
{
    gui_chat_printf (NULL, "");
    gui_chat_printf_date_tags (NULL, 0, "no_trigger",
                               "%s[%s%s%s]%s (%s)",
                               GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                               GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                               section->name,
                               GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                               GUI_COLOR(GUI_COLOR_CHAT),
                               config_file->filename);
}

/*
 * Displays a configuration option.
 */

void
command_set_display_option (struct t_config_option *option,
                            const char *message)
{
    struct t_config_option *ptr_parent_option;
    char *value, *inherited_value, *default_value;
    int is_file_plugins_conf, is_value_inherited, is_default_value_inherited;

    ptr_parent_option = NULL;

    value = NULL;
    inherited_value = NULL;
    default_value = NULL;

    is_file_plugins_conf = (option->config_file && option->config_file->name
                            && (strcmp (option->config_file->name, "plugins") == 0));
    is_value_inherited = 0;
    is_default_value_inherited = 0;

    /* check if option has a parent option */
    if (option->parent_name)
    {
        config_file_search_with_string (option->parent_name, NULL, NULL,
                                        &ptr_parent_option, NULL);
        if (ptr_parent_option && (ptr_parent_option->type != option->type))
            ptr_parent_option = NULL;
    }

    /* check if the value is inherited from parent option */
    if (!option->value && ptr_parent_option && ptr_parent_option->value)
        is_value_inherited = 1;

    value = config_file_option_value_to_string (option, 0, 1, 1);

    if (is_value_inherited)
    {
        inherited_value = config_file_option_value_to_string (
            ptr_parent_option, 0, 1, 1);
    }

    if (option->value)
    {
        if (ptr_parent_option)
        {
            is_default_value_inherited = 1;
            default_value = config_file_option_value_to_string (
                ptr_parent_option, 0, 1, 1);
        }
        else if (!is_file_plugins_conf
                 && config_file_option_has_changed (option))
        {
            default_value = config_file_option_value_to_string (
                option, 1, 1, 1);
        }
    }

    gui_chat_printf_date_tags (
        NULL, 0,
        "no_trigger," GUI_CHAT_TAG_NO_HIGHLIGHT,
        "%s%s.%s.%s%s = %s%s%s%s%s%s%s%s%s%s%s",
        (message) ? message : "  ",
        (option->config_file) ? option->config_file->name : "",
        (option->section) ? option->section->name : "",
        option->name,
        GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
        (value) ? value : "?",
        (inherited_value) ? GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS) : "",
        (inherited_value) ? " -> " : "",
        (inherited_value) ? inherited_value : "",
        (default_value) ? GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS) : "",
        (default_value) ? "  (" : "",
        (default_value) ? GUI_COLOR(GUI_COLOR_CHAT) : "",
        (default_value) ? ((is_default_value_inherited) ? _("default if null: ") : _("default: ")) : "",
        (default_value) ? default_value : "",
        (default_value) ? GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS) : "",
        (default_value) ? ")" : "");

    if (value)
        free (value);
    if (inherited_value)
        free (inherited_value);
    if (default_value)
        free (default_value);
}

/*
 * Displays a list of options.
 *
 * Returns the number of options displayed.
 */

int
command_set_display_option_list (const char *message, const char *search,
                                 int display_only_changed)
{
    int number_found, section_displayed, length;
    struct t_config_file *ptr_config;
    struct t_config_section *ptr_section;
    struct t_config_option *ptr_option;
    char *option_full_name;

    number_found = 0;

    for (ptr_config = config_files; ptr_config;
         ptr_config = ptr_config->next_config)
    {
        /*
         * if we are displaying only changed options, skip options plugins.*
         * because they are all "changed" (default value is always empty string)
         */
        if (display_only_changed && (strcmp (ptr_config->name, "plugins") == 0))
            continue;
        for (ptr_section = ptr_config->sections; ptr_section;
             ptr_section = ptr_section->next_section)
        {
            section_displayed = 0;

            for (ptr_option = ptr_section->options; ptr_option;
                 ptr_option = ptr_option->next_option)
            {
                /*
                 * if we are displaying only changed options, skip the option if
                 * value has not changed (if it is the same as default value)
                 */
                if (display_only_changed &&
                    !config_file_option_has_changed (ptr_option))
                    continue;

                length = strlen (ptr_config->name) + 1
                    + strlen (ptr_section->name) + 1
                    + strlen (ptr_option->name) + 1;
                option_full_name = malloc (length);
                if (option_full_name)
                {
                    snprintf (option_full_name, length, "%s.%s.%s",
                              ptr_config->name, ptr_section->name,
                              ptr_option->name);
                    if ((!search) ||
                        (search && search[0]
                         && (string_match (option_full_name, search, 1))))
                    {
                        if (!section_displayed)
                        {
                            command_set_display_section (ptr_config,
                                                         ptr_section);
                            section_displayed = 1;
                        }
                        command_set_display_option (ptr_option, message);
                        number_found++;
                    }
                    free (option_full_name);
                }
            }
        }
    }

    return number_found;
}

/*
 * Displays multiple lists of options.
 *
 * If display_only_changed == 1, then it will display only options with value
 * changed (different from default value).
 *
 * Returns the total number of options displayed.
 */

int
command_set_display_option_lists (char **argv, int arg_start, int arg_end,
                                  int display_only_changed)
{
    int i, total_number_found, number_found;

    total_number_found = 0;

    for (i = arg_start; i <= arg_end; i++)
    {
        number_found = command_set_display_option_list (NULL, argv[i],
                                                        display_only_changed);

        total_number_found += number_found;

        if (display_only_changed && (arg_start == arg_end))
            break;

        if (number_found == 0)
        {
            if (argv[i])
            {
                gui_chat_printf (NULL,
                                 _("%sOption \"%s\" not found (tip: you can use "
                                   "wildcard \"*\" in option to see a sublist)"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[i]);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("No option found"));
            }
        }
        else
        {
            gui_chat_printf (NULL, "");
            if (argv[i])
            {
                if (display_only_changed)
                {
                    gui_chat_printf (NULL,
                                     NG_("%s%d%s option with value changed "
                                         "(matching with \"%s\")",
                                         "%s%d%s options with value changed "
                                         "(matching with \"%s\")",
                                         number_found),
                                     GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                     number_found,
                                     GUI_COLOR(GUI_COLOR_CHAT),
                                     argv[i]);
                }
                else
                {
                    gui_chat_printf (NULL,
                                     NG_("%s%d%s option (matching with \"%s\")",
                                         "%s%d%s options (matching with \"%s\")",
                                         number_found),
                                     GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                     number_found,
                                     GUI_COLOR(GUI_COLOR_CHAT),
                                     argv[i]);
                }
            }
            else
            {
                gui_chat_printf (NULL,
                                 NG_("%s%d%s option",
                                     "%s%d%s options",
                                     number_found),
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 number_found,
                                 GUI_COLOR(GUI_COLOR_CHAT));
            }
        }
    }

    return total_number_found;
}

/*
 * Callback for command "/set": displays or sets configuration options.
 */

COMMAND_CALLBACK(set)
{
    char *value;
    const char *ptr_string;
    int i, number_found, rc, display_only_changed, arg_option_start;
    int arg_option_end, list_size;
    struct t_config_option *ptr_option, *ptr_option_before;
    struct t_weelist *list;
    struct t_weelist_item *item;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    /* display/set environment variables */
    if ((argc > 1) && (string_strcmp (argv[1], "env") == 0))
    {
        if (argc == 2)
        {
            /* display a sorted list of all environment variables */
            list = weelist_new ();
            if (!list)
                COMMAND_ERROR;
            for (i = 0; environ[i]; i++)
            {
                weelist_add (list, environ[i], WEECHAT_LIST_POS_SORT, NULL);
            }
            list_size = weelist_size (list);
            for (i = 0; i < list_size; i++)
            {
                item = weelist_get (list, i);
                if (item)
                {
                    ptr_string = weelist_string (item);
                    if (ptr_string)
                        gui_chat_printf (NULL, "%s", ptr_string);
                }
            }
            weelist_free (list);
            return WEECHAT_RC_OK;
        }

        if (argc == 3)
        {
            /* display an environment variable */
            value = getenv (argv[2]);
            if (value)
            {
                gui_chat_printf (NULL, "%s=%s", argv[2], value);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("Environment variable \"%s\" is not "
                                   "defined"),
                                 argv[2]);
            }
            return WEECHAT_RC_OK;
        }

        /* set/unset an environment variable */
        value = string_remove_quotes (argv_eol[3], "'\"");
        if (value && value[0])
        {
            /* set variable */
            if (setenv (argv[2], value, 1) == 0)
            {
                gui_chat_printf (NULL, "%s=%s", argv[2], value);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("%sFailed to set variable \"%s\": %s"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2],
                                 strerror (errno));
            }
        }
        else
        {
            /* unset variable */
            if (unsetenv (argv[2]) == 0)
            {
                gui_chat_printf (NULL,
                                 _("Variable \"%s\" unset"),
                                 argv[2]);
            }
            else
            {
                gui_chat_printf (NULL,
                                 _("%sFailed to unset variable \"%s\": %s"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                 argv[2],
                                 strerror (errno));
            }
        }
        if (value)
            free (value);
        return WEECHAT_RC_OK;
    }

    display_only_changed = 0;
    arg_option_start = 1;
    arg_option_end = argc - 1;

    /* if "diff" is specified as first argument, display only changed values */
    if ((argc >= 2) && (string_strcmp (argv[1], "diff") == 0))
    {
        display_only_changed = 1;
        arg_option_start = 2;
    }

    if (arg_option_end < arg_option_start)
        arg_option_end = arg_option_start;

    /* display list of options */
    if ((argc < 3) || display_only_changed)
    {
        number_found = command_set_display_option_lists (argv,
                                                         arg_option_start,
                                                         arg_option_end,
                                                         display_only_changed);
        if (display_only_changed)
        {
            gui_chat_printf (NULL, "");
            if (arg_option_start == argc - 1)
            {
                gui_chat_printf (NULL,
                                 NG_("%s%d%s option with value changed "
                                     "(matching with \"%s\")",
                                     "%s%d%s options with value changed "
                                     "(matching with \"%s\")",
                                     number_found),
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 number_found,
                                 GUI_COLOR(GUI_COLOR_CHAT),
                                 argv[arg_option_start]);
            }
            else
            {
                gui_chat_printf (NULL,
                                 NG_("%s%d%s option with value changed",
                                     "%s%d%s options with value changed",
                                     number_found),
                                 GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                                 number_found,
                                 GUI_COLOR(GUI_COLOR_CHAT));
            }
        }
        return WEECHAT_RC_OK;
    }

    /* set option value */
    config_file_search_with_string (argv[1], NULL, NULL, &ptr_option_before,
                                    NULL);
    value = (string_strcmp (argv_eol[2], WEECHAT_CONFIG_OPTION_NULL) == 0) ?
        NULL : string_remove_quotes (argv_eol[2], "'\"");
    rc = config_file_option_set_with_string (argv[1], value);
    if (value)
        free (value);
    switch (rc)
    {
        case WEECHAT_CONFIG_OPTION_SET_ERROR:
            gui_chat_printf (NULL,
                             _("%sFailed to set option \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[1]);
            return WEECHAT_RC_OK;
        case WEECHAT_CONFIG_OPTION_SET_OPTION_NOT_FOUND:
            gui_chat_printf (NULL,
                             _("%sOption \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[1]);
            return WEECHAT_RC_OK;
        default:
            config_file_search_with_string (argv[1], NULL, NULL,
                                            &ptr_option, NULL);
            if (ptr_option)
            {
                command_set_display_option (ptr_option,
                                            (ptr_option_before) ?
                                            _("Option changed: ") :
                                            _("Option created: "));
            }
            else
                gui_chat_printf (NULL, _("Option changed"));
            break;
    }

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/toggle": toggles value of configuration option.
 */

COMMAND_CALLBACK(toggle)
{
    char **sargv;
    int sargc, rc;
    struct t_config_option *ptr_option;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    COMMAND_MIN_ARGS(2, "");

    config_file_search_with_string (argv[1], NULL, NULL, &ptr_option, NULL);
    if (!ptr_option)
    {
        /* try to create option with empty value if not existing */
        rc = config_file_option_set_with_string (argv[1], "");
        if ((rc == WEECHAT_CONFIG_OPTION_SET_OK_CHANGED) ||
            (rc == WEECHAT_CONFIG_OPTION_SET_OK_SAME_VALUE))
        {
            config_file_search_with_string (argv[1], NULL, NULL, &ptr_option,
                                            NULL);
        }
        if (!ptr_option)
        {
            gui_chat_printf (NULL,
                             _("%sOption \"%s\" not found"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[1]);
            return WEECHAT_RC_OK;
        }
    }

    if ((ptr_option->type != CONFIG_OPTION_TYPE_BOOLEAN)
        && (ptr_option->type != CONFIG_OPTION_TYPE_STRING))
    {
        /* only boolean options can be toggled without a value */
        COMMAND_MIN_ARGS(3, "");
    }

    if (argc > 2)
    {
        sargv = string_split_shell (argv_eol[2], &sargc);
        if (!sargv)
            COMMAND_ERROR;
        if (string_strcmp (argv[2], WEECHAT_CONFIG_OPTION_NULL) == 0)
        {
            if (sargv[0])
                free (sargv[0]);
            sargv[0] = NULL;
        }
    }
    else
    {
        sargv = NULL;
        sargc = 0;
    }

    rc = config_file_option_toggle (ptr_option, (const char **)sargv, sargc, 1);
    string_free_split (sargv);
    switch (rc)
    {
        case WEECHAT_CONFIG_OPTION_SET_ERROR:
            gui_chat_printf (NULL,
                             _("%sFailed to set option \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[1]);
            return WEECHAT_RC_OK;
        case WEECHAT_CONFIG_OPTION_SET_OK_CHANGED:
            command_set_display_option (ptr_option, _("Option changed: "));
            break;
        default:
            break;
    }

    return WEECHAT_RC_OK;
}

/*
 * Unsets/resets one option.
 */

void
command_unset_option (struct t_config_option *option,
                      const char *option_full_name,
                      int *number_reset, int *number_removed)
{
    switch (config_file_option_unset (option))
    {
        case WEECHAT_CONFIG_OPTION_UNSET_ERROR:
            gui_chat_printf (NULL,
                             _("%sFailed to unset option \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             option_full_name);
            break;
        case WEECHAT_CONFIG_OPTION_UNSET_OK_NO_RESET:
            break;
        case WEECHAT_CONFIG_OPTION_UNSET_OK_RESET:
            command_set_display_option (option, _("Option reset: "));
            if (number_reset)
                (*number_reset)++;
            break;
        case WEECHAT_CONFIG_OPTION_UNSET_OK_REMOVED:
            gui_chat_printf (NULL,
                             _("Option removed: %s"), option_full_name);
            if (number_removed)
                (*number_removed)++;
            break;
    }
}

/*
 * Callback for command "/unset": unsets/resets configuration options.
 */

COMMAND_CALLBACK(unset)
{
    struct t_config_file *ptr_config;
    struct t_config_section *ptr_section;
    struct t_config_option *ptr_option, *next_option;
    const char *ptr_name;
    char *option_full_name;
    int mask, length, number_reset, number_removed;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    COMMAND_MIN_ARGS(2, "");

    mask = 0;
    ptr_name = argv_eol[1];
    number_reset = 0;
    number_removed = 0;

    if (string_strcmp (argv[1], "-mask") == 0)
    {
        COMMAND_MIN_ARGS(3, "-mask");
        mask = 1;
        ptr_name = argv_eol[2];
    }

    if (mask && (strcmp (ptr_name, "*") == 0))
    {
        gui_chat_printf (NULL,
                         _("%sReset of all options is not allowed"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
        return WEECHAT_RC_OK;
    }

    if (mask)
    {
        /* unset all options matching the mask */
        for (ptr_config = config_files; ptr_config;
             ptr_config = ptr_config->next_config)
        {
            for (ptr_section = ptr_config->sections; ptr_section;
                 ptr_section = ptr_section->next_section)
            {
                ptr_option = ptr_section->options;
                while (ptr_option)
                {
                    next_option = ptr_option->next_option;

                    length = strlen (ptr_config->name) + 1
                        + strlen (ptr_section->name) + 1
                        + strlen (ptr_option->name) + 1;
                    option_full_name = malloc (length);
                    if (option_full_name)
                    {
                        snprintf (option_full_name, length, "%s.%s.%s",
                                  ptr_config->name, ptr_section->name,
                                  ptr_option->name);
                        if (string_match (option_full_name, ptr_name, 1))
                        {
                            command_unset_option (ptr_option,
                                                  option_full_name,
                                                  &number_reset,
                                                  &number_removed);
                        }
                        free (option_full_name);
                    }

                    ptr_option = next_option;
                }
            }
        }
    }
    else
    {
        /* unset one option */
        config_file_search_with_string (ptr_name, NULL, NULL, &ptr_option,
                                        NULL);
        if (ptr_option)
        {
            command_unset_option (ptr_option, ptr_name,
                                  &number_reset, &number_removed);
        }
    }

    gui_chat_printf (NULL,
                     _("%d option(s) reset, %d option(s) removed"),
                     number_reset,
                     number_removed);

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/upgrade": upgrades WeeChat.
 */

COMMAND_CALLBACK(upgrade)
{
    char *ptr_binary;
    char *exec_args[7] = { NULL, "-a", "--dir", NULL, "--upgrade", NULL };
    struct stat stat_buf;
    int confirm_ok, index_args, rc, quit;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;

    confirm_ok = 0;
    index_args = 1;

    if ((argc > 1) && (string_strcmp (argv[1], "-yes") == 0))
    {
        confirm_ok = 1;
        index_args = 2;
    }

    /* if confirmation is required, check that "-yes" is given */
    if (CONFIG_BOOLEAN(config_look_confirm_upgrade) && !confirm_ok)
    {
        gui_chat_printf (NULL,
                         _("%sYou must confirm /%s command with extra "
                           "argument \"-yes\" (see /help %s)"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                         "upgrade", "upgrade");
        return WEECHAT_RC_OK;
    }

    if ((argc > index_args)
        && (string_strcmp (argv[index_args], "-dummy") == 0))
    {
        return WEECHAT_RC_OK;
    }

    if ((argc > index_args)
        && (string_strcmp (argv[index_args], "-save") == 0))
    {
        /* send "upgrade" signal to plugins */
        (void) hook_signal_send ("upgrade", WEECHAT_HOOK_SIGNAL_STRING,
                                 "save");
        /* save WeeChat session */
        if (!upgrade_weechat_save())
        {
            gui_chat_printf (NULL,
                             _("%sUnable to save WeeChat session "
                               "(files *.upgrade)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
        }
        gui_chat_printf (NULL, _("WeeChat session saved (files *.upgrade)"));
        return WEECHAT_RC_OK;
    }

    /*
     * it is forbidden to upgrade while there are some background process
     * (hook type "process" or "connect")
     */
    if (weechat_hooks[HOOK_TYPE_PROCESS] || weechat_hooks[HOOK_TYPE_CONNECT])
    {
        gui_chat_printf (NULL,
                         _("%sCan't upgrade: there is one or more background "
                           "process (hook type 'process' or 'connect')"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
        return WEECHAT_RC_OK;
    }

    ptr_binary = NULL;
    quit = 0;

    if (argc > index_args)
    {
        if (string_strcmp (argv[index_args], "-quit") == 0)
            quit = 1;
        else
        {
            ptr_binary = string_expand_home (argv_eol[index_args]);
            if (ptr_binary)
            {
                /* check if weechat binary is here and executable by user */
                rc = stat (ptr_binary, &stat_buf);
                if ((rc != 0) || (!S_ISREG(stat_buf.st_mode)))
                {
                    gui_chat_printf (NULL,
                                     _("%sCan't upgrade: WeeChat binary \"%s\" "
                                       "does not exist"),
                                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                     ptr_binary);
                    free (ptr_binary);
                    return WEECHAT_RC_OK;
                }
                if ((!(stat_buf.st_mode & S_IXUSR)) && (!(stat_buf.st_mode & S_IXGRP))
                    && (!(stat_buf.st_mode & S_IXOTH)))
                {
                    gui_chat_printf (NULL,
                                     _("%sCan't upgrade: WeeChat binary \"%s\" "
                                       "does not have execute permissions"),
                                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                                     ptr_binary);
                    free (ptr_binary);
                    return WEECHAT_RC_OK;
                }
            }
        }
    }
    if (!ptr_binary && !quit)
    {
        ptr_binary = (weechat_argv0) ? strdup (weechat_argv0) : NULL;
        if (!ptr_binary)
        {
            gui_chat_printf (NULL,
                             _("%sNo binary specified"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
            return WEECHAT_RC_OK;
        }
    }

    if (!ptr_binary && !quit)
    {
        gui_chat_printf (NULL,
                         _("%sNot enough memory (%s)"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                         "/upgrade");
        return WEECHAT_RC_OK;
    }

    if (ptr_binary)
    {
        gui_chat_printf (NULL,
                         _("Upgrading WeeChat with binary file: \"%s\"..."),
                         ptr_binary);
    }

    /* send "upgrade" signal to plugins */
    (void) hook_signal_send ("upgrade", WEECHAT_HOOK_SIGNAL_STRING,
                             (quit) ? "quit" : NULL);

    if (!upgrade_weechat_save ())
    {
        gui_chat_printf (NULL,
                         _("%sUnable to save WeeChat session "
                           "(files *.upgrade)"),
                         gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
        if (ptr_binary)
            free (ptr_binary);
        return WEECHAT_RC_OK;
    }

    weechat_quit = 1;
    weechat_upgrading = 1;

    /* store layout, unload plugins, save config, then upgrade */
    gui_layout_store_on_exit ();
    plugin_end ();
    if (CONFIG_BOOLEAN(config_look_save_config_on_exit))
        (void) config_weechat_write ();
    gui_main_end (1);
    log_close ();

    if (quit)
    {
        exit (0);
        return WEECHAT_RC_OK;
    }

    /*
     * set passphrase in environment var, so that it will not be asked to user
     * when starting the new binary
     */
    if (secure_passphrase)
        setenv (SECURE_ENV_PASSPHRASE, secure_passphrase, 1);

    /* execute binary */
    exec_args[0] = ptr_binary;
    exec_args[3] = dir_get_string_home_dirs ();
    execvp (exec_args[0], exec_args);

    /* this code should not be reached if execvp is OK */
    string_fprintf (stderr, "\n\n*****\n");
    string_fprintf (stderr,
                    _("***** Error: exec failed (program: \"%s\"), "
                      "exiting WeeChat"),
                    exec_args[0]);
    string_fprintf (stderr, "\n*****\n\n");

    free (exec_args[0]);
    free (exec_args[3]);

    exit (EXIT_FAILURE);

    /* never executed */
    COMMAND_ERROR;
}

/*
 * Callback for command "/uptime": displays WeeChat uptime.
 */

COMMAND_CALLBACK(uptime)
{
    int days, hours, minutes, seconds;
    char string[512], str_first_start[128];

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) argv_eol;

    util_get_time_diff (weechat_first_start_time, time (NULL),
                        NULL, &days, &hours, &minutes, &seconds);

    if ((argc >= 2) && (string_strcmp (argv[1], "-o") == 0))
    {
        snprintf (str_first_start, sizeof (str_first_start),
                  "%s", ctime (&weechat_first_start_time));
        if (str_first_start[0])
            str_first_start[strlen (str_first_start) - 1] = '\0';
        snprintf (string, sizeof (string),
                  "WeeChat uptime: %d %s %02d:%02d:%02d, started on %s",
                  days,
                  (days != 1) ? "days" : "day",
                  hours,
                  minutes,
                  seconds,
                  str_first_start);
        (void) input_data (buffer, string, NULL);
    }
    else if ((argc >= 2) && (string_strcmp (argv[1], "-ol") == 0))
    {
        snprintf (string, sizeof (string),
                  /* TRANSLATORS: "%s" after "started on" is a date */
                  _("WeeChat uptime: %d %s %02d:%02d:%02d, started on %s"),
                  days,
                  NG_("day", "days", days),
                  hours,
                  minutes,
                  seconds,
                  util_get_time_string (&weechat_first_start_time));
        (void) input_data (buffer, string, NULL);
    }
    else
    {
        gui_chat_printf (NULL,
                         /* TRANSLATORS: "%s%s" after "started on" is a date */
                         _("WeeChat uptime: %s%d %s%s "
                           "%s%02d%s:%s%02d%s:%s%02d%s, "
                           "started on %s%s"),
                         GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                         days,
                         GUI_COLOR(GUI_COLOR_CHAT),
                         NG_("day", "days", days),
                         GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                         hours,
                         GUI_COLOR(GUI_COLOR_CHAT),
                         GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                         minutes,
                         GUI_COLOR(GUI_COLOR_CHAT),
                         GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                         seconds,
                         GUI_COLOR(GUI_COLOR_CHAT),
                         GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                         util_get_time_string (&weechat_first_start_time));
    }

    return WEECHAT_RC_OK;
}

/*
 * Displays WeeChat version.
 */

void
command_version_display (struct t_gui_buffer *buffer,
                         int send_to_buffer_as_input,
                         int translated_string,
                         int display_git_version)
{
    char string[1024], str_first_start[128], str_last_start[128];
    time_t weechat_last_start_time;

    str_first_start[0] = '\0';
    str_last_start[0] = '\0';

    if (weechat_upgrade_count > 0)
    {
        weechat_last_start_time = (time_t)weechat_current_start_timeval.tv_sec;

        if (send_to_buffer_as_input && !translated_string)
        {
            snprintf (str_first_start, sizeof (str_first_start),
                      "%s", ctime (&weechat_first_start_time));
            if (str_first_start[0])
                str_first_start[strlen (str_first_start) - 1] = '\0';
            snprintf (str_last_start, sizeof (str_last_start),
                      "%s", ctime (&weechat_last_start_time));
            if (str_last_start[0])
                str_last_start[strlen (str_last_start) - 1] = '\0';
        }
        else
        {
            snprintf (str_first_start, sizeof (str_first_start),
                      "%s", util_get_time_string (&weechat_first_start_time));
            snprintf (str_last_start, sizeof (str_last_start),
                      "%s", util_get_time_string (&weechat_last_start_time));
        }
    }

    if (send_to_buffer_as_input)
    {
        if (translated_string)
        {
            snprintf (string, sizeof (string),
                      "WeeChat %s [%s %s %s]",
                      (display_git_version) ? version_get_version_with_git () : version_get_version (),
                      _("compiled on"),
                      version_get_compilation_date (),
                      version_get_compilation_time ());
            (void) input_data (buffer, string, NULL);
            if (weechat_upgrade_count > 0)
            {
                snprintf (string, sizeof (string),
                          _("Upgraded %d %s, first start: %s, last start: %s"),
                          weechat_upgrade_count,
                          /* TRANSLATORS: text is: "upgraded xx times" */
                          NG_("time", "times", weechat_upgrade_count),
                          str_first_start,
                          str_last_start);
                (void) input_data (buffer, string, NULL);
            }
        }
        else
        {
            snprintf (string, sizeof (string),
                      "WeeChat %s [%s %s %s]",
                      (display_git_version) ? version_get_version_with_git () : version_get_version (),
                      "compiled on",
                      version_get_compilation_date (),
                      version_get_compilation_time ());
            (void) input_data (buffer, string, NULL);
            if (weechat_upgrade_count > 0)
            {
                snprintf (string, sizeof (string),
                          "Upgraded %d %s, first start: %s, last start: %s",
                          weechat_upgrade_count,
                          (weechat_upgrade_count > 1) ? "times" : "time",
                          str_first_start,
                          str_last_start);
                (void) input_data (buffer, string, NULL);
            }
        }
    }
    else
    {
        gui_chat_printf (NULL, "%sWeeChat %s %s[%s%s %s %s%s]",
                         GUI_COLOR(GUI_COLOR_CHAT_BUFFER),
                         (display_git_version) ? version_get_version_with_git () : version_get_version (),
                         GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                         GUI_COLOR(GUI_COLOR_CHAT_VALUE),
                         _("compiled on"),
                         version_get_compilation_date (),
                         version_get_compilation_time (),
                         GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS));
        if (weechat_upgrade_count > 0)
        {
            gui_chat_printf (NULL,
                             _("Upgraded %d %s, first start: %s, last start: %s"),
                             weechat_upgrade_count,
                             /* TRANSLATORS: text is: "upgraded xx times" */
                             NG_("time", "times", weechat_upgrade_count),
                             str_first_start,
                             str_last_start);
        }
    }
}

/*
 * Callback for command "/version": displays WeeChat version.
 */

COMMAND_CALLBACK(version)
{
    int send_to_buffer_as_input, translated_string;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) argv_eol;

    send_to_buffer_as_input = 0;
    translated_string = 0;

    if (argc >= 2)
    {
        if (string_strcmp (argv[1], "-o") == 0)
            send_to_buffer_as_input = 1;
        else if (string_strcmp (argv[1], "-ol") == 0)
        {
            send_to_buffer_as_input = 1;
            translated_string = 1;
        }
    }

    command_version_display (buffer, send_to_buffer_as_input,
                             translated_string, 1);

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/wait": schedules a command execution in future.
 */

COMMAND_CALLBACK(wait)
{
    long delay;

    /* make C compiler happy */
    (void) pointer;
    (void) data;

    COMMAND_MIN_ARGS(3, "");

    delay = util_parse_delay (argv[1], 1000);
    if (delay < 1)
        COMMAND_ERROR;

    if (input_data_delayed (buffer, argv_eol[2], NULL, delay) != WEECHAT_RC_OK)
        COMMAND_ERROR;

    return WEECHAT_RC_OK;
}

/*
 * Callback for command "/window": manages windows.
 */

COMMAND_CALLBACK(window)
{
    struct t_gui_window *ptr_win;
    struct t_gui_window_tree *ptr_tree;
    char *error, *ptr_sizearg, sign;
    long number;
    int win_args;

    /* make C compiler happy */
    (void) pointer;
    (void) data;
    (void) buffer;
    (void) argv_eol;

    if ((argc == 1) || (string_strcmp (argv[1], "list") == 0))
    {
        /* list all windows */
        gui_chat_printf (NULL, "");
        gui_chat_printf (NULL, _("Windows list:"));

        for (ptr_win = gui_windows; ptr_win; ptr_win = ptr_win->next_window)
        {
            gui_chat_printf (NULL, "%s[%s%d%s] (%s%d:%d%s;%s%dx%d%s) ",
                             GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                             GUI_COLOR(GUI_COLOR_CHAT),
                             ptr_win->number,
                             GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                             GUI_COLOR(GUI_COLOR_CHAT),
                             ptr_win->win_x,
                             ptr_win->win_y,
                             GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS),
                             GUI_COLOR(GUI_COLOR_CHAT),
                             ptr_win->win_width,
                             ptr_win->win_height,
                             GUI_COLOR(GUI_COLOR_CHAT_DELIMITERS));
        }

        return WEECHAT_RC_OK;
    }

    /* silently ignore argument "*" (can happen when clicking in a root bar) */
    if (strcmp (argv_eol[1], "*") == 0)
        return WEECHAT_RC_OK;

    /* refresh screen */
    if (string_strcmp (argv[1], "refresh") == 0)
    {
        gui_window_ask_refresh (2);
        return WEECHAT_RC_OK;
    }

    /* balance windows */
    if (string_strcmp (argv[1], "balance") == 0)
    {
        if (gui_window_balance (gui_windows_tree))
            gui_window_ask_refresh (1);
        return WEECHAT_RC_OK;
    }

    /*
     * search window, for actions related to a given window
     * (default is current window if no number is given)
     */
    ptr_win = gui_current_window;
    win_args = 2;
    if ((argc > 3) && (string_strcmp (argv[2], "-window") == 0))
    {
        error = NULL;
        number = strtol (argv[3], &error, 10);
        if (error && !error[0])
            ptr_win = gui_window_search_by_number (number);
        else
        {
            /* invalid number */
            gui_chat_printf (NULL,
                             _("%sInvalid window number: \"%s\""),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             argv[3]);
            return WEECHAT_RC_OK;
        }
        win_args = 4;
    }
    if (!ptr_win)
        return WEECHAT_RC_OK;

    /* page up */
    if (string_strcmp (argv[1], "page_up") == 0)
    {
        gui_window_page_up (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* page down */
    if (string_strcmp (argv[1], "page_down") == 0)
    {
        gui_window_page_down (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* vertical scroll */
    if (string_strcmp (argv[1], "scroll") == 0)
    {
        if (argc > win_args)
            gui_window_scroll (ptr_win, argv[win_args]);
        return WEECHAT_RC_OK;
    }

    /* horizontal scroll in window (for buffers with free content) */
    if (string_strcmp (argv[1], "scroll_horiz") == 0)
    {
        if ((argc > win_args)
            && (ptr_win->buffer->type == GUI_BUFFER_TYPE_FREE))
        {
            gui_window_scroll_horiz (ptr_win, argv[win_args]);
        }
        return WEECHAT_RC_OK;
    }

    /* scroll up */
    if (string_strcmp (argv[1], "scroll_up") == 0)
    {
        gui_window_scroll_up (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* scroll down */
    if (string_strcmp (argv[1], "scroll_down") == 0)
    {
        gui_window_scroll_down (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* scroll to top of window */
    if (string_strcmp (argv[1], "scroll_top") == 0)
    {
        gui_window_scroll_top (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* scroll to bottom of window */
    if (string_strcmp (argv[1], "scroll_bottom") == 0)
    {
        gui_window_scroll_bottom (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* scroll beyond the end of buffer */
    if (string_strcmp (argv[1], "scroll_beyond_end") == 0)
    {
        gui_window_scroll_beyond_end (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* scroll to previous highlight */
    if (string_strcmp (argv[1], "scroll_previous_highlight") == 0)
    {
        gui_window_scroll_previous_highlight (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* scroll to next highlight */
    if (string_strcmp (argv[1], "scroll_next_highlight") == 0)
    {
        gui_window_scroll_next_highlight (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* scroll to unread marker */
    if (string_strcmp (argv[1], "scroll_unread") == 0)
    {
        gui_window_scroll_unread (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* split window horizontally */
    if (string_strcmp (argv[1], "splith") == 0)
    {
        if (argc > win_args)
        {
            error = NULL;
            number = strtol (argv[win_args], &error, 10);
            if (error && !error[0]
                && (number > 0) && (number < 100))
            {
                gui_window_split_horizontal (ptr_win, number);
            }
        }
        else
            gui_window_split_horizontal (ptr_win, 50);
        return WEECHAT_RC_OK;
    }

    /* split window vertically */
    if (string_strcmp (argv[1], "splitv") == 0)
    {
        if (argc > win_args)
        {
            error = NULL;
            number = strtol (argv[win_args], &error, 10);
            if (error && !error[0]
                && (number > 0) && (number < 100))
            {
                gui_window_split_vertical (ptr_win, number);
            }
        }
        else
            gui_window_split_vertical (ptr_win, 50);
        return WEECHAT_RC_OK;
    }

    /* resize window */
    if (string_strcmp (argv[1], "resize") == 0)
    {
        if (argc > win_args)
        {
            ptr_sizearg = argv[win_args];
            sign = 0;
            if ((ptr_sizearg[0] == 'h') || (ptr_sizearg[0] == 'v'))
            {
                ptr_tree = gui_window_tree_get_split (ptr_win->ptr_tree,
                                                      ptr_sizearg[0]);
                ptr_sizearg++;
            }
            else
            {
                ptr_tree = ptr_win->ptr_tree;
            }
            if ((ptr_sizearg[0] == '+') || (ptr_sizearg[0] == '-'))
            {
                sign = ptr_sizearg[0];
                ptr_sizearg++;
            }
            error = NULL;
            number = strtol (ptr_sizearg, &error, 10);
            if (error && !error[0])
            {
                if (sign)
                {
                    if (sign == '-')
                        number *= -1;
                    gui_window_resize_delta (ptr_tree, number);
                }
                else
                {
                    gui_window_resize (ptr_tree, number);
                }
            }
        }
        return WEECHAT_RC_OK;
    }

    /* merge windows */
    if (string_strcmp (argv[1], "merge") == 0)
    {
        if (argc > win_args)
        {
            if (string_strcmp (argv[win_args], "all") == 0)
                gui_window_merge_all (ptr_win);
            else
                COMMAND_ERROR;
        }
        else
        {
            if (!gui_window_merge (ptr_win))
            {
                gui_chat_printf (NULL,
                                 _("%sCan not merge windows, there's no other "
                                   "window with same size near current one"),
                                 gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
                return WEECHAT_RC_OK;
            }
        }
        return WEECHAT_RC_OK;
    }

    /* close window */
    if (string_strcmp (argv[1], "close") == 0)
    {
        if (!gui_window_close (ptr_win))
        {
            gui_chat_printf (NULL,
                             _("%sCan not close window, there's no other "
                               "window with same size near current one"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
            return WEECHAT_RC_OK;
        }
        return WEECHAT_RC_OK;
    }

    /* switch to previous window */
    if (string_strcmp (argv[1], "-1") == 0)
    {
        gui_window_switch_previous (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* switch to next window */
    if (string_strcmp (argv[1], "+1") == 0)
    {
        gui_window_switch_next (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* switch to window above */
    if (string_strcmp (argv[1], "up") == 0)
    {
        gui_window_switch_up (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* switch to window below */
    if (string_strcmp (argv[1], "down") == 0)
    {
        gui_window_switch_down (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* switch to window on the left */
    if (string_strcmp (argv[1], "left") == 0)
    {
        gui_window_switch_left (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* switch to window on the right */
    if (string_strcmp (argv[1], "right") == 0)
    {
        gui_window_switch_right (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* swap windows */
    if (string_strcmp (argv[1], "swap") == 0)
    {
        if (argc > win_args)
        {
            if (string_strcmp (argv[win_args], "up") == 0)
                gui_window_swap (ptr_win, 1);
            else if (string_strcmp (argv[win_args], "down") == 0)
                gui_window_swap (ptr_win, 3);
            else if (string_strcmp (argv[win_args], "left") == 0)
                gui_window_swap (ptr_win, 4);
            else if (string_strcmp (argv[win_args], "right") == 0)
                gui_window_swap (ptr_win, 2);
            else
                COMMAND_ERROR;
        }
        else
        {
            gui_window_swap (ptr_win, 0);
        }
        return WEECHAT_RC_OK;
    }

    /* zoom window */
    if (string_strcmp (argv[1], "zoom") == 0)
    {
        gui_window_zoom (ptr_win);
        return WEECHAT_RC_OK;
    }

    /* bare display */
    if (string_strcmp (argv[1], "bare") == 0)
    {
        gui_window_bare_display_toggle ((argc > 2) ? argv[2] : NULL);
        return WEECHAT_RC_OK;
    }

    /* jump to window by buffer number */
    if (string_strncmp (argv[1], "b", 1) == 0)
    {
        error = NULL;
        number = strtol (argv[1] + 1, &error, 10);
        if (error && !error[0])
        {
            gui_window_switch_by_buffer (ptr_win, number);
            return WEECHAT_RC_OK;
        }
    }

    /* jump to window by number */
    error = NULL;
    number = strtol (argv[1], &error, 10);
    if (error && !error[0])
    {
        gui_window_switch_by_number (number);
        return WEECHAT_RC_OK;
    }

    COMMAND_ERROR;
}

/*
 * Hooks WeeChat core commands.
 */

void
command_init ()
{
    hook_command (
        NULL, "allbuf",
        N_("execute a command on all buffers"),
        N_("<command>"),
        N_("command: command to execute (or text to send to buffer if "
           "command does not start with '/')\n"
           "\n"
           "Examples:\n"
           "  set unread marker on all buffers:\n"
           "    /allbuf /buffer set unread"),
        "%(commands:/)", &command_allbuf, NULL, NULL);
    hook_command (
        NULL, "away",
        N_("set or remove away status"),
        N_("[-all] [<message>]"),
        N_("   -all: set or remove away status on all connected servers\n"
           "message: message for away (if no message is given, away status is "
           "removed)"),
        "-all", &command_away, NULL, NULL);
    hook_command (
        NULL, "bar",
        N_("manage bars"),
        N_("list|listfull|listitems"
           " || add <name> <type>[,<conditions>] <position> <size> <separator> "
           "<item1>[,<item2>...]"
           " || default [input|title|status|nicklist]"
           " || del <name>|-all"
           " || set <name> <option> <value>"
           " || hide|show|toggle <name>"
           " || scroll <name> <window> <scroll_value>"),
        N_("         list: list all bars\n"
           "     listfull: list all bars (verbose)\n"
           "    listitems: list all bar items\n"
           "          add: add a new bar\n"
           "         name: name of bar (must be unique)\n"
           "         type:   root: outside windows,\n"
           "               window: inside windows, with optional conditions "
           "(see below)\n"
           "   conditions: the conditions to display the bar:\n"
           "                 active: on active window\n"
           "               inactive: on inactive windows\n"
           "               nicklist: on windows with nicklist\n"
           "               other condition: see /help weechat.bar.xxx.conditions "
           "and /help eval\n"
           "               without conditions, the bar is always displayed\n"
           "     position: bottom, top, left or right\n"
           "         size: size of bar (in chars)\n"
           "    separator: 1 for using separator (line), 0 or nothing means no "
           "separator\n"
           "    item1,...: items for this bar (items can be separated by comma "
           "(space between items) or \"+\" (glued items))\n"
           "      default: create a default bar (all default bars if no bar "
           "name is given)\n"
           "          del: delete a bar (or all bars with -all)\n"
           "          set: set a value for a bar property\n"
           "       option: option to change (for options list, look at /set "
           "weechat.bar.<barname>.*)\n"
           "        value: new value for option\n"
           "         hide: hide a bar\n"
           "         show: show an hidden bar\n"
           "       toggle: hide/show a bar\n"
           "       scroll: scroll bar\n"
           "       window: window number (use '*' for current window or for "
           "root bars)\n"
           " scroll_value: value for scroll: 'x' or 'y' (optional), followed by "
           "'+', '-', 'b' (beginning) or 'e' (end), value (for +/-), and "
           "optional % (to scroll by % of width/height, otherwise value is "
           "number of chars)\n"
           "\n"
           "Examples:\n"
           "  create a bar with time, buffer number + name, and completion:\n"
           "    /bar add mybar root bottom 1 0 [time],buffer_number+:+"
           "buffer_name,completion\n"
           "  hide a bar:\n"
           "    /bar hide mybar\n"
           "  scroll nicklist 10 lines down on current buffer:\n"
           "    /bar scroll nicklist * y+10\n"
           "  scroll to end of nicklist on current buffer:\n"
           "    /bar scroll nicklist * ye"),
        "list"
        " || listfull"
        " || listitems"
        " || add %(bars_names) root|window bottom|top|left|right"
        " || default input|title|status|nicklist|%*"
        " || del %(bars_names)|-all"
        " || set %(bars_names) %(bars_options)"
        " || hide %(bars_names)"
        " || show %(bars_names)"
        " || toggle %(bars_names)"
        " || scroll %(bars_names) %(windows_numbers)|*",
        &command_bar, NULL, NULL);
    hook_command (
        NULL, "buffer",
        N_("manage buffers"),
        N_("list"
           " || add [-free] [-switch] <name>"
           " || clear [<number>|<name>|-merged|-all [<number>|<name>...]]"
           " || move <number>|-|+"
           " || swap <number1>|<name1> [<number2>|<name2>]"
           " || cycle <number>|<name> [<number>|<name>...]"
           " || merge <number>|<name>"
           " || unmerge [<number>|-all]"
           " || hide [<number>|<name>|-all [<number>|<name>...]]"
           " || unhide [<number>|<name>|-all [<number>|<name>...]]"
           " || switch [-previous]"
           " || zoom"
           " || renumber [<number1> [<number2> [<start>]]]"
           " || close [<n1>[-<n2>]|<name>...]"
           " || notify [<level>]"
           " || listvar [<number>|<name>]"
           " || setvar <name> [<value>]"
           " || delvar <name>"
           " || set <property> [<value>]"
           " || get <property>"
           " || jump smart|last_displayed|prev_visited|next_visited"
           " || <number>|-|+|<name>"),
        N_("    list: list buffers (without argument, this list is displayed)\n"
           "     add: add a new buffer (it can be closed with \"/buffer close\" "
           "or input \"q\")\n"
           "   clear: clear buffer content (number for a buffer, -merged for "
           "merged buffers, -all for all buffers, or nothing for current buffer)\n"
           "    move: move buffer in the list (may be relative, for example -1); "
           "\"-\" = move to first buffer number, \"+\" = move to last buffer "
           "number + 1\n"
           "    swap: swap two buffers (swap with current buffer if only one "
           "number/name given)\n"
           "   cycle: jump loop between a list of buffers\n"
           "   merge: merge current buffer to another buffer (chat area will "
           "be mix of both buffers)\n"
           "          (by default ctrl-x switches between merged buffers)\n"
           " unmerge: unmerge buffer from other buffers which have same number\n"
           "    hide: hide the buffer\n"
           "  unhide: unhide the buffer\n"
           "  switch: switch to next merged buffer (or to previous buffer "
           "with \"-previous\")\n"
           "    zoom: zoom on merged buffer\n"
           "renumber: renumber buffers (works only if option weechat.look."
           "buffer_auto_renumber is off)\n"
           "   close: close buffer (number/range or name is optional)\n"
           "  notify: display or set notify level for current buffer: this level "
           "determines whether buffer will be added to hotlist or not:\n"
           "               none: never\n"
           "          highlight: for highlights only\n"
           "            message: for messages from users + highlights\n"
           "                all: all messages\n"
           "              reset: reset to default value (all)\n"
           " listvar: display local variables in a buffer\n"
           "  setvar: set a local variable in the current buffer\n"
           "  delvar: delete a local variable from the current buffer\n"
           "     set: set a property in the current buffer\n"
           "     get: display a property of current buffer\n"
           "    jump: jump to another buffer:\n"
           "          smart: next buffer with activity\n"
           "          last_displayed: last buffer displayed (before last jump "
           "to a buffer)\n"
           "          prev_visited: previously visited buffer\n"
           "          next_visited: jump to next visited buffer\n"
           "  number: jump to buffer by number, possible prefix:\n"
           "          '+': relative jump, add number to current\n"
           "          '-': relative jump, sub number to current\n"
           "          '*': jump to number, using option \"weechat.look."
           "jump_current_to_previous_buffer\"\n"
           "       -: jump to first buffer number\n"
           "       +: jump to last buffer number\n"
           "    name: jump to buffer by (partial) name\n"
           "\n"
           "Examples:\n"
           "  clear current buffer:\n"
           "    /buffer clear\n"
           "  move buffer to number 5:\n"
           "    /buffer move 5\n"
           "  swap buffer 1 with 3:\n"
           "    /buffer swap 1 3\n"
           "  swap buffer #weechat with current buffer:\n"
           "    /buffer swap #weechat\n"
           "  jump on #chan1, #chan2, #chan3 and loop:\n"
           "    /buffer cycle #chan1 #chan2 #chan3\n"
           "  merge with core buffer:\n"
           "    /buffer merge 1\n"
           "  merge with #weechat buffer:\n"
           "    /buffer merge #weechat\n"
           "  unmerge buffer:\n"
           "    /buffer unmerge\n"
           "  close current buffer:\n"
           "    /buffer close\n"
           "  close buffers 5 to 7:\n"
           "    /buffer close 5-7\n"
           "  jump to #weechat:\n"
           "    /buffer #weechat\n"
           "  jump to next buffer:\n"
           "    /buffer +1\n"
           "  jump to last buffer number:\n"
           "    /buffer +"),
        "add -free|-switch"
        " || clear -merged|-all|%(buffers_numbers)|%(buffers_plugins_names) "
        "%(buffers_numbers)|%(buffers_plugins_names)|%*"
        " || move %(buffers_numbers)"
        " || swap %(buffers_numbers)|%(buffers_plugins_names) "
        "%(buffers_numbers)|%(buffers_plugins_names)"
        " || cycle %(buffers_numbers)|%(buffers_plugins_names)|%*"
        " || merge %(buffers_numbers)|%(buffers_plugins_names)"
        " || unmerge %(buffers_numbers)|-all"
        " || hide %(buffers_numbers)|%(buffers_plugins_names)|-all "
        "%(buffers_numbers)|%(buffers_plugins_names)|%*"
        " || unhide %(buffers_numbers)|%(buffers_plugins_names)|-all "
        " || switch -previous"
        " || zoom"
        "%(buffers_numbers)|%(buffers_plugins_names)|%*"
        " || renumber %(buffers_numbers) %(buffers_numbers) %(buffers_numbers)"
        " || close %(buffers_plugins_names)|%*"
        " || list"
        " || notify reset|none|highlight|message|all"
        " || listvar %(buffers_numbers)|%(buffers_plugins_names)"
        " || setvar %(buffer_local_variables) %(buffer_local_variable_value)"
        " || delvar %(buffer_local_variables)"
        " || set %(buffer_properties_set)"
        " || get %(buffer_properties_get)"
        " || jump smart|last_displayed|prev_visited|next_visited"
        " || %(buffers_plugins_names)|%(buffers_names)|%(irc_channels)|"
        "%(irc_privates)|%(buffers_numbers)|-|-1|+|+1",
        &command_buffer, NULL, NULL);
    hook_command (
        NULL, "color",
        N_("define color aliases and display palette of colors"),
        N_("alias <color> <name>"
           " || unalias <color>"
           " || reset"
           " || term2rgb <color>"
           " || rgb2term <rgb> [<limit>]"
           " || -o"),
        N_("   alias: add an alias for a color\n"
           " unalias: delete an alias\n"
           "   color: color number (greater than or equal to 0, max depends on "
           "terminal, commonly 63 or 255)\n"
           "    name: alias name for color (for example: \"orange\")\n"
           "   reset: reset all color pairs (required when no more color pairs "
           "are available if automatic reset is disabled, see option "
           "\"weechat.look.color_pairs_auto_reset\")\n"
           "term2rgb: convert a terminal color (0-255) to RGB color\n"
           "rgb2term: convert a RGB color to terminal color (0-255)\n"
           "   limit: number of colors to use in terminal table (starting from "
           "0); default is 256\n"
           "      -o: send terminal/colors info to current buffer as input\n"
           "\n"
           "Without argument, this command displays colors in a new buffer.\n"
           "\n"
           "Examples:\n"
           "  add alias \"orange\" for color 214:\n"
           "    /color alias 214 orange\n"
           "  delete color 214:\n"
           "    /color unalias 214"),
        "alias %(palette_colors)"
        " || unalias %(palette_colors)"
        " || reset"
        " || term2rgb"
        " || rgb2term"
        " || -o",
        &command_color, NULL, NULL);
    /*
     * give high priority (50000) so that an alias will not take precedence
     * over this command
     */
    hook_command (
        NULL, "50000|command",
        N_("launch explicit WeeChat or plugin command"),
        N_("[-buffer <name>] <plugin> <command>"),
        N_("-buffer: execute the command on this buffer\n"
           " plugin: execute the command from this plugin; 'core' for a "
           "WeeChat command, '*' for automatic plugin (it depends on the "
           "buffer where the command is executed)\n"
           "command: command to execute (a '/' is automatically added if not "
           "found at beginning of command)"),
        "-buffer %(buffers_plugins_names) "
        "%(plugins_names)|" PLUGIN_CORE " %(plugins_commands:/)"
        " || %(plugins_names)|" PLUGIN_CORE " %(plugins_commands:/)",
        &command_command, NULL, NULL);
    hook_command (
        NULL, "cursor",
        N_("free movement of cursor on screen to execute actions on specific "
           "areas of screen"),
        N_("go chat|<bar>|<x>,<y>"
           " || move up|down|left|right|area_up|area_down|area_left|area_right"
           " || stop"),
        N_("  go: move cursor to chat area, a bar (using bar name) or "
           "coordinates \"x,y\"\n"
           "move: move cursor with direction\n"
           "stop: stop cursor mode\n"
           "\n"
           "Without argument, this command toggles cursor mode.\n"
           "\n"
           "When mouse is enabled (see /help mouse), by default a middle click "
           "will start cursor mode at this point.\n"
           "\n"
           "Default keys in cursor mode on chat messages:\n"
           "  m  quote message\n"
           "  q  quote prefix + message\n"
           "  Q  quote time + prefix + message\n"
           "\n"
           "Default keys in cursor mode on nicklist:\n"
           "  b  ban nick (/ban)\n"
           "  k  kick nick (/kick)\n"
           "  K  kick and ban nick (/kickban)\n"
           "  q  open query with nick (/query)\n"
           "  w  query information about user (/whois)\n"
           "\n"
           "Other default keys in cursor mode:\n"
           "  arrow      move cursor\n"
           "  alt+arrow  move cursor to the next area\n"
           "  enter      exit cursor mode\n"
           "\n"
           "Examples:\n"
           "  go to nicklist:\n"
           "    /cursor go nicklist\n"
           "  go to coordinates x=10, y=5:\n"
           "    /cursor go 10,5"),
        "go %(cursor_areas)"
        " || move up|down|left|right|area_up|area_down|area_left|area_right"
        " || stop",
        &command_cursor, NULL, NULL);
    hook_command (
        NULL, "debug",
        N_("debug functions"),
        N_("list"
           " || set <plugin> <level>"
           " || dump|hooks [<plugin>]"
           " || buffer|certs|color|dirs|infolists|libs|memory|tags|"
           "term|windows"
           " || mouse|cursor [verbose]"
           " || hdata [free]"
           " || time <command>"
           " || unicode <string>"),
        N_("     list: list plugins with debug levels\n"
           "      set: set debug level for plugin\n"
           "   plugin: name of plugin (\"core\" for WeeChat core)\n"
           "    level: debug level for plugin (0 = disable debug)\n"
           "     dump: save memory dump in WeeChat log file (same dump is "
           "written when WeeChat crashes)\n"
           "    hooks: display infos about hooks (with a plugin: display "
           "detailed info about hooks created by the plugin)\n"
           "   buffer: dump buffer content with hexadecimal values in log file\n"
           "    certs: display number of loaded trusted certificate authorities\n"
           "    color: display infos about current color pairs\n"
           "   cursor: toggle debug for cursor mode\n"
           "     dirs: display directories\n"
           "    hdata: display infos about hdata (with free: remove all hdata "
           "in memory)\n"
           "infolists: display infos about infolists\n"
           "     libs: display infos about external libraries used\n"
           "   memory: display infos about memory usage\n"
           "    mouse: toggle debug for mouse\n"
           "     tags: display tags for lines\n"
           "     term: display infos about terminal\n"
           "  windows: display windows tree\n"
           "     time: measure time to execute a command or to send text to "
           "the current buffer\n"
           "  unicode: display information about string and unicode chars "
           "(evaluated, see /help eval)\n"
           "\n"
           "Examples:\n"
           "  /debug set irc 1\n"
           "  /debug mouse verbose\n"
           "  /debug time /filter toggle\n"
           "  /debug unicode ${chars:${\\u26C0}-${\\u26CF}}"),
        "list"
        " || set %(plugins_names)|" PLUGIN_CORE
        " || dump %(plugins_names)|" PLUGIN_CORE
        " || buffer"
        " || certs"
        " || color"
        " || cursor verbose"
        " || dirs"
        " || hdata free"
        " || hooks %(plugins_names)|" PLUGIN_CORE
        " || infolists"
        " || libs"
        " || memory"
        " || mouse verbose"
        " || tags"
        " || term"
        " || windows"
        " || time %(commands:/)"
        " || unicode",
        &command_debug, NULL, NULL);
    hook_command (
        NULL, "eval",
        N_("evaluate expression"),
        N_("[-n|-s] [-d] <expression>"
           " || [-n] [-d [-d]] -c <expression1> <operator> <expression2>"),
        N_("        -n: display result without sending it to buffer "
           "(debug mode)\n"
           "        -s: split expression before evaluating it "
           "(many commands can be separated by semicolons)\n"
           "        -d: display debug output after evaluation "
           "(with two -d: more verbose debug)\n"
           "        -c: evaluate as condition: use operators and parentheses, "
           "return a boolean value (\"0\" or \"1\")\n"
           "expression: expression to evaluate, variables with format "
           "${variable} are replaced (see below); many commands can be "
           "separated by semicolons\n"
           "  operator: a logical or comparison operator:\n"
           "            - logical operators:\n"
           "                &&   boolean \"and\"\n"
           "                ||   boolean \"or\"\n"
           "            - comparison operators:\n"
           "                ==   equal\n"
           "                !=   not equal\n"
           "                <=   less or equal\n"
           "                <    less\n"
           "                >=   greater or equal\n"
           "                >    greater\n"
           "                =~   is matching POSIX extended regex\n"
           "                !~   is NOT matching POSIX extended regex\n"
           "                ==*  is matching mask, case sensitive "
           "(wildcard \"*\" is allowed)\n"
           "                !!*  is NOT matching mask, case sensitive "
           "(wildcard \"*\" is allowed)\n"
           "                =*   is matching mask, case insensitive "
           "(wildcard \"*\" is allowed)\n"
           "                !*   is NOT matching mask, case insensitive "
           "(wildcard \"*\" is allowed)\n"
           "                ==-  is included, case sensitive\n"
           "                !!-  is NOT included, case sensitive\n"
           "                =-   is included, case insensitive\n"
           "                !-   is NOT included, case insensitive\n"
           "\n"
           "An expression is considered as \"true\" if it is not NULL, not "
           "empty, and different from \"0\".\n"
           "The comparison is made using floating point numbers if the two "
           "expressions are valid numbers, with one of the following formats:\n"
           "  - integer (examples: 5, -7)\n"
           "  - floating point number (examples: 5.2, -7.5, 2.83e-2)\n"
           "  - hexadecimal number (examples: 0xA3, -0xA3)\n"
           "To force a string comparison, you can add double quotes around "
           "each expression, for example:\n"
           "  50 > 100      ==> 0\n"
           "  \"50\" > \"100\"  ==> 1\n"
           "\n"
           "Some variables are replaced in expression, using the format "
           "${variable}, variable can be, by order of priority:\n"
           "  - the string itself without evaluation (format: \"raw:xxx\")\n"
           "  - a user-defined variable (format: \"name\")\n"
           "  - an evaluated sub-string (format: \"eval:xxx\")\n"
           "  - an evaluated condition (format: \"eval_cond:xxx\")\n"
           "  - a string with escaped chars (format: \"esc:xxx\" or \"\\xxx\")\n"
           "  - a string with a range of chars (format: \"chars:xxx\" or "
           "\"chars:c1-c2\" where \"xxx\" is one of: \"digit\", \"xdigit\", "
           "\"lower\", \"upper\", \"alpha\", \"alnum\")\n"
           "  - a string converted to lower case (format: \"lower:xxx\")\n"
           "  - a string converted to upper case (format: \"upper:xxx\")\n"
           "  - a string with chars to hide (format: \"hide:char,string\")\n"
           "  - a string with max chars (format: \"cut:max,suffix,string\" "
           "or \"cut:+max,suffix,string\")\n"
           "     or max chars displayed on screen "
           "(format: \"cutscr:max,suffix,string\" or "
           "\"cutscr:+max,suffix,string\")\n"
           "  - a reversed string (format: \"rev:xxx\" or \"revscr:xxx\")\n"
           "  - a repeated string (format: \"repeat:count,string\")\n"
           "  - length of a string (format: \"length:xxx\" or "
           "\"lengthscr:xxx\")\n"
           "  - split of a string (format: "
           "\"split:number,separators,flags,xxx\")\n"
           "  - split of shell argmuents (format: \"split_shell:number,xxx\")\n"
           "  - a color (format: \"color:xxx\", see \"Plugin API "
           "reference\", function \"color\")\n"
           "  - a modifier (format: \"modifier:name,data,string\")\n"
           "  - an info (format: \"info:name,arguments\", arguments are "
           "optional)\n"
           "  - a base 16/32/64 encoded/decoded string (format: "
           "\"base_encode:base,xxx\" or \"base_decode:base,xxx\")\n"
           "  - current date/time (format: \"date\" or \"date:format\")\n"
           "  - an environment variable (format: \"env:XXX\")\n"
           "  - a ternary operator (format: "
           "\"if:condition?value_if_true:value_if_false\")\n"
           "  - result of an expression with parentheses and operators "
           "+ - * / // % ** (format: \"calc:xxx\")\n"
           "  - a random integer number (format: \"random:min,max\")\n"
           "  - a translated string (format: \"translate:xxx\")\n"
           "  - define a user variable (format: \"define:name,value\")\n"
           "  - an option (format: \"file.section.option\")\n"
           "  - a local variable in buffer\n"
           "  - a hdata name/variable (the value is automatically converted "
           "to string), by default \"window\" and \"buffer\" point to current "
           "window/buffer.\n"
           "Format for hdata can be one of following:\n"
           "  hdata.var1.var2...: start with a hdata (pointer must be known), "
           "and ask variables one after one (other hdata can be followed)\n"
           "  hdata[list].var1.var2...: start with a hdata using a "
           "list/pointer/pointer name, for example:\n"
           "    ${buffer[gui_buffers].full_name}: full name of first buffer "
           "in linked list of buffers\n"
           "    ${plugin[weechat_plugins].name}: name of first plugin in "
           "linked list of plugins\n"
           "  hdata[pointer].var1.var2...: start with a hdata using a "
           "pointer, for example:\n"
           "    ${buffer[0x1234abcd].full_name}: full name of the buffer "
           "with this pointer (can be used in triggers)\n"
           "    ${buffer[my_pointer].full_name}: full name of the buffer "
           "with this pointer name (can be used in triggers)\n"
           "  hdata[pointer].var1.method(): when var1 is a hashtable, methods "
           "\"keys()\", \"values()\", \"keys_sorted()\", \"keys_values()\" "
           "and \"keys_values_sorted()\" can be called\n"
           "For name of hdata and variables, please look at \"Plugin API "
           "reference\", function \"weechat_hdata_get\".\n"
           "\n"
           "Examples (simple strings):\n"
           "  /eval -n ${raw:${info:version}}                  ==> ${info:version}\n"
           "  /eval -n ${eval_cond:${window.win_width}>100}    ==> 1\n"
           "  /eval -n ${info:version}                         ==> 0.4.3\n"
           "  /eval -n ${env:HOME}                             ==> /home/user\n"
           "  /eval -n ${weechat.look.scroll_amount}           ==> 3\n"
           "  /eval -n ${sec.data.password}                    ==> secret\n"
           "  /eval -n ${window}                               ==> 0x2549aa0\n"
           "  /eval -n ${window.buffer}                        ==> 0x2549320\n"
           "  /eval -n ${window.buffer.full_name}              ==> core.weechat\n"
           "  /eval -n ${window.buffer.number}                 ==> 1\n"
           "  /eval -n ${buffer.local_variables.keys_values()} ==> plugin:core,name:weechat\n"
           "  /eval -n ${buffer.local_variables.plugin}        ==> core\n"
           "  /eval -n ${\\t}                                   ==> <tab>\n"
           "  /eval -n ${chars:digit}                          ==> 0123456789\n"
           "  /eval -n ${chars:J-T}                            ==> JKLMNOPQRST\n"
           "  /eval -n ${lower:TEST}                           ==> test\n"
           "  /eval -n ${upper:test}                           ==> TEST\n"
           "  /eval -n ${hide:-,${relay.network.password}}     ==> --------\n"
           "  /eval -n ${cut:3,+,test}                         ==> tes+\n"
           "  /eval -n ${cut:+3,+,test}                        ==> te+\n"
           "  /eval -n ${date:%H:%M:%S}                        ==> 07:46:40\n"
           "  /eval -n ${if:${info:term_width}>80?big:small}   ==> big\n"
           "  /eval -n ${rev:Hello}                            ==> olleH\n"
           "  /eval -n ${repeat:5,-}                           ==> -----\n"
           "  /eval -n ${length:test}                          ==> 4\n"
           "  /eval -n ${split:1,,,abc,def,ghi}                ==> abc\n"
           "  /eval -n ${split:-1,,,abc,def,ghi}               ==> ghi\n"
           "  /eval -n ${split:count,,,abc,def,ghi}            ==> 3\n"
           "  /eval -n ${split:random,,,abc,def,ghi}           ==> def\n"
           "  /eval -n ${split_shell:1,\"arg 1\" arg2}           ==> arg 1\n"
           "  /eval -n ${split_shell:-1,\"arg 1\" arg2}          ==> arg2\n"
           "  /eval -n ${split_shell:count,\"arg 1\" arg2}       ==> 2\n"
           "  /eval -n ${split_shell:random,\"arg 1\" arg2}      ==> arg2\n"
           "  /eval -n ${calc:(5+2)*3}                         ==> 21\n"
           "  /eval -n ${random:0,10}                          ==> 3\n"
           "  /eval -n ${base_encode:64,test}                  ==> dGVzdA==\n"
           "  /eval -n ${base_decode:64,dGVzdA==}              ==> test\n"
           "  /eval -n ${translate:Plugin}                     ==> Extension\n"
           "  /eval -n ${define:len,${calc:5+3}}${len}x${len}  ==> 8x8\n"
           "\n"
           "Examples (conditions):\n"
           "  /eval -n -c ${window.buffer.number} > 2 ==> 0\n"
           "  /eval -n -c ${window.win_width} > 100   ==> 1\n"
           "  /eval -n -c (8 > 12) || (5 > 2)         ==> 1\n"
           "  /eval -n -c (8 > 12) && (5 > 2)         ==> 0\n"
           "  /eval -n -c abcd =~ ^ABC                ==> 1\n"
           "  /eval -n -c abcd =~ (?-i)^ABC           ==> 0\n"
           "  /eval -n -c abcd =~ (?-i)^abc           ==> 1\n"
           "  /eval -n -c abcd !~ abc                 ==> 0\n"
           "  /eval -n -c abcd =* a*d                 ==> 1\n"
           "  /eval -n -c abcd =- bc                  ==> 1"),
        "-n|-s|-c -n|-s|-c",
        &command_eval, NULL, NULL);
    hook_command (
        NULL, "filter",
        N_("filter messages in buffers, to hide/show them according to tags or "
           "regex"),
        N_("list"
           " || enable|disable|toggle [<name>|@]"
           " || add|addreplace <name> <buffer>[,<buffer>...] <tags> <regex>"
           " || rename <name> <new_name>"
           " || recreate <name>"
           " || del <name>|-all [<name>...]"),
        N_("      list: list all filters\n"
           "    enable: enable filters (filters are enabled by default)\n"
           "   disable: disable filters\n"
           "    toggle: toggle filters\n"
           "      name: filter name (\"@\" = enable/disable all filters in "
           "current buffer)\n"
           "       add: add a filter\n"
           "addreplace: add or replace an existing filter\n"
           "    rename: rename a filter\n"
           "  recreate: set input with the command used to edit the filter\n"
           "       del: delete a filter\n"
           "      -all: delete all filters\n"
           "    buffer: comma separated list of buffers where filter "
           "is active:\n"
           "            - this is full name including plugin (example: \"irc."
           "libera.#weechat\" or \"irc.server.libera\")\n"
           "            - \"*\" means all buffers\n"
           "            - a name starting with '!' is excluded\n"
           "            - wildcard \"*\" is allowed\n"
           "   tags: comma separated list of tags, for example \"irc_join,"
           "irc_part,irc_quit\"\n"
           "            - logical \"and\": use \"+\" between tags "
           "(for example: \"nick_toto+irc_action\")\n"
           "            - wildcard \"*\" is allowed\n"
           "            - if tag starts with '!', then it is excluded and "
           "must NOT be in message\n"
           "  regex: POSIX extended regular expression to search in line\n"
           "            - use '\\t' to separate prefix from message, "
           "special chars like '|' must be escaped: '\\|'\n"
           "            - if regex starts with '!', then matching result is "
           "reversed (use '\\!' to start with '!')\n"
           "            - two regular expressions are created: "
           "one for prefix and one for message\n"
           "            - regex are case insensitive, they can start by "
           "\"(?-i)\" to become case sensitive\n"
           "\n"
           "The default key alt+'=' toggles filtering on/off globally and "
           "alt+'-' toggles filtering on/off in the current buffer.\n"
           "\n"
           "Tags most commonly used:\n"
           "  no_filter, no_highlight, no_log, log0..log9 (log level),\n"
           "  notify_none, notify_message, notify_private, notify_highlight,\n"
           "  self_msg, nick_xxx (xxx is nick in message), "
           "prefix_nick_ccc (ccc is color of nick),\n"
           "  host_xxx (xxx is username + host in message),\n"
           "  irc_xxx (xxx is command name or number, see /server raw or "
           "/debug tags),\n"
           "  irc_numeric, irc_error, irc_action, irc_ctcp, irc_ctcp_reply, "
           "irc_smart_filter, away_info.\n"
           "To see tags for lines in buffers: /debug tags\n"
           "\n"
           "Examples:\n"
           "  use IRC smart filter on all buffers:\n"
           "    /filter add irc_smart * irc_smart_filter *\n"
           "  use IRC smart filter on all buffers except those with "
           "\"#weechat\" in name:\n"
           "    /filter add irc_smart *,!*#weechat* irc_smart_filter *\n"
           "  filter all IRC join/part/quit messages:\n"
           "    /filter add joinquit * irc_join,irc_part,irc_quit *\n"
           "  filter nicks displayed when joining channels or with /names:\n"
           "    /filter add nicks * irc_366 *\n"
           "  filter nick \"toto\" on IRC channel #weechat:\n"
           "    /filter add toto irc.libera.#weechat nick_toto *\n"
           "  filter IRC join/action messages from nick \"toto\":\n"
           "    /filter add toto * nick_toto+irc_join,nick_toto+irc_action *\n"
           "  filter lines containing \"weechat sucks\" on IRC channel "
           "#weechat:\n"
           "    /filter add sucks irc.libera.#weechat * weechat sucks\n"
           "  filter lines that are strictly equal to \"WeeChat sucks\" on "
           "all buffers:\n"
           "    /filter add sucks2 * * (?-i)^WeeChat sucks$"),
        "list"
        " || enable %(filters_names)|@"
        " || disable %(filters_names)|@"
        " || toggle %(filters_names)|@"
        " || add|addreplace %(filters_names) %(buffers_plugins_names)|*"
        " || rename %(filters_names) %(filters_names)"
        " || recreate %(filters_names)"
        " || del %(filters_names)|-all %(filters_names)|%*",
        &command_filter, NULL, NULL);
    hook_command (
        NULL, "help",
        N_("display help about commands and options"),
        N_("-list|-listfull [<plugin> [<plugin>...]] || <command> || <option>"),
        N_("    -list: list commands, by plugin (without argument, this list is "
           "displayed)\n"
           "-listfull: list commands with description, by plugin\n"
           "   plugin: list commands for this plugin\n"
           "  command: a command name\n"
           "   option: an option name (use /set to see list)"),
        "-list %(plugins_names)|" PLUGIN_CORE "|%*"
        " || -listfull %(plugins_names)|" PLUGIN_CORE "|%*"
        " || %(commands)|%(config_options)",
        &command_help, NULL, NULL);
    hook_command (
        NULL, "history",
        N_("show buffer command history"),
        N_("clear || <value>"),
        N_("clear: clear history\n"
           "value: number of history entries to show"),
        "clear",
        &command_history, NULL, NULL);
    hook_command (
        NULL, "hotlist",
        N_("manage hotlist"),
        N_("add [low|message|private|highlight]"
           " || clear [<level>]"
           " || remove"
           " || restore [-all]"),
        N_("    add: add current buffer in hotlist (default level: \"low\", "
            "conditions defined in option weechat.look.hotlist_add_conditions "
            "are NOT checked)\n"
           "  clear: clear hotlist\n"
           "  level: \"lowest\" to clear only lowest level in hotlist, "
           "highest\" to clear only highest level in hotlist, or level mask: "
           "integer which is a combination of 1=join/part, 2=message, "
           "4=private, 8=highlight)\n"
           " remove: remove current buffer from hotlist\n"
           "restore: restore latest hotlist removed in the current buffer "
           "(or all buffers with -all)"),
        "add low|message|private|highlight || "
        "clear 1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|lowest|highest || "
        "remove || "
        "restore -all",
        &command_hotlist, NULL, NULL);
    /*
     * give high priority (50000) so that an alias will not take precedence
     * over this command
     */
    hook_command (
        NULL, "50000|input",
        N_("functions for command line"),
        N_("<action> [<arguments>]"),
        N_("list of actions:\n"
           "  return: simulate key \"enter\"\n"
           "  complete_next: complete word with next completion\n"
           "  complete_previous: complete word with previous completion\n"
           "  search_text_here: search text in buffer at current position\n"
           "  search_text: search text in buffer\n"
           "  search_switch_case: switch exact case for search\n"
           "  search_switch_regex: switch search type: string/regular expression\n"
           "  search_switch_where: switch search in messages/prefixes\n"
           "  search_previous: search previous line\n"
           "  search_next: search next line\n"
           "  search_stop_here: stop search at current position\n"
           "  search_stop: stop search\n"
           "  delete_previous_char: delete previous char\n"
           "  delete_next_char: delete next char\n"
           "  delete_previous_word: delete previous word\n"
           "  delete_previous_word_whitespace: delete previous word "
           "(until whitespace)\n"
           "  delete_next_word: delete next word\n"
           "  delete_beginning_of_line: delete from beginning of line until "
           "cursor\n"
           "  delete_end_of_line: delete from cursor until end of line\n"
           "  delete_line: delete entire line\n"
           "  clipboard_paste: paste from the internal clipboard\n"
           "  transpose_chars: transpose two chars\n"
           "  undo: undo last command line action\n"
           "  redo: redo last command line action\n"
           "  move_beginning_of_line: move cursor to beginning of line\n"
           "  move_end_of_line: move cursor to end of line\n"
           "  move_previous_char: move cursor to previous char\n"
           "  move_next_char: move cursor to next char\n"
           "  move_previous_word: move cursor to previous word\n"
           "  move_next_word: move cursor to next word\n"
           "  history_previous: recall previous command in current buffer "
           "history\n"
           "  history_next: recall next command in current buffer history\n"
           "  history_global_previous: recall previous command in global history\n"
           "  history_global_next: recall next command in global history\n"
           "  grab_key: grab a key (optional argument: delay for end of grab, "
            "default is 500 milliseconds)\n"
           "  grab_key_command: grab a key with its associated command (optional "
           "argument: delay for end of grab, default is 500 milliseconds)\n"
           "  grab_mouse: grab mouse event code\n"
           "  grab_mouse_area: grab mouse event code with area\n"
           "  insert: insert text in command line (escaped chars are allowed, "
           "see /help print)\n"
           "  send: send text to the buffer\n"
           "  paste_start: start paste (bracketed paste mode)\n"
           "  paste_stop: stop paste (bracketed paste mode)\n"
           "\n"
           "This command is used by key bindings or plugins."),
        "return || "
        "complete_next || complete_previous || search_text_here || "
        "search_text || search_switch_case || search_switch_regex || "
        "search_switch_where || search_previous || search_next || "
        "search_stop_here || search_stop || "
        "delete_previous_char || delete_next_char || delete_previous_word || "
        "delete_previous_word_whitespace || delete_next_word || "
        "delete_beginning_of_line || delete_end_of_line || delete_line || "
        "clipboard_paste || "
        "transpose_chars || "
        "undo || redo || "
        "move_beginning_of_line || move_end_of_line || move_previous_char || "
        "move_next_char || move_previous_word || move_next_word || "
        "history_previous || history_next || history_global_previous || "
        "history_global_next || "
        "grab_key || grab_key_command || grab_mouse || grab_mouse_area || "
        "insert || send || "
        "paste_start || paste_stop",
        &command_input, NULL, NULL);
    hook_command (
        NULL, "item",
        N_("manage custom bar items"),
        N_("list"
           " || add|addreplace <name> \"<conditions>\" \"<content>\""
           " || rename <name> <new_name>"
           " || refresh <name> [<name>...]"
           " || recreate <name>"
           " || del <name>|-all"),
        N_("      list: list all custom bar items\n"
           "       add: add a custom bar item\n"
           "addreplace: add or replace an existing custom bar item\n"
           "      name: custom bar item name\n"
           "conditions: evaluated conditions to display the bar item "
           "(for example to display the bar item only in specific buffers)\n"
           "   content: content (evaluated, see /help eval)\n"
           "    rename: rename a custom bar item\n"
           "   refresh: update content of item in all bars where the item is "
           "displayed; any item can be refreshed: default/plugin/custom "
           "bar items\n"
           "  recreate: set input with the command used to edit the custom "
           "bar item\n"
           "       del: delete a custom bar item\n"
           "      -all: delete all custom bar items\n"
           "\n"
           "Examples:\n"
           "  add item with terminal size, displayed only in buffers with "
           "number = 1:\n"
           "    /item add terminfo \"${buffer.number} == 1\" "
           "\"term:${info:term_width}x${info:term_height}\"\n"
           "  add item with buffer info:\n"
           "    /item add bufinfo \"\" \"${buffer.number}:${buffer.name}"
           "${if:${buffer.zoomed}?(Z)}\"\n"
           "  add item with date/time using format \"Dec 25, 12:34 +0100\", "
           "refreshed every minute:\n"
           "    /item add datetime \"\" \"${date:%b %d, %H:%M %z}\"\n"
           "    /trigger add datetime_refresh timer \"60000;60\" \"\" \"\" "
           "\"/item refresh datetime\"\n"
           "  add item with number of lines in buffer (displayed/total), "
           "refreshed each time a new line is displayed or if filtered lines "
           "have changed:\n"
           "    /item add lines_count \"\" "
           "\"${calc:${buffer.lines.lines_count}-${buffer.lines.lines_hidden}}/"
           "${buffer.lines.lines_count} lines\"\n"
           "    /trigger add lines_count_refresh_print print \"\" \"\" \"\" "
           "\"/item refresh lines_count\"\n"
           "    /trigger add lines_count_refresh_signal signal \"window_switch;"
           "buffer_switch;buffer_lines_hidden;filters_*\" \"\" \"\" "
           "\"/item refresh lines_count\"\n"
           "  force refresh of item \"lines_count\":\n"
           "    /item refresh lines_count\n"
           "  recreate item \"lines_count\" with different conditions or "
           "content:\n"
           "    /item recreate lines_count\n"
           "  delete item \"lines_count\":\n"
           "    /item del lines_count"),
        "list"
        " || add|addreplace %(custom_bar_item_add_arguments)|%*"
        " || rename %(custom_bar_items_names) %(custom_bar_items_names)"
        " || refresh %(custom_bar_items_names)|%*"
        " || recreate %(custom_bar_items_names)"
        " || del %(custom_bar_items_names)|-all",
        &command_item, NULL, NULL);
    hook_command (
        NULL, "key",
        N_("bind/unbind keys"),
        N_("list|listdefault|listdiff [<context>]"
           " || bind <key> [<command> [<args>]]"
           " || bindctxt <context> <key> [<command> [<args>]]"
           " || unbind <key>"
           " || unbindctxt <context> <key>"
           " || reset <key>"
           " || resetctxt <context> <key>"
           " || resetall -yes [<context>]"
           " || missing [<context>]"),
        N_("       list: list all current keys (without argument, this list is "
           "displayed)\n"
           "listdefault: list default keys\n"
           "   listdiff: list differences between current and default keys "
           "(keys added, redefined or deleted)\n"
           "    context: name of context (\"default\" or \"search\")\n"
           "       bind: bind a command to a key or display command bound to "
           "key (for context \"default\")\n"
           "   bindctxt: bind a command to a key or display command bound to "
           "key, for given context\n"
           "    command: command (many commands can be separated by semicolons)\n"
           "     unbind: remove a key binding (for context \"default\")\n"
           " unbindctxt: remove a key binding for given context\n"
           "      reset: reset a key to default binding (for context "
           "\"default\")\n"
           "  resetctxt: reset a key to default binding, for given context\n"
           "   resetall: restore bindings to the default values and delete ALL "
           "personal bindings (use carefully!)\n"
           "    missing: add missing keys (using default bindings), useful "
           "after installing new WeeChat version\n"
           "\n"
           "When binding a command to a key, it is recommended to use key alt+k "
           "(or Esc then k), and then press the key to bind: this will insert "
           "key code in command line.\n"
           "\n"
           "For context \"mouse\" (possible in context \"cursor\" too), key has "
           "format: \"@area:key\" or \"@area1>area2:key\" where area can be:\n"
           "          *: any area on screen\n"
           "       chat: chat area (any buffer)\n"
           "  chat(xxx): chat area for buffer with name \"xxx\" (full name "
           "including plugin)\n"
           "     bar(*): any bar\n"
           "   bar(xxx): bar \"xxx\"\n"
           "    item(*): any bar item\n"
           "  item(xxx): bar item \"xxx\"\n"
           "Wildcard \"*\" is allowed in key to match many mouse events.\n"
           "A special value for command with format \"hsignal:name\" can be "
           "used for context mouse, this will send the hsignal \"name\" with "
           "the focus hashtable as argument.\n"
           "Another special value \"-\" can be used to disable key (it will be "
           "ignored when looking for keys).\n"
           "\n"
           "Examples:\n"
           "  key alt-t to toggle nicklist bar:\n"
           "    /key bind meta-t /bar toggle nicklist\n"
           "  key alt-r to jump to #weechat IRC channel:\n"
           "    /key bind meta-r /buffer #weechat\n"
           "  restore default binding for key alt-r:\n"
           "    /key reset meta-r\n"
           "  key \"tab\" to stop search in buffer:\n"
           "    /key bindctxt search ctrl-i /input search_stop\n"
           "  middle button of mouse on a nick to retrieve info on nick:\n"
           "    /key bindctxt mouse @item(buffer_nicklist):button3 "
           "/msg nickserv info ${nick}"),
        "list %(keys_contexts)"
        " || listdefault %(keys_contexts)"
        " || listdiff %(keys_contexts)"
        " || bind %(keys_codes) %(commands:/)"
        " || bindctxt %(keys_contexts) %(keys_codes) %(commands:/)"
        " || unbind %(keys_codes)"
        " || unbindctxt %(keys_contexts) %(keys_codes)"
        " || reset %(keys_codes_for_reset)"
        " || resetctxt %(keys_contexts) %(keys_codes_for_reset)"
        " || resetall %- %(keys_contexts)"
        " || missing %(keys_contexts)",
        &command_key, NULL, NULL);
    hook_command (
        NULL, "layout",
        N_("manage buffers/windows layouts"),
        N_("store [<name>] [buffers|windows]"
           " || apply [<name>] [buffers|windows]"
           " || leave"
           " || del [<name>] [buffers|windows]"
           " || rename <name> <new_name>"),
        N_("  store: store current buffers/windows in a layout\n"
           "  apply: apply stored layout\n"
           "  leave: leave current layout (does not update any layout)\n"
           "    del: delete buffers and/or windows in a stored layout\n"
           "         (if neither \"buffers\" nor \"windows\" is given after "
           "the name, the layout is deleted)\n"
           " rename: rename a layout\n"
           "   name: name for stored layout (default is \"default\")\n"
           "buffers: store/apply only buffers (order of buffers)\n"
           "windows: store/apply only windows (buffer displayed by each "
           "window)\n"
           "\n"
           "Without argument, this command displays stored layouts.\n"
           "\n"
           "The current layout can be saved on /quit command with the option "
           "\"weechat.look.save_layout_on_exit\".\n"
           "\n"
           "Note: the layout only remembers windows split and buffers numbers. "
           "It does not open buffers. That means for example you must still "
           "auto-join IRC channels to open the buffers, the saved layout only "
           "applies once the buffers are opened."),
        "store %(layouts_names)|buffers|windows buffers|windows"
        " || apply %(layouts_names)|buffers|windows buffers|windows"
        " || leave"
        " || del %(layouts_names)|buffers|windows buffers|windows"
        " || rename %(layouts_names) %(layouts_names)",
        &command_layout, NULL, NULL);
    hook_command (
        NULL, "mouse",
        N_("mouse control"),
        N_("enable|disable|toggle [<delay>]"),
        N_(" enable: enable mouse\n"
           "disable: disable mouse\n"
           " toggle: toggle mouse\n"
           "  delay: delay (in seconds) after which initial mouse state is "
           "restored (useful to temporarily disable mouse)\n"
           "\n"
           "The mouse state is saved in option \"weechat.look.mouse\".\n"
           "\n"
           "Examples:\n"
           "  enable mouse:\n"
           "    /mouse enable\n"
           "  toggle mouse for 5 seconds:\n"
           "    /mouse toggle 5"),
        "enable|disable|toggle",
        &command_mouse, NULL, NULL);
    hook_command (
        NULL, "mute",
        N_("execute a command silently"),
        N_("[-core | -current | -buffer <name>] <command>"),
        N_("   -core: no output on WeeChat core buffer\n"
           "-current: no output on current buffer\n"
           " -buffer: no output on specified buffer\n"
           "    name: full buffer name (examples: \"irc.server.libera\", "
           "\"irc.libera.#weechat\")\n"
           " command: command to execute silently (a '/' is automatically added "
           "if not found at beginning of command)\n"
           "\n"
           "If no target is specified (-core, -current or -buffer), then "
            "default is to mute all buffers.\n"
           "\n"
           "Examples:\n"
           "  config save:\n"
           "    /mute save\n"
           "  message to current IRC channel:\n"
           "    /mute -current msg * hi!\n"
           "  message to #weechat channel:\n"
           "    /mute -buffer irc.libera.#weechat msg #weechat hi!"),
        "-core|-current %(commands:/)|%*"
        " || -buffer %(buffers_plugins_names) %(commands:/)|%*"
        " || %(commands:/)|%*",
        &command_mute, NULL, NULL);
    hook_command (
        NULL, "plugin",
        N_("list/load/unload plugins"),
        N_("list [-o|-ol|-i|-il|<name>]"
           " || listfull [<name>]"
           " || load <filename> [<arguments>]"
           " || autoload [<arguments>]"
           " || reload [<name>|* [<arguments>]]"
           " || unload [<name>]"),
        N_("     list: list loaded plugins\n"
           "       -o: send list of loaded plugins to buffer "
           "(string in English)\n"
           "      -ol: send list of loaded plugins to buffer "
           "(translated string)\n"
           "       -i: copy list of loaded plugins in command line (for "
           "sending to buffer) (string in English)\n"
           "      -il: copy list of loaded plugins in command line (for "
           "sending to buffer) (translated string)\n"
           "     name: a plugin name\n"
           " listfull: list loaded plugins (verbose)\n"
           "     load: load a plugin\n"
           " filename: plugin (file) to load\n"
           "arguments: arguments given to plugin on load\n"
           " autoload: autoload plugins in system or user directory\n"
           "   reload: reload a plugin (if no name given, unload all plugins, "
           "then autoload plugins)\n"
           "   unload: unload a plugin (if no name given, unload all plugins)\n"
           "\n"
           "Without argument, this command lists loaded plugins."),
        "list %(plugins_names)|-i|-il|-o|-ol"
        " || listfull %(plugins_names)"
        " || load %(plugins_installed)"
        " || autoload"
        " || reload %(plugins_names)|* -a|-s"
        " || unload %(plugins_names)",
        &command_plugin, NULL, NULL);
    hook_command (
        NULL, "print",
        N_("display text on a buffer"),
        N_("[-buffer <number>|<name>] [-newbuffer <name>] [-free] [-switch] "
           "[-core|-current] [-y <line>] [-escape] [-date <date>] "
           "[-tags <tags>] [-action|-error|-join|-network|-quit] [<text>]"
           " || -stdout|-stderr [<text>]"
           " || -beep"),
        N_("   -buffer: display text in this buffer (default: buffer where "
           "command is executed)\n"
           "-newbuffer: create a new buffer and display text in this buffer\n"
           "     -free: create a buffer with free content "
           "(with -newbuffer only)\n"
           "   -switch: switch to the buffer\n"
           "     -core: alias of \"-buffer core.weechat\"\n"
           "  -current: display text on current buffer\n"
           "        -y: display on a custom line (for buffer with free content "
           "only)\n"
           "      line: line number for buffer with free content (first line "
           "is 0, a negative number displays after last line: -1 = after last "
           "line, -2 = two lines after last line, etc.)\n"
           "   -escape: interpret escaped chars (for example \\a, \\07, \\x07)\n"
           "     -date: message date, format can be:\n"
           "              -n: 'n' seconds before now\n"
           "              +n: 'n' seconds in the future\n"
           "               n: 'n' seconds since the Epoch (see man time)\n"
           "              date/time (ISO 8601): yyyy-mm-ddThh:mm:ss, example: "
           "2014-01-19T04:32:55\n"
           "              time: hh:mm:ss (example: 04:32:55)\n"
           "     -tags: comma-separated list of tags (see /help filter for a "
           "list of tags most commonly used)\n"
           "      text: text to display (prefix and message must be separated by "
           "\"\\t\", if text starts with \"-\", then add a \"\\\" before)\n"
           "   -stdout: display text on stdout (escaped chars are interpreted)\n"
           "   -stderr: display text on stderr (escaped chars are interpreted)\n"
           "     -beep: alias of \"-stderr \\a\"\n"
           "\n"
           "The options -action ... -quit use the prefix defined in options "
           "\"weechat.look.prefix_*\".\n"
           "\n"
           "Following escaped chars are supported:\n"
           "  \\\" \\\\ \\a \\b \\e \\f \\n \\r \\t \\v \\0ooo \\xhh \\uhhhh "
           "\\Uhhhhhhhh\n"
           "\n"
           "Examples:\n"
           "  display a reminder on core buffer with a highlight:\n"
           "    /print -core -tags notify_highlight Reminder: buy milk\n"
           "  display an error on core buffer:\n"
           "    /print -core -error Some error here\n"
           "  display message on core buffer with prefix \"abc\":\n"
           "    /print -core abc\\tThe message\n"
           "  display a message on channel #weechat:\n"
           "    /print -buffer irc.libera.#weechat Message on #weechat\n"
           "  display a snowman (U+2603):\n"
           "    /print -escape \\u2603\n"
           "  send alert (BEL):\n"
           "    /print -beep"),
        "-buffer %(buffers_numbers)|%(buffers_plugins_names)"
        " || -newbuffer"
        " || -y -1|0|1|2|3"
        " || -free|-switch|-core|-current|-escape|-date|-tags|-action|-error|"
        "-join|-network|-quit"
        " || -stdout"
        " || -stderr"
        " || -beep",
        &command_print, NULL, NULL);
    hook_command (
        NULL, "proxy",
        N_("manage proxies"),
        N_("list"
           " || add <name> <type> <address> <port> [<username> [<password>]]"
           " || del <name>|-all"
           " || set <name> <option> <value>"),
        N_("    list: list all proxies\n"
           "     add: add a new proxy\n"
           "    name: name of proxy (must be unique)\n"
           "    type: http, socks4 or socks5\n"
           " address: IP or hostname\n"
           "    port: port\n"
           "username: username (optional)\n"
           "password: password (optional)\n"
           "     del: delete a proxy (or all proxies with -all)\n"
           "     set: set a value for a proxy property\n"
           "  option: option to change (for options list, look at /set "
           "weechat.proxy.<proxyname>.*)\n"
           "   value: new value for option\n"
           "\n"
           "Examples:\n"
           "  add a http proxy, running on local host, port 8888:\n"
           "    /proxy add local http 127.0.0.1 8888\n"
           "  add a http proxy using IPv6 protocol:\n"
           "    /proxy add local http ::1 8888\n"
           "    /proxy set local ipv6 on\n"
           "  add a socks5 proxy with username/password:\n"
           "    /proxy add myproxy socks5 sample.host.org 3128 myuser mypass\n"
           "  delete a proxy:\n"
           "    /proxy del myproxy"),
        "list"
        " || add %(proxies_names) http|socks4|socks5"
        " || del %(proxies_names)"
        " || set %(proxies_names) %(proxies_options)",
        &command_proxy, NULL, NULL);
    hook_command (
        NULL, "quit",
        N_("quit WeeChat"),
        N_("[-yes] [<arguments>]"),
        N_("     -yes: required if option \"weechat.look.confirm_quit\" "
           "is enabled\n"
           "arguments: text sent with signal \"quit\"\n"
           "           (for example irc plugin uses this text to send quit "
           "message to server)\n"
           "\n"
           "By default when quitting the configuration files are saved "
           "(see option \"weechat.look.save_config_on_exit\") and the current "
           "layout can be saved (see option "
           "\"weechat.look.save_layout_on_exit\")."),
        "",
        &command_quit, NULL, NULL);
    hook_command (
        NULL, "reload",
        N_("reload configuration files from disk"),
        N_("[<file> [<file>...]]"),
        N_("file: configuration file to reload (without extension \".conf\")\n"
           "\n"
           "Without argument, all files (WeeChat and plugins) are reloaded."),
        "%(config_files)|%*",
        &command_reload, NULL, NULL);
    hook_command (
        NULL, "repeat",
        N_("execute a command several times"),
        N_("[-interval <delay>[<unit>]] <count> <command>"),
        N_("  delay: delay between execution of commands\n"
           "   unit: optional, values are:\n"
           "           ms: milliseconds\n"
           "            s: seconds (default)\n"
           "            m: minutes\n"
           "            h: hours\n"
           "  count: number of times to execute command\n"
           "command: command to execute (or text to send to buffer if command "
           "does not start with '/')\n"
           "\n"
           "Note: the command is executed on buffer where /repeat was executed "
           "(if the buffer does not exist any more, the command is not "
           "executed).\n"
           "\n"
           "Example:\n"
           "  scroll 2 pages up:\n"
           "    /repeat 2 /window page_up"),
        "%- %(commands:/)",
        &command_repeat, NULL, NULL);
    hook_command (
        NULL, "save",
        N_("save configuration files to disk"),
        N_("[<file> [<file>...]]"),
        N_("file: configuration file to save (without extension \".conf\")\n"
           "\n"
           "Without argument, all files (WeeChat and plugins) are saved.\n"
           "\n"
           "By default all configuration files are saved to disk on /quit "
           "command (see option \"weechat.look.save_config_on_exit\")."),
        "%(config_files)|%*",
        &command_save, NULL, NULL);
    hook_command (
        NULL, "secure",
        N_("manage secured data (passwords or private data encrypted in file "
           "sec.conf)"),
        N_("passphrase <passphrase>|-delete"
           " || decrypt <passphrase>|-discard"
           " || set <name> <value>"
           " || del <name>"),
        N_("passphrase: change the passphrase (without passphrase, data is "
           "stored as plain text in file sec.conf)\n"
           "   -delete: delete passphrase\n"
           "   decrypt: decrypt data still encrypted (it happens only if "
           "passphrase was not given on startup)\n"
           "  -discard: discard all data still encrypted\n"
           "       set: add or change secured data\n"
           "       del: delete secured data\n"
           "\n"
           "Without argument, this command displays secured data in a new "
           "buffer.\n"
           "\n"
           "Keys on secure buffer:\n"
           "  alt+v  toggle values\n"
           "\n"
           "When a passphrase is used (data encrypted), it is asked by WeeChat "
           "on startup.\n"
           "It is possible to set environment variable \"WEECHAT_PASSPHRASE\" "
           "to prevent the prompt (this same variable is used by WeeChat on "
           "/upgrade), or to set option sec.crypt.passphrase_command to read "
           "the passphrase from the output of an external command like a "
           "password manager (see /help sec.crypt.passphrase_command).\n"
           "\n"
           "Secured data with format ${sec.data.xxx} can be used in:\n"
           "  - command /eval\n"
           "  - command line argument \"--run-command\"\n"
           "  - options weechat.startup.command_{before|after}_plugins\n"
           "  - other options that may contain a password or sensitive data "
           "(for example proxy, irc server and relay); see /help on the "
           "options to check if they are evaluated.\n"
           "\n"
           "Examples:\n"
           "  set a passphrase:\n"
           "    /secure passphrase this is my passphrase\n"
           "  use program \"pass\" to read the passphrase on startup:\n"
           "    /set sec.crypt.passphrase_command "
           "\"/usr/bin/pass show weechat/passphrase\"\n"
           "  encrypt libera SASL password:\n"
           "    /secure set libera mypassword\n"
           "    /set irc.server.libera.sasl_password \"${sec.data.libera}\"\n"
           "  encrypt oftc password for nickserv:\n"
           "    /secure set oftc mypassword\n"
           "    /set irc.server.oftc.command \"/msg nickserv identify "
           "${sec.data.oftc}\"\n"
           "  alias to ghost the nick \"mynick\":\n"
           "    /alias add ghost /eval /msg -server libera nickserv ghost mynick "
           "${sec.data.libera}"),
        "passphrase -delete"
        " || decrypt -discard"
        " || set %(secured_data)"
        " || del %(secured_data)",
        &command_secure, NULL, NULL);
    hook_command (
        NULL, "set",
        N_("set config options and environment variables"),
        N_("[<option> [<value>]]"
           " || diff [<option> [<option>...]]"
           " || env [<variable> [<value>]]"),
        N_("option: name of an option (wildcard \"*\" is allowed to list "
           "options, if no value is specified)\n"
           " value: new value for option, according to type:\n"
           "          boolean: on, off or toggle\n"
           "          integer: number, ++number or --number\n"
           "           string: any string (\"\" for empty string)\n"
           "            color: color name, ++number or --number\n"
           "        Note: for all types, you can use null to remove option "
           "value (undefined value). This works only for some special plugin "
           "variables.\n"
           "  diff: display only changed options\n"
           "   env: display or set an environment variable (use value \"\" to "
           "unset a variable)\n"
           "\n"
           "Examples:\n"
           "  display options about highlight:\n"
           "    /set *highlight*\n"
           "  add a word to highlight:\n"
           "    /set weechat.look.highlight \"word\"\n"
           "  display changed options:\n"
           "    /set diff\n"
           "  display changed options in irc plugin:\n"
           "    /set diff irc.*\n"
           "  display value of environment variable LANG:\n"
           "    /set env LANG\n"
           "  set environment variable LANG and use it:\n"
           "    /set env LANG fr_FR.UTF-8\n"
           "    /upgrade\n"
           "  unset environment variable ABC:\n"
           "    /set env ABC \"\""),
        "%(config_options) %(config_option_values)"
        " || diff %(config_options)|%*"
        " || env %(env_vars) %(env_value)",
        &command_set, NULL, NULL);
    hook_command (
        NULL, "toggle",
        N_("toggle value of a config option"),
        N_("<option> [<value> [<value>...]]"),
        N_("option: name of an option\n"
           " value: possible values for the option (values are split like the "
           "shell command arguments: quotes can be used to preserve spaces at "
           "the beginning/end of values)\n"
           "\n"
           "Behavior:\n"
           "  - only an option of type boolean or string can be toggled "
           "without a value:\n"
           "      - boolean: toggle between on/off according to current value\n"
           "      - string: toggle between empty string and default value "
           "(works only if empty string is allowed for the option)\n"
           "  - with a single value given, toggle between this value and "
           "the default value of option\n"
           "  - with multiple values given, toggle between these values: "
           "the value used is the one following the current value of option; "
           "if the current value of option is not in list, the first value in "
           "the list is used\n"
           "  - the special value \"null\" can be given, but only as first "
           "value in the list and without quotes around.\n"
           "\n"
           "Examples:\n"
           "  toggle display of time in chat area (without displaying the "
           "new value used):\n"
           "    /mute /toggle weechat.look.buffer_time_format\n"
           "  switch format of time in chat area (with seconds, without "
           "seconds, disabled):\n"
           "    /toggle weechat.look.buffer_time_format \"%H:%M:%S\" "
           "\"%H:%M\" \"\"\n"
           "  toggle autojoin of #weechat channel on libera server:\n"
           "    /toggle irc.server.libera.autojoin null #weechat"),
        "%(config_options) %(config_option_values)",
        &command_toggle, NULL, NULL);
    hook_command (
        NULL, "unset",
        N_("unset/reset config options"),
        N_("<option>"
           " || -mask <option>"),
        N_("option: name of an option\n"
           " -mask: use a mask in option (wildcard \"*\" is allowed to "
           "mass-reset options, use carefully!)\n"
           "\n"
           "According to option, it's reset (for standard options) or removed "
           "(for optional settings, like server values).\n"
           "\n"
           "Examples:\n"
           "  reset one option:\n"
           "    /unset weechat.look.item_time_format\n"
           "  reset all color options:\n"
           "    /unset -mask weechat.color.*"),
        "%(config_options)"
        " || -mask %(config_options)",
        &command_unset, NULL, NULL);
    hook_command (
        NULL, "upgrade",
        N_("save WeeChat session and reload the WeeChat binary without "
           "disconnecting from servers"),
        N_("[-yes] [<path_to_binary>|-save|-quit]"),
        N_("          -yes: required if option \"weechat.look.confirm_upgrade\" "
           "is enabled\n"
           "path_to_binary: path to WeeChat binary (default is current binary)\n"
           "        -dummy: do nothing (option used to prevent accidental "
           "completion with \"-quit\")\n"
           "         -save: only save the session, do not quit nor reload "
           "WeeChat; the configuration files are not saved (if needed you can "
           "use /save before this command)\n"
           "         -quit: close *ALL* connections, save session and quit "
           "WeeChat, which makes possible a delayed restoration (see below)\n"
           "\n"
           "This command upgrades and reloads a running WeeChat session. The "
           "new WeeChat binary must have been compiled or installed with a "
           "package manager before running this command.\n"
           "\n"
           "Note: SSL connections are lost during upgrade (except with -save), "
           "because the reload of SSL sessions is currently not possible with "
           "GnuTLS. There is automatic reconnection after upgrade.\n"
           "\n"
           "Important: use of option -save can be dangerous, it is recommended "
           "to use only /upgrade (or with -quit) for a standard upgrade and "
           "a restart; the option -save can be used to save the session "
           "regularly and restore it in case of after abnormal exit "
           "(power outage, crash, etc.)\n"
           "\n"
           "Upgrade process has 4 steps:\n"
           "  1. save session into files for core and plugins (buffers, "
           "history, ..)\n"
           "  2. unload all plugins (configuration files (*.conf) are written "
           "on disk)\n"
           "  3. save WeeChat configuration (weechat.conf)\n"
           "  4. execute new WeeChat binary and reload session.\n"
           "\n"
           "With option \"-quit\", the process is:\n"
           "  1. close *ALL* connections (irc, xfer, relay, ...)\n"
           "  2. save session into files (*.upgrade)\n"
           "  3. unload all plugins\n"
           "  4. save WeeChat configuration\n"
           "  5. quit WeeChat\n"
           "\n"
           "With option \"-save\", the process is:\n"
           "  1. save session into files (*.upgrade) with a disconnected state "
           "for IRC servers and Relay clients (but no disconnection is made)\n"
           "\n"
           "With -quit or -save, you can restore the session later with "
           "this command: weechat --upgrade\n"
           "IMPORTANT: you must restore the session with exactly same "
           "configuration (files *.conf) and if possible the same WeeChat "
           "version (or a more recent one).\n"
           "It is possible to restore WeeChat session on another machine if you "
           "copy the content of WeeChat home directories (see /debug dirs)."),
        "%(filename)|-dummy|-save|-quit",
        &command_upgrade, NULL, NULL);
    hook_command (
        NULL, "uptime",
        N_("show WeeChat uptime"),
        "[-o|-ol]",
        N_(" -o: send uptime to current buffer as input (English string)\n"
           "-ol: send uptime to current buffer as input (translated string)"),
        "-o|-ol",
        &command_uptime, NULL, NULL);
    hook_command (
        NULL, "version",
        N_("show WeeChat version and compilation date"),
        "[-o|-ol]",
        N_(" -o: send version to current buffer as input (English string)\n"
           "-ol: send version to current buffer as input (translated string)\n"
           "\n"
           "The default alias /v can be used to execute this command on "
           "all buffers (otherwise the irc command /version is used on irc "
           "buffers)."),
        "-o|-ol",
        &command_version, NULL, NULL);
    hook_command (
        NULL, "wait",
        N_("schedule a command execution in future"),
        N_("<number>[<unit>] <command>"),
        N_(" number: amount of time to wait (integer number)\n"
           "   unit: optional, values are:\n"
           "           ms: milliseconds\n"
           "            s: seconds (default)\n"
           "            m: minutes\n"
           "            h: hours\n"
           "command: command to execute (or text to send to buffer if command "
           "does not start with '/')\n"
           "\n"
           "Note: the command is executed on buffer where /wait was executed "
           "(if the buffer does not exist any more, the command is not "
           "executed).\n"
           "\n"
           "Examples:\n"
           "  join channel in 10 seconds:\n"
           "    /wait 10 /join #test\n"
           "  set away in 15 minutes:\n"
           "    /wait 15m /away -all I'm away\n"
           "  say 'hello' in 2 minutes:\n"
           "    /wait 2m hello"),
        "%- %(commands:/)",
        &command_wait, NULL, NULL);
    hook_command (
        NULL, "window",
        N_("manage windows"),
        /* xgettext:no-c-format */
        N_("list"
           " || -1|+1|b#|up|down|left|right [-window <number>]"
           " || <number>"
           " || splith|splitv [-window <number>] [<pct>]"
           " || resize [-window <number>] [h|v][+|-]<pct>"
           " || balance"
           " || merge [-window <number>] [all]"
           " || close [-window <number>]"
           " || page_up|page_down [-window <number>]"
           " || refresh"
           " || scroll [-window <number>] [+|-]<value>[s|m|h|d|M|y]"
           " || scroll_horiz [-window <number>] [+|-]<value>[%]"
           " || scroll_up|scroll_down|scroll_top|scroll_bottom|"
           "scroll_beyond_end|scroll_previous_highlight|scroll_next_highlight|"
           "scroll_unread [-window <number>]"
           " || swap [-window <number>] [up|down|left|right]"
           " || zoom [-window <number>]"
           " || bare [<delay>]"),
        /* xgettext:no-c-format */
        N_("         list: list opened windows (without argument, this list is "
           "displayed)\n"
           "           -1: jump to previous window\n"
           "           +1: jump to next window\n"
           "           b#: jump to next window displaying buffer number #\n"
           "           up: switch to window above current one\n"
           "         down: switch to window below current one\n"
           "         left: switch to window on the left\n"
           "        right: switch to window on the right\n"
           "       number: window number (see /window list)\n"
           "       splith: split current window horizontally "
           "(to undo: /window merge)\n"
           "       splitv: split current window vertically "
           "(to undo: /window merge)\n"
           "       resize: resize window size, new size is <pct> percentage of "
           "parent window\n"
           "               if \"h\" or \"v\" is specified, the resize affects "
           "the nearest parent window with a split of this type "
           "(horizontal/vertical)\n"
           "      balance: balance the sizes of all windows\n"
           "        merge: merge window with another (all = keep only one "
           "window)\n"
           "        close: close window\n"
           "      page_up: scroll one page up\n"
           "    page_down: scroll one page down\n"
           "      refresh: refresh screen\n"
           "       scroll: scroll a number of lines (+/-N) or with time: "
           "s=seconds, m=minutes, h=hours, d=days, M=months, y=years\n"
           " scroll_horiz: scroll horizontally a number of columns (+/-N) or "
           "percentage of window size (this scrolling is possible only on "
           "buffers with free content)\n"
           "    scroll_up: scroll a few lines up\n"
           "  scroll_down: scroll a few lines down\n"
           "   scroll_top: scroll to top of buffer\n"
           "scroll_bottom: scroll to bottom of buffer\n"
           "scroll_beyond_end: scroll beyond the end of buffer\n"
           "scroll_previous_highlight: scroll to previous highlight\n"
           "scroll_next_highlight: scroll to next highlight\n"
           "scroll_unread: scroll to unread marker\n"
           "         swap: swap buffers of two windows (with optional direction "
           "for target window)\n"
           "         zoom: zoom on window\n"
           "         bare: toggle bare display (with optional delay in "
           "seconds for automatic return to standard display mode)\n"
           "\n"
           "For splith and splitv, pct is a percentage which represents size of "
           "new window, computed with current window as size reference. For "
           "example 25 means create a new window with size = current_size / 4\n"
           "\n"
           "Examples:\n"
           "  jump to window displaying buffer #1:\n"
           "    /window b1\n"
           "  scroll 2 lines up:\n"
           "    /window scroll -2\n"
           "  scroll 2 days up:\n"
           "    /window scroll -2d\n"
           "  scroll to beginning of current day:\n"
           "    /window scroll -d\n"
           "  zoom on window #2:\n"
           "    /window zoom -window 2\n"
           "  split window horizontally using 30% of space for the window on "
           "top:\n"
           "    /window splith 30\n"
           "  resize window to 75% of the parent window size:\n"
           "    /window resize 75\n"
           "  resize vertical split, add 10% in size:\n"
           "    /window resize v+10\n"
           "  remove the split, keep the current window:\n"
           "    /window merge\n"
           "  close the current window:\n"
           "    /window close\n"
           "  enable bare display for 2 seconds:\n"
           "    /window bare 2"),
        "list"
        " || -1 -window %(windows_numbers)"
        " || +1 -window %(windows_numbers)"
        " || up -window %(windows_numbers)"
        " || down -window %(windows_numbers)"
        " || left -window %(windows_numbers)"
        " || right -window %(windows_numbers)"
        " || splith -window %(windows_numbers)"
        " || splitv -window %(windows_numbers)"
        " || resize -window %(windows_numbers)"
        " || balance"
        " || page_up -window %(windows_numbers)"
        " || page_down -window %(windows_numbers)"
        " || refresh"
        " || scroll -window %(windows_numbers)"
        " || scroll_horiz -window %(windows_numbers)"
        " || scroll_up -window %(windows_numbers)"
        " || scroll_down -window %(windows_numbers)"
        " || scroll_top -window %(windows_numbers)"
        " || scroll_bottom -window %(windows_numbers)"
        " || scroll_beyond_end -window %(windows_numbers)"
        " || scroll_previous_highlight -window %(windows_numbers)"
        " || scroll_next_highlight -window %(windows_numbers)"
        " || scroll_unread  -window %(windows_numbers)"
        " || swap up|down|left|right|-window %(windows_numbers)"
        " || zoom -window %(windows_numbers)"
        " || merge all|-window %(windows_numbers)"
        " || close -window %(windows_numbers)"
        " || bare"
        " || %(windows_numbers)",
        &command_window, NULL, NULL);
}

/*
 * Executes a list of commands (separated by ";").
 */

void
command_exec_list (const char *command_list)
{
    char **commands, **ptr_command, *command_eval;

    if (!command_list || !command_list[0])
        return;

    commands = string_split_command (command_list, ';');
    if (commands)
    {
        for (ptr_command = commands; *ptr_command; ptr_command++)
        {
            command_eval = eval_expression (*ptr_command, NULL, NULL, NULL);
            if (command_eval)
            {
                (void) input_data (gui_buffer_search_main (),
                                   command_eval, NULL);
                free (command_eval);
            }
        }
        string_free_split_command (commands);
    }
}

/*
 * Executes commands at startup.
 */

void
command_startup (int plugins_loaded)
{
    int i;

    if (plugins_loaded)
    {
        command_exec_list (CONFIG_STRING(config_startup_command_after_plugins));
        if (weechat_startup_commands)
        {
            for (i = 0; i < weelist_size (weechat_startup_commands); i++)
            {
                command_exec_list (
                    weelist_string (
                        weelist_get (weechat_startup_commands, i)));
            }
        }
    }
    else
        command_exec_list (CONFIG_STRING(config_startup_command_before_plugins));
}
