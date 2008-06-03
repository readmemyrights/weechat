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


#ifndef __WEECHAT_XFER_FILE_H
#define __WEECHAT_XFER_FILE_H 1

extern int xfer_file_resume (struct t_xfer *xfer, const char *filename);
extern void xfer_file_find_filename (struct t_xfer *xfer);
extern void xfer_file_calculate_speed (struct t_xfer *xfer, int ended);

#endif /* xfer-file.h */
