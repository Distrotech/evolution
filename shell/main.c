/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* main.c
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
 * Author: Ettore Perazzoli
 */

#include <config.h>
#include <fcntl.h>
#include <glib.h>

#include <gtk/gtkframe.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>

#include <gdk/gdkx.h>
#include <X11/Xlib.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <libgnomeui/gnome-init.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnomeui/gnome-window-icon.h>
#include <bonobo/bonobo-main.h>
#include <glade/glade.h>
#include <liboaf/liboaf.h>

#ifdef GTKHTML_HAVE_GCONF
#include <gconf/gconf.h>
#endif

#include <gal/widgets/e-gui-utils.h>
#include <gal/widgets/e-cursors.h>

#include "e-util/e-gtk-utils.h"

#include "e-shell-constants.h"
#include "e-setup.h"

#include "e-shell.h"


static EShell *shell = NULL;
static char *evolution_directory = NULL;
static gboolean no_splash = FALSE;
extern char *evolution_debug_log;


static GtkWidget *
quit_box_new (void)
{
	GtkWidget *window;
	GtkWidget *label;
	GtkWidget *frame;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);

	/* (Just to prevent smart-ass window managers like Sawfish from setting
	  the make the dialog as big as the standard Evolution window).  */
	gtk_window_set_wmclass (GTK_WINDOW (window), "evolution-quit", "Evolution:quit");

	e_make_widget_backing_stored (window);

	gtk_window_set_title (GTK_WINDOW (window), _("Evolution"));

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
	gtk_container_add (GTK_CONTAINER (window), frame);

	label = gtk_label_new (_("Evolution is now exiting ..."));
	gtk_misc_set_padding (GTK_MISC (label), 30, 25);

	gtk_container_add (GTK_CONTAINER (frame), label);

	gtk_widget_show_now (frame);
	gtk_widget_show_now (label);
	gtk_widget_show_now (window);

	/* For some reason, the window fails to update without this
	   sometimes.  */
	gtk_widget_queue_draw (window);
	gtk_widget_queue_draw (label);
	gtk_widget_queue_draw (frame);

	gdk_flush ();

	while (gtk_events_pending ())
		gtk_main_iteration ();

	gdk_flush ();

	return window;
}

static void
quit_box_destroyed_callback (GtkObject *object,
			     void *data)
{
	GtkWidget **p;

	p = (GtkWidget **) data;
	*p = NULL;
}

static void
no_views_left_cb (EShell *shell, gpointer data)
{
	GtkWidget *quit_box;

	quit_box = quit_box_new ();
	gtk_signal_connect (GTK_OBJECT (quit_box), "destroy", quit_box_destroyed_callback, &quit_box);

	/* FIXME: This is wrong.  We should exit only when the shell is
	   destroyed.  But refcounting is broken at present, so this is a
	   reasonable workaround for now.  */

	e_shell_unregister_all (shell);

	/* FIXME: And this is another ugly hack.  We have a strange race
	   condition that I cannot work around.  What happens is that the
	   EShell object gets unreffed and its aggregate EActivityHandler gets
	   destroyed too.  But for some reason, the EActivityHanlder GtkObject
	   gets freed, while its CORBA object counterpart is still an active
	   server.  So there is a slight chance that we receive CORBA
	   invocation that act on an uninitialized object, and we crash.  (See
	   #8615.) 

	   The CORBA invocation on the dead object only happens because we
	   ::unref the BonoboConf database server in the ::destroy method of
	   the shell.  Since this is a CORBA call, it allows incoming CORBA
	   calls to happen -- and these get invoked on the partially
	   uninitialized object.

	   Since I am not 100% sure what the reason for this half-stale object
	   is, I am just going to make sure that no CORBA ops happen in
	   ::destroy...  And this is achieved by placing this call here.  (If
	   the DB is disconnected, there will be no ::unref of it in
	   ::destroy.)  */

	e_shell_disconnect_db (shell);

	bonobo_object_unref (BONOBO_OBJECT (shell));

	if (quit_box != NULL)
		gtk_widget_destroy (quit_box);

	gtk_main_quit ();
}

static void
destroy_cb (GtkObject *object, gpointer data)
{
	gtk_main_quit ();
}


/* This is for doing stuff that requires the GTK+ loop to be running already.  */

