/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* importer.c
 *
 * Copyright (C) 2000  Ximian, Inc.
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
 * Author: Iain Holmes  <iain@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-druid.h>
#include <libgnomeui/gnome-druid-page-finish.h>
#include <libgnomeui/gnome-druid-page-standard.h>
#include <libgnomeui/gnome-druid-page-start.h>
#include <libgnomeui/gnome-file-entry.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnomeui/gnome-dialog.h>

#include <liboaf/liboaf.h>

#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-widget.h>

#include "e-shell.h"
#include "e-shell-view.h"
#include "e-local-storage.h" /* for E_LOCAL_STORAGE_NAME */
#include "e-shell-folder-selection-dialog.h"

#include "importer/evolution-importer-client.h"

#include <glade/glade.h>
#include <gtkhtml/gtkhtml.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/widgets/e-unicode.h>

#include "e-util/e-html-utils.h"
#include "e-util/e-gtk-utils.h"

#include "e-shell-importer.h"
#include "importer/GNOME_Evolution_Importer.h"

typedef struct _ImportDialogFilePage {
	GtkWidget *vbox;
	GtkWidget *filename;
	GtkWidget *filetype;
	GtkWidget *menu;

	gboolean need_filename;
} ImportDialogFilePage;

typedef struct _ImportDialogTypePage {
	GtkWidget *vbox;
	GtkWidget *intelligent;
	GtkWidget *file;
} ImportDialogTypePage;

typedef struct _ImportDialogImporterPage {
	GtkWidget *vbox;

	GList *importers;
	gboolean prepared;
	int running;
} ImportDialogImporterPage;

typedef struct _ImportData {
	EShell *shell;
	EShellView *view;
	
	GladeXML *wizard;
	GtkWidget *dialog;
	GtkWidget *druid;
	ImportDialogFilePage *filepage;
	ImportDialogTypePage *typepage;
	ImportDialogImporterPage *importerpage;

	GtkWidget *filedialog;
	GtkWidget *typedialog;
	GtkWidget *intelligent;
	GnomeDruidPageStart *start;
	GnomeDruidPageFinish *finish;
	GtkWidget *vbox;

	char *choosen_iid;
} ImportData;

typedef struct _IntelligentImporterData {
	CORBA_Object object;
	Bonobo_Control control;
	GtkWidget *widget;

	char *name;
	char *blurb;
	char *iid;
} IntelligentImporterData;

typedef struct _SelectedImporterData{
	CORBA_Object importer;
	char *iid;
} SelectedImporterData;

/*
  #define IMPORTER_DEBUG
*/
#ifdef IMPORTER_DEBUG
#define IN g_print ("=====> %s (%d)\n", __FUNCTION__, __LINE__)
#define OUT g_print ("<==== %s (%d)\n", __FUNCTION__, __LINE__)
#else
#define IN
#define OUT
#endif

/* Some HTML helper functions copied from mail/mail-config-druid.c */
static struct {
	char *name;
	char *text;
} info[] = {
	{ "type_html",
	  N_("Choose the type of importer to run:")
	},
	{ "file_html",
	  N_("Choose the file that you want to import into Evolution, "
	     "and select what type of file it is from the list.\n\n"
	     "You can select \"Automatic\" if you do not know, and "
	     "Evolution will attempt to work it out.")
	},
	{ "intelligent_html",
	  N_("Please select the information that you would like to import:")
	}
};
static int num_info = (sizeof (info) / sizeof (info[0]));

static void
html_size_req (GtkWidget *widget,
	       GtkRequisition *requisition)
{
	requisition->height = GTK_LAYOUT (widget)->height;
}

static GtkWidget *
create_html (const char *name)
{
	GtkWidget *scrolled, *html;
	GtkHTMLStream *stream;
	GtkStyle *style;
	char *utf8;
	int i;

	html = gtk_html_new ();
	GTK_LAYOUT (html)->height = 0;
	gtk_signal_connect (GTK_OBJECT (html), "size_request",
			    GTK_SIGNAL_FUNC (html_size_req), NULL);
	gtk_html_set_editable (GTK_HTML (html), FALSE);
	style = gtk_rc_get_style (html);
	if (!style)
		style = gtk_widget_get_style (html);
	if (style) {
		gtk_html_set_default_background_color (GTK_HTML (html),
						       &style->bg[0]);
	}
	gtk_widget_show (html);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (scrolled);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	gtk_container_add (GTK_CONTAINER (scrolled), html);

	for (i = 0; i < num_info; i++) {
		if (!strcmp (name, info[i].name))
			break;
	}
	g_return_val_if_fail (i != num_info, scrolled);

	stream = gtk_html_begin_content (GTK_HTML (html),
					 "text/html; charset=utf-8");
	gtk_html_write (GTK_HTML (html), stream, "<html><p>", 9);
	utf8 = e_utf8_from_locale_string (_(info[i].text));
	gtk_html_write (GTK_HTML (html), stream, utf8, strlen (utf8));
	g_free (utf8);
	gtk_html_write (GTK_HTML (html), stream, "</p></html>", 11);
	gtk_html_end (GTK_HTML (html), stream, GTK_HTML_STREAM_OK);

	return scrolled;
}

