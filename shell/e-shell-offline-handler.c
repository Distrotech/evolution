/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell-offline-handler.c
 *
 * Copyright (C) 2001  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtktypeutils.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkclist.h>

#include <gal/util/e-util.h>

#include <libgnomeui/gnome-dialog.h>
#include <libgnome/gnome-i18n.h>

#include <glade/glade-xml.h>

#include <bonobo/bonobo-main.h>

#include "e-shell-offline-handler.h"


#define GLADE_DIALOG_FILE_NAME EVOLUTION_GLADEDIR "/e-active-connection-dialog.glade"


#define PARENT_TYPE GTK_TYPE_OBJECT
static GtkObjectClass *parent_class = NULL;


/* Private part.  */

struct _OfflineProgressListenerServant {
	POA_GNOME_Evolution_OfflineProgressListener servant;
	EShellOfflineHandler *offline_handler;
	char *component_id;
};
typedef struct _OfflineProgressListenerServant OfflineProgressListenerServant;

struct _ComponentInfo {
	/* Component ID.  */
	char *id;

	/* The `Evolution::Offline' interface for this component (cached just
	   to avoid going through the EComponentRegistry all the time).  */
	GNOME_Evolution_Offline offline_interface;

	/* The interface and servant for the
 	   `Evolution::OfflineProgressListener' we have to implement to get
 	   notifications about progress of the off-line process.  */
	GNOME_Evolution_OfflineProgressListener progress_listener_interface;
	OfflineProgressListenerServant *progress_listener_servant;

	/* The current active connections for this component.  This is updated
	   by the component itself through the `::ProgressListener' interface;
	   when the count reaches zero, the off-line process is considered to
	   be complete.  */
	GNOME_Evolution_ConnectionList *active_connection_list;
};
typedef struct _ComponentInfo ComponentInfo;

struct _EShellOfflineHandlerPrivate {
	EComponentRegistry *component_registry;

	EShellView *parent_shell_view;

	GladeXML *dialog_gui;

	int num_total_connections;
	GHashTable *id_to_component_info;

	int procedure_in_progress : 1;
	int finished : 1;
};


/* Signals.   */

