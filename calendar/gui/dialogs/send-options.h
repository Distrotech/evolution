/* Evolution calendar - Main page of the Groupwise send options Dialog
 *
 * Copyright (C) 2004 Novell, Inc.
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Bangalore, MA 02111-1307, India.
 */

#include <libecal/e-cal-component.h>
#include <libecal/e-cal.h>

typedef enum {
	PRIORITY_LOW,
	PRIORITY_STANDARD,
	PRIORITY_HIGH
} ESendOptionsPriority;

typedef enum {
	RETURN_NOTIFY_NONE,
	RETURN_NOTIFY_MAIL
} ESendOptionsReturnNotify;

typedef enum {
	DELIVERED,
	DELIVERED_OPENED,
	ALL,
} TrackInfo;

typedef struct {
	ESendOptionsPriority priority;
	gboolean reply_enabled;
	gboolean reply_convinient;
	gint reply_within;
	gboolean expiration_enabled;
	gint expire_after;
	gboolean delay_enabled;
	char *delay_until;
} ESendOptionsGeneral;

typedef struct {
	gboolean tracking_enabled;
	TrackInfo track_when;
	ESendOptionsReturnNotify opened;
	ESendOptionsReturnNotify accepted;
	ESendOptionsReturnNotify declined;
	ESendOptionsReturnNotify completed;
} ESendOptionsStatusTracking;

typedef struct {
	gboolean initialized;

	icaltimezone *zone;
	ESendOptionsGeneral *general_opts;
	ESendOptionsStatusTracking *status_opts;
	
} ESendOptionsData;

gboolean send_options_run_dialog (GtkWidget *parent, ECal *ecal, ESendOptionsData *options_data);
ESendOptionsData *send_options_new (void);
void send_options_finalize (ESendOptionsData *options_data);
void send_options_fill_component (ECalComponent *comp, ESendOptionsData *options);