/* Importing functions */

/* Data to be passed around */
typedef struct _ImporterComponentData {
	EvolutionImporterClient *client;
	EvolutionImporterListener *listener;
	char *filename;

	GnomeDialog *dialog;
	GtkWidget *contents;

	int item;

	gboolean stop;
	gboolean destroyed;
} ImporterComponentData;

static gboolean importer_timeout_fn (gpointer data);
static void
import_cb (EvolutionImporterListener *listener,
	   EvolutionImporterResult result,
	   gboolean more_items,
	   void *data)
{
	ImporterComponentData *icd = (ImporterComponentData *) data;
	char *label;

	IN;
	if (icd->stop != TRUE) {
		if (result == EVOLUTION_IMPORTER_NOT_READY) {
			/* Importer isn't ready yet. 
			   Wait 5 seconds and try again. */
			
			label = g_strdup_printf (_("Importing %s\nImporter not ready."
						   "\nWaiting 5 seconds to retry."),
						 icd->filename);
			gtk_label_set_text (GTK_LABEL (icd->contents), label);
			g_free (label);
			while (gtk_events_pending ())
				gtk_main_iteration ();
			
			gtk_timeout_add (5000, importer_timeout_fn, data);
			OUT;
			return;
		}
		
		if (result == EVOLUTION_IMPORTER_BUSY) {
			gtk_timeout_add (5000, importer_timeout_fn, data);
			OUT;
			return;
		}

		if (more_items) {
			label = g_strdup_printf (_("Importing %s\nImporting item %d."),
						 icd->filename, ++(icd->item));
			gtk_label_set_text (GTK_LABEL (icd->contents), label);
			g_free (label);
			while (gtk_events_pending ())
				gtk_main_iteration ();
			
			g_idle_add_full (G_PRIORITY_LOW, importer_timeout_fn, 
					 data, NULL);
			OUT;
			return;
		}
	}
	
	g_free (icd->filename);
	if (!icd->destroyed)
		gtk_object_destroy (GTK_OBJECT (icd->dialog));
	bonobo_object_unref (BONOBO_OBJECT (icd->listener));
	gtk_object_unref (GTK_OBJECT (icd->client));
	g_free (icd);

	OUT;
}

static gboolean
importer_timeout_fn (gpointer data)
{
	ImporterComponentData *icd = (ImporterComponentData *) data;
	char *label;

	IN;
	label = g_strdup_printf (_("Importing %s\nImporting item %d."),
				 icd->filename, icd->item);
	gtk_label_set_text (GTK_LABEL (icd->contents), label);
	g_free (label);
	while (gtk_events_pending ())
		gtk_main_iteration ();
	
	evolution_importer_client_process_item (icd->client, icd->listener);
	OUT;
	return FALSE;
}

static void
dialog_clicked_cb (GnomeDialog *dialog,
		   int button_number,
		   ImporterComponentData *icd)
{
	if (button_number != 0)
		return; /* Interesting... */

	icd->stop = TRUE;
}

static void
dialog_destroy_cb (GtkObject *object,
		   ImporterComponentData *icd)
{
	icd->stop = TRUE;
	icd->destroyed = TRUE;
}

