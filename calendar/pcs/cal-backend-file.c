/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 2000-2003 Ximian, Inc.
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

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs.h>
#include "e-util/e-xml-hash-utils.h"
#include "cal-util/cal-recur.h"
#include "cal-util/cal-util.h"
#include "cal-backend-file-events.h"
#include "cal-backend-util.h"
#include "cal-backend-object-sexp.h"



/* Private part of the CalBackendFile structure */
struct _CalBackendFilePrivate {
	/* URI where the calendar data is stored */
	char *uri;

	/* Filename in the dir */
	char *file_name;	

	/* Toplevel VCALENDAR component */
	icalcomponent *icalcomp;

	/* All the objects in the calendar, hashed by UID.  The
	 * hash key *is* the uid returned by cal_component_get_uid(); it is not
	 * copied, so don't free it when you remove an object from the hash
	 * table.
	 */
	GHashTable *comp_uid_hash;

	GList *comp;
	
	/* Config database handle for free/busy organizer information */
	EConfigListener *config_listener;
	
	/* Idle handler for saving the calendar when it is dirty */
	guint idle_id;

	/* The calendar's default timezone, used for resolving DATE and
	   floating DATE-TIME values. */
	icaltimezone *default_zone;

	/* The list of live queries */
	GList *queries;
};



static void cal_backend_file_class_init (CalBackendFileClass *class);
static void cal_backend_file_init (CalBackendFile *cbfile, CalBackendFileClass *class);
static void cal_backend_file_dispose (GObject *object);
static void cal_backend_file_finalize (GObject *object);

static CalBackendSyncStatus cal_backend_file_is_read_only (CalBackendSync *backend, Cal *cal, gboolean *read_only);
static CalBackendSyncStatus cal_backend_file_get_cal_address (CalBackendSync *backend, Cal *cal, char **address);
static CalBackendSyncStatus cal_backend_file_get_alarm_email_address (CalBackendSync *backend, Cal *cal, char **address);
static CalBackendSyncStatus cal_backend_file_get_ldap_attribute (CalBackendSync *backend, Cal *cal, char **attribute);
static CalBackendSyncStatus cal_backend_file_get_static_capabilities (CalBackendSync *backend, Cal *cal, char **capabilities);

static CalBackendSyncStatus cal_backend_file_open (CalBackendSync *backend, Cal *cal, gboolean only_if_exists);
static CalBackendSyncStatus cal_backend_file_remove (CalBackendSync *backend, Cal *cal);

static CalBackendSyncStatus cal_backend_file_remove_object (CalBackendSync *backend, Cal *cal, const char *uid, CalObjModType mod);

static CalBackendSyncStatus cal_backend_file_get_object_list (CalBackendSync *backend, Cal *cal, const char *sexp, GList **objects);

static gboolean cal_backend_file_is_loaded (CalBackend *backend);
static void cal_backend_file_start_query (CalBackend *backend, Query *query);

static CalMode cal_backend_file_get_mode (CalBackend *backend);
static void cal_backend_file_set_mode (CalBackend *backend, CalMode mode);

static char *cal_backend_file_get_default_object (CalBackend *backend, CalObjType type);
static CalComponent *cal_backend_file_get_object_component (CalBackend *backend, const char *uid, const char *rid);
static char *cal_backend_file_get_timezone_object (CalBackend *backend, const char *tzid);
static GList *cal_backend_file_get_free_busy (CalBackend *backend, GList *users, time_t start, time_t end);
static GNOME_Evolution_Calendar_CalObjChangeSeq *cal_backend_file_get_changes (
	CalBackend *backend, CalObjType type, const char *change_id);

static GNOME_Evolution_Calendar_CalComponentAlarmsSeq *cal_backend_file_get_alarms_in_range (
	CalBackend *backend, time_t start, time_t end);

static GNOME_Evolution_Calendar_CalComponentAlarms *cal_backend_file_get_alarms_for_object (
	CalBackend *backend, const char *uid,
	time_t start, time_t end, gboolean *object_found);

static CalBackendResult cal_backend_file_discard_alarm (CalBackend *backend,
							const char *uid,
							const char *auid);

static CalBackendResult cal_backend_file_update_objects (CalBackend *backend,
							 const char *calobj,
							 CalObjModType mod);

static CalBackendSendResult cal_backend_file_send_object (CalBackend *backend, 
							  const char *calobj, gchar **new_calobj,
							  GNOME_Evolution_Calendar_UserList **user_list,
							  char error_msg[256]);

static icaltimezone* cal_backend_file_get_timezone (CalBackend *backend, const char *tzid);
static icaltimezone* cal_backend_file_get_default_timezone (CalBackend *backend);
static gboolean cal_backend_file_set_default_timezone (CalBackend *backend,
						       const char *tzid);

static CalBackendSyncClass *parent_class;



/**
 * cal_backend_file_get_type:
 * @void: 
 * 
 * Registers the #CalBackendFile class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #CalBackendFile class.
 **/
GType
cal_backend_file_get_type (void)
{
	static GType cal_backend_file_type = 0;

	if (!cal_backend_file_type) {
		static GTypeInfo info = {
                        sizeof (CalBackendFileClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) cal_backend_file_class_init,
                        NULL, NULL,
                        sizeof (CalBackendFile),
                        0,
                        (GInstanceInitFunc) cal_backend_file_init
                };
		cal_backend_file_type = g_type_register_static (CAL_TYPE_BACKEND_SYNC,
								"CalBackendFile", &info, 0);
	}

	return cal_backend_file_type;
}

