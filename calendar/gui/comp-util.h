/* Evolution calendar - Utilities for manipulating CalComponent objects
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#ifndef COMP_UTIL_H
#define COMP_UTIL_H

#include <gtk/gtkwidget.h>
#include <cal-util/cal-component.h>
#include <cal-client/cal-client.h>

void cal_comp_util_add_exdate (CalComponent *comp, time_t t, icaltimezone *zone);


/* Returns TRUE if the component uses the given timezone for both DTSTART
   and DTEND, or if the UTC offsets of the start and end times are the same
   as in the given zone. */
gboolean cal_comp_util_compare_event_timezones (CalComponent *comp,
						CalClient *client,
						icaltimezone *zone);

typedef enum {
	EMPTY_COMP_REMOVE_LOCALLY,
	EMPTY_COMP_REMOVED_FROM_SERVER,
	EMPTY_COMP_DO_NOT_REMOVE
} ConfirmDeleteEmptyCompResult;

ConfirmDeleteEmptyCompResult cal_comp_confirm_delete_empty_comp (CalComponent *comp,
								 CalClient *client,
								 GtkWidget *widget);

CalComponent *cal_comp_event_new_with_defaults (void);

#endif
