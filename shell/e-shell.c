/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
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
 * Author: Ettore Perazzoli
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gdk/gdkx.h>

#include <X11/Xatom.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>

/* (For the displayName stuff.)  */
#include <gdk/gdkprivate.h>
#include <X11/Xlib.h>

#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>

#include <gal/widgets/e-gui-utils.h>
#include <gal/util/e-util.h>

#include "Evolution.h"

#include "e-util/e-dialog-utils.h"

#include "e-activity-handler.h"
#include "e-component-registry.h"
#include "e-corba-shortcuts.h"
#include "e-corba-storage-registry.h"
#include "e-folder-type-registry.h"
#include "e-local-storage.h"
#include "e-shell-constants.h"
#include "e-shell-folder-selection-dialog.h"
#include "e-shell-offline-handler.h"
#include "e-shell-startup-wizard.h"
#include "e-shell-view.h"
#include "e-shortcuts.h"
#include "e-storage-set.h"
#include "e-splash.h"
#include "e-summary-storage.h"
#include "e-uri-schema-registry.h"

#include "evolution-storage-set-view-factory.h"

#include "e-shell.h"

#include "importer/intelligent.h"


#define PARENT_TYPE bonobo_x_object_get_type ()
static BonoboXObjectClass *parent_class = NULL;

struct _EShellPrivate {
	/* IID for registering the object on OAF.  */
	char *iid;

	char *local_directory;

	GList *views;

	EStorageSet *storage_set;
	ELocalStorage *local_storage;
	ESummaryStorage *summary_storage;

	EShortcuts *shortcuts;
	EFolderTypeRegistry *folder_type_registry;
	EUriSchemaRegistry *uri_schema_registry;

	EComponentRegistry *component_registry;

	EShellUserCreatableItemsHandler *user_creatable_items_handler;

	/* ::StorageRegistry interface handler.  */
	ECorbaStorageRegistry *corba_storage_registry; /* <aggregate> */

	/* ::Activity interface handler.  */
	EActivityHandler *activity_handler; /* <aggregate> */

	/* ::Shortcuts interface handler.  */
	ECorbaShortcuts *corba_shortcuts; /* <aggregate> */

	/* This object handles going off-line.  If the pointer is not NULL, it
	   means we have a going-off-line process in progress.  */
	EShellOfflineHandler *offline_handler;

	/* Names for the types of the folders that have maybe crashed.  */
	GList *crash_type_names; /* char * */

	/* Line status.  */
	EShellLineStatus line_status;

	/* Configuration Database */
	Bonobo_ConfigDatabase db;

	/* Whether the shell is succesfully initialized.  This is needed during
	   the start-up sequence, to avoid CORBA calls to do make wrong things
	   to happen while the shell is initializing.  */
	unsigned int is_initialized : 1;

	/* Wether the shell is working in "interactive" mode or not.
	   (Currently, it's interactive IIF there is at least one active
	   view.)  */
	unsigned int is_interactive : 1;
};


/* Constants.  */

/* FIXME: We need a component repository instead.  */

#define SHORTCUTS_FILE_NAME     "shortcuts.xml"
#define LOCAL_STORAGE_DIRECTORY "local"


enum {
	NO_VIEWS_LEFT,
	LINE_STATUS_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


/* Interactivity handling.  */

static void
set_interactive (EShell *shell,
		 gboolean interactive)
{
	EShellPrivate *priv;
	GList *id_list, *p;

	priv = shell->priv;

	if (!! priv->is_interactive == !! interactive)
		return;

	priv->is_interactive = interactive;

	id_list = e_component_registry_get_id_list (priv->component_registry);
	for (p = id_list; p != NULL; p = p->next) {
		EvolutionShellComponentClient *shell_component_client;
		GNOME_Evolution_ShellComponent shell_component_objref;
		const char *id;
		CORBA_Environment ev;

		id = (const char *) p->data;
		shell_component_client = e_component_registry_get_component_by_id (priv->component_registry, id);
		shell_component_objref = bonobo_object_corba_objref (BONOBO_OBJECT (shell_component_client));

		CORBA_exception_init (&ev);

		GNOME_Evolution_ShellComponent_interactive (shell_component_objref, interactive, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("Error changing interactive status of component %s to %s -- %s\n",
				   id, interactive ? "TRUE" : "FALSE", ev._repo_id);

		CORBA_exception_free (&ev);
	}

	e_free_string_list (id_list);
}


/* Callback for the folder selection dialog.  */

static void
folder_selection_dialog_cancelled_cb (EShellFolderSelectionDialog *folder_selection_dialog,
				      void *data)
{
	GNOME_Evolution_FolderSelectionListener listener;
	CORBA_Environment ev;

	listener = gtk_object_get_data (GTK_OBJECT (folder_selection_dialog), "corba_listener");

	CORBA_exception_init (&ev);

	GNOME_Evolution_FolderSelectionListener_notifyCanceled (listener, &ev);

	CORBA_exception_free (&ev);

	gtk_widget_destroy (GTK_WIDGET (folder_selection_dialog));
}

static void
folder_selection_dialog_folder_selected_cb (EShellFolderSelectionDialog *folder_selection_dialog,
					    const char *path,
					    void *data)
{
	CORBA_Environment ev;
	EShell *shell;
	GNOME_Evolution_FolderSelectionListener listener;
	EStorageSet *storage_set;
	EFolder *folder;
	char *uri;
	const char *physical_uri;

	shell = E_SHELL (data);
	listener = gtk_object_get_data (GTK_OBJECT (folder_selection_dialog), "corba_listener");

	CORBA_exception_init (&ev);

	storage_set = e_shell_get_storage_set (shell);
	folder = e_storage_set_get_folder (storage_set, path);

	uri = g_strconcat (E_SHELL_URI_PREFIX, path, NULL);

	if (folder == NULL)
		physical_uri = "";
	else
		physical_uri = e_folder_get_physical_uri (folder);

	GNOME_Evolution_FolderSelectionListener_notifySelected (listener, uri, physical_uri, &ev);
	g_free (uri);

	CORBA_exception_free (&ev);

	gtk_widget_destroy (GTK_WIDGET (folder_selection_dialog));
}


/* CORBA interface implementation.  */

static gboolean
raise_exception_if_not_ready (PortableServer_Servant servant,
			      CORBA_Environment *ev)
{
	EShell *shell;

	shell = E_SHELL (bonobo_object_from_servant (servant));

	if (! shell->priv->is_initialized) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_NotReady, NULL);
		return TRUE;
	}

	return FALSE;
}