/* Class initialization function for the file backend */
static void
cal_backend_file_class_init (CalBackendFileClass *class)
{
	GObjectClass *object_class;
	CalBackendClass *backend_class;
	CalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (CalBackendClass *) class;
	sync_class = (CalBackendSyncClass *) class;

	parent_class = (CalBackendSyncClass *) g_type_class_peek_parent (class);

	object_class->dispose = cal_backend_file_dispose;
	object_class->finalize = cal_backend_file_finalize;

	sync_class->is_read_only_sync = cal_backend_file_is_read_only;
	sync_class->get_cal_address_sync = cal_backend_file_get_cal_address;
 	sync_class->get_alarm_email_address_sync = cal_backend_file_get_alarm_email_address;
 	sync_class->get_ldap_attribute_sync = cal_backend_file_get_ldap_attribute;
 	sync_class->get_static_capabilities_sync = cal_backend_file_get_static_capabilities;
	sync_class->open_sync = cal_backend_file_open;
	sync_class->remove_sync = cal_backend_file_remove;
	sync_class->remove_object_sync = cal_backend_file_remove_object;
	sync_class->get_object_list_sync = cal_backend_file_get_object_list;

	backend_class->is_loaded = cal_backend_file_is_loaded;
	backend_class->start_query = cal_backend_file_start_query;
	backend_class->get_mode = cal_backend_file_get_mode;
	backend_class->set_mode = cal_backend_file_set_mode;	
 	backend_class->get_default_object = cal_backend_file_get_default_object;
	backend_class->get_object_component = cal_backend_file_get_object_component;
	backend_class->get_timezone_object = cal_backend_file_get_timezone_object;
	backend_class->get_free_busy = cal_backend_file_get_free_busy;
	backend_class->get_changes = cal_backend_file_get_changes;
	backend_class->get_alarms_in_range = cal_backend_file_get_alarms_in_range;
	backend_class->get_alarms_for_object = cal_backend_file_get_alarms_for_object;
	backend_class->discard_alarm = cal_backend_file_discard_alarm;
	backend_class->update_objects = cal_backend_file_update_objects;
	backend_class->send_object = cal_backend_file_send_object;

	backend_class->get_timezone = cal_backend_file_get_timezone;
	backend_class->get_default_timezone = cal_backend_file_get_default_timezone;
	backend_class->set_default_timezone = cal_backend_file_set_default_timezone;
}

/* Object initialization function for the file backend */
static void
cal_backend_file_init (CalBackendFile *cbfile, CalBackendFileClass *class)
{
	CalBackendFilePrivate *priv;

	priv = g_new0 (CalBackendFilePrivate, 1);
	cbfile->priv = priv;

	priv->uri = NULL;
	priv->file_name = g_strdup ("calendar.ics");
	priv->icalcomp = NULL;
	priv->comp_uid_hash = NULL;
	priv->comp = NULL;

	/* The timezone defaults to UTC. */
	priv->default_zone = icaltimezone_get_utc_timezone ();

	priv->config_listener = e_config_listener_new ();
}

void
cal_backend_file_set_file_name (CalBackendFile *cbfile, const char *file_name)
{
	CalBackendFilePrivate *priv;
	
	g_return_if_fail (cbfile != NULL);
	g_return_if_fail (IS_CAL_BACKEND_FILE (cbfile));
	g_return_if_fail (file_name != NULL);

	priv = cbfile->priv;
	
	if (priv->file_name)
		g_free (priv->file_name);
	
	priv->file_name = g_strdup (file_name);
}

const char *
cal_backend_file_get_file_name (CalBackendFile *cbfile)
{
	CalBackendFilePrivate *priv;

	g_return_val_if_fail (cbfile != NULL, NULL);
	g_return_val_if_fail (IS_CAL_BACKEND_FILE (cbfile), NULL);

	priv = cbfile->priv;	

	return priv->file_name;
}

/* g_hash_table_foreach() callback to destroy a CalComponent */
static void
free_cal_component (gpointer key, gpointer value, gpointer data)
{
	CalComponent *comp;

	g_free (key);

	comp = CAL_COMPONENT (value);
	g_object_unref (comp);
}

/* g_hash_table_foreach() callback to destroy a CalComponent */
static void
free_object (gpointer key, gpointer value, gpointer data)
{
	CalComponent *comp = value;

	g_object_unref (comp);
}

/* Saves the calendar data */
static void
save (CalBackendFile *cbfile)
{
	CalBackendFilePrivate *priv;
	GnomeVFSURI *uri, *backup_uri;
	GnomeVFSHandle *handle = NULL;
	GnomeVFSResult result = GNOME_VFS_ERROR_BAD_FILE;
	GnomeVFSFileSize out;
	gchar *tmp, *backup_uristr;
	char *buf;
	
	priv = cbfile->priv;
	g_assert (priv->uri != NULL);
	g_assert (priv->icalcomp != NULL);

	uri = gnome_vfs_uri_new (priv->uri);
	if (!uri)
		goto error_malformed_uri;

	/* save calendar to backup file */
	tmp = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
	if (!tmp) {
		gnome_vfs_uri_unref (uri);
		goto error_malformed_uri;
	}
		
	backup_uristr = g_strconcat (tmp, "~", NULL);
	backup_uri = gnome_vfs_uri_new (backup_uristr);

	g_free (tmp);
	g_free (backup_uristr);

	if (!backup_uri) {
		gnome_vfs_uri_unref (uri);
		goto error_malformed_uri;
	}
	
	result = gnome_vfs_create_uri (&handle, backup_uri,
                                       GNOME_VFS_OPEN_WRITE,
                                       FALSE, 0666);
	if (result != GNOME_VFS_OK) {
		gnome_vfs_uri_unref (uri);
		gnome_vfs_uri_unref (backup_uri);
		goto error;
	}

	buf = icalcomponent_as_ical_string (priv->icalcomp);
	result = gnome_vfs_write (handle, buf, strlen (buf) * sizeof (char), &out);
	gnome_vfs_close (handle);
	if (result != GNOME_VFS_OK) {
		gnome_vfs_uri_unref (uri);
		gnome_vfs_uri_unref (backup_uri);
		goto error;
	}

	/* now copy the temporary file to the real file */
	result = gnome_vfs_move_uri (backup_uri, uri, TRUE);

	gnome_vfs_uri_unref (uri);
	gnome_vfs_uri_unref (backup_uri);
	if (result != GNOME_VFS_OK)
		goto error;

	return;

 error_malformed_uri:
	cal_backend_notify_error (CAL_BACKEND (cbfile),
				  _("Can't save calendar data: Malformed URI."));
	return;

 error:
	cal_backend_notify_error (CAL_BACKEND (cbfile), gnome_vfs_result_to_string (result));
	return;
}

/* Dispose handler for the file backend */
static void
cal_backend_file_dispose (GObject *object)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;

	cbfile = CAL_BACKEND_FILE (object);
	priv = cbfile->priv;

	/* Save if necessary */

	if (priv->idle_id != 0) {
		save (cbfile);
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}

	if (priv->comp_uid_hash) {
		g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) free_object, NULL);
		g_hash_table_destroy (priv->comp_uid_hash);
		priv->comp_uid_hash = NULL;
	}

	g_list_free (priv->comp);
	priv->comp = NULL;

	if (priv->icalcomp) {
		icalcomponent_free (priv->icalcomp);
		priv->icalcomp = NULL;
	}

	if (priv->config_listener) {
		g_object_unref (priv->config_listener);
		priv->config_listener = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
cal_backend_file_finalize (GObject *object)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_CAL_BACKEND_FILE (object));

	cbfile = CAL_BACKEND_FILE (object);
	priv = cbfile->priv;

	/* Clean up */

	if (priv->uri) {
	        g_free (priv->uri);
		priv->uri = NULL;
	}

	g_free (priv);
	cbfile->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}



