/* Evolution calendar - Live search query implementation
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtksignal.h>
#include <bonobo/bonobo-exception.h>
#include <gal/widgets/e-unicode.h>
#include <e-util/e-sexp.h>
#include <cal-util/cal-recur.h>
#include <cal-util/timeutil.h>
#include "query.h"



/* States of a query */
typedef enum {
	QUERY_START_PENDING,	/* the query is not populated yet */
	QUERY_IN_PROGRESS,	/* the query is populated; components are still being processed */
	QUERY_DONE,		/* the query is done, but still accepts object changes */
	QUERY_PARSE_ERROR	/* a parse error occurred when initially creating the ESexp */
} QueryState;

/* Private part of the Query structure */
struct _QueryPrivate {
	/* The backend we are monitoring */
	CalBackend *backend;

	/* The default timezone for the calendar. */
	icaltimezone *default_zone;

	/* Listener to which we report changes in the live query */
	GNOME_Evolution_Calendar_QueryListener ql;

	/* Sexp that defines the query */
	char *sexp;
	ESExp *esexp;

	/* Idle handler ID for asynchronous queries and the current state */
	guint idle_id;
	QueryState state;

	/* List of UIDs that we still have to process */
	GList *pending_uids;
	int n_pending;
	int pending_total;

	/* Table of the UIDs we know do match the query */
	GHashTable *uids;

	/* The next component that will be handled in e_sexp_eval(); put here
	 * just because the query object itself is the esexp context.
	 */
	CalComponent *next_comp;
};



static void query_class_init (QueryClass *class);
static void query_init (Query *query);
static void query_destroy (GtkObject *object);

static BonoboXObjectClass *parent_class;



BONOBO_X_TYPE_FUNC_FULL (Query,
			 GNOME_Evolution_Calendar_Query,
			 BONOBO_X_OBJECT_TYPE,
			 query);

/* Class initialization function for the live search query */
static void
query_class_init (QueryClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (BONOBO_X_OBJECT_TYPE);

	object_class->destroy = query_destroy;

	/* The Query interface (ha ha! query interface!) has no methods, so we
	 * don't need to fiddle with the epv.
	 */
}

/* Object initialization function for the live search query */
static void
query_init (Query *query)
{
	QueryPrivate *priv;

	priv = g_new0 (QueryPrivate, 1);
	query->priv = priv;

	priv->backend = NULL;
	priv->default_zone = NULL;
	priv->ql = CORBA_OBJECT_NIL;
	priv->sexp = NULL;

	priv->idle_id = 0;
	priv->state = QUERY_START_PENDING;

	priv->pending_uids = NULL;
	priv->uids = g_hash_table_new (g_str_hash, g_str_equal);

	priv->next_comp = NULL;
}

/* Used from g_hash_table_foreach(); frees a UID */
static void
free_uid_cb (gpointer key, gpointer value, gpointer data)
{
	char *uid;

	uid = key;
	g_free (uid);
}

/* Destroy handler for the live search query */
static void
query_destroy (GtkObject *object)
{
	Query *query;
	QueryPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_QUERY (object));

	query = QUERY (object);
	priv = query->priv;

	if (priv->backend) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (priv->backend), query);
		gtk_object_unref (GTK_OBJECT (priv->backend));
		priv->backend = NULL;
	}

	if (priv->ql != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		bonobo_object_release_unref (priv->ql, &ev);

		if (BONOBO_EX (&ev))
			g_message ("query_destroy(): Could not unref the listener\n");

		CORBA_exception_free (&ev);

		priv->ql = CORBA_OBJECT_NIL;
	}

	if (priv->sexp) {
		g_free (priv->sexp);
		priv->sexp = NULL;
	}

	if (priv->esexp) {
		e_sexp_unref (priv->esexp);
		priv->esexp = NULL;
	}

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}

	if (priv->pending_uids) {
		GList *l;

		for (l = priv->pending_uids; l; l = l->next) {
			char *uid;

			uid = l->data;
			g_assert (uid != NULL);
			g_free (uid);
		}

		g_list_free (priv->pending_uids);
		priv->pending_uids = NULL;
		priv->n_pending = 0;
	}

	g_hash_table_foreach (priv->uids, free_uid_cb, NULL);
	g_hash_table_destroy (priv->uids);
	priv->uids = NULL;

	g_free (priv);
	query->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* E-Sexp functions */

/* (time-now)
 *
 * Returns a time_t of time (NULL).
 */
