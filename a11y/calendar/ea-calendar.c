/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* Evolution Accessibility: ea-calendar.c
 *
 * Copyright (C) 2003 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Bolian Yin <bolian.yin@sun.com> Sun Microsystem Inc., 2003
 *
 */

#include "ea-factory.h"
#include "ea-calendar.h"

#include "calendar/ea-day-view.h"
#include "calendar/ea-day-view-event.h"
#include "calendar/ea-gnome-calendar.h"

EA_FACTORY (EA_TYPE_DAY_VIEW, ea_day_view, ea_day_view_new);
EA_FACTORY (EA_TYPE_GNOME_CALENDAR, ea_gnome_calendar, ea_gnome_calendar_new);

/* ea_calendar_init will register all the atk factories for calendar,
 * other init's will register specific factories for their widgets
 */
void
ea_calendar_init (void)
{
    EA_SET_FACTORY (gnome_calendar_get_type(), ea_gnome_calendar);
    EA_SET_FACTORY (e_day_view_get_type(), ea_day_view);
}

void
gnome_calendar_a11y_init (void)
{
    EA_SET_FACTORY (gnome_calendar_get_type(), ea_gnome_calendar);

}

void
e_day_view_a11y_init (void)
{
    EA_SET_FACTORY (e_day_view_get_type(), ea_day_view);
}