static char *
get_iid_for_filetype (const char *filename)
{
	OAF_ServerInfoList *info_list;
	CORBA_Environment ev;
	GList *can_handle = NULL, *l;
	char *ret_iid;
	int i, len = 0;

	CORBA_exception_init (&ev);
	info_list = oaf_query ("repo_ids.has ('IDL:GNOME/Evolution/Importer:1.0')", NULL, &ev);

	for (i = 0; i < info_list->_length; i++) {
		CORBA_Environment ev2;
		CORBA_Object importer;
		const OAF_ServerInfo *info;

		info = info_list->_buffer + i;

		CORBA_exception_init (&ev2);
		importer = oaf_activate_from_id ((char *) info->iid, 0, NULL, &ev2);
		if (ev2._major != CORBA_NO_EXCEPTION) {
			g_warning ("Error activating %s", info->iid);
			CORBA_exception_free (&ev2);
			continue;
		}

		if (GNOME_Evolution_Importer_supportFormat (importer,
							    filename, &ev2)) {
			can_handle = g_list_prepend (can_handle, 
						     g_strdup (info->iid));
			len++;
		}

		bonobo_object_release_unref (importer, &ev2);
		CORBA_exception_free (&ev2);
	}
	CORBA_free (info_list);

	if (len == 1) {
		ret_iid = can_handle->data;
		g_list_free (can_handle);
		return ret_iid;
	} else if (len > 1) {
		/* FIXME: Some way to choose between multiple iids */
		/* FIXME: Free stuff */
		g_warning ("Multiple iids can support %s", filename);
		ret_iid = g_strdup (can_handle->data);
		
		for (l = can_handle; l; l = l->next)
			g_free (l->data);
		g_list_free (can_handle);
		return ret_iid;
	} else {
		return NULL;
	}
}

static void
show_error (const char *message,
	    const char *title)
{
	GtkWidget *box;

	box = gnome_message_box_new (message, GNOME_MESSAGE_BOX_ERROR, GNOME_STOCK_BUTTON_OK, NULL);
	gtk_window_set_title (GTK_WINDOW (box), title);

	gtk_widget_show (box);
}

static void
start_import (const char *folderpath,
	      const char *filename,
	      const char *iid)
{
	ImporterComponentData *icd;
	char *label;
	char *real_iid;
	char *localpath;
	struct stat buf;
	
	if (stat (filename, &buf) == -1) {
		char *message;

		message = g_strdup_printf (_("File %s does not exist"), filename);
		show_error (message, _("Evolution Error"));
		g_free (message);

		return;
	}

	/* Only allow importing to /local */
	localpath = "/" E_LOCAL_STORAGE_NAME "/";
	if (folderpath != NULL) {
		if (strncmp (folderpath, localpath, strlen (localpath))) {
			show_error (_("You may only import to local folders"), _("Evolution Error"));
			return;
		}
	}

	if (iid == NULL || strcmp (iid, "Automatic") == 0) {
		/* Work out the component to use */
		real_iid = get_iid_for_filetype (filename);
	} else {
		real_iid = g_strdup (iid);
	}

	if (real_iid == NULL) {
		char *message;

		message = g_strdup_printf (_("There is no importer that is able to handle\n%s"), filename);
		show_error (message, _("Evolution Error"));
		g_free (message);

		return;
	}

	icd = g_new (ImporterComponentData, 1);
	icd->stop = FALSE;
	icd->destroyed = FALSE;
	icd->dialog = GNOME_DIALOG (gnome_dialog_new (_("Importing"),
						      GNOME_STOCK_BUTTON_CANCEL,
						      NULL));
	gtk_signal_connect (GTK_OBJECT (icd->dialog), "clicked",
			    GTK_SIGNAL_FUNC (dialog_clicked_cb), icd);
	gtk_signal_connect (GTK_OBJECT (icd->dialog), "destroy",
			    GTK_SIGNAL_FUNC (dialog_destroy_cb), icd);
	
	label = g_strdup_printf (_("Importing %s.\nStarting %s"),
				 filename, real_iid);
	icd->contents = gtk_label_new (label);
	g_free (label);
	
	gtk_box_pack_start (GTK_BOX (icd->dialog->vbox), icd->contents,
			    TRUE, TRUE, 0);
	gtk_widget_show_all (GTK_WIDGET (icd->dialog));
	while (gtk_events_pending ())
		gtk_main_iteration ();
	
	icd->client = evolution_importer_client_new_from_id (real_iid);
	if (icd->client == NULL) {
		label = g_strdup_printf (_("Error starting %s"), real_iid);
		g_free (real_iid);
		gtk_label_set_text (GTK_LABEL (icd->contents), label);
		g_free (label);
		while (gtk_events_pending ())
			gtk_main_iteration ();

		gtk_object_unref (GTK_OBJECT (icd->dialog));
		g_free (icd);
		return;
	}
	g_free (real_iid);

	/* NULL for folderpath means use Inbox */
	if (*folderpath == '/') {
		folderpath = strchr (folderpath + 1, '/');
	}

	if (evolution_importer_client_load_file (icd->client, filename, folderpath) == FALSE) {
		label = g_strdup_printf (_("Error loading %s"), filename);
		show_error (label, _("Evolution Error"));

		gtk_label_set_text (GTK_LABEL (icd->contents), label);
		g_free (label);
		while (gtk_events_pending ())
			gtk_main_iteration ();
		
		gtk_object_unref (GTK_OBJECT (icd->client));
		gtk_object_unref (GTK_OBJECT (icd->dialog));
		g_free (icd);
		return;
	}

	icd->filename = g_strdup (filename);
	icd->item = 1;
	
	label = g_strdup_printf (_("Importing %s\nImporting item 1."),
				 filename);
	gtk_label_set_text (GTK_LABEL (icd->contents), label);
	g_free (label);
	while (gtk_events_pending ())
		gtk_main_iteration ();

	icd->listener = evolution_importer_listener_new (import_cb, icd);
	evolution_importer_client_process_item (icd->client, icd->listener);
}

