/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell-offline-sync.c - Sync folders before going into Offline mode.
 *
 * Copyright (C) 2002 Ximian, Inc.
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

#include "e-shell-offline-sync.h"

#include "e-shell.h"
#include "e-shell-constants.h"

#include "Evolution.h"

#include <gal/widgets/e-gui-utils.h>

#include <gtk/gtklabel.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkprogressbar.h>
#include <gtk/gtksignal.h>

#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnome/gnome-i18n.h>

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-exception.h>


typedef struct _SyncData SyncData;
typedef struct _SyncFolderProgressListenerServant SyncFolderProgressListenerServant;

struct _SyncFolderProgressListenerServant {
	POA_GNOME_Evolution_SyncFolderProgressListener servant;
	SyncData *sync_data;
};

struct _SyncData {
	/* The shell.  */
	EShell *shell;

	/* The progress dialog.  */
	GtkWidget *dialog;

	/* Label in the progress dialog.  */
	GtkWidget *label;

	/* Progress bar in the progress dialog.  */
	GtkWidget *progress_bar;

	/* Path of the folder currently being synced.  */
	char *current_folder_path;

	/* Whether to cancel the current folder's syncing.  */
	gboolean cancel;

	/* Whether the current folder is finished syncing; used for async
	   notification of completion.  */
	gboolean current_folder_finished;

	/* CORBA cruft.  */
	SyncFolderProgressListenerServant *progress_listener_servant;
	GNOME_Evolution_SyncFolderProgressListener progress_listener_objref;
};


/* The progress listener interface.  */

static PortableServer_ServantBase__epv SyncFolderProgressListener_base_epv;
static POA_GNOME_Evolution_SyncFolderProgressListener__epv SyncFolderProgressListener_epv;
static POA_GNOME_Evolution_SyncFolderProgressListener__vepv SyncFolderProgressListener_vepv;

static SyncFolderProgressListenerServant *
progress_listener_servant_new (SyncData *sync_data)
{
	SyncFolderProgressListenerServant *servant;

	servant = g_new0 (SyncFolderProgressListenerServant, 1);

	servant->servant.vepv = &SyncFolderProgressListener_vepv;
	servant->sync_data    = sync_data;

	return servant;
}

static void
progress_listener_servant_free (SyncFolderProgressListenerServant *servant)
{
	CORBA_Environment ev;
	PortableServer_ObjectId *oid;

	CORBA_exception_init (&ev);

	oid = PortableServer_POA_servant_to_id (bonobo_poa (), servant, &ev);
	PortableServer_POA_deactivate_object (bonobo_poa (), oid, &ev);
	CORBA_free (oid);

	POA_GNOME_Evolution_SyncFolderProgressListener__fini ((POA_GNOME_Evolution_SyncFolderProgressListener *) servant, &ev);

	CORBA_exception_free (&ev);

	g_free (servant);
}

static void
impl_SyncFolderProgressListener_updateProgress (PortableServer_Servant servant,
						const CORBA_float percent,
						CORBA_Environment *ev)
{
	SyncData *sync_data;

	sync_data = ((SyncFolderProgressListenerServant *) servant)->sync_data;
	gtk_progress_set_percentage (GTK_PROGRESS (sync_data->progress_bar), percent);
}

static void
impl_SyncFolderProgressListener_reportSuccess (PortableServer_Servant servant,
					       CORBA_Environment *ev)
{
	SyncData *sync_data;

	sync_data = ((SyncFolderProgressListenerServant *) servant)->sync_data;
	sync_data->current_folder_finished = TRUE;
}

static void
impl_SyncFolderProgressListener_reportFailure (PortableServer_Servant servant,
					       const CORBA_char *message,
					       CORBA_Environment *ev)
{
	EFolder *folder;
	SyncData *sync_data;

	sync_data = ((SyncFolderProgressListenerServant *) servant)->sync_data;

	folder = e_storage_set_get_folder (e_shell_get_storage_set (sync_data->shell),
					   sync_data->current_folder_path);

	/* FIXME -- We probably should give the user more of a chance to do
	   something about it.  */
	e_notice (GTK_WINDOW (sync_data->dialog),
		  _("Error synchronizing \"%s\":\n%s"), e_folder_get_name (folder), message);

	sync_data->current_folder_finished = TRUE;
}

