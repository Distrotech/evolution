/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 */

#ifndef __Cal_BACKEND_SYNC_H__
#define __Cal_BACKEND_SYNC_H__

#include <glib.h>
#include <pcs/cal-backend.h>
#include <pcs/evolution-calendar.h>

G_BEGIN_DECLS

#define CAL_TYPE_BACKEND_SYNC         (cal_backend_sync_get_type ())
#define CAL_BACKEND_SYNC(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CAL_TYPE_BACKEND_SYNC, CalBackendSync))
#define CAL_BACKEND_SYNC_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CAL_TYPE_BACKEND_SYNC, CalBackendSyncClass))
#define CAL_IS_BACKEND_SYNC(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CAL_TYPE_BACKEND_SYNC))
#define CAL_IS_BACKEND_SYNC_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CAL_TYPE_BACKEND_SYNC))
#define CAL_BACKEND_SYNC_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((k), CAL_TYPE_BACKEND_SYNC, CalBackendSyncClass))
typedef struct _CalBackendSync CalBackendSync;
typedef struct _CalBackendSyncClass CalBackendSyncClass;
typedef struct _CalBackendSyncPrivate CalBackendSyncPrivate;

typedef GNOME_Evolution_Calendar_CallStatus CalBackendSyncStatus;

struct _CalBackendSync {
	CalBackend parent_object;

	CalBackendSyncPrivate *priv;
};

struct _CalBackendSyncClass {
	CalBackendClass parent_class;

	/* Virtual methods */
	CalBackendSyncStatus (*is_read_only_sync)  (CalBackendSync *backend, Cal *cal, gboolean *read_only);
	CalBackendSyncStatus (*get_cal_address_sync)  (CalBackendSync *backend, Cal *cal, char **address);
	CalBackendSyncStatus (*get_alarm_email_address_sync)  (CalBackendSync *backend, Cal *cal, char **address);
	CalBackendSyncStatus (*get_ldap_attribute_sync)  (CalBackendSync *backend, Cal *cal, char **attribute);
	CalBackendSyncStatus (*get_static_capabilities_sync)  (CalBackendSync *backend, Cal *cal, char **capabilities);
	CalBackendSyncStatus (*open_sync)  (CalBackendSync *backend, Cal *cal, gboolean only_if_exists);
	CalBackendSyncStatus (*remove_sync)  (CalBackendSync *backend, Cal *cal);

	/* Padding for future expansion */
	void (*_cal_reserved0) (void);
	void (*_cal_reserved1) (void);
	void (*_cal_reserved2) (void);
	void (*_cal_reserved3) (void);
	void (*_cal_reserved4) (void);

};

typedef CalBackendSync * (*CalBackendSyncFactoryFn) (void);

GType                cal_backend_sync_get_type                (void);
CalBackendSyncStatus cal_backend_sync_is_read_only         (CalBackendSync  *backend,
							    Cal             *cal,
							    gboolean *read_only);
CalBackendSyncStatus cal_backend_sync_get_cal_address         (CalBackendSync  *backend,
							       Cal             *cal,
							       char           **address);
CalBackendSyncStatus cal_backend_sync_get_alarm_email_address (CalBackendSync  *backend,
							       Cal             *cal,
							       char           **address);
CalBackendSyncStatus cal_backend_sync_get_ldap_attribute      (CalBackendSync  *backend,
							       Cal             *cal,
							       char           **attribute);
CalBackendSyncStatus cal_backend_sync_get_static_capabilities (CalBackendSync  *backend,
							       Cal             *cal,
							       char           **capabiliites);

CalBackendSyncStatus cal_backend_sync_open (CalBackendSync  *backend, Cal *cal, gboolean only_if_exists);
CalBackendSyncStatus cal_backend_sync_remove (CalBackendSync  *backend, Cal *cal);

G_END_DECLS

#endif /* ! __CAL_BACKEND_SYNC_H__ */