static ESExpResult *
func_time_now (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *result;

	if (argc != 0) {
		e_sexp_fatal_error (esexp, _("time-now expects 0 arguments"));
		return NULL;
	}

	result = e_sexp_result_new (esexp, ESEXP_RES_TIME);
	result->value.time = time (NULL);

	return result;
}

/* (make-time ISODATE)
 *
 * ISODATE - string, ISO 8601 date/time representation
 *
 * Constructs a time_t value for the specified date.
 */
static ESExpResult *
func_make_time (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	const char *str;
	time_t t;
	ESExpResult *result;

	if (argc != 1) {
		e_sexp_fatal_error (esexp, _("make-time expects 1 argument"));
		return NULL;
	}

	if (argv[0]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (esexp, _("make-time expects argument 1 "
					     "to be a string"));
		return NULL;
	}
	str = argv[0]->value.string;

	t = time_from_isodate (str);
	if (t == -1) {
		e_sexp_fatal_error (esexp, _("make-time argument 1 must be an "
					     "ISO 8601 date/time string"));
		return NULL;
	}

	result = e_sexp_result_new (esexp, ESEXP_RES_TIME);
	result->value.time = t;

	return result;
}

/* (time-add-day TIME N)
 *
 * TIME - time_t, base time
 * N - int, number of days to add
 *
 * Adds the specified number of days to a time value.
 *
 * FIXME: TIMEZONES - need to use a timezone or daylight saving changes will
 * make the result incorrect.
 */
static ESExpResult *
func_time_add_day (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *result;
	time_t t;
	int n;

	if (argc != 2) {
		e_sexp_fatal_error (esexp, _("time-add-day expects 2 arguments"));
		return NULL;
	}

	if (argv[0]->type != ESEXP_RES_TIME) {
		e_sexp_fatal_error (esexp, _("time-add-day expects argument 1 "
					     "to be a time_t"));
		return NULL;
	}
	t = argv[0]->value.time;

	if (argv[1]->type != ESEXP_RES_INT) {
		e_sexp_fatal_error (esexp, _("time-add-day expects argument 2 "
					     "to be an integer"));
		return NULL;
	}
	n = argv[1]->value.number;
	
	result = e_sexp_result_new (esexp, ESEXP_RES_TIME);
	result->value.time = time_add_day (t, n);

	return result;
}

/* (time-day-begin TIME)
 *
 * TIME - time_t, base time
 *
 * Returns the start of the day, according to the local time.
 *
 * FIXME: TIMEZONES - this uses the current Unix timezone.
 */
static ESExpResult *
func_time_day_begin (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	time_t t;
	ESExpResult *result;

	if (argc != 1) {
		e_sexp_fatal_error (esexp, _("time-day-begin expects 1 argument"));
		return NULL;
	}

	if (argv[0]->type != ESEXP_RES_TIME) {
		e_sexp_fatal_error (esexp, _("time-day-begin expects argument 1 "
					     "to be a time_t"));
		return NULL;
	}
	t = argv[0]->value.time;

	result = e_sexp_result_new (esexp, ESEXP_RES_TIME);
	result->value.time = time_day_begin (t);

	return result;
}

/* (time-day-end TIME)
 *
 * TIME - time_t, base time
 *
 * Returns the end of the day, according to the local time.
 *
 * FIXME: TIMEZONES - this uses the current Unix timezone.
 */
static ESExpResult *
func_time_day_end (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	time_t t;
	ESExpResult *result;

	if (argc != 1) {
		e_sexp_fatal_error (esexp, _("time-day-end expects 1 argument"));
		return NULL;
	}

	if (argv[0]->type != ESEXP_RES_TIME) {
		e_sexp_fatal_error (esexp, _("time-day-end expects argument 1 "
					     "to be a time_t"));
		return NULL;
	}
	t = argv[0]->value.time;

	result = e_sexp_result_new (esexp, ESEXP_RES_TIME);
	result->value.time = time_day_end (t);

	return result;
}

/* (get-vtype)
 *
 * Returns a string indicating the type of component (VEVENT, VTODO, VJOURNAL,
 * VFREEBUSY, VTIMEZONE, UNKNOWN).
 */
