/* Evolution calendar utilities and types
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

#ifndef CAL_UTIL_H
#define CAL_UTIL_H

#include <libgnome/gnome-defs.h>
#include <ical.h>
#include <time.h>
#include <glib.h>
#include <cal-util/cal-component.h>
#include <cal-util/cal-recur.h>

BEGIN_GNOME_DECLS



/* Instance of a calendar object.  This can be an actual occurrence, a
 * recurrence, or an alarm trigger of a `real' calendar object.
 */
typedef struct {
	char *uid;			/* UID of the object */
	time_t start;			/* Start time of instance */
	time_t end;			/* End time of instance */
} CalObjInstance;

void cal_obj_instance_list_free (GList *list);

/* Used for multiple UID queries */
typedef enum {
	CALOBJ_TYPE_EVENT   = 1 << 0,
	CALOBJ_TYPE_TODO    = 1 << 1,
	CALOBJ_TYPE_JOURNAL = 1 << 2,
	CALOBJ_TYPE_ANY     = 0x07
} CalObjType;

/* Used for mode stuff */
typedef enum {
	CAL_MODE_INVALID = -1,
	CAL_MODE_LOCAL   = 1 << 0,
	CAL_MODE_REMOTE  = 1 << 1,
	CAL_MODE_ANY     = 0x07
} CalMode;

void cal_obj_uid_list_free (GList *list);

icalcomponent *cal_util_new_top_level (void);

CalComponentAlarms *cal_util_generate_alarms_for_comp (CalComponent *comp,
						       time_t start,
						       time_t end,
						       CalRecurResolveTimezoneFn resolve_tzid,
						       gpointer user_data,
						       icaltimezone *default_timezone);
int cal_util_generate_alarms_for_list (GList *comps,
				       time_t start,
				       time_t end,
				       GSList **comp_alarms,
				       CalRecurResolveTimezoneFn resolve_tzid,
				       gpointer user_data,
				       icaltimezone *default_timezone);

icaltimezone *cal_util_resolve_tzid (const char *tzid, gpointer data);

char *cal_util_priority_to_string (int priority);
int cal_util_priority_from_string (const char *string);

char *cal_util_expand_uri (char *uri, gboolean tasks);

void cal_util_add_timezones_from_component (icalcomponent *vcal_comp,
					    CalComponent *comp);

END_GNOME_DECLS

#endif