static CORBA_char *
impl_Shell__get_displayName (PortableServer_Servant servant,
			     CORBA_Environment *ev)
{
	char *display_string;
	CORBA_char *retval;

	if (raise_exception_if_not_ready (servant, ev))
		return NULL;

	display_string = DisplayString (gdk_display);
	if (display_string == NULL) 
		return CORBA_string_dup ("");

	retval = CORBA_string_dup (display_string);
	XFree (display_string);

	return retval;
}

static GNOME_Evolution_ShellComponent
impl_Shell_getComponentByType (PortableServer_Servant servant,
			       const CORBA_char *type,
			       CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EvolutionShellComponentClient *handler;
	EFolderTypeRegistry *folder_type_registry;
	GNOME_Evolution_ShellComponent corba_component;
	EShell *shell;

	if (raise_exception_if_not_ready (servant, ev))
		return CORBA_OBJECT_NIL;

	bonobo_object = bonobo_object_from_servant (servant);
	shell = E_SHELL (bonobo_object);
	folder_type_registry = shell->priv->folder_type_registry;

	handler = e_folder_type_registry_get_handler_for_type (folder_type_registry, type);

	if (handler == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Evolution_Shell_NotFound, NULL);
		return CORBA_OBJECT_NIL;
	}

	corba_component = bonobo_object_corba_objref (BONOBO_OBJECT (handler));
	Bonobo_Unknown_ref (corba_component, ev);

	return CORBA_Object_duplicate (corba_component, ev);
}

static GNOME_Evolution_ShellView
impl_Shell_createNewView (PortableServer_Servant servant,
			  const CORBA_char *uri,
			  CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EShell *shell;
	EShellView *shell_view;
	GNOME_Evolution_ShellView shell_view_interface;

	if (raise_exception_if_not_ready (servant, ev))
		return CORBA_OBJECT_NIL;

	bonobo_object = bonobo_object_from_servant (servant);
	shell = E_SHELL (bonobo_object);

	if (strncmp (uri, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) != 0) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_UnsupportedSchema,
				     NULL);
		return CORBA_OBJECT_NIL;
	}

	shell_view = e_shell_create_view (shell, uri, NULL);
	if (shell_view == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_NotFound, NULL);
		return CORBA_OBJECT_NIL;
	}

	shell_view_interface = e_shell_view_get_corba_interface (shell_view);
	if (shell_view_interface == CORBA_OBJECT_NIL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_InternalError, NULL);
		return CORBA_OBJECT_NIL;
	}

	Bonobo_Unknown_ref (shell_view_interface, ev);
	return CORBA_Object_duplicate ((CORBA_Object) shell_view_interface, ev);
}

static void
impl_Shell_handleURI (PortableServer_Servant servant,
		      const CORBA_char *uri,
		      CORBA_Environment *ev)
{
	EvolutionShellComponentClient *schema_handler;
	EShell *shell;
	EShellPrivate *priv;
	const char *colon_p;
	char *schema;

	if (raise_exception_if_not_ready (servant, ev))
		return;

	shell = E_SHELL (bonobo_object_from_servant (servant));
	priv = shell->priv;

	if (strncmp (uri, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) == 0) {
		GNOME_Evolution_Shell_createNewView (bonobo_object_corba_objref (BONOBO_OBJECT (shell)), uri, ev);
		return;
	}

	/* Extract the schema.  */

	colon_p = strchr (uri, ':');
	if (colon_p == NULL || colon_p == uri) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_InvalidURI, NULL);
		return;
	}

	schema = g_strndup (uri, colon_p - uri);
	schema_handler = e_uri_schema_registry_get_handler_for_schema (priv->uri_schema_registry, schema);
	g_free (schema);

	if (schema_handler == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_UnsupportedSchema, NULL);
		return;
	}

	if (evolution_shell_component_client_handle_external_uri (schema_handler, uri)
	    != EVOLUTION_SHELL_COMPONENT_OK) {
		/* FIXME: Just a wild guess here.  */
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_NotFound, NULL);
		return;
	}
}

static void
corba_listener_destroy_notify (void *data)
{
	CORBA_Environment ev;
	GNOME_Evolution_FolderSelectionListener listener_interface;

	listener_interface = (GNOME_Evolution_FolderSelectionListener) data;

	CORBA_exception_init (&ev);
	CORBA_Object_release (listener_interface, &ev);
	CORBA_exception_free (&ev);
}

static void
impl_Shell_selectUserFolder (PortableServer_Servant servant,
			     const CORBA_long_long parent_xid,
			     const GNOME_Evolution_FolderSelectionListener listener,
			     const CORBA_char *title,
			     const CORBA_char *default_folder,
			     const GNOME_Evolution_Shell_FolderTypeNameList *corba_allowed_type_names,
			     const CORBA_char *default_type,
			     CORBA_Environment *ev)
{
	GtkWidget *folder_selection_dialog;
	BonoboObject *bonobo_object;
	GNOME_Evolution_FolderSelectionListener listener_duplicate;
	EShell *shell;
	const char **allowed_type_names;
	int i;

	if (raise_exception_if_not_ready (servant, ev))
		return;

	bonobo_object = bonobo_object_from_servant (servant);
	shell = E_SHELL (bonobo_object);

	allowed_type_names = alloca (sizeof (allowed_type_names[0]) * (corba_allowed_type_names->_length + 1));
	for (i = 0; i < corba_allowed_type_names->_length; i++)
		allowed_type_names[i] = corba_allowed_type_names->_buffer[i];
	allowed_type_names[corba_allowed_type_names->_length] = NULL;

	/* CORBA doesn't allow you to pass a NULL pointer. */
	if (!*default_folder)
		default_folder = NULL;
	folder_selection_dialog = e_shell_folder_selection_dialog_new (shell,
								       title,
								       NULL,
								       default_folder,
								       allowed_type_names,
								       default_type);


	listener_duplicate = CORBA_Object_duplicate (listener, ev);
	gtk_object_set_data_full (GTK_OBJECT (folder_selection_dialog), "corba_listener",
				  listener_duplicate, corba_listener_destroy_notify);

	gtk_signal_connect (GTK_OBJECT (folder_selection_dialog), "folder_selected",
			    GTK_SIGNAL_FUNC (folder_selection_dialog_folder_selected_cb), shell);
	gtk_signal_connect (GTK_OBJECT (folder_selection_dialog), "cancelled",
			    GTK_SIGNAL_FUNC (folder_selection_dialog_cancelled_cb), shell);

	if (parent_xid == 0) {
		gtk_widget_show (folder_selection_dialog);
	} else {
		XClassHint class_hints;
		XWMHints *parent_wm_hints;

		/* Set the WM class and the WindowGroup hint to be the same as
		   the foreign parent window's.  This way smartass window
		   managers like Sawfish don't get confused.  */

		e_set_dialog_parent_from_xid (GTK_WINDOW (folder_selection_dialog), parent_xid);

		if (XGetClassHint (GDK_DISPLAY (), (Window) parent_xid, &class_hints)) {
			gtk_window_set_wmclass (GTK_WINDOW (folder_selection_dialog),
						class_hints.res_name, class_hints.res_class);
			XFree (class_hints.res_name);
			XFree (class_hints.res_class);
		}

		gtk_widget_show (folder_selection_dialog);

		while (folder_selection_dialog->window == NULL)
			gtk_main_iteration ();

		parent_wm_hints = XGetWMHints (GDK_DISPLAY (), (Window) parent_xid);

		if (parent_wm_hints != NULL && (parent_wm_hints->flags & WindowGroupHint)) {
			XWMHints *wm_hints;

			wm_hints = XAllocWMHints ();
			wm_hints->flags = WindowGroupHint;
			wm_hints->window_group = parent_wm_hints->window_group;
			XSetWMHints (GDK_DISPLAY (), GDK_WINDOW_XWINDOW (folder_selection_dialog->window), wm_hints);
			XFree (wm_hints);
			XFree (parent_wm_hints);
		}
	}
}