static void
filename_changed (GtkEntry *entry,
		  ImportData *data)
{
	ImportDialogFilePage *page;
	char *filename;

	page = data->filepage;

	filename = gtk_entry_get_text (entry);
	if (filename != NULL && *filename != '\0')
		page->need_filename = FALSE;
	else
		page->need_filename = TRUE;

	gnome_druid_set_buttons_sensitive (GNOME_DRUID (data->druid), 
					   TRUE, !page->need_filename, TRUE);
}

static const char *
get_name_from_component_info (const OAF_ServerInfo *info)
{
	OAF_Property *property;
	const char *name;

	property = oaf_server_info_prop_find ((OAF_ServerInfo *) info,
					      "evolution:menu-name");
	if (property == NULL || property->v._d != OAF_P_STRING)
		return NULL;

	name = property->v._u.value_string;

	return name;
}

static void
item_selected (GtkWidget *item,
	       ImportData *data)
{
	char *iid;

	g_free (data->choosen_iid);
	iid = gtk_object_get_data (GTK_OBJECT (item), "oafiid");
	if (iid == NULL)
		data->choosen_iid = g_strdup ("Automatic");
	else
		data->choosen_iid = g_strdup (iid);
}

static GtkWidget *
create_plugin_menu (ImportData *data)
{
	OAF_ServerInfoList *info_list;
	CORBA_Environment ev;
	int i;
	GtkWidget *menu;
	GtkWidget *item;

	menu = gtk_menu_new ();
	item = gtk_menu_item_new_with_label (_("Automatic"));
	gtk_object_set_data_full (GTK_OBJECT (item), "oafiid",
				  g_strdup ("Automatic"), g_free);
	gtk_menu_append (GTK_MENU (menu), item);

	CORBA_exception_init (&ev);
	info_list = oaf_query ("repo_ids.has ('IDL:GNOME/Evolution/Importer:1.0')", NULL, &ev);
	for (i = 0; i < info_list->_length; i++) {
		const OAF_ServerInfo *info;
		char *name = NULL;

		info = info_list->_buffer + i;

		name = g_strdup (get_name_from_component_info (info));
		if (name == NULL) {
			name = g_strdup (info->iid);
		}

		item = gtk_menu_item_new_with_label (name);
		g_free (name);

		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (item_selected), data);

		gtk_object_set_data_full (GTK_OBJECT (item), "oafiid",
					  g_strdup (info->iid), g_free);
		gtk_menu_append (GTK_MENU (menu), item);
	}
	CORBA_free (info_list);

	return menu;
}

static ImportDialogFilePage *
importer_file_page_new (ImportData *data)
{
	ImportDialogFilePage *page;
	GtkWidget *table, *label;
	int row = 0;

	page = g_new0 (ImportDialogFilePage, 1);

	page->vbox = gtk_vbox_new (FALSE, 5);
	page->need_filename = TRUE;

	table = gtk_table_new (2, 2, FALSE);
	gtk_table_set_row_spacings (GTK_TABLE (table), 2);
	gtk_table_set_col_spacings (GTK_TABLE (table), 10);
	gtk_container_set_border_width (GTK_CONTAINER (table), 8);
	gtk_box_pack_start (GTK_BOX (page->vbox), table, TRUE, TRUE, 0);

	label = gtk_label_new (_("Filename:"));
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1, 
			  GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);

	page->filename = gnome_file_entry_new (NULL, _("Select a file"));
	gtk_signal_connect (GTK_OBJECT (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (page->filename))),
			    "changed", GTK_SIGNAL_FUNC (filename_changed),
			    data);

	gtk_table_attach (GTK_TABLE (table), page->filename, 1, 2, 
			  row, row + 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

	row++;

	label = gtk_label_new (_("File type:"));
	gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
			  GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);

	page->filetype = gtk_option_menu_new ();
	page->menu = create_plugin_menu (data);
	gtk_option_menu_set_menu (GTK_OPTION_MENU (page->filetype), page->menu);
	gtk_table_attach (GTK_TABLE (table), page->filetype, 1, 2, 
			  row, row + 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

	gtk_widget_show_all (table);

	return page;
}

