/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* 
 * Author : 
 *  Damon Chaplin <damon@ximian.com>
 *
 * Copyright 2001, Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#ifndef _E_DAY_VIEW_LAYOUT_H_
#define _E_DAY_VIEW_LAYOUT_H_

#include "e-day-view.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* I've split these functions away from EDayView so we can use them for
   printing. */

void e_day_view_layout_long_events	(GArray	   *events,
					 gint	    days_shown,
					 time_t	   *day_starts,
					 gint	   *rows_in_top_display);


void e_day_view_layout_day_events	(GArray	   *events,
					 gint	    rows,
					 gint	    mins_per_row,
					 gint8	   *cols_per_row);

gboolean   e_day_view_find_long_event_days	(EDayViewEvent	*event,
						 gint		 days_shown,
						 time_t		*day_starts,
						 gint		*start_day,
						 gint		*end_day);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_DAY_VIEW_LAYOUT_H_ */
