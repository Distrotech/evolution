/* Evolution calendar - Component editor factory object
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
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

#include <bonobo/bonobo-exception.h>
#include <evolution-calendar.h>
#include <e-util/e-url.h>
#include <cal-client/cal-client.h>
#include "calendar-config.h"
#include "e-comp-editor-registry.h"
#include "comp-editor-factory.h"
#include "comp-util.h"
#include "dialogs/event-editor.h"
#include "dialogs/task-editor.h"

extern ECompEditorRegistry *comp_editor_registry;



/* A pending request */

typedef enum {
	REQUEST_EXISTING,
	REQUEST_NEW
} RequestType;

typedef struct {
	RequestType type;

	union {
		struct {
			char *uid;
		} existing;

		struct {
			 GNOME_Evolution_Calendar_CompEditorFactory_CompEditorMode type;
		} new;
	} u;
} Request;

/* A client we have open */
typedef struct {
	/* Our parent CompEditorFactory */
	CompEditorFactory *factory;

	/* Uri of the calendar, used as key in the clients hash table */
	char *uri;

	/* Client of the calendar */
	CalClient *client;
 
	/* Count editors using this client */
	int editor_count;

	/* Pending requests; they are pending if the client is still being opened */
	GSList *pending;

	/* Whether this is open or still waiting */
	guint open : 1;
} OpenClient;

/* Private part of the CompEditorFactory structure */
struct CompEditorFactoryPrivate {
	/* Hash table of URI->OpenClient */
	GHashTable *uri_client_hash;
};



static void comp_editor_factory_class_init (CompEditorFactoryClass *class);
static void comp_editor_factory_init (CompEditorFactory *factory);
static void comp_editor_factory_destroy (GtkObject *object);

static void impl_editExisting (PortableServer_Servant servant,
			       const CORBA_char *str_uri,
			       const GNOME_Evolution_Calendar_CalObjUID uid,
			       CORBA_Environment *ev);
static void impl_editNew (PortableServer_Servant servant,
			  const CORBA_char *str_uri,
			  const GNOME_Evolution_Calendar_CalObjType type,
			  CORBA_Environment *ev);

static BonoboXObjectClass *parent_class = NULL;



BONOBO_X_TYPE_FUNC_FULL (CompEditorFactory,
			 GNOME_Evolution_Calendar_CompEditorFactory,
			 BONOBO_X_OBJECT_TYPE,
			 comp_editor_factory);

/* Class initialization function for the component editor factory */
static void
comp_editor_factory_class_init (CompEditorFactoryClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (BONOBO_X_OBJECT_TYPE);

	class->epv.editExisting = impl_editExisting;
	class->epv.editNew = impl_editNew;

	object_class->destroy = comp_editor_factory_destroy;
}

/* Object initialization function for the component editor factory */
static void
comp_editor_factory_init (CompEditorFactory *factory)
{
	CompEditorFactoryPrivate *priv;

	priv = g_new (CompEditorFactoryPrivate, 1);
	factory->priv = priv;

	priv->uri_client_hash = g_hash_table_new (g_str_hash, g_str_equal);
}

/* Frees a Request structure */
static void
free_request (Request *r)
{
	if (r->type == REQUEST_EXISTING) {
		g_assert (r->u.existing.uid != NULL);
		g_free (r->u.existing.uid);
	}

	g_free (r);
}

/* Frees an OpenClient structure */
static void
free_client (OpenClient *oc)
{
	GSList *l;

	g_free (oc->uri);
	oc->uri = NULL;

	gtk_object_unref (GTK_OBJECT (oc->client));
	oc->client = NULL;

	for (l = oc->pending; l; l = l->next) {
		Request *r;

		r = l->data;
		free_request (r);
	}
	g_slist_free (oc->pending);
	oc->pending = NULL;

	g_free (oc);
}

/* Used from g_hash_table_foreach(); frees a client structure */
static void
free_client_cb (gpointer key, gpointer value, gpointer data)
{
	OpenClient *oc;

	oc = value;
	free_client (oc);
}

