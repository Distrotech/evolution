/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - ToDo Conduit
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

#include <config.h>

#include <liboaf/liboaf.h>
#include <bonobo.h>
#include <bonobo-conf/bonobo-config-database.h>
#include <cal-client/cal-client-types.h>
#include <cal-client/cal-client.h>
#include <cal-util/timeutil.h>
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-file.h>
#include <pi-dlp.h>
#include <libical/src/libical/icaltypes.h>
#include <e-pilot-util.h>

#define TODO_CONFIG_LOAD 1
#define TODO_CONFIG_DESTROY 1
#include <todo-conduit-config.h>
#undef TODO_CONFIG_LOAD
#undef TODO_CONFIG_DESTROY

#include <todo-conduit.h>

static void free_local (EToDoLocalRecord *local);
GnomePilotConduit * conduit_get_gpilot_conduit (guint32);
void conduit_destroy_gpilot_conduit (GnomePilotConduit*);

#define CONDUIT_VERSION "0.1.4"
#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "etodoconduit"

#define DEBUG_CALCONDUIT 1
/* #undef DEBUG_CALCONDUIT */

#ifdef DEBUG_CALCONDUIT
#define LOG(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, e)
#else
#define LOG(e...)
#endif 

#define WARN(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, e)
#define INFO(e...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, e)