/* Looks up a component by its UID on the backend's component hash table */
static CalComponent *
lookup_component (CalBackendFile *cbfile, const char *uid)
{
	CalBackendFilePrivate *priv;

	priv = cbfile->priv;

	return g_hash_table_lookup (priv->comp_uid_hash, uid);
}



/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static CalBackendSyncStatus
cal_backend_file_is_read_only (CalBackendSync *backend, Cal *cal, gboolean *read_only)
{
	/* we just return FALSE, since all calendars are read-write */
	*read_only = FALSE;
	
	return GNOME_Evolution_Calendar_Success;
}

/* Get_email_address handler for the file backend */
static CalBackendSyncStatus
cal_backend_file_get_cal_address (CalBackendSync *backend, Cal *cal, char **address)
{
	/* A file backend has no particular email address associated
	 * with it (although that would be a useful feature some day).
	 */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static CalBackendSyncStatus
cal_backend_file_get_ldap_attribute (CalBackendSync *backend, Cal *cal, char **attribute)
{
	*attribute = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static CalBackendSyncStatus
cal_backend_file_get_alarm_email_address (CalBackendSync *backend, Cal *cal, char **address)
{
 	/* A file backend has no particular email address associated
 	 * with it (although that would be a useful feature some day).
 	 */
	*address = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static CalBackendSyncStatus
cal_backend_file_get_static_capabilities (CalBackendSync *backend, Cal *cal, char **capabilities)
{
	*capabilities = CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS;
	
	return GNOME_Evolution_Calendar_Success;
}

/* function to resolve timezones */
static icaltimezone *
resolve_tzid (const char *tzid, gpointer user_data)
{
	icalcomponent *vcalendar_comp = user_data;

        if (!tzid || !tzid[0])
                return NULL;
        else if (!strcmp (tzid, "UTC"))
                return icaltimezone_get_utc_timezone ();

	return icalcomponent_get_timezone (vcalendar_comp, tzid);
}

/* Idle handler; we save the calendar since it is dirty */
static gboolean
save_idle (gpointer data)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;

	cbfile = CAL_BACKEND_FILE (data);
	priv = cbfile->priv;

	g_assert (priv->icalcomp != NULL);

	save (cbfile);

	priv->idle_id = 0;
	return FALSE;
}

/* Marks the file backend as dirty and queues a save operation */
static void
mark_dirty (CalBackendFile *cbfile)
{
	CalBackendFilePrivate *priv;

	priv = cbfile->priv;

	if (priv->idle_id != 0)
		return;

	priv->idle_id = g_idle_add (save_idle, cbfile);
}

/* Checks if the specified component has a duplicated UID and if so changes it */
static void
check_dup_uid (CalBackendFile *cbfile, CalComponent *comp)
{
	CalBackendFilePrivate *priv;
	CalComponent *existing_comp;
	const char *uid;
	char *new_uid;

	priv = cbfile->priv;

	cal_component_get_uid (comp, &uid);

	existing_comp = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (!existing_comp)
		return; /* Everything is fine */

	g_message ("check_dup_uid(): Got object with duplicated UID `%s', changing it...", uid);

	new_uid = cal_component_gen_uid ();
	cal_component_set_uid (comp, new_uid);
	g_free (new_uid);

	/* FIXME: I think we need to reset the SEQUENCE property and reset the
	 * CREATED/DTSTAMP/LAST-MODIFIED.
	 */

	mark_dirty (cbfile);
}

static char *
get_rid_string (CalComponent *comp)
{
        CalComponentRange range;
        struct icaltimetype tt;
                                                                                   
        cal_component_get_recurid (comp, &range);
        if (!range.datetime.value)
                return "0";
        tt = *range.datetime.value;
        cal_component_free_range (&range);
                                                                                   
        return icaltime_is_valid_time (tt) && !icaltime_is_null_time (tt) ?
                icaltime_as_ical_string (tt) : "0";
}

/* Tries to add an icalcomponent to the file backend.  We only store the objects
 * of the types we support; all others just remain in the toplevel component so
 * that we don't lose them.
 */
static void
add_component (CalBackendFile *cbfile, CalComponent *comp, gboolean add_to_toplevel)
{
	CalBackendFilePrivate *priv;
	const char *uid;
	GSList *categories;

	priv = cbfile->priv;

	/* Ensure that the UID is unique; some broken implementations spit
	 * components with duplicated UIDs.
	 */
	check_dup_uid (cbfile, comp);
	cal_component_get_uid (comp, &uid);

	g_hash_table_insert (priv->comp_uid_hash, uid, comp);

	priv->comp = g_list_prepend (priv->comp, comp);

	/* Put the object in the toplevel component if required */

	if (add_to_toplevel) {
		icalcomponent *icalcomp;

		icalcomp = cal_component_get_icalcomponent (comp);
		g_assert (icalcomp != NULL);

		icalcomponent_add_component (priv->icalcomp, icalcomp);
	}

	/* Update the set of categories */
	cal_component_get_categories_list (comp, &categories);
	cal_backend_ref_categories (CAL_BACKEND (cbfile), categories);
	cal_component_free_categories_list (categories);
}

/* Removes a component from the backend's hash and lists.  Does not perform
 * notification on the clients.  Also removes the component from the toplevel
 * icalcomponent.
 */
static void
remove_component (CalBackendFile *cbfile, CalComponent *comp)
{
	CalBackendFilePrivate *priv;
	icalcomponent *icalcomp;
	const char *uid;
	GList *l;
	GSList *categories;

	priv = cbfile->priv;

	/* Remove the icalcomp from the toplevel */

	icalcomp = cal_component_get_icalcomponent (comp);
	g_assert (icalcomp != NULL);

	icalcomponent_remove_component (priv->icalcomp, icalcomp);

	/* Remove it from our mapping */

	cal_component_get_uid (comp, &uid);
	g_hash_table_remove (priv->comp_uid_hash, uid);

	l = g_list_find (priv->comp, comp);
	g_assert (l != NULL);
	priv->comp = g_list_delete_link (priv->comp, l);

	/* Update the set of categories */
	cal_component_get_categories_list (comp, &categories);
	cal_backend_unref_categories (CAL_BACKEND (cbfile), categories);
	cal_component_free_categories_list (categories);

	g_object_unref (comp);
}

/* Scans the toplevel VCALENDAR component and stores the objects it finds */
static void
scan_vcalendar (CalBackendFile *cbfile)
{
	CalBackendFilePrivate *priv;
	icalcompiter iter;

	priv = cbfile->priv;
	g_assert (priv->icalcomp != NULL);
	g_assert (priv->comp_uid_hash != NULL);

	for (iter = icalcomponent_begin_component (priv->icalcomp, ICAL_ANY_COMPONENT);
	     icalcompiter_deref (&iter) != NULL;
	     icalcompiter_next (&iter)) {
		icalcomponent *icalcomp;
		icalcomponent_kind kind;
		CalComponent *comp;

		icalcomp = icalcompiter_deref (&iter);
		
		kind = icalcomponent_isa (icalcomp);

		if (!(kind == ICAL_VEVENT_COMPONENT
		      || kind == ICAL_VTODO_COMPONENT
		      || kind == ICAL_VJOURNAL_COMPONENT))
			continue;

		comp = cal_component_new ();

		if (!cal_component_set_icalcomponent (comp, icalcomp))
			continue;

		add_component (cbfile, comp, FALSE);
	}
}

/* Parses an open iCalendar file and loads it into the backend */
static CalBackendSyncStatus
open_cal (CalBackendFile *cbfile, const char *uristr)
{
	CalBackendFilePrivate *priv;
	icalcomponent *icalcomp;

	priv = cbfile->priv;

	icalcomp = cal_util_parse_ics_file (uristr);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_OtherError;

	/* FIXME: should we try to demangle XROOT components and
	 * individual components as well?
	 */

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);

		return GNOME_Evolution_Calendar_OtherError;
	}

	priv->icalcomp = icalcomp;

	priv->comp_uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
	scan_vcalendar (cbfile);

	priv->uri = g_strdup (uristr);

	return GNOME_Evolution_Calendar_Success;
}

