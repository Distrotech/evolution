/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 2003, Ximian, Inc.
 */

#ifdef CONFIG_H
#include <config.h>
#endif

#include "cal-backend-sync.h"

struct _CalBackendSyncPrivate {
  int mumble;
};

static GObjectClass *parent_class;

G_LOCK_DEFINE_STATIC (cal_sync_mutex);
#define	SYNC_LOCK()		G_LOCK (cal_sync_mutex)
#define	SYNC_UNLOCK()		G_UNLOCK (cal_sync_mutex)

CalBackendSyncStatus
cal_backend_sync_is_read_only  (CalBackendSync *backend, Cal *cal, gboolean *read_only)
{
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (read_only, GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->is_read_only_sync);

	return (* CAL_BACKEND_SYNC_GET_CLASS (backend)->is_read_only_sync) (backend, cal, read_only);
}

CalBackendSyncStatus
cal_backend_sync_get_cal_address  (CalBackendSync *backend, Cal *cal, char **address)
{
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (address, GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->get_cal_address_sync);

	return (* CAL_BACKEND_SYNC_GET_CLASS (backend)->get_cal_address_sync) (backend, cal, address);
}

CalBackendSyncStatus
cal_backend_sync_get_alarm_email_address  (CalBackendSync *backend, Cal *cal, char **address)
{
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (address, GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->get_alarm_email_address_sync);

	return (* CAL_BACKEND_SYNC_GET_CLASS (backend)->get_alarm_email_address_sync) (backend, cal, address);
}

CalBackendSyncStatus
cal_backend_sync_get_ldap_attribute  (CalBackendSync *backend, Cal *cal, char **attribute)
{
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (attribute, GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->get_ldap_attribute_sync);

	return (* CAL_BACKEND_SYNC_GET_CLASS (backend)->get_ldap_attribute_sync) (backend, cal, attribute);
}

CalBackendSyncStatus
cal_backend_sync_get_static_capabilities  (CalBackendSync *backend, Cal *cal, char **capabilities)
{
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (capabilities, GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->get_static_capabilities_sync);

	return (* CAL_BACKEND_SYNC_GET_CLASS (backend)->get_static_capabilities_sync) (backend, cal, capabilities);
}

CalBackendSyncStatus
cal_backend_sync_open  (CalBackendSync *backend, Cal *cal, gboolean only_if_exists)
{
	CalBackendSyncStatus status;
	
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->open_sync);

	SYNC_LOCK ();
	
	status = (* CAL_BACKEND_SYNC_GET_CLASS (backend)->open_sync) (backend, cal, only_if_exists);

	SYNC_UNLOCK ();

	return status;
}

CalBackendSyncStatus
cal_backend_sync_remove  (CalBackendSync *backend, Cal *cal)
{
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_sync);

	return (* CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_sync) (backend, cal);
}

CalBackendSyncStatus
cal_backend_sync_remove_object (CalBackendSync *backend, Cal *cal, const char *uid, CalObjModType mod)
{
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_object_sync);

	return (* CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_object_sync) (backend, cal, uid, mod);
}