/* Debug routines */
static char *
print_local (EToDoLocalRecord *local)
{
	static char buff[ 4096 ];

	if (local == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	if (local->todo && local->todo->description) {
		g_snprintf (buff, 4096, "[%d %ld %d %d '%s' '%s']",
			    local->todo->indefinite,
			    mktime (& local->todo->due),
			    local->todo->priority,
			    local->todo->complete,
			    local->todo->description ?
			    local->todo->description : "",
			    local->todo->note ?
			    local->todo->note : "");
		return buff;
	}

	return "";
}

static char *print_remote (GnomePilotRecord *remote)
{
	static char buff[ 4096 ];
	struct ToDo todo;

	if (remote == NULL) {
		sprintf (buff, "[NULL]");
		return buff;
	}

	memset (&todo, 0, sizeof (struct ToDo));
	unpack_ToDo (&todo, remote->record, remote->length);

	g_snprintf (buff, 4096, "[%d %ld %d %d '%s' '%s']",
		    todo.indefinite,
		    mktime (&todo.due),
		    todo.priority,
		    todo.complete,
		    todo.description ?
		    todo.description : "",
		    todo.note ?
		    todo.note : "");

	free_ToDo (&todo);
	
	return buff;
}

/* Context Routines */
static EToDoConduitContext *
e_todo_context_new (guint32 pilot_id) 
{
	EToDoConduitContext *ctxt = g_new0 (EToDoConduitContext, 1);

	todoconduit_load_configuration (&ctxt->cfg, pilot_id);

	ctxt->client = NULL;
	ctxt->uids = NULL;
	ctxt->changed_hash = NULL;
	ctxt->changed = NULL;
	ctxt->locals = NULL;
	ctxt->map = NULL;

	return ctxt;
}

static gboolean
e_todo_context_foreach_change (gpointer key, gpointer value, gpointer data) 
{
	g_free (key);
	
	return TRUE;
}

static void
e_todo_context_destroy (EToDoConduitContext *ctxt)
{
	GList *l;
	
	g_return_if_fail (ctxt != NULL);

	if (ctxt->cfg != NULL)
		todoconduit_destroy_configuration (&ctxt->cfg);

	if (ctxt->client != NULL)
		gtk_object_unref (GTK_OBJECT (ctxt->client));

	if (ctxt->uids != NULL)
		cal_obj_uid_list_free (ctxt->uids);

	if (ctxt->changed_hash != NULL) {
		g_hash_table_foreach_remove (ctxt->changed_hash, e_todo_context_foreach_change, NULL);
		g_hash_table_destroy (ctxt->changed_hash);
	}

	if (ctxt->locals != NULL) {
		for (l = ctxt->locals; l != NULL; l = l->next)
			free_local (l->data);
		g_list_free (ctxt->locals);
	}
	
	if (ctxt->changed != NULL)
		cal_client_change_list_free (ctxt->changed);
	
	if (ctxt->map != NULL)
		e_pilot_map_destroy (ctxt->map);

	g_free (ctxt);
}

/* Calendar Server routines */
static void
start_calendar_server_cb (CalClient *cal_client,
			  CalClientOpenStatus status,
			  gpointer data)
{
	gboolean *success = data;

	if (status == CAL_CLIENT_OPEN_SUCCESS) {
		*success = TRUE;
	} else {
		*success = FALSE;
		WARN ("Failed to open calendar!\n");
	}
	
	gtk_main_quit (); /* end the sub event loop */
}

static int
start_calendar_server (EToDoConduitContext *ctxt)
{
	char *calendar_file;
	gboolean success = FALSE;
	
	g_return_val_if_fail (ctxt != NULL, -2);

	ctxt->client = cal_client_new ();

	/* FIX ME */
	calendar_file = g_concat_dir_and_file (g_get_home_dir (),
					       "evolution/local/"
					       "Tasks/tasks.ics");

	gtk_signal_connect (GTK_OBJECT (ctxt->client), "cal_opened",
			    start_calendar_server_cb, &success);

	if (!cal_client_open_calendar (ctxt->client, calendar_file, FALSE))
		return -1;

	/* run a sub event loop to turn cal-client's async load
	   notification into a synchronous call */
	gtk_main ();
	g_free (calendar_file);
	
	if (success)
		return 0;

	return -1;
}

/* Utility routines */
static icaltimezone *
get_timezone (CalClient *client, const char *tzid) 
{
	icaltimezone *timezone = NULL;

	timezone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (timezone == NULL)
		 cal_client_get_timezone (client, tzid, &timezone);
	
	return timezone;
}

static icaltimezone *
get_default_timezone (void)
{
	Bonobo_ConfigDatabase db;
	icaltimezone *timezone = NULL;
	char *location;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	
	db = bonobo_get_object ("wombat:", "Bonobo/ConfigDatabase", &ev);
	
	if (BONOBO_EX (&ev) || db == CORBA_OBJECT_NIL) {
		CORBA_exception_free (&ev);
		return NULL;
 	}

	CORBA_exception_free (&ev);

	location = bonobo_config_get_string (db, "/Calendar/Display/Timezone", NULL);
	if (location == NULL)
		goto cleanup;
	
	timezone = icaltimezone_get_builtin_timezone (location);
	g_free (location);

 cleanup:
	bonobo_object_release_unref (db, NULL);

	return timezone;	
}

static char *
map_name (EToDoConduitContext *ctxt) 
{
	char *filename;
	
	filename = g_strdup_printf ("%s/evolution/local/Tasks/pilot-map-todo-%d.xml", g_get_home_dir (), ctxt->cfg->pilot_id);

	return filename;
}

static gboolean
is_empty_time (struct tm time) 
{
	if (time.tm_sec || time.tm_min || time.tm_hour 
	    || time.tm_mday || time.tm_mon || time.tm_year) 
		return FALSE;
	
	return TRUE;
}

static GList *
next_changed_item (EToDoConduitContext *ctxt, GList *changes) 
{
	CalClientChange *ccc;
	GList *l;
	
	for (l = changes; l != NULL; l = l->next) {
		const char *uid;

		ccc = l->data;
		
		cal_component_get_uid (ccc->comp, &uid);
		if (g_hash_table_lookup (ctxt->changed_hash, uid))
			return l;
	}
	
	return NULL;
}

static void
compute_status (EToDoConduitContext *ctxt, EToDoLocalRecord *local, const char *uid)
{
	CalClientChange *ccc;

	local->local.archived = FALSE;
	local->local.secret = FALSE;

	ccc = g_hash_table_lookup (ctxt->changed_hash, uid);
	
	if (ccc == NULL) {
		local->local.attr = GnomePilotRecordNothing;
		return;
	}
	
	switch (ccc->type) {
	case CAL_CLIENT_CHANGE_ADDED:
		local->local.attr = GnomePilotRecordNew;
		break;	
	case CAL_CLIENT_CHANGE_MODIFIED:
		local->local.attr = GnomePilotRecordModified;
		break;
	case CAL_CLIENT_CHANGE_DELETED:
		local->local.attr = GnomePilotRecordDeleted;
		break;
	}
}

static void
free_local (EToDoLocalRecord *local) 
{
	gtk_object_unref (GTK_OBJECT (local->comp));
	free_ToDo (local->todo);
	g_free (local->todo);	
	g_free (local);
}

static GnomePilotRecord
local_record_to_pilot_record (EToDoLocalRecord *local,
			      EToDoConduitContext *ctxt)
{
	GnomePilotRecord p;
	static char record[0xffff];

	g_assert (local->comp != NULL);
	g_assert (local->todo != NULL );
	
	LOG ("local_record_to_pilot_record\n");

	p.ID = local->local.ID;
	p.category = local->local.category;
	p.attr = local->local.attr;
	p.archived = local->local.archived;
	p.secret = local->local.secret;

	/* Generate pilot record structure */
	p.record = record;
	p.length = pack_ToDo (local->todo, p.record, 0xffff);

	return p;	
}

/*
 * converts a CalComponent object to a EToDoLocalRecord
 */
static void
local_record_from_comp (EToDoLocalRecord *local, CalComponent *comp, EToDoConduitContext *ctxt) 
{
	const char *uid;
	int *priority;
	icalproperty_status status;
	CalComponentText summary;
	GSList *d_list = NULL;
	CalComponentText *description;
	CalComponentDateTime due;
	CalComponentClassification classif;
	icaltimezone *default_tz = get_default_timezone ();
	
	LOG ("local_record_from_comp\n");

	g_return_if_fail (local != NULL);
	g_return_if_fail (comp != NULL);

	local->comp = comp;
	gtk_object_ref (GTK_OBJECT (comp));

	cal_component_get_uid (local->comp, &uid);
	local->local.ID = e_pilot_map_lookup_pid (ctxt->map, uid, TRUE);

	compute_status (ctxt, local, uid);

	local->todo = g_new0 (struct ToDo,1);

	/* Don't overwrite the category */
	if (local->local.ID != 0) {
		char record[0xffff];
		int cat = 0;
		
		if (dlp_ReadRecordById (ctxt->dbi->pilot_socket, 
					ctxt->dbi->db_handle,
					local->local.ID, &record, 
					NULL, NULL, NULL, &cat) > 0) {
			local->local.category = cat;			
		}
	}

	/* STOP: don't replace these with g_strdup, since free_ToDo
	   uses free to deallocate */
	cal_component_get_summary (comp, &summary);
	if (summary.value) 
		local->todo->description = e_pilot_utf8_to_pchar (summary.value);

	cal_component_get_description_list (comp, &d_list);
	if (d_list) {
		description = (CalComponentText *) d_list->data;
		if (description && description->value)
			local->todo->note = e_pilot_utf8_to_pchar (description->value);
		else
			local->todo->note = NULL;
	} else {
		local->todo->note = NULL;
	}

	cal_component_get_due (comp, &due);	
	if (due.value) {
		icaltimezone_convert_time (due.value,
					   get_timezone (ctxt->client, due.tzid),
					   default_tz);
		local->todo->due = icaltimetype_to_tm (due.value);
		local->todo->indefinite = 0;
	} else {
		local->todo->indefinite = 1;
	}
	cal_component_free_datetime (&due);	

	cal_component_get_status (comp, &status);	
	if (status == ICAL_STATUS_COMPLETED)
		local->todo->complete = 1;
	else
		local->todo->complete = 0;
	
	cal_component_get_priority (comp, &priority);
	if (priority && *priority != 0) {
		if (*priority <= 3)
			local->todo->priority = 1;
		else if (*priority == 4)
			local->todo->priority = 2;
		else if (*priority == 5)
			local->todo->priority = 3;
		else if (*priority <= 7)
			local->todo->priority = 4;
		else
			local->todo->priority = 5;
		
		cal_component_free_priority (priority);
	} else {
		local->todo->priority = 3;
	}	
	
	cal_component_get_classification (comp, &classif);

	if (classif == CAL_COMPONENT_CLASS_PRIVATE)
		local->local.secret = 1;
	else
		local->local.secret = 0;

	local->local.archived = 0;
}

static void 
local_record_from_uid (EToDoLocalRecord *local,
		       const char *uid,
		       EToDoConduitContext *ctxt)
{
	CalComponent *comp;
	CalClientGetStatus status;

	g_assert(local!=NULL);

	status = cal_client_get_object (ctxt->client, uid, &comp);

	if (status == CAL_CLIENT_GET_SUCCESS) {
		local_record_from_comp (local, comp, ctxt);
		gtk_object_unref (GTK_OBJECT (comp));
	} else if (status == CAL_CLIENT_GET_NOT_FOUND) {
		comp = cal_component_new ();
		cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);
		cal_component_set_uid (comp, uid);
		local_record_from_comp (local, comp, ctxt);
		gtk_object_unref (GTK_OBJECT (comp));
	} else {
		INFO ("Object did not exist");
	}


}