static GNOME_Evolution_Storage
impl_Shell_getLocalStorage (PortableServer_Servant servant,
			    CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	GNOME_Evolution_Storage local_storage_interface;
	EShell *shell;
	EShellPrivate *priv;

	bonobo_object = bonobo_object_from_servant (servant);
	shell = E_SHELL (bonobo_object);
	priv = shell->priv;

	if (priv->local_storage == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Shell_NotReady, NULL);
		return CORBA_OBJECT_NIL;
	}

	local_storage_interface = e_local_storage_get_corba_interface (priv->local_storage);

	bonobo_object_dup_ref (local_storage_interface, ev);

	return local_storage_interface;
}

static Bonobo_Control
impl_Shell_createStorageSetView (PortableServer_Servant servant,
				 CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EShell *shell;
	BonoboControl *control;

	if (raise_exception_if_not_ready (servant, ev))
		return CORBA_OBJECT_NIL;

	bonobo_object = bonobo_object_from_servant (servant);
	shell = E_SHELL (bonobo_object);

	control = evolution_storage_set_view_factory_new_view (shell);

	return bonobo_object_corba_objref (BONOBO_OBJECT (control));
}

static void
impl_Shell_setLineStatus (PortableServer_Servant servant,
			  CORBA_boolean online,
			  CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EShell *shell;

	if (raise_exception_if_not_ready (servant, ev))
		return;

	bonobo_object = bonobo_object_from_servant (servant);
	shell = E_SHELL (bonobo_object);

	if (online)
		e_shell_go_online (shell, NULL);
	else
		e_shell_go_offline (shell, NULL);
}


/* Set up the ::Activity interface.  */

static void
setup_activity_interface (EShell *shell)
{
	EActivityHandler *activity_handler;
	EShellPrivate *priv;

	priv = shell->priv;

	activity_handler = e_activity_handler_new ();

	bonobo_object_add_interface (BONOBO_OBJECT (shell), BONOBO_OBJECT (activity_handler));
	priv->activity_handler = activity_handler;
}


/* Set up the ::Shortcuts interface.  */

static void
setup_shortcuts_interface (EShell *shell)
{
	ECorbaShortcuts *corba_shortcuts;
	EShellPrivate *priv;

	priv = shell->priv;

	g_assert (priv->shortcuts != NULL);

	corba_shortcuts = e_corba_shortcuts_new (priv->shortcuts);

	bonobo_object_add_interface (BONOBO_OBJECT (shell), BONOBO_OBJECT (corba_shortcuts));
	priv->corba_shortcuts = corba_shortcuts;
}


/* Initialization of the storages.  */

static gboolean
setup_corba_storages (EShell *shell)
{
	EShellPrivate *priv;
	ECorbaStorageRegistry *corba_storage_registry;

	priv = shell->priv;

	g_assert (priv->storage_set != NULL);
	corba_storage_registry = e_corba_storage_registry_new (priv->storage_set);

	if (corba_storage_registry == NULL)
		return FALSE;

	bonobo_object_add_interface (BONOBO_OBJECT (shell),
				     BONOBO_OBJECT (corba_storage_registry));

	priv->corba_storage_registry = corba_storage_registry;

	return TRUE;
}

static gboolean
setup_local_storage (EShell *shell)
{
	EStorage *local_storage;
	EShellPrivate *priv;
	gchar *local_storage_path;

	priv = shell->priv;

	g_assert (priv->folder_type_registry != NULL);
	g_assert (priv->local_storage == NULL);

	local_storage_path = g_concat_dir_and_file (priv->local_directory, LOCAL_STORAGE_DIRECTORY);
	local_storage = e_local_storage_open (priv->folder_type_registry, local_storage_path);
	if (local_storage == NULL) {
		g_warning (_("Cannot set up local storage -- %s"), local_storage_path);
		g_free (local_storage_path);
		return FALSE;
	}
	g_free (local_storage_path);

	e_storage_set_add_storage (priv->storage_set, local_storage);
	priv->local_storage = E_LOCAL_STORAGE (local_storage);

	priv->summary_storage = E_SUMMARY_STORAGE (e_summary_storage_new ());
	e_storage_set_add_storage (priv->storage_set, E_STORAGE (priv->summary_storage));

	return TRUE;
}


/* Initialization of the components.  */

static char *
get_icon_path_for_component_info (const OAF_ServerInfo *info)
{
	OAF_Property *property;
	const char *shell_component_icon_value;

	/* FIXME: liboaf is not const-safe.  */
	property = oaf_server_info_prop_find ((OAF_ServerInfo *) info,
					      "evolution:shell-component-icon");

	if (property == NULL || property->v._d != OAF_P_STRING)
		return gnome_pixmap_file ("gnome-question.png");

	shell_component_icon_value = property->v._u.value_string;

	if (g_path_is_absolute (shell_component_icon_value))
		return g_strdup (shell_component_icon_value);

	else
		return g_concat_dir_and_file (EVOLUTION_IMAGES, shell_component_icon_value);
}