static ESExpResult *
func_get_vtype (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	Query *query;
	QueryPrivate *priv;
	CalComponent *comp;
	CalComponentVType vtype;
	char *str;
	ESExpResult *result;

	query = QUERY (data);
	priv = query->priv;

	g_assert (priv->next_comp != NULL);
	comp = priv->next_comp;

	/* Check argument types */

	if (argc != 0) {
		e_sexp_fatal_error (esexp, _("get-vtype expects 0 arguments"));
		return NULL;
	}

	/* Get the type */

	vtype = cal_component_get_vtype (comp);

	switch (vtype) {
	case CAL_COMPONENT_EVENT:
		str = g_strdup ("VEVENT");
		break;

	case CAL_COMPONENT_TODO:
		str = g_strdup ("VTODO");
		break;

	case CAL_COMPONENT_JOURNAL:
		str = g_strdup ("VJOURNAL");
		break;

	case CAL_COMPONENT_FREEBUSY:
		str = g_strdup ("VFREEBUSY");
		break;

	case CAL_COMPONENT_TIMEZONE:
		str = g_strdup ("VTIMEZONE");
		break;

	default:
		str = g_strdup ("UNKNOWN");
		break;
	}

	result = e_sexp_result_new (esexp, ESEXP_RES_STRING);
	result->value.string = str;

	return result;
}

/* Sets a boolean value in the data to TRUE; called from
 * cal_recur_generate_instances() to indicate that at least one instance occurs
 * in the sought time range.  We always return FALSE because we want the
 * recurrence engine to finish as soon as possible.
 */
static gboolean
instance_occur_cb (CalComponent *comp, time_t start, time_t end, gpointer data)
{
	gboolean *occurs;

	occurs = data;
	*occurs = TRUE;

	return FALSE;
}

/* Call the backend function to get a timezone from a TZID. */
static icaltimezone*
resolve_tzid (const char *tzid, gpointer data)
{
	Query *query = data;

	if (!tzid || !tzid[0])
		return NULL;
	else
		return cal_backend_get_timezone (query->priv->backend, tzid);
}


/* (occur-in-time-range? START END)
 *
 * START - time_t, start of the time range
 * END - time_t, end of the time range
 *
 * Returns a boolean indicating whether the component has any occurrences in the
 * specified time range.
 */
static ESExpResult *
func_occur_in_time_range (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	Query *query;
	QueryPrivate *priv;
	CalComponent *comp;
	time_t start, end;
	gboolean occurs;
	ESExpResult *result;

	query = QUERY (data);
	priv = query->priv;

	g_assert (priv->next_comp != NULL);
	comp = priv->next_comp;

	/* Check argument types */

	if (argc != 2) {
		e_sexp_fatal_error (esexp, _("occur-in-time-range? expects 2 arguments"));
		return NULL;
	}

	if (argv[0]->type != ESEXP_RES_TIME) {
		e_sexp_fatal_error (esexp, _("occur-in-time-range? expects argument 1 "
					     "to be a time_t"));
		return NULL;
	}
	start = argv[0]->value.time;

	if (argv[1]->type != ESEXP_RES_TIME) {
		e_sexp_fatal_error (esexp, _("occur-in-time-range? expects argument 2 "
					     "to be a time_t"));
		return NULL;
	}
	end = argv[1]->value.time;

	/* See if there is at least one instance in that range */

	occurs = FALSE;

	cal_recur_generate_instances (comp, start, end,
				      instance_occur_cb, &occurs,
				      resolve_tzid, query, priv->default_zone);

	result = e_sexp_result_new (esexp, ESEXP_RES_BOOL);
	result->value.bool = occurs;

	return result;
}

/* Returns whether a list of CalComponentText items matches the specified string */
static gboolean
matches_text_list (GSList *text_list, const char *str)
{
	GSList *l;
	gboolean matches;

	matches = FALSE;

	for (l = text_list; l; l = l->next) {
		CalComponentText *text;

		text = l->data;
		g_assert (text->value != NULL);

		if (e_utf8_strstrcasedecomp (text->value, str) != NULL) {
			matches = TRUE;
			break;
		}
	}

	return matches;
}

/* Returns whether the comments in a component matches the specified string */
static gboolean
matches_comment (CalComponent *comp, const char *str)
{
	GSList *list;
	gboolean matches;

	cal_component_get_comment_list (comp, &list);
	matches = matches_text_list (list, str);
	cal_component_free_text_list (list);

	return matches;
}

/* Returns whether the description in a component matches the specified string */
static gboolean
matches_description (CalComponent *comp, const char *str)
{
	GSList *list;
	gboolean matches;

	cal_component_get_description_list (comp, &list);
	matches = matches_text_list (list, str);
	cal_component_free_text_list (list);

	return matches;
}

