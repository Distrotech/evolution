/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* elm-importer.c
 * 
 * Authors: 
 *    Iain Holmes  <iain@ximian.com>
 *
 * Copyright 2001 Ximian, Inc. (www.ximian.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <glib.h>
#include <gnome.h>

#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-context.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <bonobo-conf/bonobo-config-database.h>

#include <importer/evolution-intelligent-importer.h>
#include <importer/evolution-importer-client.h>
#include <importer/GNOME_Evolution_Importer.h>

#define ELM_INTELLIGENT_IMPORTER_IID "OAFIID:GNOME_Evolution_Elm_Intelligent_Importer_Factory"
#define MBOX_IMPORTER_IID "OAFIID:GNOME_Evolution_Mail_Mbox_Importer"
#define KEY "elm-mail-imported"

/*#define SUPER_IMPORTER_DEBUG*/
#ifdef SUPER_IMPORTER_DEBUG
#define d(x) x
#else
#define d(x)
#endif

typedef struct {
	GList *dir_list;

	int num;
	
	GNOME_Evolution_Importer importer;
	EvolutionImporterListener *listener;

	GtkWidget *mail;
	gboolean do_mail;

	Bonobo_ConfigDatabase db;

	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *progressbar;
} ElmImporter;

typedef struct {
	char *parent;
	char *foldername;
	char *path;
} ElmFolder;

static GHashTable *elm_prefs = NULL;

static void import_next (ElmImporter *importer);

static GtkWidget *
create_importer_gui (ElmImporter *importer)
{
	GtkWidget *dialog;

	dialog = gnome_message_box_new (_("Evolution is importing your old Elm mail"), GNOME_MESSAGE_BOX_INFO, NULL);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Importing..."));

	importer->label = gtk_label_new (_("Please wait"));
	importer->progressbar = gtk_progress_bar_new ();
	gtk_progress_set_activity_mode (GTK_PROGRESS (importer->progressbar), TRUE);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), importer->label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), importer->progressbar, FALSE, FALSE, 0);

	return dialog;
}

static void
elm_store_settings (ElmImporter *importer)
{
	bonobo_config_set_boolean (importer->db, "/Importer/Elm/mail", 
				   importer->do_mail, NULL);
}

static void
elm_restore_settings (ElmImporter *importer)
{
	importer->do_mail = bonobo_config_get_boolean_with_default (
		importer->db, "/Importer/Elm/mail", TRUE, NULL);
}

static void
importer_cb (EvolutionImporterListener *listener,
	     EvolutionImporterResult result,
	     gboolean more_items,
	     void *data)
{
	ElmImporter *importer = (ElmImporter *) data;
	CORBA_Object objref;
	CORBA_Environment ev;

	if (more_items) {
		GtkAdjustment *adj;
		float newval;

		adj = GTK_PROGRESS (importer->progressbar)->adjustment;
		newval = adj->value + 1;
		if (newval > adj->upper) {
			newval = adj->lower;
		}

		gtk_progress_set_value (GTK_PROGRESS (importer->progressbar), newval);
		CORBA_exception_init (&ev);
		objref = bonobo_object_corba_objref (BONOBO_OBJECT (importer->listener));
		GNOME_Evolution_Importer_processItem (importer->importer,
						      objref, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("Exception: %s", CORBA_exception_id (&ev));
			CORBA_exception_free (&ev);
			return;
		}
		CORBA_exception_free (&ev);
		return;
	}

	import_next (importer);
}