static gint
idle_cb (void *data)
{
	GSList *uri_list;
	GNOME_Evolution_Shell corba_shell;
	CORBA_Environment ev;
	EShellConstructResult result;
	GSList *p;
	gboolean have_evolution_uri;
	gboolean display_default;

	CORBA_exception_init (&ev);

	uri_list = (GSList *) data;

	shell = e_shell_new (evolution_directory, ! no_splash, &result);
	g_free (evolution_directory);

	switch (result) {
	case E_SHELL_CONSTRUCT_RESULT_OK:
		gtk_signal_connect (GTK_OBJECT (shell), "no_views_left",
				    GTK_SIGNAL_FUNC (no_views_left_cb), NULL);
		gtk_signal_connect (GTK_OBJECT (shell), "destroy",
				    GTK_SIGNAL_FUNC (destroy_cb), NULL);
		corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell));
		corba_shell = CORBA_Object_duplicate (corba_shell, &ev);
		break;

	case E_SHELL_CONSTRUCT_RESULT_CANNOTREGISTER:
		corba_shell = oaf_activate_from_id (E_SHELL_OAFIID, 0, NULL, &ev);
		if (ev._major != CORBA_NO_EXCEPTION || corba_shell == CORBA_OBJECT_NIL) {
			e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
				  _("Cannot access the Ximian Evolution shell."));
			CORBA_exception_free (&ev);
			gtk_main_quit ();
			return FALSE;
		}
		break;

	default:
		e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
			  _("Cannot initialize the Ximian Evolution shell: %s"),
			  e_shell_construct_result_to_string (result));
		CORBA_exception_free (&ev);
		gtk_main_quit ();
		return FALSE;

	}

	have_evolution_uri = FALSE;
	for (p = uri_list; p != NULL; p = p->next) {
		const char *uri;

		uri = (const char *) p->data;
		if (strncmp (uri, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) == 0)
			have_evolution_uri = TRUE;
	}

	if (shell == NULL) {
		if (uri_list == NULL)
			display_default = TRUE;
		else
			display_default = FALSE;
	} else {
		if (! have_evolution_uri) {
			if (! e_shell_restore_from_settings (shell)) 
				display_default = TRUE;
			else
				display_default = FALSE;
		} else {
			display_default = FALSE;
		}
	}

	if (display_default) {
		const char *uri;

		uri = E_SHELL_VIEW_DEFAULT_URI;
		GNOME_Evolution_Shell_handleURI (corba_shell, uri, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("CORBA exception %s when requesting URI -- %s", ev._repo_id, uri);
	}

	for (p = uri_list; p != NULL; p = p->next) {
		const char *uri;

		uri = (const char *) p->data;
		GNOME_Evolution_Shell_handleURI (corba_shell, uri, &ev);
		if (ev._major != CORBA_NO_EXCEPTION)
			g_warning ("CORBA exception %s when requesting URI -- %s", ev._repo_id, uri);

		if (strncmp (uri, E_SHELL_URI_PREFIX, E_SHELL_URI_PREFIX_LEN) == 0)
			have_evolution_uri = TRUE;
	}

	g_slist_free (uri_list);

	CORBA_Object_release (corba_shell, &ev);

	CORBA_exception_free (&ev);
	
	if (shell == NULL)
		gtk_main_quit ();

	return FALSE;
}

int
main (int argc, char **argv)
{
	struct poptOption options[] = {
		{ "no-splash", '\0', POPT_ARG_NONE, &no_splash, 0, N_("Disable splash screen"), NULL },
		{ "debug", '\0', POPT_ARG_STRING, &evolution_debug_log, 0, N_("Send the debugging output of all components to a file."), NULL },
		{ NULL, '\0', POPT_ARG_INCLUDE_TABLE, &oaf_popt_options, 0, NULL, NULL },
		POPT_AUTOHELP
		{ NULL, '\0', 0, NULL, 0, NULL, NULL }
	};
	GSList *uri_list;
	const char **args;
	poptContext popt_context;

	bindtextdomain (PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain (PACKAGE);

	/* Make ElectricFence work.  */
	free (malloc (10));

	gnome_init_with_popt_table ("Evolution", VERSION " [" SUB_VERSION "]", argc, argv, options, 0, &popt_context);

	if (evolution_debug_log) {
		int fd;

		fd = open (evolution_debug_log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd) {
			dup2 (fd, STDOUT_FILENO);
			dup2 (fd, STDERR_FILENO);
			close (fd);
		} else
			g_warning ("Could not set up debugging output file.");
	}

	oaf_init (argc, argv);

#ifdef GTKHTML_HAVE_GCONF
	gconf_init (argc, argv, NULL);
#endif

	glade_gnome_init ();
	e_cursors_init ();

	gnome_window_icon_set_default_from_file (EVOLUTION_IMAGES "/evolution-inbox.png");

	if (! bonobo_init (CORBA_OBJECT_NIL, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL)) {
		e_notice (NULL, GNOME_MESSAGE_BOX_ERROR,
			  _("Cannot initialize the Bonobo component system."));
		exit (1);
	}

	/* FIXME */
	evolution_directory = g_concat_dir_and_file (g_get_home_dir (), "evolution");

	if (! e_setup (evolution_directory))
		exit (1);

	uri_list = NULL;

	args = poptGetArgs (popt_context);
	if (args != NULL) {
		const char **p;

		for (p = args; *p != NULL; p++)
			uri_list = g_slist_prepend (uri_list, (char *) *p);
	}

	gtk_idle_add (idle_cb, uri_list);

	bonobo_main ();

	return 0;
}