static void
setup_components (EShell *shell,
		  ESplash *splash)
{
	EShellPrivate *priv;
	OAF_ServerInfoList *info_list;
	CORBA_Environment ev;
	int i;

	CORBA_exception_init (&ev);

	priv = shell->priv;
	priv->component_registry = e_component_registry_new (shell);

	info_list = oaf_query ("repo_ids.has ('IDL:GNOME/Evolution/ShellComponent:1.0')", NULL, &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		g_error ("Eeek!  Cannot perform OAF query for Evolution components.");

	if (info_list->_length == 0)
		g_warning ("No Evolution components installed.");

	for (i = 0; i < info_list->_length; i++) {
		const OAF_ServerInfo *info;
		GdkPixbuf *icon_pixbuf;
		char *icon_path;

		info = info_list->_buffer + i;

		icon_path = get_icon_path_for_component_info (info);

		icon_pixbuf = gdk_pixbuf_new_from_file (icon_path);

		if (splash != NULL)
			e_splash_add_icon (splash, icon_pixbuf);

		gdk_pixbuf_unref (icon_pixbuf);

		g_free (icon_path);
	}

	while (gtk_events_pending ())
		gtk_main_iteration ();

	for (i = 0; i < info_list->_length; i++) {
		const OAF_ServerInfo *info;

		info = info_list->_buffer + i;

		if (! e_component_registry_register_component (priv->component_registry, info->iid)) {
			g_warning ("Cannot activate Evolution component -- %s", info->iid);
		} else {
			e_shell_user_creatable_items_handler_add_component
				(priv->user_creatable_items_handler,
				 e_component_registry_get_component_by_id (priv->component_registry, info->iid));
		}

		if (splash != NULL)
			e_splash_set_icon_highlight (splash, i, TRUE);

		while (gtk_events_pending ())
			gtk_main_iteration ();
	}

	CORBA_free (info_list);

	CORBA_exception_free (&ev);
}

/* FIXME what if anything fails here?  */
static void
set_owner_on_components (EShell *shell)
{
	GNOME_Evolution_Shell corba_shell;
	EShellPrivate *priv;
	const char *local_directory;
	GList *id_list;
	GList *p;

	priv = shell->priv;
	local_directory = e_shell_get_local_directory (shell);

	corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell));

	id_list = e_component_registry_get_id_list (priv->component_registry);
	for (p = id_list; p != NULL; p = p->next) {
		EvolutionShellComponentClient *component_client;
		EvolutionShellComponentResult result;
		const char *id;

		id = (const char *) p->data;
		component_client = e_component_registry_get_component_by_id (priv->component_registry, id);

		result = evolution_shell_component_client_set_owner (component_client, corba_shell, local_directory);
		if (result != EVOLUTION_SHELL_COMPONENT_OK) {
			g_warning ("Error setting owner on component %s -- %s",
				   id, evolution_shell_component_result_to_string (result));

			if (result == EVOLUTION_SHELL_COMPONENT_OLDOWNERHASDIED) {
				component_client = e_component_registry_restart_component (priv->component_registry, id);
				result = evolution_shell_component_client_set_owner (component_client, corba_shell,
										     local_directory);
				if (result != EVOLUTION_SHELL_COMPONENT_OK) {
					g_warning ("Error re-setting owner on component %s -- %s",
						   id, evolution_shell_component_result_to_string (result));
					/* (At this point, we give up.)  */
				}
			}
		}
	}

	e_free_string_list (id_list);
}


/* EShellView handling and bookkeeping.  */

static int
view_delete_event_cb (GtkWidget *widget,
		      GdkEventAny *ev,
		      void *data)
{
	EShell *shell;

	g_assert (E_IS_SHELL_VIEW (widget));

	shell = E_SHELL (data);
	e_shell_save_settings (shell);

	/* Destroy it */
	return FALSE;
}

static void
view_destroy_cb (GtkObject *object,
		 void *data)
{
	EShell *shell;
	int num_views;

	g_assert (E_IS_SHELL_VIEW (object));

	shell = E_SHELL (data);

	num_views = g_list_length (shell->priv->views);

	/* If this is our last view, save settings now because in the
	   callback for no_views_left shell->priv->views will be NULL
	   and settings won't be saved because of that */
	if (num_views - 1 == 0)
		e_shell_save_settings (shell);

	shell->priv->views = g_list_remove (shell->priv->views, object);

	if (shell->priv->views == NULL) {
		set_interactive (shell, FALSE);

		bonobo_object_ref (BONOBO_OBJECT (shell));
		gtk_signal_emit (GTK_OBJECT (shell), signals [NO_VIEWS_LEFT]);
		bonobo_object_unref (BONOBO_OBJECT (shell));
	}
}

static EShellView *
create_view (EShell *shell,
	     const char *uri,
	     EShellView *template_view)
{
	EShellPrivate *priv;
	EShellView *view;
	ETaskBar *task_bar;

	priv = shell->priv;

	view = e_shell_view_new (shell);

	gtk_signal_connect (GTK_OBJECT (view), "delete_event",
			    GTK_SIGNAL_FUNC (view_delete_event_cb), shell);
	gtk_signal_connect (GTK_OBJECT (view), "destroy",
			    GTK_SIGNAL_FUNC (view_destroy_cb), shell);

	if (uri != NULL) {
		if (!e_shell_view_display_uri (E_SHELL_VIEW (view), uri)) {
			/* FIXME: Consider popping a dialog box up about how the provided URI does not
			   exist/could not be displayed.  */
			e_shell_view_display_uri (E_SHELL_VIEW (view), E_SHELL_VIEW_DEFAULT_URI);
		}
	}

	shell->priv->views = g_list_prepend (shell->priv->views, view);

	task_bar = e_shell_view_get_task_bar (view);
	e_activity_handler_attach_task_bar (priv->activity_handler, task_bar);

	if (template_view != NULL) {
		e_shell_view_show_folder_bar (view, e_shell_view_folder_bar_shown (template_view));
		e_shell_view_show_shortcut_bar (view, e_shell_view_shortcut_bar_shown (template_view));
	}

	return view;
}


/* GtkObject methods.  */