static ImportDialogTypePage *
importer_type_page_new (ImportData *data)
{
	ImportDialogTypePage *page;

	page = g_new0 (ImportDialogTypePage, 1);

	page->vbox = gtk_vbox_new (FALSE, 5);
	page->intelligent = gtk_radio_button_new_with_label (NULL, 
							     _("Import data and settings from older programs"));
	gtk_box_pack_start (GTK_BOX (page->vbox), page->intelligent, FALSE, FALSE, 0);

	page->file = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (page->intelligent),
								  _("Import a single file"));
	gtk_box_pack_start (GTK_BOX (page->vbox), page->file, FALSE, FALSE, 0);
	gtk_widget_show_all (page->vbox);
	return page;
}

static ImportDialogImporterPage *
importer_importer_page_new (ImportData *data)
{
	ImportDialogImporterPage *page;
	GtkWidget *sep;

	page = g_new0 (ImportDialogImporterPage, 1);

	page->vbox = gtk_vbox_new (FALSE, 5);
	gtk_container_set_border_width (GTK_CONTAINER (page->vbox), 4);

	sep = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (page->vbox), sep, FALSE, FALSE, 0);

	page->prepared = FALSE;
	gtk_widget_show_all (page->vbox);

	return page;
}

static GList *
get_intelligent_importers (void)
{
	OAF_ServerInfoList *info_list;
	GList *iids_ret = NULL;
	CORBA_Environment ev;
	int i;

	CORBA_exception_init (&ev);
	info_list = oaf_query ("repo_ids.has ('IDL:GNOME/Evolution/IntelligentImporter:1.0')", NULL, &ev);
	CORBA_exception_free (&ev);

	for (i = 0; i < info_list->_length; i++) {
		const OAF_ServerInfo *info;

		info = info_list->_buffer + i;
		iids_ret = g_list_prepend (iids_ret, g_strdup (info->iid));
	}

	return iids_ret;
}

static gboolean
prepare_intelligent_page (GnomeDruid *druid,
			  GnomeDruidPage *page,
			  ImportData *data)
{
	GtkWidget *dialog;
	ImportDialogImporterPage *import;
	GList *l, *importers;
	GtkWidget *table;
	int running = 0;

	if (data->importerpage->prepared == TRUE) {
		return TRUE;
	}

	data->importerpage->prepared = TRUE;

	dialog = gnome_message_box_new (_("Please wait...\nScanning for existing setups"), GNOME_MESSAGE_BOX_INFO, NULL);
	e_make_widget_backing_stored (dialog);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Starting Intelligent Importers"));
	gtk_widget_show_all (dialog);
	gtk_widget_show_now (dialog);

	gtk_widget_queue_draw (dialog);
	gdk_flush ();

	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}

	import = data->importerpage;
	importers = get_intelligent_importers ();
	if (importers == NULL) {
		/* No importers, go directly to finish, do not pass go
		   Do not collect $200 */
		import->running = 0;
		gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->finish))
