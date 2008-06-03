/*
 * Copyright (c) 2003-2008 by FlashCode <flashcode@flashtux.org>
 * See README for License detail, AUTHORS for developers list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* wee-input.c: default callback function to read user input */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "weechat.h"
#include "wee-input.h"
#include "wee-config.h"
#include "wee-hook.h"
#include "wee-string.h"
#include "../gui/gui-buffer.h"
#include "../gui/gui-chat.h"
#include "../plugins/plugin.h"



/*
 * input_is_command: return 1 if line is a command, 0 otherwise
 */

int
input_is_command (const char *line)
{
    char *pos_slash, *pos_space;

    if (strncmp (line, "/*", 2) == 0)
        return 0;
    
    pos_slash = strchr (line + 1, '/');
    pos_space = strchr (line + 1, ' ');
    
    return (line[0] == '/')
        && (!pos_slash || (pos_space && pos_slash > pos_space));
}

/*
 * input_exec_command: executes a command (WeeChat internal or a
 *                     plugin command)
 *                     if only_builtin == 1, then try only
 *                     WeeChat commands (not plugins neither aliases)
 *                     returns: 1 if command was executed succesfully
 *                              0 if error (command not executed)
 */

int
input_exec_command (struct t_gui_buffer *buffer,
                    int any_plugin,
                    struct t_weechat_plugin *plugin,
                    const char *string)
{
    int rc;
    char *command, *pos, *ptr_args;
    
    if ((!string) || (!string[0]) || (string[0] != '/'))
        return 0;
    
    command = strdup (string);
    if (!command)
        return 0;
    
    /* look for end of command */
    ptr_args = NULL;
    
    pos = &command[strlen (command) - 1];
    if (pos[0] == ' ')
    {
        while ((pos > command) && (pos[0] == ' '))
            pos--;
        pos[1] = '\0';
    }
    
    rc = hook_command_exec (buffer, any_plugin, plugin, command);
    
    pos = strchr (command, ' ');
    if (pos)
    {
        pos[0] = '\0';
        pos++;
        while (pos[0] == ' ')
            pos++;
        ptr_args = pos;
        if (!ptr_args[0])
            ptr_args = NULL;
    }
    
    switch (rc)
    {
        case 0: /* command hooked, KO */
            break;
        case 1: /* command hooked, OK (executed) */
            break;
        case -3: /* command is running */
            gui_chat_printf (NULL,
                             _("%sError: too much calls to command \"%s\" "
                               "(looping)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             command + 1);
            break;
        default: /* no command hooked */
            gui_chat_printf (NULL,
                             _("%sError: unknown command \"%s\" (type /help "
                               "for help)"),
                             gui_chat_prefix[GUI_CHAT_PREFIX_ERROR],
                             command + 1);
            break;
    }
    free (command);
    return 0;
}

/*
 * input_data: read user input and send data to buffer callback
 */

void
input_data (struct t_gui_buffer *buffer, const char *data)
{
    char *new_data, *pos;
    const char *ptr_data;
    
    if (!buffer || !data || !data[0] || (data[0] == '\r') || (data[0] == '\n'))
        return;

    /* TODO: modifier for input */
    /*new_data = plugin_modifier_exec (PLUGIN_MODIFIER_IRC_USER,
      "", data);*/
    new_data = strdup (data);
    
    /* no changes in new data */
    if (new_data && (strcmp (data, new_data) == 0))
    {
        free (new_data);
        new_data = NULL;
    }
    
    /* message not dropped? */
    if (!new_data || new_data[0])
    {
        /* use new data (returned by plugin) */
        ptr_data = (new_data) ? new_data : data;
        
        while (ptr_data && ptr_data[0])
        {
            pos = strchr (ptr_data, '\n');
            if (pos)
                pos[0] = '\0';
            
            if (input_is_command (ptr_data))
            {
                /* WeeChat or plugin command */
                (void) input_exec_command (buffer, 1, buffer->plugin, ptr_data);
            }
            else
            {
                if ((ptr_data[0] == '/') && (ptr_data[1] == '/'))
                    ptr_data++;
                
                hook_command_exec (buffer, 1, buffer->plugin, ptr_data);
                
                if (buffer->input_callback)
                {
                    (void)(buffer->input_callback) (buffer->input_callback_data,
                                                    buffer,
                                                    ptr_data);
                }
                else
                    gui_chat_printf (buffer,
                                     _("%sYou can not write text in this "
                                       "buffer"),
                                     gui_chat_prefix[GUI_CHAT_PREFIX_ERROR]);
            }

            if (pos)
            {
                pos[0] = '\n';
                ptr_data = pos + 1;
            }
            else
                ptr_data = NULL;
        }
    }
}
