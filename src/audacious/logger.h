/*  Audacious - Cross-platform multimedia player
 *  Copyright (C) 2005-2007  Audacious development team
 *
 *  Based on BMP:
 *  Copyright (C) 2003-2004  BMP development team.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <glib.h>


#define BMP_LOGGER_DEFAULT_LOG_LEVEL  G_LOG_LEVEL_MESSAGE

/* default log file max size: 512kb */
#define BMP_LOGGER_FILE_MAX_SIZE      ((size_t)1 << 19)


gboolean bmp_logger_start(const gchar * filename);
void bmp_logger_stop(void);

#endif