;
		gtk_widget_destroy (dialog);
		return TRUE;
	}

	table = gtk_table_new (g_list_length (importers), 2, FALSE);
	for (l = importers; l; l = l->next) {
		GtkWidget *label;
		IntelligentImporterData *id;
		CORBA_Environment ev;
		gboolean can_run;
		char *str;
		
		id = g_new0 (IntelligentImporterData, 1);
		id->iid = g_strdup (l->data);

		CORBA_exception_init (&ev);
		id->object = oaf_activate_from_id ((char *) id->iid, 0, NULL, &ev);
		if (BONOBO_EX (&ev)) {
			g_warning ("Could not start %s:%s", id->iid,
				   CORBA_exception_id (&ev));

			CORBA_exception_free (&ev);
			/* Clean up the IID */
			g_free (id->iid);
			g_free (id);
			continue;
		}

		if (id->object == CORBA_OBJECT_NIL) {
			g_warning ("Could not activate component %s", id->iid);
			CORBA_exception_free (&ev);

			g_free (id->iid);
			g_free (id);
			continue;
		}

		can_run = GNOME_Evolution_IntelligentImporter_canImport (id->object, &ev);
		if (BONOBO_EX (&ev)) {
			g_warning ("Could not call canImport(%s): %s", id->iid,
				   CORBA_exception_id (&ev));
			bonobo_object_release_unref (id->object, &ev);
			CORBA_exception_free (&ev);

			g_free (id->iid);
			g_free (id);
			continue;
		}

		if (can_run == FALSE) {
			bonobo_object_release_unref (id->object, &ev);
			CORBA_exception_free (&ev);
			g_free (id->iid);
			g_free (id);
			continue;
		}

		running++;
		id->name = GNOME_Evolution_IntelligentImporter__get_importername (id->object, &ev);
		if (BONOBO_EX (&ev)) {
			g_warning ("Could not get name(%s): %s", id->iid,
				   CORBA_exception_id (&ev));
			bonobo_object_release_unref (id->object, &ev);
			CORBA_exception_free (&ev);
			g_free (id->iid);
			g_free (id);
			continue;
		}

		id->blurb = GNOME_Evolution_IntelligentImporter__get_message (id->object, &ev);
		if (BONOBO_EX (&ev)) {
			g_warning ("Could not get message(%s): %s",
				   id->iid, CORBA_exception_id (&ev));
			bonobo_object_release_unref (id->object, &ev);
			CORBA_exception_free (&ev);
			g_free (id->iid);
			CORBA_free (id->name);
			g_free (id);
			continue;
		}

		id->control = Bonobo_Unknown_queryInterface (id->object,
							     "IDL:Bonobo/Control:1.0", &ev);
		if (BONOBO_EX (&ev)) {
			g_warning ("Could not QI for Bonobo/Control:1.0 %s:%s",
				   id->iid, CORBA_exception_id (&ev));
			bonobo_object_release_unref (id->object, &ev);
			CORBA_exception_free (&ev);
			g_free (id->iid);
			CORBA_free (id->name);
			CORBA_free (id->blurb);
			continue;
		}

		if (id->control != CORBA_OBJECT_NIL) {
			id->widget = bonobo_widget_new_control_from_objref (id->control, CORBA_OBJECT_NIL);
			gtk_widget_show (id->widget);
		} else {
			id->widget = gtk_label_new ("");
			gtk_widget_show (id->widget);
		}

		CORBA_exception_free (&ev);

		import->importers = g_list_prepend (import->importers, id);
		str = g_strdup_printf (_("From %s:"), id->name);
		label = gtk_label_new (str);
		g_free (str);
		
		gtk_misc_set_alignment (GTK_MISC (label), 0, .5); 

		gtk_table_attach (GTK_TABLE (table), label, 0, 1, running - 1,
				  running, GTK_FILL, 0, 0, 0);
		gtk_table_attach (GTK_TABLE (table), id->widget, 1, 2,
				  running - 1, running, GTK_FILL, 0, 3, 0);
		gtk_widget_show_all (table);

		gtk_box_pack_start (GTK_BOX (data->importerpage->vbox), table,
				    FALSE, FALSE, 0);
	}

	if (running == 0) {
		gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->finish));
		gtk_widget_destroy (dialog);
		return TRUE;
	}

	import->running = running;
	gtk_widget_destroy (dialog);

	return FALSE;
}

static void
import_druid_cancel (GnomeDruid *druid,
		     ImportData *data)
{
  	gtk_widget_destroy (GTK_WIDGET (data->dialog));
}

static void
import_druid_destroy (GtkObject *object,
		      ImportData *data)
{
	gtk_object_unref (GTK_OBJECT (data->wizard));
	g_free (data->choosen_iid);
	g_free (data);
}

static void
folder_selected (EShellFolderSelectionDialog *dialog,
		 const char *path,
		 ImportData *data)
{
	char *filename, *iid;

	iid = g_strdup (data->choosen_iid);
	filename = gnome_file_entry_get_full_path (GNOME_FILE_ENTRY (data->filepage->filename), FALSE);

	gtk_widget_destroy (data->dialog);
	gtk_widget_hide (GTK_WIDGET (dialog));

	start_import (path, filename, iid);

	g_free (iid);
	g_free (filename);
}

static void
folder_cancelled (EShellFolderSelectionDialog *dialog,
		  ImportData *data)
{
	gtk_widget_destroy (data->dialog);
}

static void
free_importers (ImportData *data)
{
	GList *l;

	for (l = data->importerpage->importers; l; l = l->next) {
		IntelligentImporterData *iid;

		iid = l->data;
		if (iid->object != CORBA_OBJECT_NIL) {
			bonobo_object_release_unref (iid->object, NULL);
		}
	}

	g_list_free (data->importerpage->importers);
}

static void
start_importers (GList *p)
{
	CORBA_Environment ev;
	
	for (; p; p = p->next) {
		SelectedImporterData *sid = p->data;

		CORBA_exception_init (&ev);
		GNOME_Evolution_IntelligentImporter_importData (sid->importer, &ev);
		if (BONOBO_EX (&ev)) {
			g_warning ("Error importing %s\n%s", sid->iid,
				   CORBA_exception_id (&ev));
		}
		CORBA_exception_free (&ev);
	}
}