/* Returns whether the summary in a component matches the specified string */
static gboolean
matches_summary (CalComponent *comp, const char *str)
{
	CalComponentText text;

	cal_component_get_summary (comp, &text);

	if (!text.value)
		return FALSE;

	return e_utf8_strstrcasedecomp (text.value, str) != NULL;
}

/* Returns whether any text field in a component matches the specified string */
static gboolean
matches_any (CalComponent *comp, const char *str)
{
	/* As an optimization, and to make life easier for the individual
	 * predicate functions, see if we are looking for the empty string right
	 * away.
	 */
	if (strlen (str) == 0)
		return TRUE;

	return (matches_comment (comp, str)
		|| matches_description (comp, str)
		|| matches_summary (comp, str));
}

/* (contains? FIELD STR)
 *
 * FIELD - string, name of field to match (any, comment, description, summary)
 * STR - string, match string
 *
 * Returns a boolean indicating whether the specified field contains the
 * specified string.
 */
static ESExpResult *
func_contains (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	Query *query;
	QueryPrivate *priv;
	CalComponent *comp;
	const char *field;
	const char *str;
	gboolean matches;
	ESExpResult *result;

	query = QUERY (data);
	priv = query->priv;

	g_assert (priv->next_comp != NULL);
	comp = priv->next_comp;

	/* Check argument types */

	if (argc != 2) {
		e_sexp_fatal_error (esexp, _("contains? expects 2 arguments"));
		return NULL;
	}

	if (argv[0]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (esexp, _("contains? expects argument 1 "
					     "to be a string"));
		return NULL;
	}
	field = argv[0]->value.string;

	if (argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (esexp, _("contains? expects argument 2 "
					     "to be a string"));
		return NULL;
	}
	str = argv[1]->value.string;

	/* See if it matches */

	if (strcmp (field, "any") == 0)
		matches = matches_any (comp, str);
	else if (strcmp (field, "comment") == 0)
		matches = matches_comment (comp, str);
	else if (strcmp (field, "description") == 0)
		matches = matches_description (comp, str);
	else if (strcmp (field, "summary") == 0)
		matches = matches_summary (comp, str);
	else {
		e_sexp_fatal_error (esexp, _("contains? expects argument 1 to "
					     "be one of \"any\", \"summary\", \"description\""));
		return NULL;
	}

	result = e_sexp_result_new (esexp, ESEXP_RES_BOOL);
	result->value.bool = matches;

	return result;
}

/* (has-categories? STR+)
 * (has-categories? #f)
 *
 * STR - At least one string specifying a category
 * Or you can specify a single #f (boolean false) value for components
 * that have no categories assigned to them ("unfiled").
 *
 * Returns a boolean indicating whether the component has all the specified
 * categories.
 */
static ESExpResult *
func_has_categories (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	Query *query;
	QueryPrivate *priv;
	CalComponent *comp;
	gboolean unfiled;
	int i;
	GSList *categories;
	gboolean matches;
	ESExpResult *result;

	query = QUERY (data);
	priv = query->priv;

	g_assert (priv->next_comp != NULL);
	comp = priv->next_comp;

	/* Check argument types */

	if (argc < 1) {
		e_sexp_fatal_error (esexp, _("has-categories? expects at least 1 argument"));
		return NULL;
	}

	if (argc == 1 && argv[0]->type == ESEXP_RES_BOOL)
		unfiled = TRUE;
	else
		unfiled = FALSE;

	if (!unfiled)
		for (i = 0; i < argc; i++)
			if (argv[i]->type != ESEXP_RES_STRING) {
				e_sexp_fatal_error (esexp, _("has-categories? expects all arguments "
							     "to be strings or one and only one "
							     "argument to be a boolean false (#f)"));
				return NULL;
			}

	/* Search categories.  First, if there are no categories we return
	 * whether unfiled components are supposed to match.
	 */

	cal_component_get_categories_list (comp, &categories);
	if (!categories) {
		result = e_sexp_result_new (esexp, ESEXP_RES_BOOL);
		result->value.bool = unfiled;

		return result;
	}

	/* Otherwise, we *do* have categories but unfiled components were
	 * requested, so this component does not match.
	 */
	if (unfiled) {
		result = e_sexp_result_new (esexp, ESEXP_RES_BOOL);
		result->value.bool = FALSE;

		return result;
	}

	matches = TRUE;

	for (i = 0; i < argc; i++) {
		const char *sought;
		GSList *l;
		gboolean has_category;

		sought = argv[i]->value.string;

		has_category = FALSE;

		for (l = categories; l; l = l->next) {
			const char *category;

			category = l->data;

			if (strcmp (category, sought) == 0) {
				has_category = TRUE;
				break;
			}
		}

		if (!has_category) {
			matches = FALSE;
			break;
		}
	}

	cal_component_free_categories_list (categories);

	result = e_sexp_result_new (esexp, ESEXP_RES_BOOL);
	result->value.bool = matches;

	return result;
}

