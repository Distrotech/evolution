/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Calendar Conduit
 *
 * Copyright (C) 1998 Free Software Foundation
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Eskil Heyn Olsen <deity@eskil.dk> 
 *          JP Rosevear <jpr@ximian.com>
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

#ifndef __CALENDAR_CONDUIT_H__
#define __CALENDAR_CONDUIT_H__

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <gnome.h>
#include <pi-datebook.h>
#include <gpilotd/gnome-pilot-conduit.h>
#include <gpilotd/gnome-pilot-conduit-sync-abs.h>
#include <libical/src/libical/icaltime.h>
#include <cal-client/cal-client.h>
#include <e-pilot-map.h>

/* This is the local record structure for the Evolution Calendar conduit. */
typedef struct _ECalLocalRecord ECalLocalRecord;
struct _ECalLocalRecord {
	/* The stuff from gnome-pilot-conduit-standard-abs.h
	   Must be first in the structure, or instances of this
	   structure cannot be used by gnome-pilot-conduit-standard-abs.
	*/
	GnomePilotDesktopRecord local;

	/* The corresponding Comp object */
	CalComponent *comp;

        /* pilot-link appointment structure */
	struct Appointment *appt;
};

/* This is the context for all the Evolution Calendar conduit methods. */
typedef struct _ECalConduitContext ECalConduitContext;
struct _ECalConduitContext {
	ECalConduitCfg *cfg;
	GnomePilotDBInfo *dbi;
	
	struct AppointmentAppInfo ai;

	CalClient *client;

	icaltimezone *timezone;
	GList *uids;
	GList *changed;
	GHashTable *changed_hash;
	GList *locals;
	
	EPilotMap *map;
};

#endif /* __CALENDAR_CONDUIT_H__ */






