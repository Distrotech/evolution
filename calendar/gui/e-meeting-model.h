/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-model.h
 *
 * Copyright (C) 2001  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
 * Author: JP Rosevear
 */

#ifndef _E_MODEL_H_
#define _E_MODEL_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <gal/e-table/e-table-scrolled.h>
#include <gal/e-table/e-table-model.h>
#include <cal-client/cal-client.h>
#include "e-meeting-attendee.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E_TYPE_MEETING_MODEL			(e_meeting_model_get_type ())
#define E_MEETING_MODEL(obj)			(GTK_CHECK_CAST ((obj), E_TYPE_MEETING_MODEL, EMeetingModel))
#define E_MEETING_MODEL_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), E_TYPE_MEETING_MODEL, EMeetingModelClass))
#define E_IS_MEETING_MODEL(obj)			(GTK_CHECK_TYPE ((obj), E_TYPE_MEETING_MODEL))
#define E_IS_MEETING_MODEL_CLASS(klass)		(GTK_CHECK_CLASS_TYPE ((obj), E_TYPE_MEETING_MODEL))


typedef struct _EMeetingModel        EMeetingModel;
typedef struct _EMeetingModelPrivate EMeetingModelPrivate;
typedef struct _EMeetingModelClass   EMeetingModelClass;

struct _EMeetingModel {
	ETableModel parent;

	EMeetingModelPrivate *priv;
};

struct _EMeetingModelClass {
	ETableModelClass parent_class;
};

typedef void	(* EMeetingModelRefreshCallback) (gpointer data);


GtkType    e_meeting_model_get_type (void);
GtkObject *e_meeting_model_new      (void);

CalClient *e_meeting_model_get_cal_client (EMeetingModel *im);
void e_meeting_model_set_cal_client (EMeetingModel *im, CalClient *client);

void e_meeting_model_add_attendee (EMeetingModel *im, EMeetingAttendee *ia);
EMeetingAttendee *e_meeting_model_add_attendee_with_defaults (EMeetingModel *im);

void e_meeting_model_remove_attendee (EMeetingModel *im, EMeetingAttendee *ia);
void e_meeting_model_remove_all_attendees (EMeetingModel *im);

EMeetingAttendee *e_meeting_model_find_attendee (EMeetingModel *im, const gchar *address, gint *row);
EMeetingAttendee *e_meeting_model_find_attendee_at_row (EMeetingModel *im, gint row);

gint e_meeting_model_count_attendees (EMeetingModel *im);
const GPtrArray *e_meeting_model_get_attendees (EMeetingModel *im);
void e_meeting_model_refresh_busy_periods (EMeetingModel *im, EMeetingModelRefreshCallback call_back, gpointer data);

ETableScrolled    *e_meeting_model_etable_from_model (EMeetingModel *im, const gchar *spec_file, const gchar *state_file);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_MEETING_MODEL_H_ */
