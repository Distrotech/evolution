/* Evolution calendar - Print support
 *
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *          Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef PRINT_H
#define PRINT_H

#include "gnome-cal.h"



typedef enum {
	PRINT_VIEW_DAY,
	PRINT_VIEW_WEEK,
	PRINT_VIEW_MONTH,
	PRINT_VIEW_YEAR
} PrintView;

void print_calendar (GnomeCalendar *gcal, gboolean preview, time_t at, PrintView default_view);
void print_comp (CalComponent *comp, gboolean preview);

void print_setup (void);



#endif