/* Destroy handler for the component editor factory */
static void
comp_editor_factory_destroy (GtkObject *object)
{
	CompEditorFactory *factory;
	CompEditorFactoryPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_COMP_EDITOR_FACTORY (object));

	factory = COMP_EDITOR_FACTORY (object);
	priv = factory->priv;

	g_hash_table_foreach (priv->uri_client_hash, free_client_cb, NULL);
	g_hash_table_destroy (priv->uri_client_hash);
	priv->uri_client_hash = NULL;

	g_free (priv);
	factory->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* Callback used when a component editor gets destroyed */
static void
editor_destroy_cb (GtkObject *object, gpointer data)
{
	OpenClient *oc;
	CompEditorFactory *factory;
	CompEditorFactoryPrivate *priv;

	oc = data;
	factory = oc->factory;
	priv = factory->priv;

	oc->editor_count--;

	/* See if we need to free the client */
	g_assert (oc->pending == NULL);

	if (oc->editor_count != 0)
		return;

	g_hash_table_remove (priv->uri_client_hash, oc->uri);
	free_client (oc);
}

/* Starts editing an existing component on a client that is already open */
static void
edit_existing (OpenClient *oc, const char *uid)
{
	CalComponent *comp;
	CalClientGetStatus status;
	CompEditor *editor;
	CalComponentVType vtype;

	g_assert (oc->open);

	/* Get the object */

	status = cal_client_get_object (oc->client, uid, &comp);

	switch (status) {
	case CAL_CLIENT_GET_SUCCESS:
		/* see below */
		break;

	case CAL_CLIENT_GET_NOT_FOUND:
		/* The object disappeared from the server */
		return;

	case CAL_CLIENT_GET_SYNTAX_ERROR:
		g_message ("edit_exiting(): Syntax error while getting component `%s'", uid);
		return;

	default:
		g_assert_not_reached ();
		return;
	}

	/* Create the appropriate type of editor */
	
	vtype = cal_component_get_vtype (comp);

	switch (vtype) {
	case CAL_COMPONENT_EVENT:
		editor = COMP_EDITOR (event_editor_new (oc->client));
		break;

	case CAL_COMPONENT_TODO:
		editor = COMP_EDITOR (task_editor_new (oc->client));
		break;

	default:
		g_message ("edit_exiting(): Unsupported object type %d", (int) vtype);
		gtk_object_unref (GTK_OBJECT (comp));
		return;
	}

	/* Set the object on the editor */
	comp_editor_edit_comp (editor, comp);
	comp_editor_focus (editor);

	oc->editor_count++;
	gtk_signal_connect (GTK_OBJECT (editor), "destroy",
			    GTK_SIGNAL_FUNC (editor_destroy_cb), oc);

	e_comp_editor_registry_add (comp_editor_registry, editor, TRUE);
}

/* Creates a component with the appropriate defaults for the specified component
 * type.
 */
static CalComponent *
get_default_event (gboolean all_day) 
{
	CalComponent *comp;
	struct icaltimetype itt;
	CalComponentDateTime dt;
	char *location;
	icaltimezone *zone;

	comp = cal_comp_event_new_with_defaults ();

	location = calendar_config_get_timezone ();
	zone = icaltimezone_get_builtin_timezone (location);

	if (all_day) {
		itt = icaltime_from_timet_with_zone (time (NULL), 1, zone);

		dt.value = &itt;
		dt.tzid = icaltimezone_get_tzid (zone);
		
		cal_component_set_dtstart (comp, &dt);
		cal_component_set_dtend (comp, &dt);		
	} else {
		itt = icaltime_current_time_with_zone (zone);
		icaltime_adjust (&itt, 0, 1, -itt.minute, -itt.second);
		
		dt.value = &itt;
		dt.tzid = icaltimezone_get_tzid (zone);
		
		cal_component_set_dtstart (comp, &dt);
		icaltime_adjust (&itt, 0, 1, 0, 0);
		cal_component_set_dtend (comp, &dt);
	}

	cal_component_commit_sequence (comp);

	return comp;
}

static CalComponent *
get_default_task (void)
{
	CalComponent *comp;
	
	comp = cal_component_new ();
	cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);

	return comp;
}