static CalComponent *
comp_from_remote_record (GnomePilotConduitSyncAbs *conduit,
			 GnomePilotRecord *remote,
			 CalComponent *in_comp,
			 icaltimezone *timezone)
{
	CalComponent *comp;
	struct ToDo todo;
	CalComponentText summary = {NULL, NULL};
	CalComponentDateTime dt = {NULL, icaltimezone_get_tzid (timezone)};
	struct icaltimetype due, now;
	icaltimezone *utc_zone;
	int priority;
	char *txt;
	
	g_return_val_if_fail (remote != NULL, NULL);

	memset (&todo, 0, sizeof (struct ToDo));
	unpack_ToDo (&todo, remote->record, remote->length);

	utc_zone = icaltimezone_get_utc_timezone ();
	now = icaltime_from_timet_with_zone (time (NULL), FALSE, 
					     utc_zone);

	if (in_comp == NULL) {
		comp = cal_component_new ();
		cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);
		cal_component_set_created (comp, &now);
	} else {
		comp = cal_component_clone (in_comp);
	}

	cal_component_set_last_modified (comp, &now);

	summary.value = txt = e_pilot_utf8_from_pchar (todo.description);
	cal_component_set_summary (comp, &summary);
	free (txt);
	
	/* The iCal description field */
	if (!todo.note) {
		cal_component_set_comment_list (comp, NULL);
	} else {
		GSList l;
		CalComponentText text;

		text.value = txt = e_pilot_utf8_from_pchar (todo.note);
		text.altrep = NULL;
		l.data = &text;
		l.next = NULL;

		cal_component_set_description_list (comp, &l);
		free (txt);
	} 

	if (todo.complete) {
		int percent = 100;

		cal_component_set_completed (comp, &now);
		cal_component_set_percent (comp, &percent);
		cal_component_set_status (comp, ICAL_STATUS_COMPLETED);
	} else {
		int *percent = NULL;
		icalproperty_status status;
		
		cal_component_set_completed (comp, NULL);
		
		cal_component_get_percent (comp, &percent);
		if (percent == NULL || *percent == 100) {
			int p = 0;
			cal_component_set_percent (comp, &p);
		}
		if (percent != NULL)
			cal_component_free_percent (percent);

		cal_component_get_status (comp, &status);
		if (status == ICAL_STATUS_COMPLETED)
			cal_component_set_status (comp, ICAL_STATUS_NEEDSACTION);
	}

	if (!is_empty_time (todo.due)) {
		due = tm_to_icaltimetype (&todo.due, FALSE);
		dt.value = &due;
		cal_component_set_due (comp, &dt);
	}

	switch (todo.priority) {
	case 1:
		priority = 3;
		break;
	case 2:
		priority = 4;
		break;
	case 3:
		priority = 5;
		break;
	case 4:
		priority = 7;
		break;
	default:
		priority = 9;
	}
	
	cal_component_set_priority (comp, &priority);
	cal_component_set_transparency (comp, CAL_COMPONENT_TRANSP_NONE);

	if (remote->attr & dlpRecAttrSecret)
		cal_component_set_classification (comp, CAL_COMPONENT_CLASS_PRIVATE);
	else
		cal_component_set_classification (comp, CAL_COMPONENT_CLASS_PUBLIC);

	cal_component_commit_sequence (comp);
	
	free_ToDo(&todo);

	return comp;
}