/* (is-completed?)
 *
 * Returns a boolean indicating whether the component is completed (i.e. has
 * a COMPLETED property. This is really only useful for TODO components.
 */
static ESExpResult *
func_is_completed (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	Query *query;
	QueryPrivate *priv;
	CalComponent *comp;
	ESExpResult *result;
	struct icaltimetype *t;
	gboolean complete = FALSE;

	query = QUERY (data);
	priv = query->priv;

	g_assert (priv->next_comp != NULL);
	comp = priv->next_comp;

	/* Check argument types */

	if (argc != 0) {
		e_sexp_fatal_error (esexp, _("is-completed? expects 0 arguments"));
		return NULL;
	}

	cal_component_get_completed (comp, &t);
	if (t) {
		complete = TRUE;
		cal_component_free_icaltimetype (t);
	}

	result = e_sexp_result_new (esexp, ESEXP_RES_BOOL);
	result->value.bool = complete;

	return result;
}

/* (completed-before? TIME)
 *
 * TIME - time_t
 *
 * Returns a boolean indicating whether the component was completed on or
 * before the given time (i.e. it checks the COMPLETED property).
 * This is really only useful for TODO components.
 */
static ESExpResult *
func_completed_before (ESExp *esexp, int argc, ESExpResult **argv, void *data)
{
	Query *query;
	QueryPrivate *priv;
	CalComponent *comp;
	ESExpResult *result;
	struct icaltimetype *tt;
	icaltimezone *zone;
	gboolean retval = FALSE;
	time_t before_time, completed_time;

	query = QUERY (data);
	priv = query->priv;

	g_assert (priv->next_comp != NULL);
	comp = priv->next_comp;

	/* Check argument types */

	if (argc != 1) {
		e_sexp_fatal_error (esexp, _("completed-before? expects 1 argument"));
		return NULL;
	}

	if (argv[0]->type != ESEXP_RES_TIME) {
		e_sexp_fatal_error (esexp, _("completed-before? expects argument 1 "
					     "to be a time_t"));
		return NULL;
	}
	before_time = argv[0]->value.time;

	cal_component_get_completed (comp, &tt);
	if (tt) {
		/* COMPLETED must be in UTC. */
		zone = icaltimezone_get_utc_timezone ();
		completed_time = icaltime_as_timet_with_zone (*tt, zone);

#if 0
		g_print ("Query Time    : %s", ctime (&before_time));
		g_print ("Completed Time: %s", ctime (&completed_time));
#endif

		/* We want to return TRUE if before_time is after
		   completed_time. */
		if (difftime (before_time, completed_time) > 0) {
#if 0
			g_print ("  Returning TRUE\n");
#endif
			retval = TRUE;
		}

		cal_component_free_icaltimetype (tt);
	}

	result = e_sexp_result_new (esexp, ESEXP_RES_BOOL);
	result->value.bool = retval;

	return result;
}



/* Adds a component to our the UIDs hash table and notifies the client */
static void
add_component (Query *query, const char *uid, gboolean query_in_progress, int n_scanned, int total)
{
	QueryPrivate *priv;
	char *old_uid;
	CORBA_Environment ev;

	if (query_in_progress)
		g_assert (n_scanned > 0 || n_scanned <= total);

	priv = query->priv;

	if (g_hash_table_lookup_extended (priv->uids, uid, (gpointer *) &old_uid, NULL)) {
		g_hash_table_remove (priv->uids, old_uid);
		g_free (old_uid);
	}

	g_hash_table_insert (priv->uids, g_strdup (uid), NULL);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_QueryListener_notifyObjUpdated (
		priv->ql,
		(char *) uid,
		query_in_progress,
		n_scanned,
		total,
		&ev);

	if (BONOBO_EX (&ev))
		g_message ("add_component(): Could not notify the listener of an "
			   "updated component");

	CORBA_exception_free (&ev);
}