/* Edits a new object in the context of a client */
static void
edit_new (OpenClient *oc, const GNOME_Evolution_Calendar_CompEditorFactory_CompEditorMode type)
{
	CalComponent *comp;
	CompEditor *editor;
	
	switch (type) {
	case GNOME_Evolution_Calendar_CompEditorFactory_EDITOR_MODE_EVENT:
	case GNOME_Evolution_Calendar_CompEditorFactory_EDITOR_MODE_MEETING:
		editor = COMP_EDITOR (event_editor_new (oc->client));
		comp = get_default_event (FALSE);
		break;
	case GNOME_Evolution_Calendar_CompEditorFactory_EDITOR_MODE_ALLDAY_EVENT:
		editor = COMP_EDITOR (event_editor_new (oc->client));
		comp = get_default_event (TRUE);
		break;
	case GNOME_Evolution_Calendar_CompEditorFactory_EDITOR_MODE_TODO:
		editor = COMP_EDITOR (task_editor_new (oc->client));
		comp = get_default_task ();
		break;
	default:
		g_assert_not_reached ();
		return;
	}

	comp_editor_edit_comp (editor, comp);
	if (type == GNOME_Evolution_Calendar_CompEditorFactory_EDITOR_MODE_MEETING)
		event_editor_show_meeting (EVENT_EDITOR (editor));
	comp_editor_focus (editor);

	oc->editor_count++;
	gtk_signal_connect (GTK_OBJECT (editor), "destroy",
			    GTK_SIGNAL_FUNC (editor_destroy_cb), oc);

	e_comp_editor_registry_add (comp_editor_registry, editor, TRUE);
}

/* Resolves all the pending requests for a client */
static void
resolve_pending_requests (OpenClient *oc)
{
	CompEditorFactory *factory;
	CompEditorFactoryPrivate *priv;
	GSList *l;
	char *location;
	icaltimezone *zone;

	factory = oc->factory;
	priv = factory->priv;

	g_assert (oc->pending != NULL);

	/* Set the default timezone in the backend. */
	location = calendar_config_get_timezone ();
	zone = icaltimezone_get_builtin_timezone (location);
	if (zone)
		cal_client_set_default_timezone (oc->client, zone);

	for (l = oc->pending; l; l = l->next) {
		Request *request;

		request = l->data;

		switch (request->type) {
		case REQUEST_EXISTING:
			edit_existing (oc, request->u.existing.uid);
			break;

		case REQUEST_NEW:
			edit_new (oc, request->u.new.type);
			break;
		}

		free_request (request);
	}

	g_slist_free (oc->pending);
	oc->pending = NULL;
}

/* Callback used when a client is finished opening.  We resolve all the pending
 * requests.
 */
static void
cal_opened_cb (CalClient *client, CalClientOpenStatus status, gpointer data)
{
	OpenClient *oc;
	CompEditorFactory *factory;
	CompEditorFactoryPrivate *priv;

	oc = data;
	factory = oc->factory;
	priv = factory->priv;

	switch (status) {
	case CAL_CLIENT_OPEN_SUCCESS:
		oc->open = TRUE;
		resolve_pending_requests (oc);
		return;

	case CAL_CLIENT_OPEN_ERROR:
		g_message ("cal_opened_cb(): Error while opening the calendar");
		break;

	case CAL_CLIENT_OPEN_NOT_FOUND:
		/* bullshit; we specified only_if_exists = FALSE */
		g_assert_not_reached ();
		return;

	case CAL_CLIENT_OPEN_METHOD_NOT_SUPPORTED:
		g_message ("cal_opened_cb(): Method not supported when opening the calendar");
		break;

	default:
		g_assert_not_reached ();
		return;
	}

	g_hash_table_remove (priv->uri_client_hash, oc->uri);
	free_client (oc);
}

/* Creates a new OpenClient structure and queues the component editing/creation
 * process until the client is open.  Returns NULL if it could not issue the
 * open request.
 */