enum {
	OFFLINE_PROCEDURE_STARTED,
	OFFLINE_PROCEDURE_FINISHED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


/* Forward declarations for the dialog handling.  */

static void update_dialog_clist (EShellOfflineHandler *offline_handler);


/* Implementation for the OfflineProgressListener interface.  */

static PortableServer_ServantBase__epv OfflineProgressListener_base_epv;
static POA_GNOME_Evolution_OfflineProgressListener__epv OfflineProgressListener_epv;
static POA_GNOME_Evolution_OfflineProgressListener__vepv OfflineProgressListener_vepv;

static OfflineProgressListenerServant *
progress_listener_servant_new (EShellOfflineHandler *offline_handler,
			       const char *id)
{
	OfflineProgressListenerServant *servant;

	servant = g_new0 (OfflineProgressListenerServant, 1);

	servant->servant.vepv    = &OfflineProgressListener_vepv;
	servant->offline_handler = offline_handler;
	servant->component_id    = g_strdup (id);

	return servant;
}

static void
progress_listener_servant_free (OfflineProgressListenerServant *servant)
{
	CORBA_Environment ev;
	PortableServer_ObjectId *oid;

	CORBA_exception_init (&ev);

	oid = PortableServer_POA_servant_to_id (bonobo_poa (), servant, &ev);
	PortableServer_POA_deactivate_object (bonobo_poa (), oid, &ev);
	CORBA_free (oid);

	POA_GNOME_Evolution_OfflineProgressListener__fini ((POA_GNOME_Evolution_OfflineProgressListener *) servant, &ev);

	CORBA_exception_free (&ev);

	g_free (servant->component_id);
	g_free (servant);
}

static GNOME_Evolution_ConnectionList *
duplicate_connection_list (const GNOME_Evolution_ConnectionList *source)
{
	GNOME_Evolution_ConnectionList *copy;
	int i;

	copy = GNOME_Evolution_ConnectionList__alloc ();

	copy->_length  = source->_length;
	copy->_maximum = source->_length;

	copy->_buffer  = CORBA_sequence_GNOME_Evolution_Connection_allocbuf (copy->_maximum);

	for (i = 0; i < source->_length; i++) {
		copy->_buffer[i].hostName = CORBA_string_dup (source->_buffer[i].hostName);
		copy->_buffer[i].type     = CORBA_string_dup (source->_buffer[i].type);
	}

	return copy;
}

static void
impl_OfflineProgressListener_updateProgress (PortableServer_Servant servant,
					     const GNOME_Evolution_ConnectionList *current_active_connections,
					     CORBA_Environment *ev)
{
	EShellOfflineHandler *offline_handler;
	EShellOfflineHandlerPrivate *priv;
	ComponentInfo *component_info;
	int connection_delta;
	const char *component_id;

	component_id = ((OfflineProgressListenerServant *) servant)->component_id;

	offline_handler = ((OfflineProgressListenerServant *) servant)->offline_handler;
	priv = offline_handler->priv;

	component_info = g_hash_table_lookup (priv->id_to_component_info, component_id);
	g_assert (component_info != NULL);

	connection_delta = component_info->active_connection_list->_length - current_active_connections->_length;
	if (connection_delta < 0) {
		/* FIXME: Should raise an exception or something?  */
		g_warning ("Weird, buggy component increased number of connection when going off-line -- %s",
			   component_id);
	}

	g_assert (priv->num_total_connections >= connection_delta);
	priv->num_total_connections -= connection_delta;

	CORBA_free (component_info->active_connection_list);
	component_info->active_connection_list = duplicate_connection_list (current_active_connections);

	update_dialog_clist (offline_handler);

	if (priv->num_total_connections == 0 && ! priv->finished) {
		gtk_signal_emit (GTK_OBJECT (offline_handler), signals[OFFLINE_PROCEDURE_FINISHED], TRUE);
		priv->finished = TRUE;
	}
}

static gboolean
create_progress_listener (EShellOfflineHandler *offline_handler,
			  const char *component_id,
			  GNOME_Evolution_OfflineProgressListener *objref_return,
			  OfflineProgressListenerServant **servant_return)
{
	OfflineProgressListenerServant *servant;
	CORBA_Environment ev;

	*servant_return = NULL;
	*objref_return  = CORBA_OBJECT_NIL;

	OfflineProgressListener_base_epv._private    = NULL;
	OfflineProgressListener_base_epv.finalize    = NULL;
	OfflineProgressListener_base_epv.default_POA = NULL;

	OfflineProgressListener_epv.updateProgress = impl_OfflineProgressListener_updateProgress;

	OfflineProgressListener_vepv._base_epv                                   = &OfflineProgressListener_base_epv;
	OfflineProgressListener_vepv.GNOME_Evolution_OfflineProgressListener_epv = &OfflineProgressListener_epv;

	servant = progress_listener_servant_new (offline_handler, component_id);

	CORBA_exception_init (&ev);

	POA_GNOME_Evolution_OfflineProgressListener__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("Cannot initialize GNOME::Evolution::Offline::ProgressListener");
		progress_listener_servant_free (servant);
		CORBA_exception_free (&ev);
		return FALSE;
	}

	CORBA_free (PortableServer_POA_activate_object (bonobo_poa (), servant, &ev));

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("Cannot activate GNOME::Evolution::Offline::ProgressListener");
		progress_listener_servant_free (servant);
		CORBA_exception_free (&ev);
		return FALSE;
	}

	*servant_return = servant;
	*objref_return  = PortableServer_POA_servant_to_reference (bonobo_poa (), servant, &ev);

	CORBA_exception_free (&ev);

	return TRUE;
}


/* ComponentInfo handling.  */

static ComponentInfo *
component_info_new (const char *id,
		    const GNOME_Evolution_Offline offline_interface,
		    GNOME_Evolution_OfflineProgressListener progress_listener_interface,
		    OfflineProgressListenerServant *progress_listener_servant,
		    GNOME_Evolution_ConnectionList *active_connection_list)
{
	ComponentInfo *new;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	new = g_new (ComponentInfo, 1);
	new->id                          = g_strdup (id);
	new->offline_interface           = CORBA_Object_duplicate (offline_interface, &ev);
	new->progress_listener_interface = progress_listener_interface;
	new->progress_listener_servant   = progress_listener_servant;
	new->active_connection_list      = active_connection_list;

	CORBA_exception_free (&ev);

	return new;
}

