/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-setup.c
 *
 * Copyright (C) 2000  Helix Code, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
 */

/* This needs to be a lot better.  */

#include <glib.h>
#include <gnome.h>

#include <errno.h>
#include <sys/stat.h>

#include "e-util/e-gui-utils.h"

#include "e-setup.h"


static gboolean
copy_default_stuff (const char *evolution_directory)
{
	GtkWidget *dialog;
	GtkWidget *label1;
	GtkWidget *label2;
	GtkWidget *label3;
	gboolean retval;
	char *command;
	int result;

	dialog = gnome_dialog_new (_("Evolution installation"),
				   GNOME_STOCK_BUTTON_OK, GNOME_STOCK_BUTTON_CANCEL,
				   NULL);

	label1 = gtk_label_new (_("This seems to be the first time you run Evolution."));
	label2 = gtk_label_new (_("Please click \"OK\" to install the Evolution user files under"));
	label3 = gtk_label_new (evolution_directory);

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), label1, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), label2, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), label3, TRUE, TRUE, 0);

	gtk_widget_show (label1);
	gtk_widget_show (label2);
	gtk_widget_show (label3);

	result = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	if (result != 0)
		return FALSE;

	if (mkdir (evolution_directory, 0700)) {
		e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
			  _("Cannot create the directory\n%s\nError: %s"),
			  evolution_directory,
			  g_strerror (errno));
		return FALSE;
	}

	command = g_strconcat ("cp -r ",
			       EVOLUTION_DATADIR,
			       "/evolution/default_user/* ",
			       evolution_directory,
			       NULL);

	if (system (command) != 0) {
		/* FIXME: Give more help.  */
		e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
			  _("Cannot copy files into\n`%s'."), evolution_directory);
		retval = FALSE;
	} else {
		e_notice (NULL, GNOME_MESSAGE_BOX_INFO,
			  _("Evolution files successfully installed."));
		retval = TRUE;
	}

	g_free (command);

	return retval;
}


/* Temporary hack to set up the Sent folder.  */
static gboolean
setup_sent_folder (const char *evolution_directory)
{
	struct stat statinfo;
	char *sent_folder_path;
	char *sent_folder_metadata_path;
	FILE *f;

	sent_folder_path = g_concat_dir_and_file (evolution_directory, "local/Sent");

	if (stat (sent_folder_path, &statinfo) != 0) {
		if (mkdir (sent_folder_path, 0700) != 0) {
			e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				  _("Cannot create the `%s' directory:\n%s"),
				  sent_folder_path, strerror (errno));
			g_free (sent_folder_path);
			return FALSE;
		}
	} else {
		if (! S_ISDIR (statinfo.st_mode)) {
			e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				  _("File `%s' exists, but is not a directory.\n"
				    "Please remove it and restart Evolution."),
				  sent_folder_path);
			g_free (sent_folder_path);
			return FALSE;
		}
	}

	/* So, the directory exists.  Now create the metadata file.  Sigh, this
           is a totally bad hack.  */

	sent_folder_metadata_path = g_concat_dir_and_file (sent_folder_path, "folder-metadata.xml");

	f = fopen (sent_folder_metadata_path, "w");
	if (f == NULL) {
		e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
			  _("Cannot create `%s':\n%s"),
			  sent_folder_path, g_strerror (errno));
		g_free (sent_folder_path);
		g_free (sent_folder_metadata_path);
		return FALSE;
	}

	fprintf (f,
		 "<?xml version=\"1.0\"?>\n"
		 "<efolder>\n"
		 "	<type>mail</type>\n"
		 "	<description>Sent mail folder</description>\n"
		 "</efolder>\n");

	fclose (f);

	g_free (sent_folder_metadata_path);
	g_free (sent_folder_path);

	return TRUE;
}


gboolean
e_setup (const char *evolution_directory)
{
	struct stat statinfo;
	char *file;

	if (stat (evolution_directory, &statinfo) != 0)
		return copy_default_stuff (evolution_directory);

	if (! S_ISDIR (statinfo.st_mode)) {
		e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
			  _("The file `%s' is not a directory.\n"
			    "Please move it in order to allow installation\n"
			    "of the Evolution user files."));
		return FALSE;
	}

	/* Make sure this is really our directory, not an Evolution
	 * build tree or something like that.
	 */
	file = g_strdup_printf ("%s/shortcuts.xml", evolution_directory);
	if (stat (file, &statinfo) != 0) {
		e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
			  _("The directory `%s' exists but is not the\n"
			    "Evolution directory.  Please move it in order\n"
			    "to allow installation of the Evolution user "
			    "files."), evolution_directory);
		g_free (file);
		return FALSE;
	}
	g_free (file);

	/* If the user has an old-style config file, replace it with
	 * the new-style config directory. FIXME: This should be
	 * temporary.
	 */
	file = g_strdup_printf ("%s/config", evolution_directory);
	if (stat (file, &statinfo) == 0 && ! S_ISDIR (statinfo.st_mode)) {
		char *old = g_strdup_printf ("%s.old", file);
		rename (file, old);
		mkdir (file, 0700);
		g_free (old);
	}
	g_free (file);

	/* Finally, make sure there is a Sent folder.
	 * FIXME: This should not be done here.
	 */
	return setup_sent_folder (evolution_directory);
}