static void
update_comp (GnomePilotConduitSyncAbs *conduit, CalComponent *comp,
	     EToDoConduitContext *ctxt) 
{
	gboolean success;

	g_return_if_fail (conduit != NULL);
	g_return_if_fail (comp != NULL);

	success = cal_client_update_object (ctxt->client, comp);

	if (!success)
		WARN (_("Error while communicating with calendar server"));
}

static void
check_for_slow_setting (GnomePilotConduit *c, EToDoConduitContext *ctxt)
{
	GnomePilotConduitStandard *conduit = GNOME_PILOT_CONDUIT_STANDARD (c);
	int map_count;

	/* If there are no objects or objects but no log */
	map_count = g_hash_table_size (ctxt->map->pid_map);
	if (map_count == 0)
		gnome_pilot_conduit_standard_set_slow (conduit, TRUE);

	if (gnome_pilot_conduit_standard_get_slow (conduit)) {
		ctxt->map->write_touched_only = TRUE;
		LOG ("    doing slow sync\n");
	} else {
		LOG ("    doing fast sync\n");
	}	
}

/* Pilot syncing callbacks */
static gint
pre_sync (GnomePilotConduit *conduit,
	  GnomePilotDBInfo *dbi,
	  EToDoConduitContext *ctxt)
{
	GnomePilotConduitSyncAbs *abs_conduit;
	GList *l;
	int len;
	unsigned char *buf;
	char *filename, *change_id;
	gint num_records, add_records = 0, mod_records = 0, del_records = 0;

	abs_conduit = GNOME_PILOT_CONDUIT_SYNC_ABS (conduit);

	LOG ("---------------------------------------------------------\n");
	LOG ("pre_sync: ToDo Conduit v.%s", CONDUIT_VERSION);
	g_message ("ToDo Conduit v.%s", CONDUIT_VERSION);

	ctxt->dbi = dbi;	
	ctxt->client = NULL;
	
	if (start_calendar_server (ctxt) != 0) {
		WARN(_("Could not start wombat server"));
		gnome_pilot_conduit_error (conduit, _("Could not start wombat"));
		return -1;
	}

	/* Get the timezone */
	ctxt->timezone = get_default_timezone ();
	if (ctxt->timezone == NULL)
		return -1;
	LOG ("  Using timezone: %s", icaltimezone_get_tzid (ctxt->timezone));

	/* Load the uid <--> pilot id map */
	filename = map_name (ctxt);
	e_pilot_map_read (filename, &ctxt->map);
	g_free (filename);

	/* Get the local database */
	ctxt->uids = cal_client_get_uids (ctxt->client, CALOBJ_TYPE_TODO);

	/* Count and hash the changes */
	change_id = g_strdup_printf ("pilot-sync-evolution-todo-%d", ctxt->cfg->pilot_id);
	ctxt->changed = cal_client_get_changes (ctxt->client, CALOBJ_TYPE_TODO, change_id);
	ctxt->changed_hash = g_hash_table_new (g_str_hash, g_str_equal);
	g_free (change_id);
	
	for (l = ctxt->changed; l != NULL; l = l->next) {
		CalClientChange *ccc = l->data;
		const char *uid;
		
		cal_component_get_uid (ccc->comp, &uid);
		if (!e_pilot_map_uid_is_archived (ctxt->map, uid)) {
			
			g_hash_table_insert (ctxt->changed_hash, g_strdup (uid), ccc);
			
			switch (ccc->type) {
			case CAL_CLIENT_CHANGE_ADDED:
				add_records++;
				break;
			case CAL_CLIENT_CHANGE_MODIFIED:
				mod_records++;
				break;
			case CAL_CLIENT_CHANGE_DELETED:
				del_records++;
				break;
			}
		} else if (ccc->type == CAL_CLIENT_CHANGE_DELETED) {
			e_pilot_map_remove_by_uid (ctxt->map, uid);
		}
	}

	/* Set the count information */
	num_records = cal_client_get_n_objects (ctxt->client, CALOBJ_TYPE_TODO);
	gnome_pilot_conduit_sync_abs_set_num_local_records(abs_conduit, num_records);
	gnome_pilot_conduit_sync_abs_set_num_new_local_records (abs_conduit, add_records);
	gnome_pilot_conduit_sync_abs_set_num_updated_local_records (abs_conduit, mod_records);
	gnome_pilot_conduit_sync_abs_set_num_deleted_local_records(abs_conduit, del_records);

	buf = (unsigned char*)g_malloc (0xffff);
	len = dlp_ReadAppBlock (dbi->pilot_socket, dbi->db_handle, 0,
			      (unsigned char *)buf, 0xffff);
	
	if (len < 0) {
		WARN (_("Could not read pilot's ToDo application block"));
		WARN ("dlp_ReadAppBlock(...) = %d", len);
		gnome_pilot_conduit_error (conduit,
					   _("Could not read pilot's ToDo application block"));
		return -1;
	}
	unpack_ToDoAppInfo (&(ctxt->ai), buf, len);
	g_free (buf);

	check_for_slow_setting (conduit, ctxt);
	if (ctxt->cfg->sync_type == GnomePilotConduitSyncTypeCopyToPilot
	    || ctxt->cfg->sync_type == GnomePilotConduitSyncTypeCopyFromPilot)
		ctxt->map->write_touched_only = TRUE;
	
	return 0;
}

