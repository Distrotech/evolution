/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
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

#ifndef CAL_BACKEND_H
#define CAL_BACKEND_H

#include <e-util/e-list.h>
#include <cal-util/cal-util.h>
#include <cal-util/cal-component.h>
#include "pcs/evolution-calendar.h"
#include "pcs/cal-common.h"
#include "pcs/cal.h"
#include "pcs/query.h"

G_BEGIN_DECLS



#define CAL_BACKEND_TYPE            (cal_backend_get_type ())
#define CAL_BACKEND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CAL_BACKEND_TYPE, CalBackend))
#define CAL_BACKEND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CAL_BACKEND_TYPE,		\
				     CalBackendClass))
#define IS_CAL_BACKEND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CAL_BACKEND_TYPE))
#define IS_CAL_BACKEND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CAL_BACKEND_TYPE))

typedef enum {
	CAL_BACKEND_RESULT_SUCCESS,
	CAL_BACKEND_RESULT_INVALID_OBJECT,
	CAL_BACKEND_RESULT_NOT_FOUND,
	CAL_BACKEND_RESULT_PERMISSION_DENIED
} CalBackendResult;

/* Send result values */
typedef enum {
	CAL_BACKEND_SEND_SUCCESS,
	CAL_BACKEND_SEND_INVALID_OBJECT,
	CAL_BACKEND_SEND_BUSY,
	CAL_BACKEND_SEND_PERMISSION_DENIED,
} CalBackendSendResult;

/* Result codes for ::get_alarms_in_range() */
typedef enum {
	CAL_BACKEND_GET_ALARMS_SUCCESS,
	CAL_BACKEND_GET_ALARMS_NOT_FOUND,
	CAL_BACKEND_GET_ALARMS_INVALID_RANGE
} CalBackendGetAlarmsForObjectResult;

typedef struct _CalBackendPrivate CalBackendPrivate;

struct _CalBackend {
	GObject object;

	CalBackendPrivate *priv;
};

struct _CalBackendClass {
	GObjectClass parent_class;

	/* Notification signals */
	void (* last_client_gone) (CalBackend *backend);
	void (* cal_added) (CalBackend *backend, Cal *cal);

	/* FIXME What to pass back here */
	void (* opened) (CalBackend *backend, int status);
	void (* removed) (CalBackend *backend, int status);
	void (* obj_updated) (CalBackend *backend, const char *uid);
	void (* obj_removed) (CalBackend *backend, const char *uid);

	/* Virtual methods */
	void (* is_read_only) (CalBackend *backend, Cal *cal);
	void (* get_cal_address) (CalBackend *backend, Cal *cal);
	void (* get_alarm_email_address) (CalBackend *backend, Cal *cal);
	void (* get_ldap_attribute) (CalBackend *backend, Cal *cal);
	void (* get_static_capabilities) (CalBackend *backend, Cal *cal);
	
	void (* open) (CalBackend *backend, Cal *cal, gboolean only_if_exists);
	void (* remove) (CalBackend *backend, Cal *cal);

	void (* get_object_list) (CalBackend *backend, Cal *cal, const char *sexp);

	gboolean (* is_loaded) (CalBackend *backend);

	void (* start_query) (CalBackend *backend, Query *query);

	/* Mode relate virtual methods */
	CalMode (* get_mode) (CalBackend *backend);
	void    (* set_mode) (CalBackend *backend, CalMode mode);	

	/* General object acquirement and information related virtual methods */
	char *(* get_default_object) (CalBackend *backend, CalObjType type);
	char *(* get_object) (CalBackend *backend, const char *uid, const char *rid);
	CalComponent *(* get_object_component) (CalBackend *backend, const char *uid, const char *rid);
	char *(* get_timezone_object) (CalBackend *backend, const char *tzid);

	GList *(* get_free_busy) (CalBackend *backend, GList *users, time_t start, time_t end);

	/* Change related virtual methods */
	GNOME_Evolution_Calendar_CalObjChangeSeq * (* get_changes) (
		CalBackend *backend, CalObjType type, const char *change_id);

	/* Alarm related virtual methods */
	GNOME_Evolution_Calendar_CalComponentAlarmsSeq *(* get_alarms_in_range) (
		CalBackend *backend, time_t start, time_t end);
	GNOME_Evolution_Calendar_CalComponentAlarms *(* get_alarms_for_object) (
		CalBackend *backend, const char *uid,
		time_t start, time_t end, gboolean *object_found);
	CalBackendResult (* discard_alarm) (CalBackend *backend, const char *uid, const char *auid);