static void
do_import (ImportData *data)
{
	CORBA_Environment ev;
	GList *l, *selected = NULL;

	for (l = data->importerpage->importers; l; l = l->next) {
		IntelligentImporterData *importer_data;
		SelectedImporterData *sid;
		char *iid;

		importer_data = l->data;
		iid = g_strdup (importer_data->iid);

		sid = g_new (SelectedImporterData, 1);
		sid->iid = iid;

		CORBA_exception_init (&ev);
		sid->importer = bonobo_object_dup_ref (importer_data->object, &ev);
		if (BONOBO_EX (&ev)) {
			g_warning ("Error duplication %s\n(%s)", iid,
				   CORBA_exception_id (&ev));
			g_free (iid);
			CORBA_exception_free (&ev);
			g_free (sid);
			continue;
		}
		CORBA_exception_free (&ev);

		selected = g_list_prepend (selected, sid);
	}

	free_importers (data);

	if (selected != NULL) {
		start_importers (selected);

		for (l = selected; l; l = l->next) {
			SelectedImporterData *sid = l->data;

			CORBA_exception_init (&ev);
			bonobo_object_release_unref (sid->importer, &ev);
			CORBA_exception_free (&ev);

			g_free (sid->iid);
			g_free (sid);
		}
		g_list_free (selected);
	}
}
				
static void
import_druid_finish (GnomeDruidPage *page,
		     GnomeDruid *druid,
		     ImportData *data)
{
	GtkWidget *folder;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->typepage->intelligent))) {
		do_import (data);
		gtk_widget_destroy (data->dialog);
	} else {
		folder = e_shell_folder_selection_dialog_new (data->shell, 
							      _("Select folder"),
							      _("Select a destination folder for importing this data"),
							      e_shell_view_get_current_uri (data->view),
							      NULL, NULL);
		
		gtk_signal_connect (GTK_OBJECT (folder), "folder_selected",
				    GTK_SIGNAL_FUNC (folder_selected), data);
		gtk_signal_connect (GTK_OBJECT (folder), "cancelled",
				    GTK_SIGNAL_FUNC (folder_cancelled), data);
		
		gtk_widget_hide (data->dialog);
		gtk_widget_show (folder);
	}
}

static gboolean
prepare_file_page (GnomeDruidPage *page,
		   GnomeDruid *druid,
		   ImportData *data)
{
	gnome_druid_set_buttons_sensitive (druid, TRUE, 
					   !data->filepage->need_filename, 
					   TRUE);
	return FALSE;
}

static gboolean
next_file_page (GnomeDruidPage *page,
		GnomeDruid *druid,
		ImportData *data)
{
	gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->finish));
	return TRUE;
}

static gboolean
back_file_page (GnomeDruidPage *page,
		GnomeDruid *druid,
		ImportData *data)
{
	gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->typedialog));
	return TRUE;
}

static gboolean
next_type_page (GnomeDruidPage *page,
		GnomeDruid *druid,
		ImportData *data)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->typepage->intelligent))) {
		gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->intelligent));
	} else {
		gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->filedialog));
	}

	return TRUE;
}

static gboolean
back_finish_page (GnomeDruidPage *page,
		GnomeDruid *druid,
		ImportData *data)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->typepage->intelligent))) {
		if (data->importerpage->running != 0) {
			gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->intelligent));
		} else {
			gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->typedialog));
		}
	} else {
		gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->filedialog));
	}

	return TRUE;
}

static gboolean
back_intelligent_page (GnomeDruidPage *page,
		       GnomeDruid *druid,
		       ImportData *data)
{
	gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->typedialog));
	return TRUE;
}

static gboolean
next_intelligent_page (GnomeDruidPage *page,
		       GnomeDruid *druid,
		       ImportData *data)
{
	gnome_druid_set_page (druid, GNOME_DRUID_PAGE (data->finish));
	return TRUE;
}

/* Hack to change the Finish button */
static void
druid_finish_button_change (GnomeDruid *druid)
{
	GtkWidget *button = druid->finish;
	GtkWidget *hbox = GTK_BIN (button)->child, *hbox2;
	GtkBoxChild *child;
	GtkWidget *label;

	/* Get the second item from the children list */
	hbox2 = ((GtkBoxChild *)GTK_BOX (hbox)->children->data)->widget;

	g_return_if_fail (GTK_IS_BOX (hbox2));
	child = (GtkBoxChild *)g_list_nth_data (GTK_BOX (hbox2)->children, 0);
	label = child->widget;

	/* Safety check :) */
	g_return_if_fail (GTK_IS_LABEL (label));

	gtk_label_set_text (GTK_LABEL (label), _("Import"));
}