/* Removes a component from our the UIDs hash table and notifies the client */
static void
remove_component (Query *query, const char *uid)
{
	QueryPrivate *priv;
	char *old_uid;
	CORBA_Environment ev;

	priv = query->priv;

	if (!g_hash_table_lookup_extended (priv->uids, uid, (gpointer *) &old_uid, NULL))
		return;

	/* The component did match the query before but it no longer does, so we
	 * have to notify the client.
	 */

	g_hash_table_remove (priv->uids, old_uid);
	g_free (old_uid);

	CORBA_exception_init (&ev);
	GNOME_Evolution_Calendar_QueryListener_notifyObjRemoved (
		priv->ql,
		(char *) uid,
		&ev);

	if (BONOBO_EX (&ev))
		g_message ("remove_component(): Could not notify the listener of a "
			   "removed component");

	CORBA_exception_free (&ev);
}

/* Removes a component from the list of pending UIDs */
static void
remove_from_pending (Query *query, const char *remove_uid)
{
	QueryPrivate *priv;
	GList *l;

	priv = query->priv;

	for (l = priv->pending_uids; l; l = l->next) {
		char *uid;

		g_assert (priv->n_pending > 0);

		uid = l->data;
		if (strcmp (remove_uid, uid))
			continue;

		g_free (uid);

		priv->pending_uids = g_list_remove_link (priv->pending_uids, l);
		g_list_free_1 (l);
		priv->n_pending--;

		g_assert ((priv->pending_uids && priv->n_pending != 0)
			  || (!priv->pending_uids && priv->n_pending == 0));

		break;
	}
}

static struct {
	char *name;
	ESExpFunc *func;
} functions[] = {
	/* Time-related functions */
	{ "time-now", func_time_now },
	{ "make-time", func_make_time },
	{ "time-add-day", func_time_add_day },
	{ "time-day-begin", func_time_day_begin },
	{ "time-day-end", func_time_day_end },

	/* Component-related functions */
	{ "get-vtype", func_get_vtype },
	{ "occur-in-time-range?", func_occur_in_time_range },
	{ "contains?", func_contains },
	{ "has-categories?", func_has_categories },
	{ "is-completed?", func_is_completed },
	{ "completed-before?", func_completed_before }
};

/* Initializes a sexp by interning our own symbols */
static ESExp *
create_sexp (Query *query)
{
	ESExp *esexp;
	int i;

	esexp = e_sexp_new ();

	for (i = 0; i < (sizeof (functions) / sizeof (functions[0])); i++)
		e_sexp_add_function (esexp, 0, functions[i].name, functions[i].func, query);

	return esexp;
}

/* Ensures that the sexp has been parsed and the ESexp has been created.  If a
 * parse error occurs, it sets the query state to QUERY_PARSE_ERROR and returns
 * FALSE.
 */
static gboolean
ensure_sexp (Query *query)
{
	QueryPrivate *priv;

	priv = query->priv;

	if (priv->state == QUERY_PARSE_ERROR)
		g_assert_not_reached (); /* we should already have terminated everything */

	if (priv->esexp)
		return TRUE;

	/* Compile the query string */

	priv->esexp = create_sexp (query);

	g_assert (priv->sexp != NULL);
	e_sexp_input_text (priv->esexp, priv->sexp, strlen (priv->sexp));

	if (e_sexp_parse (priv->esexp) == -1) {
		const char *error_str;
		CORBA_Environment ev;

		/* Change the state and disconnect from any notifications */

		priv->state = QUERY_PARSE_ERROR;
		gtk_signal_disconnect_by_data (GTK_OBJECT (priv->backend), query);

		/* Report the error to the listener */

		error_str = e_sexp_error (priv->esexp);
		g_assert (error_str != NULL);

		CORBA_exception_init (&ev);
		GNOME_Evolution_Calendar_QueryListener_notifyQueryDone (
			priv->ql,
			GNOME_Evolution_Calendar_QueryListener_PARSE_ERROR,
			error_str,
			&ev);

		if (BONOBO_EX (&ev))
			g_message ("ensure_sexp(): Could not notify the listener of "
				   "a parse error");

		CORBA_exception_free (&ev);

		e_sexp_unref (priv->esexp);
		priv->esexp = NULL;

		return FALSE;
	}

	return TRUE;
}

/* Evaluates the query sexp on the specified component and notifies the listener
 * as appropriate.
 */