static gint
post_sync (GnomePilotConduit *conduit,
	   GnomePilotDBInfo *dbi,
	   EToDoConduitContext *ctxt)
{
	GList *changed;
	gchar *filename, *change_id;

	LOG ("post_sync: ToDo Conduit v.%s", CONDUIT_VERSION);
	
	filename = map_name (ctxt);
	e_pilot_map_write (filename, ctxt->map);
	g_free (filename);

	/* FIX ME ugly hack - our changes musn't count, this does introduce
	 * a race condition if anyone changes a record elsewhere during sycnc
         */
	change_id = g_strdup_printf ("pilot-sync-evolution-todo-%d", ctxt->cfg->pilot_id);
	changed = cal_client_get_changes (ctxt->client, CALOBJ_TYPE_TODO, change_id);
	cal_client_change_list_free (changed);
	g_free (change_id);
	
	LOG ("---------------------------------------------------------\n");

	return 0;
}

static gint
set_pilot_id (GnomePilotConduitSyncAbs *conduit,
	      EToDoLocalRecord *local,
	      guint32 ID,
	      EToDoConduitContext *ctxt)
{
	const char *uid;

	LOG ("set_pilot_id: setting to %d\n", ID);
	
	cal_component_get_uid (local->comp, &uid);
	e_pilot_map_insert (ctxt->map, ID, uid, FALSE);

        return 0;
}