static CalBackendSyncStatus
create_cal (CalBackendFile *cbfile, const char *uristr)
{
	CalBackendFilePrivate *priv;

	priv = cbfile->priv;

	/* Create the new calendar information */
	priv->icalcomp = cal_util_new_top_level ();

	/* Create our internal data */
	priv->comp_uid_hash = g_hash_table_new (g_str_hash, g_str_equal);

	priv->uri = g_strdup (uristr);

	mark_dirty (cbfile);

	return GNOME_Evolution_Calendar_Success;
}

static char *
get_uri_string (CalBackend *backend)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	const char *master_uri;
	char *full_uri, *str_uri;
	GnomeVFSURI *uri;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;
	
	master_uri = cal_backend_get_uri (backend);
	g_message (G_STRLOC ": Trying to open %s", master_uri);
	
	/* FIXME Check the error conditions a little more elegantly here */
	if (g_strrstr ("tasks.ics", master_uri) || g_strrstr ("calendar.ics", master_uri)) {
		g_warning (G_STRLOC ": Existing file name %s", master_uri);

		return NULL;
	}
	
	full_uri = g_strdup_printf ("%s%s%s", master_uri, G_DIR_SEPARATOR_S, priv->file_name);
	uri = gnome_vfs_uri_new (full_uri);
	g_free (full_uri);
	
	if (!uri)
		return NULL;

	str_uri = gnome_vfs_uri_to_string (uri,
					   (GNOME_VFS_URI_HIDE_USER_NAME
					    | GNOME_VFS_URI_HIDE_PASSWORD
					    | GNOME_VFS_URI_HIDE_HOST_NAME
					    | GNOME_VFS_URI_HIDE_HOST_PORT
					    | GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD));
	gnome_vfs_uri_unref (uri);

	if (!str_uri || !strlen (str_uri)) {
		g_free (str_uri);

		return NULL;
	}	

	return str_uri;
}

/* Open handler for the file backend */
static CalBackendSyncStatus
cal_backend_file_open (CalBackendSync *backend, Cal *cal, gboolean only_if_exists)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	char *str_uri;
	CalBackendSyncStatus status;
	
	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	/* Claim a succesful open if we are already open */
	if (priv->uri && priv->comp_uid_hash)
		return GNOME_Evolution_Calendar_Success;
	
	str_uri = get_uri_string (CAL_BACKEND (backend));
	if (!str_uri)
		return GNOME_Evolution_Calendar_OtherError;
	
	if (access (str_uri, R_OK) == 0)
		status = open_cal (cbfile, str_uri);
	else {
		if (only_if_exists)
			status = GNOME_Evolution_Calendar_NoSuchCal;
		else
			status = create_cal (cbfile, str_uri);
	}

	g_free (str_uri);

	return status;
}

static CalBackendSyncStatus
cal_backend_file_remove (CalBackendSync *backend, Cal *cal)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	char *str_uri;
	
	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	str_uri = get_uri_string (CAL_BACKEND (backend));
	if (!str_uri)
		return GNOME_Evolution_Calendar_OtherError;

	if (access (str_uri, W_OK) != 0) {
		g_free (str_uri);

		return GNOME_Evolution_Calendar_PermissionDenied;
	}

	/* FIXME Remove backup file and whole directory too? */
	if (unlink (str_uri) != 0) {
		g_free (str_uri);

		return GNOME_Evolution_Calendar_OtherError;
	}
	
	g_free (str_uri);
	
	return GNOME_Evolution_Calendar_Success;
}

/* is_loaded handler for the file backend */
static gboolean
cal_backend_file_is_loaded (CalBackend *backend)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	return (priv->icalcomp != NULL);
}

/* is_remote handler for the file backend */
static CalMode
cal_backend_file_get_mode (CalBackend *backend)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	return CAL_MODE_LOCAL;	
}

/* Set_mode handler for the file backend */
static void
cal_backend_file_set_mode (CalBackend *backend, CalMode mode)
{
	cal_backend_notify_mode (backend,
				 GNOME_Evolution_Calendar_Listener_MODE_NOT_SUPPORTED,
				 GNOME_Evolution_Calendar_MODE_LOCAL);
	
}

static char *
cal_backend_file_get_default_object (CalBackend *backend, CalObjType type)
{
 	CalBackendFile *cbfile;
 	CalBackendFilePrivate *priv;
 	CalComponent *comp;
 	char *calobj;
 	
 	cbfile = CAL_BACKEND_FILE (backend);
 	priv = cbfile->priv;
 
 	comp = cal_component_new ();
 	
 	switch (type) {
 	case CALOBJ_TYPE_EVENT:
 		cal_component_set_new_vtype (comp, CAL_COMPONENT_EVENT);
 		break;
 	case CALOBJ_TYPE_TODO:
 		cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);
 		break;
 	case CALOBJ_TYPE_JOURNAL:
 		cal_component_set_new_vtype (comp, CAL_COMPONENT_JOURNAL);
 		break;
 	default:
 		g_object_unref (comp);
 		return NULL;
 	}
 	
 	calobj = cal_component_get_as_string (comp);
 	g_object_unref (comp);
 
 	return calobj;
}