static void
destroy (GtkObject *object)
{
	EShell *shell;
	EShellPrivate *priv;
	GList *p;

	shell = E_SHELL (object);
	priv = shell->priv;

	priv->is_initialized = FALSE;

	e_shell_disconnect_db (shell);

	if (priv->iid != NULL)
		oaf_active_server_unregister (priv->iid, bonobo_object_corba_objref (BONOBO_OBJECT (shell)));

	g_free (priv->local_directory);

	if (priv->storage_set != NULL) {
		gtk_object_unref (GTK_OBJECT (priv->storage_set));
		priv->storage_set = NULL;
	}

	if (priv->local_storage != NULL)
		gtk_object_unref (GTK_OBJECT (priv->local_storage));

	if (priv->summary_storage != NULL)
		gtk_object_unref (GTK_OBJECT (priv->summary_storage));

	if (priv->shortcuts != NULL)
		gtk_object_unref (GTK_OBJECT (priv->shortcuts));

	if (priv->folder_type_registry != NULL)
		gtk_object_unref (GTK_OBJECT (priv->folder_type_registry));

	if (priv->uri_schema_registry != NULL)
		gtk_object_unref (GTK_OBJECT (priv->uri_schema_registry));

	if (priv->component_registry != NULL)
		gtk_object_unref (GTK_OBJECT (priv->component_registry));

	if (priv->user_creatable_items_handler != NULL)
		gtk_object_unref (GTK_OBJECT (priv->user_creatable_items_handler));

	for (p = priv->views; p != NULL; p = p->next) {
		EShellView *view;

		view = E_SHELL_VIEW (p->data);

		gtk_signal_disconnect_by_func (GTK_OBJECT (view),
					       GTK_SIGNAL_FUNC (view_delete_event_cb),
					       shell);
		gtk_signal_disconnect_by_func (GTK_OBJECT (view),
					       GTK_SIGNAL_FUNC (view_destroy_cb),
					       shell);

		gtk_object_destroy (GTK_OBJECT (view));
	}

	g_list_free (priv->views);

	/* No unreffing for these as they are aggregate.  */
	/* bonobo_object_unref (BONOBO_OBJECT (priv->corba_storage_registry)); */
	/* bonobo_object_unref (BONOBO_OBJECT (priv->activity_handler)); */
	/* bonobo_object_unref (BONOBO_OBJECT (priv->corba_shortcuts)); */

	/* FIXME.  Maybe we should do something special here.  */
	if (priv->offline_handler != NULL)
		gtk_object_unref (GTK_OBJECT (priv->offline_handler));

	e_free_string_list (priv->crash_type_names);

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* Initialization.  */

static void
class_init (EShellClass *klass)
{
	GtkObjectClass *object_class;
	POA_GNOME_Evolution_Shell__epv *epv;

	parent_class = gtk_type_class (PARENT_TYPE);

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = destroy;

	signals[NO_VIEWS_LEFT] =
		gtk_signal_new ("no_views_left",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EShellClass, no_views_left),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	signals[LINE_STATUS_CHANGED] =
		gtk_signal_new ("line_status_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (EShellClass, line_status_changed),
				gtk_marshal_NONE__ENUM,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_ENUM);

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);

	epv = & klass->epv;
	epv->_get_displayName     = impl_Shell__get_displayName;
	epv->getComponentByType   = impl_Shell_getComponentByType;
	epv->createNewView        = impl_Shell_createNewView;
	epv->handleURI            = impl_Shell_handleURI;
	epv->selectUserFolder     = impl_Shell_selectUserFolder;
	epv->getLocalStorage      = impl_Shell_getLocalStorage;
	epv->createStorageSetView = impl_Shell_createStorageSetView;
	epv->setLineStatus        = impl_Shell_setLineStatus;
}

static void
init (EShell *shell)
{
	EShellPrivate *priv;

	priv = g_new (EShellPrivate, 1);

	priv->views = NULL;

	priv->iid                          = NULL;
	priv->local_directory              = NULL;
	priv->storage_set                  = NULL;
	priv->local_storage                = NULL;
	priv->summary_storage              = NULL;
	priv->shortcuts                    = NULL;
	priv->component_registry           = NULL;
	priv->user_creatable_items_handler = NULL;
	priv->folder_type_registry         = NULL;
	priv->uri_schema_registry          = NULL;
	priv->corba_storage_registry       = NULL;
	priv->activity_handler             = NULL;
	priv->corba_shortcuts              = NULL;
	priv->offline_handler              = NULL;
	priv->crash_type_names             = NULL;
	priv->line_status                  = E_SHELL_LINE_STATUS_ONLINE;
	priv->db                           = CORBA_OBJECT_NIL;
	priv->is_initialized               = FALSE;
	priv->is_interactive               = FALSE;

	shell->priv = priv;
}


/**
 * e_shell_construct:
 * @shell: An EShell object to construct
 * @iid: OAFIID for registering the shell into the name server
 * @local_directory: Local directory for storing local information and folders
 * @show_splash: Whether to display a splash screen.
 * 
 * Construct @shell so that it uses the specified @local_directory and
 * @corba_object.
 *
 * Return value: The result of the operation.
 **/
EShellConstructResult
e_shell_construct (EShell *shell,
		   const char *iid,
		   const char *local_directory,
		   gboolean show_splash)
{
	GtkWidget *splash;
	EShellPrivate *priv;
	CORBA_Object corba_object;
	CORBA_Environment ev;
	gchar *shortcut_path;
	g_return_val_if_fail (shell != NULL, E_SHELL_CONSTRUCT_RESULT_INVALIDARG);
	g_return_val_if_fail (E_IS_SHELL (shell), E_SHELL_CONSTRUCT_RESULT_INVALIDARG);
	g_return_val_if_fail (local_directory != NULL, E_SHELL_CONSTRUCT_RESULT_INVALIDARG);
	g_return_val_if_fail (g_path_is_absolute (local_directory), E_SHELL_CONSTRUCT_RESULT_INVALIDARG);
	
	priv = shell->priv;
	
	priv->iid                  = g_strdup (iid);
	priv->local_directory      = g_strdup (local_directory);
	priv->folder_type_registry = e_folder_type_registry_new ();
	priv->uri_schema_registry  = e_uri_schema_registry_new ();
	priv->storage_set          = e_storage_set_new (priv->folder_type_registry);
	
	/* CORBA storages must be set up before the components, because otherwise components
           cannot register their own storages.  */
	if (! setup_corba_storages (shell))
		return FALSE;
	
	CORBA_exception_init (&ev);
	
	priv->db = bonobo_get_object ("wombat:", "Bonobo/ConfigDatabase", &ev);
	if (BONOBO_EX (&ev) || priv->db == CORBA_OBJECT_NIL) {
		g_warning ("Cannot access Bonobo/ConfigDatabase on wombat: (%s)", ev._repo_id);
		
		/* Make sure the DB object is NIL so we don't mess up
		   (`bonobo_get_object()' might return an undefined value in
		   the case of an exception).  */
		priv->db = CORBA_OBJECT_NIL;
		
		CORBA_exception_free (&ev);
		return E_SHELL_CONSTRUCT_RESULT_NOCONFIGDB;
 	}
	
	CORBA_exception_free (&ev);

	/* Now we can register into OAF.  Notice that we shouldn't be
	   registering into OAF until we are sure we can complete.  */
	
	/* FIXME: Multi-display stuff.  */
	corba_object = bonobo_object_corba_objref (BONOBO_OBJECT (shell));
	if (oaf_active_server_register (iid, corba_object) != OAF_REG_SUCCESS) {
		CORBA_exception_free (&ev);
		return E_SHELL_CONSTRUCT_RESULT_CANNOTREGISTER;
	}

	if (! show_splash) {
		splash = NULL;
	} else {
		splash = e_splash_new ();
		gtk_signal_connect (GTK_OBJECT (splash), "delete_event",
				    GTK_SIGNAL_FUNC (gtk_widget_hide_on_delete), NULL);
		gtk_widget_show (splash);
	}
	
	while (gtk_events_pending ())
		gtk_main_iteration ();
	
	priv->user_creatable_items_handler = e_shell_user_creatable_items_handler_new ();
	
	if (show_splash)
		setup_components (shell, E_SPLASH (splash));
	else
		setup_components (shell, NULL);
	
	/* Set up the shortcuts.  */
	
	shortcut_path = g_concat_dir_and_file (local_directory, "shortcuts.xml");
	priv->shortcuts = e_shortcuts_new (priv->storage_set,
					   priv->folder_type_registry,
					   shortcut_path);
	g_assert (priv->shortcuts != NULL);
	
	if (e_shortcuts_get_num_groups (priv->shortcuts) == 0)
		e_shortcuts_add_default_group (priv->shortcuts);
	
	g_free (shortcut_path);
	
	/* The local storage depends on the component registry.  */
	setup_local_storage (shell);
	
	/* Set up the ::Activity interface.  This must be done before we notify
	   the components, as they might want to use it.  */
	setup_activity_interface (shell);
	
	/* Set up the shortcuts interface.  This has to be done after the
	   shortcuts are actually initialized.  */
	
	setup_shortcuts_interface (shell);
	
	/* Now that we have a local storage and all the interfaces set up, we
	   can tell the components we are here.  */
	set_owner_on_components (shell);
	
	if (show_splash) {
		gtk_widget_destroy (splash);
	}
	
	if (e_shell_startup_wizard_create () == FALSE) {
		e_shell_unregister_all (shell);
		bonobo_object_unref (BONOBO_OBJECT (shell));

		exit (0);
	}

	priv->is_initialized = TRUE;

	return E_SHELL_CONSTRUCT_RESULT_OK;
}