static gint
set_status_cleared (GnomePilotConduitSyncAbs *conduit,
		    EToDoLocalRecord *local,
		    EToDoConduitContext *ctxt)
{
	const char *uid;
	
	LOG ("set_status_cleared: clearing status\n");
	
	cal_component_get_uid (local->comp, &uid);
	g_hash_table_remove (ctxt->changed_hash, uid);
	
        return 0;
}

static gint
for_each (GnomePilotConduitSyncAbs *conduit,
	  EToDoLocalRecord **local,
	  EToDoConduitContext *ctxt)
{
	static GList *uids, *iterator;
	static int count;

	g_return_val_if_fail (local != NULL, -1);

	if (*local == NULL) {
		LOG ("beginning for_each");

		uids = ctxt->uids;
		count = 0;
		
		if (uids != NULL) {
			LOG ("iterating over %d records", g_list_length (uids));

			*local = g_new0 (EToDoLocalRecord, 1);
			local_record_from_uid (*local, uids->data, ctxt);
			g_list_prepend (ctxt->locals, *local);
			
			iterator = uids;
		} else {
			LOG ("no events");
			(*local) = NULL;
			return 0;
		}
	} else {
		count++;
		if (g_list_next (iterator)) {
			iterator = g_list_next (iterator);

			*local = g_new0 (EToDoLocalRecord, 1);
			local_record_from_uid (*local, iterator->data, ctxt);
			g_list_prepend (ctxt->locals, *local);
		} else {
			LOG ("for_each ending");

			/* Tell the pilot the iteration is over */
			*local = NULL;

			return 0;
		}
	}

	return 0;
}

static gint
for_each_modified (GnomePilotConduitSyncAbs *conduit,
		   EToDoLocalRecord **local,
		   EToDoConduitContext *ctxt)
{
	static GList *iterator;
	static int count;

	g_return_val_if_fail (local != NULL, 0);

	if (*local == NULL) {
		LOG ("for_each_modified beginning\n");
		
		iterator = ctxt->changed;
		
		count = 0;
	
		LOG ("iterating over %d records", g_hash_table_size (ctxt->changed_hash));
		
		iterator = next_changed_item (ctxt, iterator);
		if (iterator != NULL) {
			CalClientChange *ccc = iterator->data;
		
			*local = g_new0 (EToDoLocalRecord, 1);
			local_record_from_comp (*local, ccc->comp, ctxt);
			g_list_prepend (ctxt->locals, *local);
		} else {
			LOG ("no events");

			*local = NULL;
		}
	} else {
		count++;
		iterator = g_list_next (iterator);
		if (iterator && (iterator = next_changed_item (ctxt, iterator))) {
			CalClientChange *ccc = iterator->data;
			
			*local = g_new0 (EToDoLocalRecord, 1);
			local_record_from_comp (*local, ccc->comp, ctxt);			
			g_list_prepend (ctxt->locals, *local);
		} else {
			LOG ("for_each_modified ending");

			/* Signal the iteration is over */
			*local = NULL;
		}
	}

	return 0;
}