/* Get_object_component handler for the file backend */
static CalComponent *
cal_backend_file_get_object_component (CalBackend *backend, const char *uid, const char *rid)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	CalComponent *comp;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (uid != NULL, NULL);

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);
	g_assert (priv->comp_uid_hash != NULL);

	comp = lookup_component (cbfile, uid);
	if (!comp)
		return NULL;

	if (rid && *rid) {
		
	} else
		return comp;

	return NULL;
}

/* Get_timezone_object handler for the file backend */
static char *
cal_backend_file_get_timezone_object (CalBackend *backend, const char *tzid)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	icaltimezone *zone;
	icalcomponent *icalcomp;
	char *ical_string;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (tzid != NULL, NULL);

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);
	g_assert (priv->comp_uid_hash != NULL);

	zone = icalcomponent_get_timezone (priv->icalcomp, tzid);
	if (!zone) {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			return NULL;
	}

	icalcomp = icaltimezone_get_component (zone);
	if (!icalcomp)
		return NULL;

	ical_string = icalcomponent_as_ical_string (icalcomp);
	/* We dup the string; libical owns that memory. */
	if (ical_string)
		return g_strdup (ical_string);
	else
		return NULL;
}

typedef struct {
	GList *obj_list;
	gboolean search_needed;
	const char *query;
	CalBackendObjectSExp *obj_sexp;
	CalBackend *backend;
	icaltimezone *default_zone;
} MatchObjectData;

static void
match_recurrence_sexp (CalComponent *comp,
		       time_t instance_start,
		       time_t instance_end,
		       gpointer data)
{
	MatchObjectData *match_data = data;

	if ((!match_data->search_needed) ||
	    (cal_backend_object_sexp_match_comp (match_data->obj_sexp, comp, match_data->backend))) {
		match_data->obj_list = g_list_append (match_data->obj_list,
						      cal_component_get_as_string (comp));
	}

	g_object_unref (comp);
}

static void
match_object_sexp (gpointer key, gpointer value, gpointer data)
{
	CalComponent *comp = value;
	MatchObjectData *match_data = data;

	if (cal_component_has_recurrences (comp)) {
		/* FIXME: try to get time range from query */
		cal_recur_generate_instances (comp, -1, -1,
					      match_recurrence_sexp,
					      match_data,
					      resolve_tzid,
					      cal_component_get_icalcomponent (comp),
					      match_data->default_zone);
	} else {
		if ((!match_data->search_needed) ||
		    (cal_backend_object_sexp_match_comp (match_data->obj_sexp, comp, match_data->backend))) {
			match_data->obj_list = g_list_append (match_data->obj_list,
							      cal_component_get_as_string (comp));
		}
	}
}

/* Get_objects_in_range handler for the file backend */
static CalBackendSyncStatus
cal_backend_file_get_object_list (CalBackendSync *backend, Cal *cal, const char *sexp, GList **objects)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	MatchObjectData match_data;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_message (G_STRLOC ": Getting object list (%s)", sexp);

	match_data.search_needed = TRUE;
	match_data.query = sexp;
	match_data.obj_list = NULL;
	match_data.backend = CAL_BACKEND (backend);
	match_data.default_zone = priv->default_zone;

	if (!strcmp (sexp, "#t"))
		match_data.search_needed = FALSE;

	match_data.obj_sexp = cal_backend_object_sexp_new (sexp);
	if (!match_data.obj_sexp)
		return GNOME_Evolution_Calendar_InvalidQuery;

	g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) match_object_sexp, &match_data);

	*objects = match_data.obj_list;
	
	return GNOME_Evolution_Calendar_Success;	
}

/* get_query handler for the file backend */
static void
cal_backend_file_start_query (CalBackend *backend, Query *query)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	MatchObjectData match_data;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	/* try to match all currently existing objects */
	match_data.search_needed = TRUE;
	match_data.query = query_get_text (query);
	match_data.obj_list = NULL;
	match_data.backend = backend;
	match_data.default_zone = priv->default_zone;

	if (!strcmp (match_data.query, "#t"))
		match_data.search_needed = FALSE;

	match_data.obj_sexp = query_get_object_sexp (query);
	if (!match_data.obj_sexp) {
		query_notify_query_done (query, GNOME_Evolution_Calendar_InvalidQuery);
		return;
	}

	g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) match_object_sexp, &match_data);

	/* notify listeners of all objects */
	if (match_data.obj_list) {
		query_notify_objects_added (query, (const GList *) match_data.obj_list);

		/* free memory */
		g_list_foreach (match_data.obj_list, (GFunc) g_free, NULL);
		g_list_free (match_data.obj_list);
	}

	query_notify_query_done (query, GNOME_Evolution_Calendar_Success);
}

static gboolean
free_busy_instance (CalComponent *comp,
		    time_t        instance_start,
		    time_t        instance_end,
		    gpointer      data)
{
	icalcomponent *vfb = data;
	icalproperty *prop;
	icalparameter *param;
	struct icalperiodtype ipt;
	icaltimezone *utc_zone;

	utc_zone = icaltimezone_get_utc_timezone ();

	ipt.start = icaltime_from_timet_with_zone (instance_start, FALSE, utc_zone);
	ipt.end = icaltime_from_timet_with_zone (instance_end, FALSE, utc_zone);
	ipt.duration = icaldurationtype_null_duration ();
	
        /* add busy information to the vfb component */
	prop = icalproperty_new (ICAL_FREEBUSY_PROPERTY);
	icalproperty_set_freebusy (prop, ipt);
	
	param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSY);
	icalproperty_add_parameter (prop, param);
	
	icalcomponent_add_property (vfb, prop);

	return TRUE;
}