static gboolean
elm_import_file (ElmImporter *importer,
		 const char *path,
		 const char *folderpath)
{
	CORBA_boolean result;
	CORBA_Environment ev;
	CORBA_Object objref;
	char *str;

	CORBA_exception_init (&ev);

	str = g_strdup_printf (_("Importing %s as %s"), path, folderpath);
	gtk_label_set_text (GTK_LABEL (importer->label), str);
	g_free (str);
	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}

	result = GNOME_Evolution_Importer_loadFile (importer->importer, path,
						    folderpath, &ev);
	if (ev._major != CORBA_NO_EXCEPTION || result == FALSE) {
		g_warning ("Exception here: %s", CORBA_exception_id (&ev));
		CORBA_Object_release (importer->importer, &ev);
		CORBA_exception_free (&ev);
		return FALSE;
	}

	importer->listener = evolution_importer_listener_new (importer_cb,
							      importer);
	objref = bonobo_object_corba_objref (BONOBO_OBJECT (importer->listener));
	GNOME_Evolution_Importer_processItem (importer->importer, objref, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("Exception: %s", CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return FALSE;
	}
	CORBA_exception_free (&ev);

	return TRUE;
}

static void
parse_elm_rc (const char *elmrc)
{
	static gboolean parsed = FALSE;
	char line[4096];
	FILE *handle;
	gboolean exists;

	if (parsed == TRUE)
		return;

	elm_prefs = g_hash_table_new (g_str_hash, g_str_equal);

	exists = g_file_exists (elmrc);
	if (exists == FALSE) {
		parsed = TRUE;
		return;
	}

	handle = fopen (elmrc, "r");
	if (handle == NULL) {
		parsed = TRUE;
		return;
	}

	while (fgets (line, 4096, handle) != NULL) {
		char *linestart, *end;
		char *key, *value;
		if (*line == '#' &&
		    (line[1] != '#' && line[2] != '#')) {
			continue;
		} else if (*line == '\n') {
			continue;
		} else if (*line == '#' && line[1] == '#' && line[2] == '#') {
			linestart = line + 4;
		} else {
			linestart = line;
		}

		end = strstr (linestart, " = ");
		if (end == NULL) {
			g_warning ("Broken line");
			continue;
		}

		*end = 0;
		key = g_strdup (linestart);

		linestart = end + 3;
		end = strchr (linestart, '\n');
		if (end == NULL) {
			g_warning ("Broken line");
			g_free (key);
			continue;
		}

		*end = 0;
		value = g_strdup (linestart);

		g_hash_table_insert (elm_prefs, key, value);
	}

	parsed = TRUE;
	fclose (handle);
}

static char *
elm_get_rc_value (const char *value)
{
	if (elm_prefs == NULL)
		return NULL;

	return g_hash_table_lookup (elm_prefs, value);
}

static gboolean
elm_can_import (EvolutionIntelligentImporter *ii,
		void *closure)
{
	ElmImporter *importer = closure;
	char *elmdir, *maildir, *aliasfile;
	char *elmrc;
	gboolean exists, mailexists, aliasexists;
	gboolean mail, alias;

	mail = bonobo_config_get_boolean_with_default (importer->db, 
                "/Importer/Elm/mail-imported", FALSE, NULL);

	if (mail)
		return FALSE;
	
	importer->do_mail = !mail;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (importer->mail),
				      importer->do_mail);
	
	elmdir = gnome_util_prepend_user_home (".elm");
	exists = g_file_exists (elmdir);
	
	g_free (elmdir);
	if (exists == FALSE)
		return FALSE;

	elmrc = gnome_util_prepend_user_home (".elm/elmrc");
	parse_elm_rc (elmrc);

	maildir = elm_get_rc_value ("maildir");
	if (maildir == NULL) {
		maildir = g_strdup ("Mail");
	} else {
		maildir = g_strdup (maildir);
	}

	if (!g_path_is_absolute (maildir)) {
		elmdir = gnome_util_prepend_user_home (maildir);
	} else {
		elmdir = g_strdup (maildir);
	}

	g_free (maildir);

	mailexists = g_file_exists (elmdir);
	g_free (elmdir);

	aliasfile = gnome_util_prepend_user_home (".elm/aliases");
	aliasexists = g_file_exists (aliasfile);
	g_free (aliasfile);

	exists = (aliasexists || mailexists);

	return exists;
}