/**
 * e_shell_new:
 * @local_directory: Local directory for storing local information and folders.
 * @show_splash: Whether to display a splash screen.
 * @construct_result_return: A pointer to an EShellConstructResult variable into
 * which the result of the operation will be stored.
 * 
 * Create a new EShell.
 * 
 * Return value: 
 **/
EShell *
e_shell_new (const char *local_directory,
	     gboolean show_splash,
	     EShellConstructResult *construct_result_return)
{
	EShell *new;
	EShellPrivate *priv;
	EShellConstructResult construct_result;

	g_return_val_if_fail (local_directory != NULL, NULL);
	g_return_val_if_fail (*local_directory != '\0', NULL);

	new = gtk_type_new (e_shell_get_type ());

	construct_result = e_shell_construct (new, E_SHELL_OAFIID, local_directory, show_splash);

	if (construct_result != E_SHELL_CONSTRUCT_RESULT_OK) {
		*construct_result_return = construct_result;
		bonobo_object_unref (BONOBO_OBJECT (new));
		return NULL;
	}

	priv = new->priv;

	if (priv->shortcuts == NULL || priv->storage_set == NULL) {
		/* FIXME? */
		*construct_result_return = E_SHELL_CONSTRUCT_RESULT_GENERICERROR;
		bonobo_object_unref (BONOBO_OBJECT (new));
		return NULL;
	}

	*construct_result_return = E_SHELL_CONSTRUCT_RESULT_OK;
	return new;
}


/**
 * e_shell_create_view:
 * @shell: The shell for which to create a new view.
 * @uri: URI for the new view.
 * @template_view: Window from which to copy the view settings (can be %NULL).
 * 
 * Create a new view for @uri.
 * 
 * Return value: The new view.
 **/
EShellView *
e_shell_create_view (EShell *shell,
		     const char *uri,
		     EShellView *template_view)
{
	EShellView *view;
	EShellPrivate *priv;

	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	priv = shell->priv;

	view = create_view (shell, uri, template_view);

	gtk_widget_show (GTK_WIDGET (view));
	while (gtk_events_pending ())
		gtk_main_iteration ();

	set_interactive (shell, TRUE);

	return view;
}

EShellView *
e_shell_create_view_from_settings (EShell *shell,
				   const char *uri,
				   EShellView *template_view,
				   int view_num,
				   gboolean *settings_found)
{
	EShellView *view;

	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	view = create_view (shell, uri, template_view);

	*settings_found = e_shell_view_load_settings (view, view_num);

	gtk_widget_show (GTK_WIDGET (view));
	while (gtk_events_pending ())
		gtk_main_iteration ();

	set_interactive (shell, TRUE);

	return view;
}


/**
 * e_shell_get_local_directory:
 * @shell: An EShell object.
 * 
 * Get the local directory associated with @shell.
 * 
 * Return value: A pointer to the path of the local directory.
 **/
const char *
e_shell_get_local_directory (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->local_directory;
}

/**
 * e_shell_get_shortcuts:
 * @shell: An EShell object.
 * 
 * Get the shortcuts associated to @shell.
 * 
 * Return value: A pointer to the EShortcuts associated to @shell.
 **/
EShortcuts *
e_shell_get_shortcuts (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->shortcuts;
}

/**
 * e_shell_get_storage_set:
 * @shell: An EShell object.
 * 
 * Get the storage set associated to @shell.
 * 
 * Return value: A pointer to the EStorageSet associated to @shell.
 **/
EStorageSet *
e_shell_get_storage_set (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->storage_set;
}

/**
 * e_shell_get_folder_type_registry:
 * @shell: An EShell object.
 * 
 * Get the folder type registry associated to @shell.
 * 
 * Return value: A pointer to the EFolderTypeRegistry associated to @shell.
 **/
EFolderTypeRegistry *
e_shell_get_folder_type_registry (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->folder_type_registry;
}

/**
 * e_shell_get_uri_schema_registry:
 * @shell: An EShell object.
 * 
 * Get the schema registry associated to @shell.
 * 
 * Return value: A pointer to the EUriSchemaRegistry associated to @shell.
 **/
EUriSchemaRegistry  *
e_shell_get_uri_schema_registry (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->uri_schema_registry;
}

/**
 * e_shell_get_local_storage:
 * @shell: An EShell object.
 *
 * Get the local storage associated to @shell.
 *
 * Return value: A pointer to the ELocalStorage associated to @shell.
 **/
ELocalStorage *
e_shell_get_local_storage (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->local_storage;
}


static gboolean
save_settings_for_views (EShell *shell)
{
	CORBA_Environment ev;
	EShellPrivate *priv;
	GList *p;
	gboolean retval;
	int i;

	priv = shell->priv;
	retval = TRUE;

	for (p = priv->views, i = 0; p != NULL; p = p->next, i++) {
		EShellView *view;

		view = E_SHELL_VIEW (p->data);

		if (! e_shell_view_save_settings (view, i)) {
			g_warning ("Cannot save settings for view -- %d", i);
			retval = FALSE;
		}
	}

	bonobo_config_set_long (priv->db, "/Shell/Views/NumberOfViews", 
				g_list_length (priv->views), NULL);

	CORBA_exception_init (&ev);
	Bonobo_ConfigDatabase_sync (priv->db, &ev);
	CORBA_exception_free (&ev);

	return TRUE;
}