static icalcomponent *
create_user_free_busy (CalBackendFile *cbfile, const char *address, const char *cn,
		       time_t start, time_t end)
{	
	CalBackendFilePrivate *priv;
	GList *l;
	icalcomponent *vfb;
	icaltimezone *utc_zone;
	CalBackendObjectSExp *obj_sexp;
	char *query;
	
	priv = cbfile->priv;

	/* create the (unique) VFREEBUSY object that we'll return */
	vfb = icalcomponent_new_vfreebusy ();
	if (address != NULL) {
		icalproperty *prop;
		icalparameter *param;
		
		prop = icalproperty_new_organizer (address);
		if (prop != NULL && cn != NULL) {
			param = icalparameter_new_cn (cn);
			icalproperty_add_parameter (prop, param);			
		}
		if (prop != NULL)
			icalcomponent_add_property (vfb, prop);		
	}
	utc_zone = icaltimezone_get_utc_timezone ();
	icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
	icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

	/* add all objects in the given interval */
	query = g_strdup_printf ("occur-in-time-range? %lu %lu", start, end);
	obj_sexp = cal_backend_object_sexp_new (query);
	g_free (query);

	if (!obj_sexp)
		return vfb;

	for (l = priv->comp; l; l = l->next) {
		CalComponent *comp = l->data;
		icalcomponent *icalcomp, *vcalendar_comp;
		icalproperty *prop;
		
		icalcomp = cal_component_get_icalcomponent (comp);
		if (!icalcomp)
			continue;

		/* If the event is TRANSPARENT, skip it. */
		prop = icalcomponent_get_first_property (icalcomp,
							 ICAL_TRANSP_PROPERTY);
		if (prop) {
			const char *transp_val = icalproperty_get_transp (prop);
			if (transp_val && !strcasecmp (transp_val, "TRANSPARENT"))
				continue;
		}
	
		if (!cal_backend_object_sexp_match_comp (obj_sexp, l->data, CAL_BACKEND (cbfile)))
			continue;
		
		vcalendar_comp = icalcomponent_get_parent (icalcomp);
		cal_recur_generate_instances (comp, start, end,
					      free_busy_instance,
					      vfb,
					      resolve_tzid,
					      vcalendar_comp,
					      priv->default_zone);
	}

	return vfb;	
}

/* Get_free_busy handler for the file backend */
static GList *
cal_backend_file_get_free_busy (CalBackend *backend, GList *users, time_t start, time_t end)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	gchar *address, *name;	
	icalcomponent *vfb;
	char *calobj;
	GList *obj_list = NULL;
	GList *l;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);
	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	if (users == NULL) {
		if (cal_backend_mail_account_get_default (priv->config_listener, &address, &name)) {
			vfb = create_user_free_busy (cbfile, address, name, start, end);
			calobj = icalcomponent_as_ical_string (vfb);
			obj_list = g_list_append (obj_list, g_strdup (calobj));
			icalcomponent_free (vfb);
			g_free (address);
			g_free (name);
		}		
	} else {
		for (l = users; l != NULL; l = l->next ) {
			address = l->data;			
			if (cal_backend_mail_account_is_valid (priv->config_listener, address, &name)) {
				vfb = create_user_free_busy (cbfile, address, name, start, end);
				calobj = icalcomponent_as_ical_string (vfb);
				obj_list = g_list_append (obj_list, g_strdup (calobj));
				icalcomponent_free (vfb);
				g_free (name);
			}
		}		
	}

	return obj_list;
}

typedef struct 
{
	CalBackend *backend;
	CalObjType type;	
	GList *changes;
	GList *change_ids;
} CalBackendFileComputeChangesData;

static void
cal_backend_file_compute_changes_foreach_key (const char *key, gpointer data)
{
	CalBackendFileComputeChangesData *be_data = data;
	char *calobj = cal_backend_get_object (be_data->backend, key, NULL);
	
	if (calobj == NULL) {
		CalComponent *comp;
		GNOME_Evolution_Calendar_CalObjChange *coc;
		char *calobj;

		comp = cal_component_new ();
		if (be_data->type == GNOME_Evolution_Calendar_TYPE_TODO)
			cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);
		else
			cal_component_set_new_vtype (comp, CAL_COMPONENT_EVENT);

		cal_component_set_uid (comp, key);
		calobj = cal_component_get_as_string (comp);

		coc = GNOME_Evolution_Calendar_CalObjChange__alloc ();
		coc->calobj =  CORBA_string_dup (calobj);
		coc->type = GNOME_Evolution_Calendar_DELETED;
		be_data->changes = g_list_prepend (be_data->changes, coc);
		be_data->change_ids = g_list_prepend (be_data->change_ids, g_strdup (key));

		g_free (calobj);
		g_object_unref (comp);
 	}
}

static GNOME_Evolution_Calendar_CalObjChangeSeq *
cal_backend_file_compute_changes (CalBackend *backend, CalObjType type, const char *change_id)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	char    *filename;
	EXmlHash *ehash;
	CalBackendFileComputeChangesData be_data;
	GNOME_Evolution_Calendar_CalObjChangeSeq *seq;
	GList *changes = NULL, *change_ids = NULL;
	GList *i, *j;
	int n;	

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	/* Find the changed ids - FIX ME, path should not be hard coded */
	if (type == GNOME_Evolution_Calendar_TYPE_TODO)
		filename = g_strdup_printf ("%s/evolution/local/Tasks/%s.db", g_get_home_dir (), change_id);
	else 
		filename = g_strdup_printf ("%s/evolution/local/Calendar/%s.db", g_get_home_dir (), change_id);
	ehash = e_xmlhash_new (filename);
	g_free (filename);
	
	/* Calculate adds and modifies */
	for (i = priv->comp; i != NULL; i = i->next) {
		GNOME_Evolution_Calendar_CalObjChange *coc;
		const char *uid;
		char *calobj;

		cal_component_get_uid (i->data, &uid);
		calobj = cal_component_get_as_string (i->data);

		g_assert (calobj != NULL);

		/* check what type of change has occurred, if any */
		switch (e_xmlhash_compare (ehash, uid, calobj)) {
		case E_XMLHASH_STATUS_SAME:
			break;
		case E_XMLHASH_STATUS_NOT_FOUND:
			coc = GNOME_Evolution_Calendar_CalObjChange__alloc ();
			coc->calobj =  CORBA_string_dup (calobj);
			coc->type = GNOME_Evolution_Calendar_ADDED;
			changes = g_list_prepend (changes, coc);
			change_ids = g_list_prepend (change_ids, g_strdup (uid));
			break;
		case E_XMLHASH_STATUS_DIFFERENT:
			coc = GNOME_Evolution_Calendar_CalObjChange__alloc ();
			coc->calobj =  CORBA_string_dup (calobj);
			coc->type = GNOME_Evolution_Calendar_MODIFIED;
			changes = g_list_prepend (changes, coc);
			change_ids = g_list_prepend (change_ids, g_strdup (uid));
			break;
		}

		g_free (calobj);
	}

	/* Calculate deletions */
	be_data.backend = backend;
	be_data.type = type;	
	be_data.changes = changes;
	be_data.change_ids = change_ids;
   	e_xmlhash_foreach_key (ehash, (EXmlHashFunc)cal_backend_file_compute_changes_foreach_key, &be_data);
	changes = be_data.changes;
	change_ids = be_data.change_ids;
	
	/* Build the sequence and update the hash */
	n = g_list_length (changes);

	seq = GNOME_Evolution_Calendar_CalObjChangeSeq__alloc ();
	seq->_length = n;
	seq->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_CalObjChange_allocbuf (n);
	CORBA_sequence_set_release (seq, TRUE);

	for (i = changes, j = change_ids, n = 0; i != NULL; i = i->next, j = j->next, n++) {
		GNOME_Evolution_Calendar_CalObjChange *coc = i->data;
		GNOME_Evolution_Calendar_CalObjChange *seq_coc;
		char *uid = j->data;

		/* sequence building */
		seq_coc = &seq->_buffer[n];
		seq_coc->calobj = CORBA_string_dup (coc->calobj);
		seq_coc->type = coc->type;

		/* hash updating */
		if (coc->type == GNOME_Evolution_Calendar_ADDED 
		    || coc->type == GNOME_Evolution_Calendar_MODIFIED) {
			e_xmlhash_add (ehash, uid, coc->calobj);
		} else {
			e_xmlhash_remove (ehash, uid);
		}		

		CORBA_free (coc);
		g_free (uid);
	}	
	e_xmlhash_write (ehash);
  	e_xmlhash_destroy (ehash);

	g_list_free (change_ids);
	g_list_free (changes);
	
	return seq;
}