static gint
compare (GnomePilotConduitSyncAbs *conduit,
	 EToDoLocalRecord *local,
	 GnomePilotRecord *remote,
	 EToDoConduitContext *ctxt)
{
	/* used by the quick compare */
	GnomePilotRecord local_pilot;
	int retval = 0;

	LOG ("compare: local=%s remote=%s...\n",
	     print_local (local), print_remote (remote));

	g_return_val_if_fail (local!=NULL,-1);
	g_return_val_if_fail (remote!=NULL,-1);

	local_pilot = local_record_to_pilot_record (local, ctxt);

	if (remote->length != local_pilot.length
	    || memcmp (local_pilot.record, remote->record, remote->length))
		retval = 1;

	if (retval == 0)
		LOG ("    equal");
	else
		LOG ("    not equal");
	
	return retval;
}

static gint
add_record (GnomePilotConduitSyncAbs *conduit,
	    GnomePilotRecord *remote,
	    EToDoConduitContext *ctxt)
{
	CalComponent *comp;
	const char *uid;
	int retval = 0;
	
	g_return_val_if_fail (remote != NULL, -1);

	LOG ("add_record: adding %s to desktop\n", print_remote (remote));

	comp = comp_from_remote_record (conduit, remote, NULL, ctxt->timezone);
	update_comp (conduit, comp, ctxt);

	cal_component_get_uid (comp, &uid);
	e_pilot_map_insert (ctxt->map, remote->ID, uid, FALSE);

	gtk_object_unref (GTK_OBJECT (comp));

	return retval;
}

static gint
replace_record (GnomePilotConduitSyncAbs *conduit,
		EToDoLocalRecord *local,
		GnomePilotRecord *remote,
		EToDoConduitContext *ctxt)
{
	CalComponent *new_comp;
	int retval = 0;
	
	g_return_val_if_fail (remote != NULL, -1);

	LOG ("replace_record: replace %s with %s\n",
	     print_local (local), print_remote (remote));

	new_comp = comp_from_remote_record (conduit, remote, local->comp, ctxt->timezone);
	gtk_object_unref (GTK_OBJECT (local->comp));
	local->comp = new_comp;
	update_comp (conduit, local->comp, ctxt);

	return retval;
}

static gint
delete_record (GnomePilotConduitSyncAbs *conduit,
	       EToDoLocalRecord *local,
	       EToDoConduitContext *ctxt)
{
	const char *uid;

	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (local->comp != NULL, -1);

	cal_component_get_uid (local->comp, &uid);

	LOG ("delete_record: deleting %s\n", uid);

	e_pilot_map_remove_by_uid (ctxt->map, uid);
	cal_client_remove_object (ctxt->client, uid);
	
        return 0;
}

static gint
archive_record (GnomePilotConduitSyncAbs *conduit,
		EToDoLocalRecord *local,
		gboolean archive,
		EToDoConduitContext *ctxt)
{
	const char *uid;
	int retval = 0;
	
	g_return_val_if_fail (local != NULL, -1);

	LOG ("archive_record: %s\n", archive ? "yes" : "no");

	cal_component_get_uid (local->comp, &uid);
	e_pilot_map_insert (ctxt->map, local->local.ID, uid, archive);
	
        return retval;
}

static gint
match (GnomePilotConduitSyncAbs *conduit,
       GnomePilotRecord *remote,
       EToDoLocalRecord **local,
       EToDoConduitContext *ctxt)
{
	const char *uid;
	
	LOG ("match: looking for local copy of %s\n",
	     print_remote (remote));	
	
	g_return_val_if_fail (local != NULL, -1);
	g_return_val_if_fail (remote != NULL, -1);

	*local = NULL;
	uid = e_pilot_map_lookup_uid (ctxt->map, remote->ID, TRUE);
	
	if (!uid)
		return 0;

	LOG ("  matched\n");
	
	*local = g_new0 (EToDoLocalRecord, 1);
	local_record_from_uid (*local, uid, ctxt);
	
	return 0;
}