static gboolean
setup_progress_listener (SyncData *sync_data)
{
	SyncFolderProgressListenerServant *servant;
	CORBA_Environment ev;

	SyncFolderProgressListener_base_epv._private    = NULL;
	SyncFolderProgressListener_base_epv.finalize    = NULL;
	SyncFolderProgressListener_base_epv.default_POA = NULL;

	SyncFolderProgressListener_epv.updateProgress = impl_SyncFolderProgressListener_updateProgress;
	SyncFolderProgressListener_epv.reportSuccess  = impl_SyncFolderProgressListener_reportSuccess;
	SyncFolderProgressListener_epv.reportFailure  = impl_SyncFolderProgressListener_reportFailure;

	SyncFolderProgressListener_vepv._base_epv = &SyncFolderProgressListener_base_epv;
	SyncFolderProgressListener_vepv.GNOME_Evolution_SyncFolderProgressListener_epv = &SyncFolderProgressListener_epv;

	servant = progress_listener_servant_new (sync_data);

	CORBA_exception_init (&ev);

	POA_GNOME_Evolution_SyncFolderProgressListener__init ((PortableServer_Servant) servant, &ev);
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

	sync_data->progress_listener_servant = servant;
	sync_data->progress_listener_objref = PortableServer_POA_servant_to_reference (bonobo_poa (),
										       servant, &ev);

	CORBA_exception_free (&ev);

	return TRUE;
}


/* Setting up the progress dialog.  */

static int
progress_dialog_close_callback (GnomeDialog *dialog,
				void *data)
{
	/* Don't allow the dialog to be closed through the window manager close
	   command.  */
	return TRUE;
}

static void
progress_dialog_clicked_callback (GnomeDialog *dialog,
				  int button_num,
				  void *data)
{
	SyncData *sync_data;

	sync_data = (SyncData *) data;
	sync_data->cancel = TRUE;
}

static void
setup_dialog (SyncData *sync_data)
{
	sync_data->dialog = gnome_dialog_new (_("Syncing Folder"), GNOME_STOCK_BUTTON_CANCEL, NULL);
	gtk_widget_set_usize (sync_data->dialog, 300, -1);
	gtk_window_set_policy (GTK_WINDOW (sync_data->dialog), FALSE, FALSE, FALSE);

	gtk_signal_connect (GTK_OBJECT (sync_data->dialog), "close",
			    GTK_SIGNAL_FUNC (progress_dialog_close_callback), sync_data);
	gtk_signal_connect (GTK_OBJECT (sync_data->dialog), "clicked",
			    GTK_SIGNAL_FUNC (progress_dialog_clicked_callback), sync_data);

	sync_data->label = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (sync_data->dialog)->vbox),
			    sync_data->label, FALSE, TRUE, 0);

	sync_data->progress_bar = gtk_progress_bar_new ();
	gtk_progress_set_activity_mode (GTK_PROGRESS (sync_data->progress_bar), FALSE);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (sync_data->dialog)->vbox),
			    sync_data->progress_bar, FALSE, TRUE, 0);

	gtk_widget_show_all (sync_data->dialog);
}