static void
match_component (Query *query, const char *uid,
		 gboolean query_in_progress, int n_scanned, int total)
{
	QueryPrivate *priv;
	CalComponent *comp;
	ESExpResult *result;

	priv = query->priv;

	g_assert (priv->state != QUERY_PARSE_ERROR);

	if (!ensure_sexp (query))
		return;

	comp = cal_backend_get_object_component (priv->backend, uid);
	g_return_if_fail (comp != NULL);
	gtk_object_ref (GTK_OBJECT (comp));

	/* Eval the sexp */

	g_assert (priv->next_comp == NULL);

	priv->next_comp = comp;
	result = e_sexp_eval (priv->esexp);
	gtk_object_unref (GTK_OBJECT (comp));
	priv->next_comp = NULL;

	if (!result) {
		const char *error_str;
		CORBA_Environment ev;

		error_str = e_sexp_error (priv->esexp);
		g_assert (error_str != NULL);

		CORBA_exception_init (&ev);
		GNOME_Evolution_Calendar_QueryListener_notifyEvalError (
			priv->ql,
			error_str,
			&ev);

		if (BONOBO_EX (&ev))
			g_message ("match_component(): Could not notify the listener of "
				   "an evaluation error");

		CORBA_exception_free (&ev);
		return;
	} else if (result->type != ESEXP_RES_BOOL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		GNOME_Evolution_Calendar_QueryListener_notifyEvalError (
			priv->ql,
			_("Evaluation of the search expression did not yield a boolean value"),
			&ev);

		if (BONOBO_EX (&ev))
			g_message ("match_component(): Could not notify the listener of "
				   "an unexpected result value type when evaluating the "
				   "search expression");

		CORBA_exception_free (&ev);
	} else {
		/* Success; process the component accordingly */

		if (result->value.bool)
			add_component (query, uid, query_in_progress, n_scanned, total);
		else
			remove_component (query, uid);
	}

	e_sexp_result_free (priv->esexp, result);
}

/* Processes a single component that is queued in the list */
static gboolean
process_component_cb (gpointer data)
{
	Query *query;
	QueryPrivate *priv;
	char *uid;
	GList *l;

	query = QUERY (data);
	priv = query->priv;

	/* No more components? */

	if (!priv->pending_uids) {
		CORBA_Environment ev;

		g_assert (priv->n_pending == 0);

		priv->idle_id = 0;
		priv->state = QUERY_DONE;

		CORBA_exception_init (&ev);
		GNOME_Evolution_Calendar_QueryListener_notifyQueryDone (
			priv->ql,
			GNOME_Evolution_Calendar_QueryListener_SUCCESS,
			"",
			&ev);

		if (BONOBO_EX (&ev))
			g_message ("process_component_cb(): Could not notify the listener of "
				   "a finished query");

		CORBA_exception_free (&ev);

		return FALSE;
	}

	g_assert (priv->n_pending > 0);

	/* Fetch the component */

	l = priv->pending_uids;
	priv->pending_uids = g_list_remove_link (priv->pending_uids, l);
	priv->n_pending--;

	g_assert ((priv->pending_uids && priv->n_pending != 0)
		  || (!priv->pending_uids && priv->n_pending == 0));

	uid = l->data;
	g_assert (uid != NULL);

	g_list_free_1 (l);

	bonobo_object_ref (BONOBO_OBJECT (query));

	match_component (query, uid,
			 TRUE,
			 priv->pending_total - priv->n_pending,
			 priv->pending_total);

	bonobo_object_unref (BONOBO_OBJECT (query));

	g_free (uid);

	return TRUE;
}

/* Populates the query with pending UIDs so that they can be processed
 * asynchronously.
 */
static void
populate_query (Query *query)
{
	QueryPrivate *priv;

	priv = query->priv;
	g_assert (priv->idle_id == 0);
	g_assert (priv->state == QUERY_START_PENDING);

	priv->pending_uids = cal_backend_get_uids (priv->backend, CALOBJ_TYPE_ANY);
	priv->pending_total = g_list_length (priv->pending_uids);
	priv->n_pending = priv->pending_total;

	priv->idle_id = g_idle_add (process_component_cb, query);
	priv->state = QUERY_IN_PROGRESS;
}

/* Callback used when a component changes in the backend */
static void
backend_obj_updated_cb (CalBackend *backend, const char *uid, gpointer data)
{
	Query *query;

	query = QUERY (data);

	bonobo_object_ref (BONOBO_OBJECT (query));

	match_component (query, uid, FALSE, 0, 0);
	remove_from_pending (query, uid);

	bonobo_object_unref (BONOBO_OBJECT (query));
}