static gint
free_match (GnomePilotConduitSyncAbs *conduit,
	    EToDoLocalRecord *local,
	    EToDoConduitContext *ctxt)
{
	LOG ("free_match: freeing\n");

	g_return_val_if_fail (local != NULL, -1);

	free_local (local);

	return 0;
}

static gint
prepare (GnomePilotConduitSyncAbs *conduit,
	 EToDoLocalRecord *local,
	 GnomePilotRecord *remote,
	 EToDoConduitContext *ctxt)
{
	LOG ("prepare: encoding local %s\n", print_local (local));

	*remote = local_record_to_pilot_record (local, ctxt);

	return 0;
}

static ORBit_MessageValidationResult
accept_all_cookies (CORBA_unsigned_long request_id,
		    CORBA_Principal *principal,
		    CORBA_char *operation)
{
	/* allow ALL cookies */
	return ORBIT_MESSAGE_ALLOW_ALL;
}


GnomePilotConduit *
conduit_get_gpilot_conduit (guint32 pilot_id)
{
	GtkObject *retval;
	EToDoConduitContext *ctxt;

	LOG ("in todo's conduit_get_gpilot_conduit\n");

	/* we need to find wombat with oaf, so make sure oaf
	   is initialized here.  once the desktop is converted
	   to oaf and gpilotd is built with oaf, this can go away */
	if (!oaf_is_initialized ()) {
		char *argv[ 1 ] = {"hi"};
		oaf_init (1, argv);

		if (bonobo_init (CORBA_OBJECT_NIL,
				 CORBA_OBJECT_NIL,
				 CORBA_OBJECT_NIL) == FALSE)
			g_error (_("Could not initialize Bonobo"));

		ORBit_set_request_validation_handler (accept_all_cookies);
	}

	retval = gnome_pilot_conduit_sync_abs_new ("ToDoDB", 0x746F646F);
	g_assert (retval != NULL);

	ctxt = e_todo_context_new (pilot_id);
	gtk_object_set_data (GTK_OBJECT (retval), "todoconduit_context", ctxt);

	gtk_signal_connect (retval, "pre_sync", (GtkSignalFunc) pre_sync, ctxt);
	gtk_signal_connect (retval, "post_sync", (GtkSignalFunc) post_sync, ctxt);

  	gtk_signal_connect (retval, "set_pilot_id", (GtkSignalFunc) set_pilot_id, ctxt);
  	gtk_signal_connect (retval, "set_status_cleared", (GtkSignalFunc) set_status_cleared, ctxt);

  	gtk_signal_connect (retval, "for_each", (GtkSignalFunc) for_each, ctxt);
  	gtk_signal_connect (retval, "for_each_modified", (GtkSignalFunc) for_each_modified, ctxt);
  	gtk_signal_connect (retval, "compare", (GtkSignalFunc) compare, ctxt);

  	gtk_signal_connect (retval, "add_record", (GtkSignalFunc) add_record, ctxt);
  	gtk_signal_connect (retval, "replace_record", (GtkSignalFunc) replace_record, ctxt);
  	gtk_signal_connect (retval, "delete_record", (GtkSignalFunc) delete_record, ctxt);
  	gtk_signal_connect (retval, "archive_record", (GtkSignalFunc) archive_record, ctxt);

  	gtk_signal_connect (retval, "match", (GtkSignalFunc) match, ctxt);
  	gtk_signal_connect (retval, "free_match", (GtkSignalFunc) free_match, ctxt);

  	gtk_signal_connect (retval, "prepare", (GtkSignalFunc) prepare, ctxt);

	return GNOME_PILOT_CONDUIT (retval);
}

void
conduit_destroy_gpilot_conduit (GnomePilotConduit *conduit)
{ 
	GtkObject *obj = GTK_OBJECT (conduit);
	EToDoConduitContext *ctxt;
	
	ctxt = gtk_object_get_data (obj, "todoconduit_context");
	e_todo_context_destroy (ctxt);

	gtk_object_destroy (obj);
}