CalBackendSyncStatus
cal_backend_sync_get_object_list (CalBackendSync *backend, Cal *cal, const char *sexp, GList **objects)
{
	g_return_val_if_fail (backend && CAL_IS_BACKEND_SYNC (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (objects, GNOME_Evolution_Calendar_OtherError);

	g_assert (CAL_BACKEND_SYNC_GET_CLASS (backend)->get_object_list_sync);

	return (* CAL_BACKEND_SYNC_GET_CLASS (backend)->get_object_list_sync) (backend, cal, sexp, objects);
}

static void
_cal_backend_is_read_only (CalBackend *backend, Cal *cal)
{
	CalBackendSyncStatus status;
	gboolean read_only;

	status = cal_backend_sync_is_read_only (CAL_BACKEND_SYNC (backend), cal, &read_only);

	cal_notify_read_only (cal, status, read_only);
}

static void
_cal_backend_get_cal_address (CalBackend *backend, Cal *cal)
{
	CalBackendSyncStatus status;
	char *address;

	status = cal_backend_sync_get_cal_address (CAL_BACKEND_SYNC (backend), cal, &address);

	cal_notify_cal_address (cal, status, address);

	g_free (address);
}

static void
_cal_backend_get_alarm_email_address (CalBackend *backend, Cal *cal)
{
	CalBackendSyncStatus status;
	char *address;

	status = cal_backend_sync_get_cal_address (CAL_BACKEND_SYNC (backend), cal, &address);

	cal_notify_alarm_email_address (cal, status, address);

	g_free (address);
}

static void
_cal_backend_get_ldap_attribute (CalBackend *backend, Cal *cal)
{
	CalBackendSyncStatus status;
	char *attribute;

	status = cal_backend_sync_get_cal_address (CAL_BACKEND_SYNC (backend), cal, &attribute);

	cal_notify_ldap_attribute (cal, status, attribute);

	g_free (attribute);
}

static void
_cal_backend_get_static_capabilities (CalBackend *backend, Cal *cal)
{
	CalBackendSyncStatus status;
	char *capabilities;

	status = cal_backend_sync_get_cal_address (CAL_BACKEND_SYNC (backend), cal, &capabilities);

	cal_notify_ldap_attribute (cal, status, capabilities);

	g_free (capabilities);
}

static void
_cal_backend_open (CalBackend *backend, Cal *cal, gboolean only_if_exists)
{
	CalBackendSyncStatus status;

	status = cal_backend_sync_open (CAL_BACKEND_SYNC (backend), cal, only_if_exists);

	cal_notify_open (cal, status);
}

static void
_cal_backend_remove (CalBackend *backend, Cal *cal)
{
	CalBackendSyncStatus status;

	status = cal_backend_sync_remove (CAL_BACKEND_SYNC (backend), cal);

	cal_notify_remove (cal, status);
}

static void
_cal_backend_remove_object (CalBackend *backend, Cal *cal, const char *uid, CalObjModType mod)
{
	CalBackendSyncStatus status;

	status = cal_backend_sync_remove_object (CAL_BACKEND_SYNC (backend), cal, uid, mod);

	cal_notify_object_removed (cal, status, uid);
}

static void
_cal_backend_get_object_list (CalBackend *backend, Cal *cal, const char *sexp)
{
	CalBackendSyncStatus status;
	GList *objects, *l;

	status = cal_backend_sync_get_object_list (CAL_BACKEND_SYNC (backend), cal, sexp, &objects);

	cal_notify_object_list (cal, status, objects);

	for (l = objects; l; l = l->next)
		g_free (l->data);
	g_list_free (objects);
}

static void
cal_backend_sync_init (CalBackendSync *backend)
{
	CalBackendSyncPrivate *priv;

	priv          = g_new0 (CalBackendSyncPrivate, 1);

	backend->priv = priv;
}

static void
cal_backend_sync_dispose (GObject *object)
{
	CalBackendSync *backend;

	backend = CAL_BACKEND_SYNC (object);

	if (backend->priv) {
		g_free (backend->priv);

		backend->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
cal_backend_sync_class_init (CalBackendSyncClass *klass)
{
	GObjectClass *object_class;
	CalBackendClass *backend_class = CAL_BACKEND_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class = (GObjectClass *) klass;

	backend_class->is_read_only = _cal_backend_is_read_only;
	backend_class->get_cal_address = _cal_backend_get_cal_address;
	backend_class->get_alarm_email_address = _cal_backend_get_alarm_email_address;
	backend_class->get_ldap_attribute = _cal_backend_get_ldap_attribute;
	backend_class->get_static_capabilities = _cal_backend_get_static_capabilities;
	backend_class->open = _cal_backend_open;
	backend_class->remove = _cal_backend_remove;
	backend_class->remove_object = _cal_backend_remove_object;
	backend_class->get_object_list = _cal_backend_get_object_list;

	object_class->dispose = cal_backend_sync_dispose;
}

/**
 * cal_backend_get_type:
 */
GType
cal_backend_sync_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (CalBackendSyncClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  cal_backend_sync_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (CalBackendSync),
			0,    /* n_preallocs */
			(GInstanceInitFunc) cal_backend_sync_init
		};

		type = g_type_register_static (CAL_BACKEND_TYPE, "CalBackendSync", &info, 0);
	}

	return type;
}