static void
import_next (ElmImporter *importer)
{
	ElmFolder *data;

	if (importer->dir_list) {
		char *folder;

		data = importer->dir_list->data;

		folder = g_concat_dir_and_file (data->parent, data->foldername);
		elm_import_file (importer, data->path, folder);
		g_free (folder);
		g_free (data->parent);
		g_free (data->path);
		g_free (data->foldername);
		g_free (data);
		importer->dir_list = importer->dir_list->next;
	}

	if (importer->db) {
		bonobo_object_release_unref (importer->db, NULL);
	}
	bonobo_object_release_unref (importer->importer, NULL);
	gtk_main_quit ();
}

static void
scan_dir (ElmImporter *importer,
	  const char *orig_parent,
	  const char *dirname)
{
	DIR *maildir;
	struct stat buf;
	struct dirent *current;
	char *str;
	
	maildir = opendir (dirname);
	if (maildir == NULL) {
		g_warning ("Could not open %s\nopendir returned: %s",
			   dirname, g_strerror (errno));
		return;
	}
	
	str = g_strdup_printf (_("Scanning %s"), dirname);
	gtk_label_set_text (GTK_LABEL (importer->label), str);
	g_free (str);

	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}

	current = readdir (maildir);
	while (current) {
		ElmFolder *pf;
		char *fullname;
		
		/* Ignore . and .. */
		if (current->d_name[0] == '.') {
			if (current->d_name[1] == '\0' ||
			    (current->d_name[1] == '.' && current->d_name[2] == '\0')) {
				current = readdir (maildir);
				continue;
			}
		}
		
		fullname = g_concat_dir_and_file (dirname, current->d_name);
		if (stat (fullname, &buf) == -1) {
			g_warning ("Could not stat %s\nstat returned: %s",
				   fullname, g_strerror (errno));
			current = readdir (maildir);
			g_free (fullname);
			continue;
		}
		
		if (S_ISREG (buf.st_mode)) {
			pf = g_new (ElmFolder, 1);
			pf->path = g_strdup (fullname);
			pf->parent = g_strdup (orig_parent);
			pf->foldername = g_strdup (current->d_name);
			importer->dir_list = g_list_append (importer->dir_list, pf);
		} else if (S_ISDIR (buf.st_mode)) {
			char *subdir;

			pf = g_new (ElmFolder, 1);
			pf->path = NULL;
			pf->parent = g_strdup (orig_parent);
			pf->foldername = g_strdup (current->d_name);
			importer->dir_list = g_list_append (importer->dir_list, pf);

			subdir = g_concat_dir_and_file (orig_parent, current->d_name);
			scan_dir (importer, subdir, fullname);
			g_free (subdir);
		}
		
		g_free (fullname);
		current = readdir (maildir);
	}
}

static void
elm_create_structure (EvolutionIntelligentImporter *ii,
		      void *closure)
{
	CORBA_Environment ev;
	ElmImporter *importer = closure;
	char *maildir;

	/* Reference our object so when the shell release_unrefs us
	   we will still exist and not go byebye */
	bonobo_object_ref (BONOBO_OBJECT (ii));

	elm_store_settings (importer);

	/* Create a dialog */
	importer->dialog = create_importer_gui (importer);
	gtk_widget_show_all (importer->dialog);
	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}

	if (importer->do_mail == TRUE) {
		char *elmdir;

		bonobo_config_set_boolean (importer->db, 
                "/Importer/Elm/mail-imported", TRUE, NULL);

		maildir = elm_get_rc_value ("maildir");
		if (maildir == NULL) {
			maildir = g_strdup ("Mail");
		} else {
			maildir = g_strdup (maildir);
		}
		
		if (!g_path_is_absolute (maildir)) {
			elmdir = gnome_util_prepend_user_home (maildir);
		} else {
			elmdir = g_strdup (maildir);
		}
		
		g_free (maildir);

		scan_dir (importer, "/", maildir);
		g_free (maildir);
		
		/* Import them */
		import_next (importer);
	}

	CORBA_exception_init (&ev);
	Bonobo_ConfigDatabase_sync (importer->db, &ev);
	CORBA_exception_free (&ev);

	if (importer->do_mail == FALSE) {
		bonobo_object_unref (BONOBO_OBJECT (ii));
	}
}