/* Sync the folder at the specified @folder_path.  */
static void
sync_folder (SyncData *sync_data,
	     const char *folder_path,
	     int num,
	     int total)
{
	EvolutionShellComponentClient *component_client;
	EStorageSet *storage_set;
	GNOME_Evolution_Folder *corba_folder;
	GNOME_Evolution_Offline offline_interface;
	CORBA_Environment ev;
	EFolder *folder;
	char *evolution_uri;
	char *msg;

	CORBA_exception_init (&ev);

	/* Retrieve the folder that needs to be synced from the storage set, as
	   well as the component that should perform the syncing.  */

	storage_set = e_shell_get_storage_set (sync_data->shell);

	folder = e_storage_set_get_folder (storage_set, folder_path);
	if (folder == NULL) {
		/* This might be a remote folder that is not visible right now,
		   or is otherwise hidden from the tree somehow.  So we just
		   ignore it, and keep going without signalling any error.  */
		return;
	}

	/* Don't attempt to sync folders that don't have the can_sync_offline
	   property set.  */
	if (! e_folder_get_can_sync_offline (folder))
		return;

	component_client = e_folder_type_registry_get_handler_for_type (e_shell_get_folder_type_registry (sync_data->shell),
									e_folder_get_type_string (folder));

	offline_interface = evolution_shell_component_client_get_offline_interface (component_client);
	if (offline_interface == CORBA_OBJECT_NIL) {
		/* The component doesn't support going off-line, just ignore
		   this as it's probably a programming error in the
		   implementation of the component.  */
		return;
	}

	/* Prepare the CORBA folder to be passed to the component.  */

	corba_folder = GNOME_Evolution_Folder__alloc ();
	evolution_uri = g_strconcat (E_SHELL_URI_PREFIX, "/", folder_path, NULL);
	e_folder_to_corba (folder, evolution_uri, corba_folder);
	g_free (evolution_uri);

	/* Prepare the dialog.  */

	msg = g_strdup_printf (_("Synchronizing \"%s\" (%d of %d) ..."),
			       e_folder_get_name (folder), num, total);
	gtk_label_set (GTK_LABEL (sync_data->label), msg);
	g_free (msg);

	gtk_progress_set_value (GTK_PROGRESS (sync_data->progress_bar), 0.0);

	/* Get the data ready.  */

	g_free (sync_data->current_folder_path);
	sync_data->current_folder_path = g_strdup (folder_path);
	sync_data->current_folder_finished = FALSE;
	sync_data->cancel = FALSE;

	/* Tell the component to start syncing.  */

	GNOME_Evolution_Offline_syncFolder (offline_interface,
					    corba_folder,
					    sync_data->progress_listener_objref,
					    &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Error invoking ::syncFolder -- %s", BONOBO_EX_ID (&ev));
		CORBA_free (corba_folder);
		CORBA_exception_free (&ev);
		return;
	}

	/* Wait for the component to signal completion...  */

	while (! sync_data->current_folder_finished && ! sync_data->cancel) {
		gtk_main_iteration ();

		/* Check if the user clicked the Cancel button.  */
		if (sync_data->cancel) {
			gnome_dialog_set_sensitive (GNOME_DIALOG (sync_data->dialog), 0, FALSE);

			GNOME_Evolution_Offline_cancelSyncFolder (offline_interface, corba_folder, &ev);

			while (! sync_data->current_folder_finished)
				gtk_main_iteration ();

			break;
		}
	}

	/* All done.  */

	CORBA_free (corba_folder);
	CORBA_exception_free (&ev);
}

/* Free up the data needed for syncing.  */
static void
cleanup (SyncData *sync_data)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	if (sync_data->dialog != NULL)
		gtk_widget_destroy (sync_data->dialog);

	if (sync_data->progress_listener_servant != NULL)
		progress_listener_servant_free (sync_data->progress_listener_servant);

	if (sync_data->progress_listener_objref != CORBA_OBJECT_NIL)
		CORBA_Object_release (sync_data->progress_listener_objref, &ev);

	g_free (sync_data);

	CORBA_exception_free (&ev);
}


void
e_shell_offline_sync_all_folders (EShell *shell,
				  GtkWindow *parent_window)
{
	Bonobo_ConfigDatabase config_db;
	CORBA_sequence_CORBA_string *folder_path_sequence;
	CORBA_any *any;
	CORBA_Environment ev;
	SyncData *sync_data;
	int i;

	config_db = e_shell_get_config_db (shell);

	CORBA_exception_init (&ev);

	/* Get the paths for the folders to sync up.  */

	any = Bonobo_ConfigDatabase_getValue (config_db, "/OfflineFolders/paths", "", &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Cannot get /OfflineFolders/paths from ConfigDatabase -- %s", BONOBO_EX_ID (&ev));
		CORBA_exception_free (&ev);
		return;
	}
	if (! CORBA_TypeCode_equal (any->_type, TC_CORBA_sequence_CORBA_string, &ev) || BONOBO_EX (&ev)) {
		g_warning ("/OfflineFolders/Paths in ConfigDatabase is not the expected type");
		CORBA_free (any);
		CORBA_exception_free (&ev);
		return;
	}

	folder_path_sequence = (CORBA_sequence_CORBA_string *) any->_value;

	sync_data = g_new0 (SyncData, 1);
	sync_data->shell = shell;

	/* Initialize everything, then go ahead and sync.  */

	if (! setup_progress_listener (sync_data))
		goto done;

	setup_dialog (sync_data);

	for (i = 0; i < folder_path_sequence->_length; i ++) {
		sync_folder (sync_data,
			     folder_path_sequence->_buffer[i],
			     i + 1, folder_path_sequence->_length);

		/* If the operation has been cancelled, stop syncing and
		   return.  */
		if (sync_data->cancel) {
			/* FIXME: Do we want to pop up a dialog asking for
			   confirmation?  */
			break;
		}
	}

 done:
	cleanup (sync_data);

	CORBA_free (folder_path_sequence);
	CORBA_exception_free (&ev);
}