static OpenClient *
open_client (CompEditorFactory *factory, const char *uristr)
{
	CompEditorFactoryPrivate *priv;
	CalClient *client;
	OpenClient *oc;

	priv = factory->priv;

	client = cal_client_new ();
	if (!client)
		return NULL;

	oc = g_new (OpenClient, 1);
	oc->factory = factory;

	oc->uri = g_strdup (uristr);

	oc->client = client;
	oc->editor_count = 0;
	oc->pending = NULL;
	oc->open = FALSE;

	gtk_signal_connect (GTK_OBJECT (oc->client), "cal_opened",
			    GTK_SIGNAL_FUNC (cal_opened_cb), oc);

	if (!cal_client_open_calendar (oc->client, uristr, FALSE)) {
		g_free (oc->uri);
		gtk_object_unref (GTK_OBJECT (oc->client));
		g_free (oc);

		return NULL;
	}

	g_hash_table_insert (priv->uri_client_hash, oc->uri, oc);

	return oc;
}

/* Looks up an open client or queues it for being opened.  Returns the client or
 * NULL on failure; in the latter case it sets the ev exception.
 */
static OpenClient *
lookup_open_client (CompEditorFactory *factory, const char *str_uri, CORBA_Environment *ev)
{
	CompEditorFactoryPrivate *priv;
	OpenClient *oc;
	EUri *uri;

	priv = factory->priv;

	/* Look up the client */

	uri = e_uri_new (str_uri);
	if (!uri) {
		bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CompEditorFactory_InvalidURI);
		return NULL;
	}
	e_uri_free (uri);

	oc = g_hash_table_lookup (priv->uri_client_hash, str_uri);
	if (!oc) {
		oc = open_client (factory, str_uri);
		if (!oc) {
			bonobo_exception_set (ev, ex_GNOME_Evolution_Calendar_CompEditorFactory_BackendContactError);
			return NULL;
		}
	}

	return oc;
}

/* Queues a request for editing an existing object */
static void
queue_edit_existing (OpenClient *oc, const char *uid)
{
	Request *request;

	g_assert (!oc->open);

	request = g_new (Request, 1);
	request->type = REQUEST_EXISTING;
	request->u.existing.uid = g_strdup (uid);

	oc->pending = g_slist_append (oc->pending, request);
}

/* ::editExisting() method implementation */
static void
impl_editExisting (PortableServer_Servant servant,
		   const CORBA_char *str_uri,
		   const GNOME_Evolution_Calendar_CalObjUID uid,
		   CORBA_Environment *ev)
{
	CompEditorFactory *factory;
	CompEditorFactoryPrivate *priv;
	OpenClient *oc;
	CompEditor *editor;

	factory = COMP_EDITOR_FACTORY (bonobo_object_from_servant (servant));
	priv = factory->priv;

	oc = lookup_open_client (factory, str_uri, ev);
	if (!oc)
		return;

	if (!oc->open) {
		queue_edit_existing (oc, uid);
		return;
	}

	/* Look up the component */
	editor = e_comp_editor_registry_find (comp_editor_registry, uid);	
	if (editor == NULL) {
		edit_existing (oc, uid);
	} else {
		comp_editor_focus (editor);
	}
}

/* Queues a request for creating a new object */
static void
queue_edit_new (OpenClient *oc, const GNOME_Evolution_Calendar_CompEditorFactory_CompEditorMode type)
{
	Request *request;

	g_assert (!oc->open);

	request = g_new (Request, 1);
	request->type = REQUEST_NEW;
	request->u.new.type = type;

	oc->pending = g_slist_append (oc->pending, request);
}

/* ::editNew() method implementation */
static void
impl_editNew (PortableServer_Servant servant,
	      const CORBA_char *str_uri,
	      const GNOME_Evolution_Calendar_CompEditorFactory_CompEditorMode corba_type,
	      CORBA_Environment *ev)
{
	CompEditorFactory *factory;
	CompEditorFactoryPrivate *priv;
	OpenClient *oc;
 
	factory = COMP_EDITOR_FACTORY (bonobo_object_from_servant (servant));
	priv = factory->priv;

	oc = lookup_open_client (factory, str_uri, ev);
	if (!oc)
		return;

	if (!oc->open)
		queue_edit_new (oc, corba_type);
	else
		edit_new (oc, corba_type);
}



/**
 * comp_editor_factory_new:
 * 
 * Creates a new calendar component editor factory.
 * 
 * Return value: A newly-created component editor factory.
 **/
CompEditorFactory *
comp_editor_factory_new (void)
{
	return gtk_type_new (TYPE_COMP_EDITOR_FACTORY);
}