static gboolean
save_settings_for_component (EShell *shell,
			     const char *id,
			     EvolutionShellComponentClient *client)
{
	Bonobo_Unknown unknown_interface;
	GNOME_Evolution_Session session_interface;
	CORBA_Environment ev;
	char *prefix;
	gboolean retval;

	unknown_interface = bonobo_object_corba_objref (BONOBO_OBJECT (client));
	g_assert (unknown_interface != CORBA_OBJECT_NIL);

	CORBA_exception_init (&ev);

	session_interface = Bonobo_Unknown_queryInterface (unknown_interface,
							   "IDL:GNOME/Evolution/Session:1.0", &ev);
	if (ev._major != CORBA_NO_EXCEPTION || CORBA_Object_is_nil (session_interface, &ev)) {
		CORBA_exception_free (&ev);
		return TRUE;
	}

	prefix = g_strconcat ("/apps/Evolution/Shell/Components/", id, NULL);
	GNOME_Evolution_Session_saveConfiguration (session_interface, prefix, &ev);

	if (ev._major == CORBA_NO_EXCEPTION)
		retval = TRUE;
	else
		retval = FALSE;

	g_free (prefix);

	CORBA_exception_free (&ev);

	return retval;
}

static gboolean
save_settings_for_components (EShell *shell)
{
	EShellPrivate *priv;
	GList *component_ids;
	GList *p;
	gboolean retval;

	priv = shell->priv;

	g_assert (priv->component_registry);
	component_ids = e_component_registry_get_id_list (priv->component_registry);

	retval = TRUE;
	for (p = component_ids; p != NULL; p = p->next) {
		EvolutionShellComponentClient *client;
		const char *id;

		id = p->data;
		client = e_component_registry_get_component_by_id (priv->component_registry, id);

		if (! save_settings_for_component (shell, id, client))
			retval = FALSE;
	}

	e_free_string_list (component_ids);

	return retval;
}

/**
 * e_shell_save_settings:
 * @shell: 
 * 
 * Save the settings for this shell.
 * 
 * Return value: %TRUE if it worked, %FALSE otherwise.  Even if %FALSE is
 * returned, it is possible that at least part of the settings for the views
 * have been saved.
 **/
gboolean
e_shell_save_settings (EShell *shell)
{
	gboolean views_saved;
	gboolean components_saved;

	g_return_val_if_fail (shell != NULL, FALSE);
	g_return_val_if_fail (E_IS_SHELL (shell), FALSE);

	views_saved      = save_settings_for_views (shell);
	components_saved = save_settings_for_components (shell);

	return views_saved && components_saved;
}

/**
 * e_shell_restore_from_settings:
 * @shell: An EShell object.
 * 
 * Restore the existing views from the saved configuration.  The shell must
 * have no views for this to work.
 * 
 * Return value: %FALSE if the shell has some open views or there is no saved
 * configuration.  %TRUE if the configuration could be restored successfully.
 **/
gboolean
e_shell_restore_from_settings (EShell *shell)
{
	EShellPrivate *priv;
	gboolean retval;
	int num_views;
	int i;

	g_return_val_if_fail (shell != NULL, FALSE);
	g_return_val_if_fail (E_IS_SHELL (shell), FALSE);
	g_return_val_if_fail (shell->priv->views == NULL, FALSE);

	priv = shell->priv;

	num_views = bonobo_config_get_long_with_default (priv->db, "/Shell/Views/NumberOfViews", 0, NULL);

	if (num_views == 0)
		return FALSE;
	
	retval = TRUE;

	for (i = 0; i < num_views; i++) {
		EShellView *view;
		gboolean settings_found;

		view = e_shell_create_view_from_settings (shell, NULL, NULL, i, &settings_found);
		if (! settings_found)
			retval = FALSE;
	}

	return retval;
}

/**
 * e_shell_destroy_all_views:
 * @shell: 
 * 
 * Destroy all the views in @shell.
 **/
void
e_shell_destroy_all_views (EShell *shell)
{
	EShellPrivate *priv;
	GList *p, *pnext;

	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));

	if (shell->priv->views)
		e_shell_save_settings (shell); 

	priv = shell->priv;

	for (p = priv->views; p != NULL; p = pnext) {
		EShellView *shell_view;

		pnext = p->next;

		shell_view = E_SHELL_VIEW (p->data);
		gtk_widget_destroy (GTK_WIDGET (shell_view));
	}
}


/**
 * e_shell_component_maybe_crashed:
 * @shell: A pointer to an EShell object
 * @uri: URI that caused the crash
 * @type_name: The type of the folder that caused the crash
 * @shell_view: Pointer to the EShellView over which we want the modal dialog
 * to appear.
 * 
 * Report that a maybe crash happened when trying to display a folder of type
 * @type_name.  The shell will pop up a crash dialog whose parent will be the
 * @shell_view.
 **/
void
e_shell_component_maybe_crashed   (EShell *shell,
				   const char *uri,
				   const char *type_name,
				   EShellView *shell_view)
{
	EShellPrivate *priv;
	GtkWindow *parent_window;
	EvolutionShellComponentClient *component;
	GList *p;

	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));
	g_return_if_fail (type_name != NULL);
	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));

	priv = shell->priv;

	if (strncmp (uri, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) == 0) {
		const char *path;

		path = uri + E_SHELL_URI_PREFIX_LEN;
		if (e_storage_set_get_folder (priv->storage_set, path) == NULL)
			return;
	}

	component = e_folder_type_registry_get_handler_for_type (priv->folder_type_registry, type_name);
	if (component != NULL
	    && bonobo_unknown_ping (bonobo_object_corba_objref (BONOBO_OBJECT (component))))
		return;

	/* See if that type has caused a crash already.  */

	for (p = priv->crash_type_names; p != NULL; p = p->next) {
		const char *crash_type_name;

		crash_type_name = (const char *) p->data;
		if (strcmp (type_name, crash_type_name) == 0) {
			/* This type caused a crash already.  */
			return;
		}
	}

	/* New crash.  */

	priv->crash_type_names = g_list_prepend (priv->crash_type_names, g_strdup (type_name));

	if (shell_view == NULL)
		parent_window = NULL;
	else
		parent_window = GTK_WINDOW (shell_view);

	e_notice (parent_window, GNOME_MESSAGE_BOX_ERROR,
		  _("The Evolution component that handles folders of type \"%s\"\n"
		    "has unexpectedly quit. You will need to quit Evolution and restart\n"
		    "in order to access that data again."),
		  type_name);

	if (shell_view)
		bonobo_window_deregister_dead_components (BONOBO_WINDOW (shell_view));

	/* FIXME: we should probably re-start the component here */
}