/* Get_changes handler for the file backend */
static GNOME_Evolution_Calendar_CalObjChangeSeq *
cal_backend_file_get_changes (CalBackend *backend, CalObjType type, const char *change_id)
{
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (IS_CAL_BACKEND (backend), NULL);

	return cal_backend_file_compute_changes (backend, type, change_id);
}

/* Get_alarms_in_range handler for the file backend */
static GNOME_Evolution_Calendar_CalComponentAlarmsSeq *
cal_backend_file_get_alarms_in_range (CalBackend *backend,
				      time_t start, time_t end)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	int n_comp_alarms;
	GSList *comp_alarms;
	GSList *l;
	int i;
	CalAlarmAction omit[] = {-1};
	
	GNOME_Evolution_Calendar_CalComponentAlarmsSeq *seq;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);

	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	/* Per RFC 2445, only VEVENTs and VTODOs can have alarms */
	comp_alarms = NULL;
	n_comp_alarms = cal_util_generate_alarms_for_list (priv->comp, start, end, omit,
							    &comp_alarms, resolve_tzid,
							    priv->icalcomp,
							    priv->default_zone);

	seq = GNOME_Evolution_Calendar_CalComponentAlarmsSeq__alloc ();
	CORBA_sequence_set_release (seq, TRUE);
	seq->_length = n_comp_alarms;
	seq->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_CalComponentAlarms_allocbuf (
		n_comp_alarms);

	for (l = comp_alarms, i = 0; l; l = l->next, i++) {
		CalComponentAlarms *alarms;
		char *comp_str;

		alarms = l->data;

		comp_str = cal_component_get_as_string (alarms->comp);
		seq->_buffer[i].calobj = CORBA_string_dup (comp_str);
		g_free (comp_str);

		cal_backend_util_fill_alarm_instances_seq (&seq->_buffer[i].alarms, alarms->alarms);

		cal_component_alarms_free (alarms);
	}

	g_slist_free (comp_alarms);

	return seq;
}

/* Get_alarms_for_object handler for the file backend */
static GNOME_Evolution_Calendar_CalComponentAlarms *
cal_backend_file_get_alarms_for_object (CalBackend *backend, const char *uid,
					time_t start, time_t end,
					gboolean *object_found)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	CalComponent *comp;
	char *comp_str;
	GNOME_Evolution_Calendar_CalComponentAlarms *corba_alarms;
	CalComponentAlarms *alarms;
	CalAlarmAction omit[] = {-1};

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);

	g_return_val_if_fail (uid != NULL, NULL);
	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);
	g_return_val_if_fail (object_found != NULL, NULL);

	comp = lookup_component (cbfile, uid);
	if (!comp) {
		*object_found = FALSE;
		return NULL;
	}

	*object_found = TRUE;

	comp_str = cal_component_get_as_string (comp);
	corba_alarms = GNOME_Evolution_Calendar_CalComponentAlarms__alloc ();

	corba_alarms->calobj = CORBA_string_dup (comp_str);
	g_free (comp_str);

	alarms = cal_util_generate_alarms_for_comp (comp, start, end, omit, resolve_tzid, priv->icalcomp, priv->default_zone);
	if (alarms) {
		cal_backend_util_fill_alarm_instances_seq (&corba_alarms->alarms, alarms->alarms);
		cal_component_alarms_free (alarms);
	} else
		cal_backend_util_fill_alarm_instances_seq (&corba_alarms->alarms, NULL);

	return corba_alarms;
}

/* Discard_alarm handler for the file backend */
static CalBackendResult
cal_backend_file_discard_alarm (CalBackend *backend, const char *uid, const char *auid)
{
	/* we just do nothing with the alarm */
	return CAL_BACKEND_RESULT_SUCCESS;
}

/* Creates a CalComponent for the given icalcomponent and adds it to our
   cache. Note that the icalcomponent is not added to the toplevel
   icalcomponent here. That needs to be done elsewhere. It returns the uid
   of the added component, or NULL if it failed. */
static const char*
cal_backend_file_update_object (CalBackendFile *cbfile,
				icalcomponent *icalcomp)
{
	CalComponent *old_comp;
	CalComponent *comp;
	const char *comp_uid;
	struct icaltimetype last_modified;

	/* Create a CalComponent wrapper for the icalcomponent. */
	comp = cal_component_new ();
	if (!cal_component_set_icalcomponent (comp, icalcomp)) {
		g_object_unref (comp);
		return NULL;
	}

	/* Get the UID, and check it isn't empty. */
	cal_component_get_uid (comp, &comp_uid);
	if (!comp_uid || !comp_uid[0]) {
		g_object_unref (comp);
		return NULL;
	}

	/* Set the LAST-MODIFIED time on the component */
	last_modified = icaltime_from_timet (time (NULL), 0);
	cal_component_set_last_modified (comp, &last_modified);

	if (cal_component_is_instance (comp)) {
		/* FIXME */
	} else {
		/* Remove any old version of the component. */
		old_comp = lookup_component (cbfile, comp_uid);
		if (old_comp)
			remove_component (cbfile, old_comp);

		/* Now add the component to our local cache, but we pass FALSE as
		   the last argument, since the libical component is assumed to have
		   been added already. */
		add_component (cbfile, comp, FALSE);
	}

	return comp_uid;
}