static void
component_info_free (ComponentInfo *component_info)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	g_free (component_info->id);

	progress_listener_servant_free (component_info->progress_listener_servant);
	CORBA_Object_release (component_info->progress_listener_interface, &ev);

	CORBA_Object_release (component_info->offline_interface, &ev);

	CORBA_free (component_info->active_connection_list);

	g_free (component_info);

	CORBA_exception_free (&ev);
}


/* Utility functions.  */

static void
hash_foreach_free_component_info (void *key,
				   void *value,
				   void *user_data)
{
	ComponentInfo *component_info;

	component_info = (ComponentInfo *) value;
	component_info_free (component_info);
}


/* Cancelling the off-line procedure.  */

static void
cancel_offline (EShellOfflineHandler *offline_handler)
{
	EShellOfflineHandlerPrivate *priv;
	GList *component_ids;
	GList *p;

	priv = offline_handler->priv;

	component_ids = e_component_registry_get_id_list (priv->component_registry);

	for (p = component_ids; p != NULL; p = p->next) {
		EvolutionShellComponentClient *shell_component_client;
		GNOME_Evolution_Offline offline_interface;
		CORBA_Environment ev;
		const char *id;

		id = (const char *) p->data;
		shell_component_client = e_component_registry_get_component_by_id (priv->component_registry, id);

		offline_interface = evolution_shell_component_client_get_offline_interface (shell_component_client);
		if (offline_interface == CORBA_OBJECT_NIL)
			continue;

		CORBA_exception_init (&ev);

		GNOME_Evolution_Offline_goOnline (offline_interface, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("Error putting component `%s' on-line.", id);

		CORBA_exception_free (&ev);
	}

	e_free_string_list (component_ids);

	priv->num_total_connections = 0;

	if (! priv->finished) {
		gtk_signal_emit (GTK_OBJECT (offline_handler), signals[OFFLINE_PROCEDURE_FINISHED], FALSE);
		priv->finished = TRUE;
	}
}


/* Preparing the off-line procedure.  */

static gboolean
prepare_for_offline (EShellOfflineHandler *offline_handler)
{
	EShellOfflineHandlerPrivate *priv;
	GList *component_ids;
	GList *p;
	gboolean error;

	priv = offline_handler->priv;

	component_ids = e_component_registry_get_id_list (priv->component_registry);

	error = FALSE;
	for (p = component_ids; p != NULL; p = p->next) {
		EvolutionShellComponentClient *shell_component_client;
		GNOME_Evolution_Offline offline_interface;
		GNOME_Evolution_OfflineProgressListener progress_listener_interface;
		GNOME_Evolution_ConnectionList *active_connection_list;
		OfflineProgressListenerServant *progress_listener_servant;
		ComponentInfo *component_info;
		CORBA_Environment ev;
		const char *id;

		id = (const char *) p->data;
		shell_component_client = e_component_registry_get_component_by_id (priv->component_registry, id);
		offline_interface = evolution_shell_component_client_get_offline_interface (shell_component_client);
		if (offline_interface == CORBA_OBJECT_NIL)
			continue;

		if (! create_progress_listener (offline_handler, id,
						&progress_listener_interface,
						&progress_listener_servant)) {
			g_warning ("Cannot create the Evolution::OfflineProgressListener interface for `%s'", id);
			continue;
		}

		CORBA_exception_init (&ev);

		GNOME_Evolution_Offline_prepareForOffline (offline_interface, &active_connection_list, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("Cannot prepare component component to go offline -- %s [%s]", id, ev._repo_id);

			progress_listener_servant_free (progress_listener_servant);
			
			CORBA_Object_release (progress_listener_interface, &ev);

			CORBA_exception_free (&ev);

			error = TRUE;
			break;
		}

		CORBA_exception_free (&ev);

		priv->num_total_connections += active_connection_list->_length;

		component_info = component_info_new (id,
						     offline_interface,
						     progress_listener_interface,
						     progress_listener_servant,
						     active_connection_list);

		g_assert (g_hash_table_lookup (priv->id_to_component_info, component_info->id) == NULL);
		g_hash_table_insert (priv->id_to_component_info, component_info->id, component_info);
	}

	/* If an error occurred while preparing, just put all the components
	   on-line again.  */
	if (error)
		cancel_offline (offline_handler);

	e_free_string_list (component_ids);

	return ! error;
}


/* Finalizing the off-line procedure.  */

static void
finalize_offline_hash_foreach (void *key,
			       void *value,
			       void *user_data)
{
	EShellOfflineHandler *offline_handler;
	EShellOfflineHandlerPrivate *priv;
	ComponentInfo *component_info;
	CORBA_Environment ev;

	offline_handler = E_SHELL_OFFLINE_HANDLER (user_data);
	priv = offline_handler->priv;

	component_info = (ComponentInfo *) value;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Offline_goOffline (component_info->offline_interface,
					   component_info->progress_listener_interface,
					   &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		/* FIXME: Should detect an error and put all the components
		   on-line again.  */
		g_warning ("Error putting component off-line -- %s", component_info->id);
	}

	CORBA_exception_free (&ev);
}

static void
finalize_offline (EShellOfflineHandler *offline_handler)
{
	EShellOfflineHandlerPrivate *priv;

	priv = offline_handler->priv;

	gtk_object_ref (GTK_OBJECT (offline_handler));

	g_hash_table_foreach (priv->id_to_component_info, finalize_offline_hash_foreach, offline_handler);

	if (priv->num_total_connections == 0 && ! priv->finished) {
		/* Nothing else to do, we are all set.  */
		gtk_signal_emit (GTK_OBJECT (offline_handler), signals[OFFLINE_PROCEDURE_FINISHED], TRUE);
		priv->finished = TRUE;
	}

	gtk_object_unref (GTK_OBJECT (offline_handler));
}


/* The confirmation dialog.  */

static void
update_dialog_clist_hash_foreach (void *key,
				  void *data,
				  void *user_data)
{
	ComponentInfo *component_info;
	const GNOME_Evolution_Connection *p;
	GtkWidget *clist;
	int i;

	clist = GTK_WIDGET (user_data);

	component_info = (ComponentInfo *) data;
	for (i = 0, p = component_info->active_connection_list->_buffer;
	     i < component_info->active_connection_list->_length;
	     i++, p++) {
		char *columns[3];

		columns[0] = p->hostName;
		columns[1] = p->type;
		columns[2] = NULL;

		gtk_clist_prepend (GTK_CLIST (clist), columns);
	}
}

static void
update_dialog_clist (EShellOfflineHandler *offline_handler)
{
	EShellOfflineHandlerPrivate *priv;
	GtkWidget *clist;

	priv = offline_handler->priv;
	if (priv->dialog_gui == NULL)
		return;

	clist = glade_xml_get_widget (priv->dialog_gui, "active_connection_clist");

	gtk_clist_set_auto_sort (GTK_CLIST (clist), TRUE);

	gtk_clist_freeze (GTK_CLIST (clist));

	/* Populate the GtkCList.  */
	gtk_clist_clear (GTK_CLIST (clist));
	g_hash_table_foreach (priv->id_to_component_info, update_dialog_clist_hash_foreach, clist);

	gtk_clist_thaw (GTK_CLIST (clist));
}

static void
dialog_handle_ok (GnomeDialog *dialog,
		  EShellOfflineHandler *offline_handler)
{
	EShellOfflineHandlerPrivate *priv;
	GtkWidget *instruction_label;

	priv = offline_handler->priv;

	gnome_dialog_set_sensitive (dialog, 0, FALSE);

	instruction_label = glade_xml_get_widget (priv->dialog_gui, "instruction_label");
	g_assert (instruction_label != NULL);
	g_assert (GTK_IS_LABEL (instruction_label));

	gtk_label_set_text (GTK_LABEL (instruction_label), _("Closing connections..."));

	finalize_offline (offline_handler);
}

static void
dialog_handle_cancel (GnomeDialog *dialog,
		      EShellOfflineHandler *offline_handler)
{
	EShellOfflineHandlerPrivate *priv;

	priv = offline_handler->priv;

	gtk_widget_destroy (GTK_WIDGET (dialog));

	gtk_object_unref (GTK_OBJECT (priv->dialog_gui));
	priv->dialog_gui = NULL;

	cancel_offline (offline_handler);
}

static void
dialog_clicked_cb (GnomeDialog *dialog,
		   int button_number,
		   void *data)
{
	EShellOfflineHandler *offline_handler;

	offline_handler = E_SHELL_OFFLINE_HANDLER (data);

	switch (button_number) {
	case 0:			/* OK */
		dialog_handle_ok (dialog, offline_handler);
		break;

	case 1:			/* Cancel */
		dialog_handle_cancel (dialog, offline_handler);
		break;

	default:
		g_assert_not_reached ();
	}
}

static void
pop_up_confirmation_dialog (EShellOfflineHandler *offline_handler)
{
	EShellOfflineHandlerPrivate *priv;
	GtkWidget *dialog;

	priv = offline_handler->priv;

	if (priv->dialog_gui == NULL) {
		priv->dialog_gui = glade_xml_new (GLADE_DIALOG_FILE_NAME, NULL);
		if (priv->dialog_gui == NULL) {
			g_warning ("Cannot load the active connection dialog (installation problem?) -- %s",
				   GLADE_DIALOG_FILE_NAME);
			finalize_offline (offline_handler);
			return;
		}
	}

	dialog = glade_xml_get_widget (priv->dialog_gui, "active_connection_dialog");

	/* FIXME: do we really want this?  */
	/* gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (priv->parent_shell_view)); */
	/* gtk_window_set_modal (GTK_WINDOW (dialog), TRUE); */

	gnome_dialog_set_default (GNOME_DIALOG (dialog), 1);

	update_dialog_clist (offline_handler);

	gtk_signal_connect (GTK_OBJECT (dialog), "clicked",
			    GTK_SIGNAL_FUNC (dialog_clicked_cb), offline_handler);

	gtk_widget_show (dialog);
}


/* GtkObject methods.  */

static void
impl_destroy (GtkObject *object)
{
	EShellOfflineHandler *offline_handler;
	EShellOfflineHandlerPrivate *priv;

	offline_handler = E_SHELL_OFFLINE_HANDLER (object);
	priv = offline_handler->priv;

	if (priv->component_registry != NULL)
		gtk_object_unref (GTK_OBJECT (priv->component_registry));

	g_hash_table_foreach (priv->id_to_component_info, hash_foreach_free_component_info, NULL);
	g_hash_table_destroy (priv->id_to_component_info);

	if (priv->dialog_gui != NULL) {
		GtkWidget *dialog;

		dialog = glade_xml_get_widget (priv->dialog_gui, "active_connection_dialog");
		gtk_widget_destroy (dialog);

		gtk_object_unref (GTK_OBJECT (priv->dialog_gui));
		priv->dialog_gui = NULL;
	}

	g_free (priv);

	if (GTK_OBJECT_CLASS (parent_class)->destroy != NULL)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* GTK type handling.  */

static void
class_init (EShellOfflineHandlerClass *klass)
{
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = impl_destroy;

	parent_class = gtk_type_class (gtk_object_get_type ());

	signals[OFFLINE_PROCEDURE_STARTED]
		= gtk_signal_new ("offline_procedure_started",
				  GTK_RUN_LAST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EShellOfflineHandlerClass, offline_procedure_started),
				  gtk_marshal_NONE__NONE,
				  GTK_TYPE_NONE, 0);

	signals[OFFLINE_PROCEDURE_FINISHED]
		= gtk_signal_new ("offline_procedure_finished",
				  GTK_RUN_LAST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EShellOfflineHandlerClass, offline_procedure_finished),
				  gtk_marshal_NONE__BOOL,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_BOOL);

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);
}