	/* Object manipulation virtual methods */
	CalBackendResult (* update_objects) (CalBackend *backend, const char *calobj, CalObjModType mod);
	CalBackendResult (* remove_object) (CalBackend *backend, const char *uid, CalObjModType mod);

	CalBackendSendResult (* send_object) (CalBackend *backend, const char *calobj, char **new_calobj,
					      GNOME_Evolution_Calendar_UserList **user_list,
					      char error_msg[256]);

	/* Timezone related virtual methods */
	icaltimezone *(* get_timezone) (CalBackend *backend, const char *tzid);
	icaltimezone *(* get_default_timezone) (CalBackend *backend);
	gboolean (* set_default_timezone) (CalBackend *backend, const char *tzid);
};

GType cal_backend_get_type (void);

const char *cal_backend_get_uri (CalBackend *backend);
icalcomponent_kind cal_backend_get_kind (CalBackend *backend);

void cal_backend_is_read_only (CalBackend *backend, Cal *cal);
void cal_backend_get_cal_address (CalBackend *backend, Cal *cal);
void cal_backend_get_alarm_email_address (CalBackend *backend, Cal *cal);
void cal_backend_get_ldap_attribute (CalBackend *backend, Cal *cal);
void cal_backend_get_static_capabilities (CalBackend *backend, Cal *cal);

void  cal_backend_open (CalBackend *backend, Cal *cal, gboolean only_if_exists);
void cal_backend_remove (CalBackend *backend, Cal *cal);

void cal_backend_get_object_list (CalBackend *backend, Cal *cal, const char *sexp);

gboolean cal_backend_is_loaded (CalBackend *backend);

void cal_backend_start_query (CalBackend *backend, Query *query);
void cal_backend_add_query (CalBackend *backend, Query *query);
EList *cal_backend_get_queries (CalBackend *backend);

CalMode cal_backend_get_mode (CalBackend *backend);
void cal_backend_set_mode (CalBackend *backend, CalMode mode);

char *cal_backend_get_default_object (CalBackend *backend, CalObjType type);

char *cal_backend_get_object (CalBackend *backend, const char *uid, const char *rid);

CalComponent *cal_backend_get_object_component (CalBackend *backend, const char *uid, const char *rid);

gboolean cal_backend_set_default_timezone (CalBackend *backend, const char *tzid);

char *cal_backend_get_timezone_object (CalBackend *backend, const char *tzid);

CalObjType cal_backend_get_type_by_uid (CalBackend *backend, const char *uid);

GList *cal_backend_get_free_busy (CalBackend *backend, GList *users, time_t start, time_t end);

GNOME_Evolution_Calendar_CalObjChangeSeq * cal_backend_get_changes (
	CalBackend *backend, CalObjType type, const char *change_id);

GNOME_Evolution_Calendar_CalComponentAlarmsSeq *cal_backend_get_alarms_in_range (
	CalBackend *backend, time_t start, time_t end, gboolean *valid_range);

GNOME_Evolution_Calendar_CalComponentAlarms *cal_backend_get_alarms_for_object (
	CalBackend *backend, const char *uid,
	time_t start, time_t end,
	CalBackendGetAlarmsForObjectResult *result);

CalBackendResult cal_backend_discard_alarm (CalBackend *backend, const char *uid, const char *auid);


CalBackendResult cal_backend_update_objects (CalBackend *backend, const char *calobj, CalObjModType mod);

CalBackendResult cal_backend_remove_object (CalBackend *backend, const char *uid, CalObjModType mod);

CalBackendSendResult cal_backend_send_object (CalBackend *backend, const char *calobj, char **new_calobj,
					      GNOME_Evolution_Calendar_UserList **user_list, 
					      char error_msg[256]);

icaltimezone* cal_backend_get_timezone (CalBackend *backend, const char *tzid);
icaltimezone* cal_backend_get_default_timezone (CalBackend *backend);

void cal_backend_add_cal (CalBackend *backend, Cal *cal);

void cal_backend_last_client_gone (CalBackend *backend);

/* FIXME what to do about status */
void cal_backend_opened (CalBackend *backend, int status);
void cal_backend_removed (CalBackend *backend, int status);

void cal_backend_obj_updated (CalBackend *backend, const char *uid);
void cal_backend_obj_removed (CalBackend *backend, const char *uid);

void cal_backend_notify_mode      (CalBackend *backend,
				   GNOME_Evolution_Calendar_Listener_SetModeStatus status, 
				   GNOME_Evolution_Calendar_CalMode mode);
void cal_backend_notify_error     (CalBackend *backend, const char *message);
void cal_backend_ref_categories   (CalBackend *backend, GSList *categories);
void cal_backend_unref_categories (CalBackend *backend, GSList *categories);



G_END_DECLS

#endif
