/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* 
 * Author : 
 *  Damon Chaplin <damon@ximian.com>
 *
 * Copyright 2000, Ximian, Inc.
 * Copyright 2000, Ximian, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#ifndef _E_CALENDAR_TABLE_H_
#define _E_CALENDAR_TABLE_H_

#include <gtk/gtktable.h>
#include <gal/e-table/e-table-scrolled.h>
#include <widgets/misc/e-cell-date-edit.h>
#include "calendar-model.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * ECalendarTable - displays the iCalendar objects in a table (an ETable).
 * Used for calendar events and tasks.
 */


#define E_CALENDAR_TABLE(obj)          GTK_CHECK_CAST (obj, e_calendar_table_get_type (), ECalendarTable)
#define E_CALENDAR_TABLE_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, e_calendar_table_get_type (), ECalendarTableClass)
#define E_IS_CALENDAR_TABLE(obj)       GTK_CHECK_TYPE (obj, e_calendar_table_get_type ())


typedef struct _ECalendarTable       ECalendarTable;
typedef struct _ECalendarTableClass  ECalendarTableClass;


struct _ECalendarTable
{
	GtkTable table;

	/* The model that we use */
	CalendarModel *model;

	GtkWidget *etable;

	/* The ECell used to view & edit dates. */
	ECellDateEdit *dates_cell;

	/* The invisible widget used for cut/copy/paste */
	GtkWidget *invisible;
	gchar *clipboard_selection;
	icalcomponent *tmp_vcal;
};

struct _ECalendarTableClass
{
	GtkTableClass parent_class;
};


GtkType	   e_calendar_table_get_type		(void);
GtkWidget* e_calendar_table_new			(void);

CalendarModel *e_calendar_table_get_model	(ECalendarTable *cal_table);

ETable *e_calendar_table_get_table (ECalendarTable *cal_table);

void e_calendar_table_complete_selected (ECalendarTable *cal_table);
void e_calendar_table_delete_selected (ECalendarTable *cal_table);

/* Clipboard related functions */
void       e_calendar_table_cut_clipboard       (ECalendarTable *cal_table);
void       e_calendar_table_copy_clipboard      (ECalendarTable *cal_table);
void       e_calendar_table_paste_clipboard     (ECalendarTable *cal_table);

/* These load and save the state of the table (headers shown etc.) to/from
   the given file. */
void	   e_calendar_table_load_state		(ECalendarTable *cal_table,
						 gchar		*filename);
void	   e_calendar_table_save_state		(ECalendarTable *cal_table,
						 gchar		*filename);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_CALENDAR_TABLE_H_ */