/* Offline/online handling.  */

static void
offline_procedure_started_cb (EShellOfflineHandler *offline_handler,
			      void *data)
{
	EShell *shell;
	EShellPrivate *priv;

	shell = E_SHELL (data);
	priv = shell->priv;

	priv->line_status = E_SHELL_LINE_STATUS_GOING_OFFLINE;
	gtk_signal_emit (GTK_OBJECT (shell), signals[LINE_STATUS_CHANGED], priv->line_status);
}

static void
offline_procedure_finished_cb (EShellOfflineHandler *offline_handler,
			       gboolean now_offline,
			       void *data)
{
	EShell *shell;
	EShellPrivate *priv;

	shell = E_SHELL (data);
	priv = shell->priv;

	if (now_offline)
		priv->line_status = E_SHELL_LINE_STATUS_OFFLINE;
	else
		priv->line_status = E_SHELL_LINE_STATUS_ONLINE;

	gtk_object_unref (GTK_OBJECT (priv->offline_handler));
	priv->offline_handler = NULL;

	gtk_signal_emit (GTK_OBJECT (shell), signals[LINE_STATUS_CHANGED], priv->line_status);
}

/**
 * e_shell_get_line_status:
 * @shell: A pointer to an EShell object.
 * 
 * Get the line status for @shell.
 * 
 * Return value: The current line status for @shell.
 **/
EShellLineStatus
e_shell_get_line_status (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, E_SHELL_LINE_STATUS_OFFLINE);
	g_return_val_if_fail (E_IS_SHELL (shell), E_SHELL_LINE_STATUS_OFFLINE);

	return shell->priv->line_status;
}

/**
 * e_shell_go_offline:
 * @shell: 
 * @action_view: 
 * 
 * Make the shell go into off-line mode.
 **/
void
e_shell_go_offline (EShell *shell,
		    EShellView *action_view)
{
	EShellPrivate *priv;

	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));
	g_return_if_fail (action_view != NULL);
	g_return_if_fail (action_view == NULL || E_IS_SHELL_VIEW (action_view));

	priv = shell->priv;

	if (priv->line_status != E_SHELL_LINE_STATUS_ONLINE)
		return;

	g_assert (priv->offline_handler == NULL);

	priv->offline_handler = e_shell_offline_handler_new (priv->component_registry);

	gtk_signal_connect (GTK_OBJECT (priv->offline_handler), "offline_procedure_started",
			    GTK_SIGNAL_FUNC (offline_procedure_started_cb), shell);
	gtk_signal_connect (GTK_OBJECT (priv->offline_handler), "offline_procedure_finished",
			    GTK_SIGNAL_FUNC (offline_procedure_finished_cb), shell);

	e_shell_offline_handler_put_components_offline (priv->offline_handler, action_view);
}

/**
 * e_shell_go_online:
 * @shell: 
 * @action_view: 
 * 
 * Make the shell go into on-line mode.
 **/
void
e_shell_go_online (EShell *shell,
		   EShellView *action_view)
{
	EShellPrivate *priv;
	GList *component_ids;
	GList *p;

	g_return_if_fail (shell != NULL);
	g_return_if_fail (E_IS_SHELL (shell));
	g_return_if_fail (action_view == NULL || E_IS_SHELL_VIEW (action_view));

	priv = shell->priv;

	component_ids = e_component_registry_get_id_list (priv->component_registry);

	for (p = component_ids; p != NULL; p = p->next) {
		CORBA_Environment ev;
		EvolutionShellComponentClient *client;
		GNOME_Evolution_Offline offline_interface;
		const char *id;

		id = (const char *) p->data;
		client = e_component_registry_get_component_by_id (priv->component_registry, id);

		CORBA_exception_init (&ev);

		offline_interface = evolution_shell_component_client_get_offline_interface (client);

		if (CORBA_Object_is_nil (offline_interface, &ev) || ev._major != CORBA_NO_EXCEPTION) {
			CORBA_exception_free (&ev);
			continue;
		}

		GNOME_Evolution_Offline_goOnline (offline_interface, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("Error putting component `%s' online.", id);

		CORBA_exception_free (&ev);
	}

	e_free_string_list (component_ids);

	priv->line_status = E_SHELL_LINE_STATUS_ONLINE;
	gtk_signal_emit (GTK_OBJECT (shell), signals[LINE_STATUS_CHANGED], priv->line_status);
}


Bonobo_ConfigDatabase 
e_shell_get_config_db (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, CORBA_OBJECT_NIL);

	return shell->priv->db;
}

EComponentRegistry *
e_shell_get_component_registry (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->component_registry;
}

EShellUserCreatableItemsHandler *
e_shell_get_user_creatable_items_handler (EShell *shell)
{
	g_return_val_if_fail (shell != NULL, NULL);
	g_return_val_if_fail (E_IS_SHELL (shell), NULL);

	return shell->priv->user_creatable_items_handler;
}


/* FIXME: These are ugly hacks, they really should not be needed.  */

void
e_shell_unregister_all (EShell *shell)
{
	EShellPrivate *priv;

	g_return_if_fail (E_IS_SHELL (shell));

	/* FIXME: This really really sucks.  */

	priv = shell->priv;

	priv->is_initialized = FALSE;

	gtk_object_unref (GTK_OBJECT (priv->component_registry));
	priv->component_registry = NULL;
}

void
e_shell_disconnect_db (EShell *shell)
{
	EShellPrivate *priv;

	g_return_if_fail (E_IS_SHELL (shell));

	priv = shell->priv;

	if (priv->db == CORBA_OBJECT_NIL)
		return;

	bonobo_object_release_unref (priv->db, NULL);
	priv->db = CORBA_OBJECT_NIL;
}


const char *
e_shell_construct_result_to_string (EShellConstructResult result)
{
	switch (result) {
	case E_SHELL_CONSTRUCT_RESULT_OK:
		return _("OK");
	case E_SHELL_CONSTRUCT_RESULT_INVALIDARG:
		return _("Invalid arguments");
	case E_SHELL_CONSTRUCT_RESULT_CANNOTREGISTER:
		return _("Cannot register on OAF");
	case E_SHELL_CONSTRUCT_RESULT_NOCONFIGDB:
		return _("Configuration Database not found");
	case E_SHELL_CONSTRUCT_RESULT_GENERICERROR:
		return _("Generic error");
	default:
		return _("Unknown error");
	}
}


E_MAKE_X_TYPE (e_shell, "EShell", EShell,
	       class_init, init, PARENT_TYPE,
	       POA_GNOME_Evolution_Shell__init,
	       GTK_STRUCT_OFFSET (EShellClass, epv));