static const char*
cal_backend_file_cancel_object (CalBackendFile *cbfile,
				icalcomponent *icalcomp)
{
	CalComponent *old_comp;
	icalproperty *uid;
	const char *comp_uid;

	/* Get the UID, and check it isn't empty. */
	uid = icalcomponent_get_first_property (icalcomp, ICAL_UID_PROPERTY);
	if (!uid)
		return NULL;
	comp_uid = icalproperty_get_uid (uid);
	if (!comp_uid || !comp_uid[0])
		return NULL;

	/* Find the old version of the component. */
	old_comp = lookup_component (cbfile, comp_uid);
	if (!old_comp)
		return NULL;

	/* And remove it */
	remove_component (cbfile, old_comp);
	return comp_uid;
}

/* Update_objects handler for the file backend. */
static CalBackendResult
cal_backend_file_update_objects (CalBackend *backend, const char *calobj, CalObjModType mod)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	icalcomponent *toplevel_comp, *icalcomp = NULL;
	icalcomponent_kind kind;
	icalproperty_method method;
	icalcomponent *subcomp;
	CalBackendResult retval = CAL_BACKEND_RESULT_SUCCESS;
	GList *updated_uids = NULL, *removed_uids = NULL, *elem;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, CAL_BACKEND_RESULT_INVALID_OBJECT);

	g_return_val_if_fail (calobj != NULL, CAL_BACKEND_RESULT_INVALID_OBJECT);

	/* Pull the component from the string and ensure that it is sane */

	toplevel_comp = icalparser_parse_string ((char *) calobj);

	if (!toplevel_comp)
		return CAL_BACKEND_RESULT_INVALID_OBJECT;

	kind = icalcomponent_isa (toplevel_comp);

	if (kind == ICAL_VEVENT_COMPONENT
	    || kind == ICAL_VTODO_COMPONENT
	    || kind == ICAL_VJOURNAL_COMPONENT) {
		/* Create a temporary toplevel component and put the VEVENT
		   or VTODO in it, to simplify the code below. */
		icalcomp = toplevel_comp;
		toplevel_comp = cal_util_new_top_level ();
		icalcomponent_add_component (toplevel_comp, icalcomp);
	} else if (kind != ICAL_VCALENDAR_COMPONENT) {
		/* We don't support this type of component */
		icalcomponent_free (toplevel_comp);
		return CAL_BACKEND_RESULT_INVALID_OBJECT;
	}

	method = icalcomponent_get_method (toplevel_comp);

	/* Step throught the VEVENT/VTODOs being added, create CalComponents
	   for them, and add them to our cache. */
	subcomp = icalcomponent_get_first_component (toplevel_comp,
						     ICAL_ANY_COMPONENT);
	while (subcomp) {
		/* We ignore anything except VEVENT, VTODO and VJOURNAL
		   components. */
		icalcomponent_kind child_kind = icalcomponent_isa (subcomp);
		if (child_kind == ICAL_VEVENT_COMPONENT
		    || child_kind == ICAL_VTODO_COMPONENT
		    || child_kind == ICAL_VJOURNAL_COMPONENT) {
			const char *comp_uid;

			if (method == ICAL_METHOD_CANCEL) {
				comp_uid = cal_backend_file_cancel_object (cbfile, subcomp);
				if (comp_uid) {
					removed_uids = g_list_prepend (removed_uids,
								       g_strdup (comp_uid));
				} else
					retval = CAL_BACKEND_RESULT_NOT_FOUND;
			} else {
				comp_uid = cal_backend_file_update_object (cbfile, subcomp);
				if (comp_uid) {
					updated_uids = g_list_prepend (updated_uids,
								       g_strdup (comp_uid));
				} else
					retval = CAL_BACKEND_RESULT_INVALID_OBJECT;
			}
		}
		subcomp = icalcomponent_get_next_component (toplevel_comp,
							    ICAL_ANY_COMPONENT);
	}

	/* Merge the iCalendar components with our existing VCALENDAR,
	   resolving any conflicting TZIDs. */
	icalcomponent_merge_component (priv->icalcomp, toplevel_comp);

	mark_dirty (cbfile);

	/* Now emit notification signals for all of the added components.
	   We do this after adding them all to make sure the calendar is in a
	   stable state before emitting signals. */
	for (elem = updated_uids; elem; elem = elem->next) {
		char *comp_uid = elem->data;
		cal_backend_obj_updated (backend, comp_uid);
		g_free (comp_uid);
	}
	g_list_free (updated_uids);

	for (elem = removed_uids; elem; elem = elem->next) {
		char *comp_uid = elem->data;
	}
	g_list_free (removed_uids);

	return retval;
}

/* Remove_object handler for the file backend */
static CalBackendSyncStatus
cal_backend_file_remove_object (CalBackendSync *backend, Cal *cal, const char *uid, CalObjModType mod)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	CalComponent *comp;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, CAL_BACKEND_RESULT_INVALID_OBJECT);

	g_return_val_if_fail (uid != NULL, CAL_BACKEND_RESULT_NOT_FOUND);

	comp = lookup_component (cbfile, uid);
	if (!comp)
		return GNOME_Evolution_Calendar_ObjectNotFound;
	
	remove_component (cbfile, comp);
	
	mark_dirty (cbfile);
		
	return GNOME_Evolution_Calendar_Success;
}

static CalBackendSendResult
cal_backend_file_send_object (CalBackend *backend, const char *calobj, char **new_calobj,
			      GNOME_Evolution_Calendar_UserList **user_list, char error_msg[256])
{
	*new_calobj = g_strdup (calobj);
	
	*user_list = GNOME_Evolution_Calendar_UserList__alloc ();
	(*user_list)->_length = 0;

	return CAL_BACKEND_SEND_SUCCESS;
}

static icaltimezone*
cal_backend_file_get_timezone (CalBackend *backend, const char *tzid)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	icaltimezone *zone;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);

	if (!strcmp (tzid, "UTC"))
	        zone = icaltimezone_get_utc_timezone ();
	else {
		zone = icalcomponent_get_timezone (priv->icalcomp, tzid);
		if (!zone)
			zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	}

	return zone;
}


static icaltimezone*
cal_backend_file_get_default_timezone (CalBackend *backend)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);

	return priv->default_zone;
}


static gboolean
cal_backend_file_set_default_timezone (CalBackend *backend,
				       const char *tzid)
{
	CalBackendFile *cbfile;
	CalBackendFilePrivate *priv;
	icaltimezone *zone;

	cbfile = CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, FALSE);

	/* Look up the VTIMEZONE in our icalcomponent. */
	zone = icalcomponent_get_timezone (priv->icalcomp, tzid);
	if (!zone)
		return FALSE;

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	return TRUE;
}