void
show_import_wizard (BonoboUIComponent *component,
		    gpointer           user_data,
		    const char        *cname)
{
	ImportData *data = g_new0 (ImportData, 1);
	GtkWidget *html;

	data->view = E_SHELL_VIEW (user_data);
	data->shell = e_shell_view_get_shell (data->view);

	data->wizard = glade_xml_new (EVOLUTION_GLADEDIR "/import.glade", NULL);
	data->dialog = glade_xml_get_widget (data->wizard, "importwizard");
	gtk_window_set_wmclass (GTK_WINDOW (data->dialog), "importdruid",
				"Evolution:shell");
	gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (user_data));
	
	data->druid = glade_xml_get_widget (data->wizard, "druid1");
	gtk_signal_connect (GTK_OBJECT (data->druid), "cancel",
			    GTK_SIGNAL_FUNC (import_druid_cancel), data);

	druid_finish_button_change (GNOME_DRUID (data->druid));
	data->start = GNOME_DRUID_PAGE_START (glade_xml_get_widget (data->wizard, "page0"));

	data->typedialog = glade_xml_get_widget (data->wizard, "page1");
	gtk_signal_connect (GTK_OBJECT (data->typedialog), "next",
			    GTK_SIGNAL_FUNC (next_type_page), data);
	data->typepage = importer_type_page_new (data);
	html = create_html ("type_html");
	gtk_box_pack_start (GTK_BOX (data->typepage->vbox), html, FALSE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (data->typepage->vbox), html, 0);

	gtk_box_pack_start (GTK_BOX (GNOME_DRUID_PAGE_STANDARD (data->typedialog)->vbox), data->typepage->vbox, TRUE, TRUE, 0);



	data->intelligent = glade_xml_get_widget (data->wizard, "page2-intelligent");
	gtk_signal_connect (GTK_OBJECT (data->intelligent), "next",
			    GTK_SIGNAL_FUNC (next_intelligent_page), data);
	gtk_signal_connect (GTK_OBJECT (data->intelligent), "back",
			    GTK_SIGNAL_FUNC (back_intelligent_page), data);
	gtk_signal_connect (GTK_OBJECT (data->intelligent), "prepare",
			    GTK_SIGNAL_FUNC (prepare_intelligent_page), data);

	data->importerpage = importer_importer_page_new (data);
	html = create_html ("intelligent_html");
	gtk_box_pack_start (GTK_BOX (data->importerpage->vbox), html, FALSE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (data->importerpage->vbox), html, 0);
	
	gtk_box_pack_start (GTK_BOX (GNOME_DRUID_PAGE_STANDARD (data->intelligent)->vbox), data->importerpage->vbox, TRUE, TRUE, 0);
	

	data->filedialog = glade_xml_get_widget (data->wizard, "page2-file");
	gtk_signal_connect (GTK_OBJECT (data->filedialog), "prepare",
			    GTK_SIGNAL_FUNC (prepare_file_page), data);
	gtk_signal_connect (GTK_OBJECT (data->filedialog), "next",
			    GTK_SIGNAL_FUNC (next_file_page), data);
	gtk_signal_connect (GTK_OBJECT (data->filedialog), "back",
			    GTK_SIGNAL_FUNC (back_file_page), data);

	data->finish = GNOME_DRUID_PAGE_FINISH (glade_xml_get_widget (data->wizard, "page3"));
	gtk_signal_connect (GTK_OBJECT (data->finish), "back",
			    GTK_SIGNAL_FUNC (back_finish_page), data);

	data->filepage = importer_file_page_new (data);

	html = create_html ("file_html");
	gtk_box_pack_start (GTK_BOX (data->filepage->vbox), html, FALSE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (data->filepage->vbox), html, 0);

	gtk_box_pack_start (GTK_BOX (GNOME_DRUID_PAGE_STANDARD (data->filedialog)->vbox), data->filepage->vbox, TRUE, TRUE, 0);

	/* Finish page */
	gtk_signal_connect (GTK_OBJECT (data->finish), "finish",
			    GTK_SIGNAL_FUNC (import_druid_finish), data);
	gtk_signal_connect (GTK_OBJECT (data->dialog), "destroy",
			    GTK_SIGNAL_FUNC (import_druid_destroy), data);

	gtk_widget_show_all (data->dialog);
}