static void
init (EShellOfflineHandler *shell_offline_handler)
{
	EShellOfflineHandlerPrivate *priv;

	priv = g_new (EShellOfflineHandlerPrivate, 1);

	priv->component_registry              = NULL;
	priv->parent_shell_view               = NULL;

	priv->dialog_gui                      = NULL;

	priv->num_total_connections           = 0;
	priv->id_to_component_info            = g_hash_table_new (g_str_hash, g_str_equal);

	priv->procedure_in_progress           = FALSE;
	priv->finished                        = FALSE;

	shell_offline_handler->priv = priv;
}


/**
 * e_shell_offline_handler_construct:
 * @offline_handler: A pointer to an EShellOfflineHandler to construct.
 * @component_registry: The registry for the components that we want to put
 * off-line.
 * 
 * Construct the @offline_handler.
 **/
void
e_shell_offline_handler_construct (EShellOfflineHandler *offline_handler,
				   EComponentRegistry *component_registry)
{
	EShellOfflineHandlerPrivate *priv;

	g_return_if_fail (offline_handler != NULL);
	g_return_if_fail (E_IS_SHELL_OFFLINE_HANDLER (offline_handler));
	g_return_if_fail (component_registry != NULL);
	g_return_if_fail (E_IS_COMPONENT_REGISTRY (component_registry));

	priv = offline_handler->priv;

	g_assert (priv->component_registry == NULL);

	GTK_OBJECT_UNSET_FLAGS (GTK_OBJECT (offline_handler), GTK_FLOATING);

	gtk_object_ref (GTK_OBJECT (component_registry));
	priv->component_registry = component_registry;
}