/* Callback used when a component is removed from the backend */
static void
backend_obj_removed_cb (CalBackend *backend, const char *uid, gpointer data)
{
	Query *query;
	QueryPrivate *priv;

	query = QUERY (data);
	priv = query->priv;

	bonobo_object_ref (BONOBO_OBJECT (query));

	remove_component (query, uid);
	remove_from_pending (query, uid);

	bonobo_object_unref (BONOBO_OBJECT (query));
}

/* Idle handler for starting a query */
static gboolean
start_query_cb (gpointer data)
{
	Query *query;
	QueryPrivate *priv;

	query = QUERY (data);
	priv = query->priv;

	priv->idle_id = 0;

	if (!ensure_sexp (query))
		return FALSE;

	/* Populate the query with UIDs so that we can process them asynchronously */

	populate_query (query);

	gtk_signal_connect (GTK_OBJECT (priv->backend), "obj_updated",
			    GTK_SIGNAL_FUNC (backend_obj_updated_cb),
			    query);
	gtk_signal_connect (GTK_OBJECT (priv->backend), "obj_removed",
			    GTK_SIGNAL_FUNC (backend_obj_removed_cb),
			    query);

	return FALSE;
}

/* Callback used when the backend gets loaded; we just queue the query to be
 * started later.
 */
static void
backend_opened_cb (CalBackend *backend, CalBackendOpenStatus status, gpointer data)
{
	Query *query;
	QueryPrivate *priv;

	query = QUERY (data);
	priv = query->priv;

	if (status == CAL_BACKEND_OPEN_SUCCESS) {
		g_assert (cal_backend_is_loaded (backend));
		g_assert (priv->idle_id == 0);

		priv->idle_id = g_idle_add (start_query_cb, query);
	}
}

/**
 * query_construct:
 * @query: A live search query.
 * @backend: Calendar backend that the query object will monitor.
 * @ql: Listener for query results.
 * @sexp: Sexp that defines the query.
 * 
 * Constructs a #Query object by binding it to a calendar backend and a query
 * listener.  The @query object will start to populate itself asynchronously and
 * call the listener as appropriate.
 * 
 * Return value: The same value as @query, or NULL if the query could not
 * be constructed.
 **/
Query *
query_construct (Query *query,
		 CalBackend *backend,
		 GNOME_Evolution_Calendar_QueryListener ql,
		 const char *sexp)
{
	QueryPrivate *priv;
	CORBA_Environment ev;

	g_return_val_if_fail (query != NULL, NULL);
	g_return_val_if_fail (IS_QUERY (query), NULL);
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (IS_CAL_BACKEND (backend), NULL);
	g_return_val_if_fail (ql != CORBA_OBJECT_NIL, NULL);
	g_return_val_if_fail (sexp != NULL, NULL);

	priv = query->priv;

	CORBA_exception_init (&ev);
	priv->ql = CORBA_Object_duplicate (ql, &ev);
	if (BONOBO_EX (&ev)) {
		g_message ("query_construct(): Could not duplicate the listener");
		priv->ql = CORBA_OBJECT_NIL;
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	priv->backend = backend;
	gtk_object_ref (GTK_OBJECT (priv->backend));

	priv->default_zone = cal_backend_get_default_timezone (backend);

	priv->sexp = g_strdup (sexp);

	/* Queue the query to be started asynchronously */

	if (cal_backend_is_loaded (priv->backend)) {
		g_assert (priv->idle_id == 0);
		priv->idle_id = g_idle_add (start_query_cb, query);
	} else
		gtk_signal_connect (GTK_OBJECT (priv->backend), "opened",
				    GTK_SIGNAL_FUNC (backend_opened_cb),
				    query);

	return query;
}

/**
 * query_new:
 * @backend: Calendar backend that the query object will monitor.
 * @ql: Listener for query results.
 * @sexp: Sexp that defines the query.
 * 
 * Creates a new query engine object that monitors a calendar backend.
 * 
 * Return value: A newly-created query object, or NULL on failure.
 **/
Query *
query_new (CalBackend *backend,
	   GNOME_Evolution_Calendar_QueryListener ql,
	   const char *sexp)
{
	Query *query;

	query = QUERY (gtk_type_new (QUERY_TYPE));
	if (!query_construct (query, backend, ql, sexp)) {
		bonobo_object_unref (BONOBO_OBJECT (query));
		return NULL;
	}

	return query;
}
