/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>    
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

#include "cal-backend-util.h"

void
cal_backend_util_fill_alarm_instances_seq (GNOME_Evolution_Calendar_CalAlarmInstanceSeq *seq,
					   GSList *alarms)
{
	int n_alarms;
	GSList *l;
	int i;

	g_return_if_fail (seq != NULL);

	n_alarms = g_slist_length (alarms);

	CORBA_sequence_set_release (seq, TRUE);
	seq->_length = n_alarms;
	seq->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_CalAlarmInstance_allocbuf (n_alarms);

	for (l = alarms, i = 0; l; l = l->next, i++) {
		CalAlarmInstance *instance;
		GNOME_Evolution_Calendar_CalAlarmInstance *corba_instance;

		instance = l->data;
		corba_instance = seq->_buffer + i;

		corba_instance->auid = CORBA_string_dup (instance->auid);
		corba_instance->trigger = (long) instance->trigger;
		corba_instance->occur_start = (long) instance->occur_start;
		corba_instance->occur_end = (long) instance->occur_end;
	}
}