/**
 * e_shell_offline_handler_new:
 * @component_registry: The registry for the components that we want to put
 * off-line.
 * 
 * Create a new offline handler.
 * 
 * Return value: A pointer to the newly created EShellOfflineHandler object.
 **/
EShellOfflineHandler *
e_shell_offline_handler_new (EComponentRegistry *component_registry)
{
	EShellOfflineHandler *offline_handler;

	g_return_val_if_fail (component_registry != NULL, NULL);
	g_return_val_if_fail (E_IS_COMPONENT_REGISTRY (component_registry), NULL);

	offline_handler = (EShellOfflineHandler *) gtk_type_new (e_shell_offline_handler_get_type ());
	e_shell_offline_handler_construct (offline_handler, component_registry);

	return offline_handler;
}


/**
 * e_shell_offline_handler_put_components_offline:
 * @offline_handler: A pointer to an EShellOfflineHandler object.
 * 
 * Put the components offline.
 **/
void
e_shell_offline_handler_put_components_offline (EShellOfflineHandler *offline_handler,
						EShellView *parent_shell_view)
{
	EShellOfflineHandlerPrivate *priv;

	g_return_if_fail (offline_handler != NULL);
	g_return_if_fail (E_IS_SHELL_OFFLINE_HANDLER (offline_handler));
	g_return_if_fail (parent_shell_view == NULL || E_IS_SHELL_VIEW (parent_shell_view));

	priv = offline_handler->priv;

	priv->procedure_in_progress = TRUE;
	priv->parent_shell_view = parent_shell_view;

	/* Add an extra ref here as the signal handlers might want to unref
	   us.  */

	gtk_object_ref (GTK_OBJECT (offline_handler));

	gtk_signal_emit (GTK_OBJECT (offline_handler), signals[OFFLINE_PROCEDURE_STARTED]);

	priv->finished = FALSE;

	if (! prepare_for_offline (offline_handler)) {
		/* FIXME: Maybe do something smarter here.  */
		g_warning ("Couldn't put components off-line");
		gtk_signal_emit (GTK_OBJECT (offline_handler), signals[OFFLINE_PROCEDURE_FINISHED], FALSE);
		priv->finished = TRUE;
		gtk_object_unref (GTK_OBJECT (offline_handler));
		return;
	}

	if (priv->num_total_connections > 0 && priv->parent_shell_view != NULL)
		pop_up_confirmation_dialog (offline_handler);
	else
		finalize_offline (offline_handler);

	gtk_object_unref (GTK_OBJECT (offline_handler));
}


E_MAKE_TYPE (e_shell_offline_handler, "EShellOfflineHandler", EShellOfflineHandler, class_init, init, PARENT_TYPE)