static void
elm_destroy_cb (GtkObject *object,
		ElmImporter *importer)
{
	CORBA_Environment ev;

	elm_store_settings (importer);

	CORBA_exception_init (&ev);
	Bonobo_ConfigDatabase_sync (importer->db, &ev);
	CORBA_exception_free (&ev);

	if (importer->db != CORBA_OBJECT_NIL)
		bonobo_object_release_unref (importer->db, NULL);
	importer->db = CORBA_OBJECT_NIL;
	bonobo_object_release_unref (importer->importer, NULL);

	gtk_main_quit ();
}

/* Fun initialisation stuff */
/* Fun control stuff */
static void
checkbox_toggle_cb (GtkToggleButton *tb,
		    gboolean *do_item)
{
	*do_item = gtk_toggle_button_get_active (tb);
}

static BonoboControl *
create_checkboxes_control (ElmImporter *importer)
{
	GtkWidget *hbox;
	BonoboControl *control;

	hbox = gtk_vbox_new (FALSE, 2);

	importer->mail = gtk_check_button_new_with_label (_("Mail"));
	gtk_signal_connect (GTK_OBJECT (importer->mail), "toggled",
			    GTK_SIGNAL_FUNC (checkbox_toggle_cb),
			    &importer->do_mail);

	gtk_box_pack_start (GTK_BOX (hbox), importer->mail, FALSE, FALSE, 0);

	gtk_widget_show_all (hbox);
	control = bonobo_control_new (hbox);
	return control;
}

static BonoboObject *
factory_fn (BonoboGenericFactory *_factory,
	    void *closure)
{
	EvolutionIntelligentImporter *importer;
	BonoboControl *control;
	ElmImporter *elm;
	CORBA_Environment ev;
	char *message = N_("Evolution has found Elm mail files\n"
			   "Would you like to import them into Evolution?");

	elm = g_new0 (ElmImporter, 1);

	CORBA_exception_init (&ev);

	elm->db = bonobo_get_object ("wombat:", "Bonobo/ConfigDatabase", &ev);

	if (BONOBO_EX (&ev) || elm->db == CORBA_OBJECT_NIL) {
		g_free (elm);
		CORBA_exception_free (&ev);
		return NULL;
 	}

	elm_restore_settings (elm);

	elm->importer = oaf_activate_from_id (MBOX_IMPORTER_IID, 0, NULL, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_free (elm);
		g_warning ("Could not start MBox importer\n%s", 
			   CORBA_exception_id (&ev));
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	importer = evolution_intelligent_importer_new (elm_can_import,
						       elm_create_structure,
						       _("Elm"),
						       _(message), elm);
	gtk_signal_connect (GTK_OBJECT (importer), "destroy",
			    GTK_SIGNAL_FUNC (elm_destroy_cb), elm);

	control = create_checkboxes_control (elm);
	bonobo_object_add_interface (BONOBO_OBJECT (importer),
				     BONOBO_OBJECT (control));
	return BONOBO_OBJECT (importer);
}

/* Entry point */
static void
importer_init (void)
{
	BonoboGenericFactory *factory;

	factory = bonobo_generic_factory_new (ELM_INTELLIGENT_IMPORTER_IID,
					      factory_fn, NULL);
	if (factory == NULL) {
		g_error ("Could not initialise Elm Intelligent Mail Importer.");
		exit (0);
	}

	bonobo_running_context_auto_exit_unref (BONOBO_OBJECT (factory));
}

int
main (int argc,
      char **argv)
{
	CORBA_ORB orb;

	bindtextdomain(PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain(PACKAGE);
	
	gnome_init_with_popt_table ("Evolution-Elm-Importer",
				    VERSION, argc, argv, oaf_popt_options, 0,
				    NULL);
	orb = oaf_init (argc, argv);
	if (bonobo_init (orb, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL) == FALSE) {
		g_error ("Could not initialise Bonobo");
		exit (0);
	}

	importer_init ();
	bonobo_main ();

	return 0;
}
