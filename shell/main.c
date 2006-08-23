/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* main.c
 *
 * Copyright (C) 2000, 2001, 2002, 2003  Ximian, Inc.
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

#include <glib.h>
#include <glib/gstdio.h>

#include "e-util/e-dialog-utils.h"
#include "e-util/e-gtk-utils.h"
#include "e-util/e-bconf-map.h"

#include <e-util/e-icon-factory.h>
#include "e-shell-constants.h"
#include "e-util/e-profile-event.h"

#include "e-shell.h"
#include "es-menu.h"
#include "es-event.h"

#include "e-util/e-util-private.h"

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <gconf/gconf-client.h>

#include <gtk/gtkalignment.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkframe.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkstock.h>

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <libgnome/gnome-sound.h>
#include <libgnomeui/gnome-ui-init.h>

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-moniker-util.h>
#include <bonobo/bonobo-exception.h>

#include <bonobo-activation/bonobo-activation.h>

#include <libedataserverui/e-passwords.h>

#include <glade/glade.h>

#include "e-config-upgrade.h"
#include "Evolution-DataServer.h"

#include <misc/e-cursors.h>
#include "e-util/e-error.h"

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pthread.h>

#include "e-util/e-plugin.h"

static EShell *shell = NULL;

/* Command-line options.  */
static gboolean start_online = FALSE;
static gboolean start_offline = FALSE;
static gboolean setup_only = FALSE;
static gboolean killev = FALSE;
#if DEVELOPMENT
static gboolean force_migrate = FALSE;
#endif
static gboolean disable_eplugin = FALSE;

static gint idle_cb (void *data);

static char *default_component_id = NULL;
static char *evolution_debug_log = NULL;
static gchar **remaining_args;

static void
no_windows_left_cb (EShell *shell, gpointer data)
{
	bonobo_object_unref (BONOBO_OBJECT (shell));
	bonobo_main_quit ();
}

static void
shell_weak_notify (void *data,
		   GObject *where_the_object_was)
{
	bonobo_main_quit ();
}


#ifdef KILL_PROCESS_CMD

static void
kill_dataserver (void)
{
	g_message ("Killing old version of evolution-data-server...");

	system (KILL_PROCESS_CMD " -9 lt-evolution-data-server 2> /dev/null");
	system (KILL_PROCESS_CMD " -9 evolution-data-server-1.0 2> /dev/null");
	system (KILL_PROCESS_CMD " -9 evolution-data-server-1.2 2> /dev/null");
	system (KILL_PROCESS_CMD " -9 evolution-data-server-1.4 2> /dev/null");
	system (KILL_PROCESS_CMD " -9 evolution-data-server-1.6 2> /dev/null");

	system (KILL_PROCESS_CMD " -9 lt-evolution-alarm-notify 2> /dev/null");
	system (KILL_PROCESS_CMD " -9 evolution-alarm-notify 2> /dev/null");
}

static void
kill_old_dataserver (void)
{
	GNOME_Evolution_DataServer_InterfaceCheck iface;
	CORBA_Environment ev;
	CORBA_char *version;

	CORBA_exception_init (&ev);

	/* FIXME Should we really kill it off?  We also shouldn't hard code the version */
	iface = bonobo_activation_activate_from_id ("OAFIID:GNOME_Evolution_DataServer_InterfaceCheck", 0, NULL, &ev);
	if (BONOBO_EX (&ev) || iface == CORBA_OBJECT_NIL) {
		kill_dataserver ();
		CORBA_exception_free (&ev);
		return;
	}

	version = GNOME_Evolution_DataServer_InterfaceCheck__get_interfaceVersion (iface, &ev);
	if (BONOBO_EX (&ev)) {
		kill_dataserver ();
		CORBA_Object_release (iface, &ev);
		CORBA_exception_free (&ev);
		return;
	}

	if (strcmp (version, DATASERVER_VERSION) != 0) {
		CORBA_free (version);
		kill_dataserver ();
		CORBA_Object_release (iface, &ev);
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_free (version);
	CORBA_Object_release (iface, &ev);
	CORBA_exception_free (&ev);
}
#endif


#if DEVELOPMENT

/* Warning dialog to scare people off a little bit.  */

static void
warning_dialog_response_callback (GtkDialog *dialog,
				 int button_number,
				 void *data)
{
	GtkCheckButton *dont_bother_me_again_checkbox;
	GConfClient *client;

	dont_bother_me_again_checkbox = GTK_CHECK_BUTTON (data);

	client = gconf_client_get_default ();
	gconf_client_set_bool (client, "/apps/evolution/shell/skip_warning_dialog",
			       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dont_bother_me_again_checkbox)),
			       NULL);
	g_object_unref (client);

	gtk_widget_destroy (GTK_WIDGET (dialog));

	idle_cb(NULL);
}

static void
show_development_warning(void)
{
	GtkWidget *vbox;
	GtkWidget *label;
	GtkWidget *warning_dialog;
	GtkWidget *dont_bother_me_again_checkbox;
	GtkWidget *alignment;
	char *text;

	warning_dialog = gtk_dialog_new ();
	gtk_window_set_title (GTK_WINDOW (warning_dialog), "Evolution " VERSION);
	gtk_window_set_modal (GTK_WINDOW (warning_dialog), TRUE);
	gtk_dialog_add_button (GTK_DIALOG (warning_dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);

	gtk_dialog_set_has_separator (GTK_DIALOG (warning_dialog), FALSE);

	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (warning_dialog)->vbox), 0);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (warning_dialog)->action_area), 12);

	vbox = gtk_vbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (warning_dialog)->vbox), vbox,
			    TRUE, TRUE, 0);

	text = g_strdup_printf(
		/* xgettext:no-c-format */
		/* Preview/Alpha/Beta version warning message */
		_("Hi.  Thanks for taking the time to download this preview release\n"
		  "of the Evolution groupware suite.\n"
		  "\n"
		  "This version of Evolution is not yet complete. It is getting close,\n"
		  "but some features are either unfinished or do not work properly.\n"
		  "\n"
		  "If you want a stable version of Evolution, we urge you to uninstall\n"
		  "this version, and install version %s instead.\n"
		  "\n"
		  "If you find bugs, please report them to us at bugzilla.gnome.org.\n"
                  "This product comes with no warranty and is not intended for\n"
		  "individuals prone to violent fits of anger.\n"
                  "\n"
		  "We hope that you enjoy the results of our hard work, and we\n"
		  "eagerly await your contributions!\n"),
		"2.6.3");
	label = gtk_label_new (text);
	g_free(text);

	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);

	gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);

	label = gtk_label_new (_("Thanks\n"
				 "The Evolution Team\n"));
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_RIGHT);
	gtk_misc_set_alignment(GTK_MISC(label), 1, .5);

	gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);

	dont_bother_me_again_checkbox = gtk_check_button_new_with_label (_("Do not tell me again"));

	alignment = gtk_alignment_new (0.0, 0.0, 0.0, 0.0);

	gtk_container_add (GTK_CONTAINER (alignment), dont_bother_me_again_checkbox);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, TRUE, TRUE, 0);

	gtk_widget_show_all (warning_dialog);

	g_signal_connect (warning_dialog, "response",
			  G_CALLBACK (warning_dialog_response_callback),
			  dont_bother_me_again_checkbox);
}

static void
destroy_config (void)
{
	GConfClient *gconf;
	
	gconf = gconf_client_get_default ();

	/* Unset the source stuff */
	gconf_client_unset (gconf, "/apps/evolution/calendar/sources", NULL);
	gconf_client_unset (gconf, "/apps/evolution/tasks/sources", NULL);
	gconf_client_unset (gconf, "/apps/evolution/addressbook/sources", NULL);
	gconf_client_unset (gconf, "/apps/evolution/addressbook/sources", NULL);

	/* Reset the version */
	gconf_client_set_string (gconf, "/apps/evolution/version", "1.4.0", NULL);

	/* Clear the dir */
	system ("rm -Rf ~/.evolution");
	
	g_object_unref (gconf);
}

#endif /* DEVELOPMENT */

static void
open_uris (GNOME_Evolution_Shell corba_shell, GSList *uri_list)
{
	GSList *p;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	for (p = uri_list; p != NULL; p = p->next) {
		const char *uri;

		uri = (const char *) p->data;
		GNOME_Evolution_Shell_handleURI (corba_shell, uri, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_warning ("Invalid URI: %s", uri);
			CORBA_exception_free (&ev);
		}
	}

	CORBA_exception_free (&ev);
}

/* This is for doing stuff that requires the GTK+ loop to be running already.  */

static gint
idle_cb (void *data)
{
	GSList *uri_list;
	GNOME_Evolution_Shell corba_shell;
	CORBA_Environment ev;
	EShellConstructResult result;
	EShellStartupLineMode startup_line_mode;

#ifdef KILL_PROCESS_CMD
	kill_old_dataserver ();
#endif

	CORBA_exception_init (&ev);

	uri_list = (GSList *) data;

	if (! start_online && ! start_offline)
		startup_line_mode = E_SHELL_STARTUP_LINE_MODE_CONFIG;
	else if (start_online)
		startup_line_mode = E_SHELL_STARTUP_LINE_MODE_ONLINE;
	else
		startup_line_mode = E_SHELL_STARTUP_LINE_MODE_OFFLINE;

	shell = e_shell_new (startup_line_mode, &result);

	switch (result) {
	case E_SHELL_CONSTRUCT_RESULT_OK:
		g_signal_connect (shell, "no_windows_left", G_CALLBACK (no_windows_left_cb), NULL);
		g_object_weak_ref (G_OBJECT (shell), shell_weak_notify, NULL);
		corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell));
		corba_shell = CORBA_Object_duplicate (corba_shell, &ev);
		break;

	case E_SHELL_CONSTRUCT_RESULT_CANNOTREGISTER:
		corba_shell = bonobo_activation_activate_from_id (E_SHELL_OAFIID, 0, NULL, &ev);
		if (ev._major != CORBA_NO_EXCEPTION || corba_shell == CORBA_OBJECT_NIL) {
			e_error_run(NULL, "shell:noshell", NULL);
			CORBA_exception_free (&ev);
			bonobo_main_quit ();
			return FALSE;
		}
		break;

	default:
		e_error_run(NULL, "shell:noshell-reason",
			    e_shell_construct_result_to_string(result), NULL);
		CORBA_exception_free (&ev);
		bonobo_main_quit ();
		return FALSE;

	}

	if (shell != NULL) {
		if (g_slist_length (uri_list) == 0)
			e_shell_create_window (shell, default_component_id, NULL);
		open_uris (corba_shell, uri_list);
	} else {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		if (uri_list != NULL)
			open_uris (corba_shell, uri_list);
		else
			if (default_component_id == NULL)
				GNOME_Evolution_Shell_createNewWindow (corba_shell, "", &ev);
			else
				GNOME_Evolution_Shell_createNewWindow (corba_shell, default_component_id, &ev);

		CORBA_exception_free (&ev);
	}

	g_slist_free (uri_list);

	CORBA_Object_release (corba_shell, &ev);

	CORBA_exception_free (&ev);
	
	if (shell == NULL)
		bonobo_main_quit ();

	return FALSE;
}

#ifndef G_OS_WIN32

/* SIGSEGV handling.
   
   The GNOME SEGV handler will lose if it's not run from the main Gtk
   thread. So if we have to redirect the signal if the crash happens in another
   thread.  */

static void (*gnome_segv_handler) (int);
static GStaticMutex segv_mutex = G_STATIC_MUTEX_INIT;
static pthread_t main_thread;

static void
segv_redirect (int sig)
{
	if (pthread_self () == main_thread)
		gnome_segv_handler (sig);
	else {
		pthread_kill (main_thread, sig);

		/* We can't return from the signal handler or the thread may
		   SEGV again. But we can't pthread_exit, because then the
		   thread may get cleaned up before bug-buddy can get a stack
		   trace. So we block by trying to lock a mutex we know is
		   already locked.  */
		g_static_mutex_lock (&segv_mutex);
	}
}

static void
setup_segv_redirect (void)
{
	struct sigaction sa, osa;

	sigaction (SIGSEGV, NULL, &osa);
	if (osa.sa_handler == SIG_DFL)
		return;

	main_thread = pthread_self ();

	sa.sa_flags = 0;
	sigemptyset (&sa.sa_mask);
	sa.sa_handler = segv_redirect;
	sigaction (SIGSEGV, &sa, NULL);
	sigaction (SIGBUS, &sa, NULL);
	sigaction (SIGFPE, &sa, NULL);
		
	sa.sa_handler = SIG_IGN;
	sigaction (SIGXFSZ, &sa, NULL);
	gnome_segv_handler = osa.sa_handler;
	g_static_mutex_lock (&segv_mutex);
}

#else
#define setup_segv_redirect() 0
#endif

static const GOptionEntry options[] = {
	{ "component", 'c', 0, G_OPTION_ARG_STRING, &default_component_id,
	  N_("Start Evolution activating the specified component"), NULL },
	{ "offline", '\0', 0, G_OPTION_ARG_NONE, &start_offline,
	  N_("Start in offline mode"), NULL },
	{ "online", '\0', 0, G_OPTION_ARG_NONE, &start_online,
	  N_("Start in online mode"), NULL },
#ifdef KILL_PROCESS_CMD
	{ "force-shutdown", '\0', 0, G_OPTION_ARG_NONE, &killev, 
	  N_("Forcibly shut down all Evolution components"), NULL },
#endif
#if DEVELOPMENT
	{ "force-migrate", '\0', 0, G_OPTION_ARG_NONE, &force_migrate, 
	  N_("Forcibly re-migrate from Evolution 1.4"), NULL },
#endif
	{ "debug", '\0', 0, G_OPTION_ARG_STRING, &evolution_debug_log, 
	  N_("Send the debugging output of all components to a file."), NULL },
	{ "disable-eplugin", '\0', 0, G_OPTION_ARG_NONE, &disable_eplugin, 
	  N_("Disable loading of any plugins."), NULL },
	{ "setup-only", '\0', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE,
	  &setup_only, NULL, NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining_args, NULL, NULL },
	{ NULL }
};

int
main (int argc, char **argv)
{
#ifdef G_OS_WIN32
	extern void link_shutdown (void);
#endif

#if DEVELOPMENT
	GConfClient *client;
	gboolean skip_warning_dialog;
#endif
	GSList *uri_list;
	GnomeProgram *program;
	GOptionContext *context;
	GList *icon_list;
	char *filename;

	/* Make ElectricFence work.  */
	free (malloc (10));

	bindtextdomain (GETTEXT_PACKAGE, EVOLUTION_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (_("- The Evolution PIM and Email Client"));

	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);

	program = gnome_program_init (PACKAGE "-" BASE_VERSION, VERSION, LIBGNOMEUI_MODULE, argc, argv, 
				      GNOME_PROGRAM_STANDARD_PROPERTIES,
				      GNOME_PARAM_GOPTION_CONTEXT, context,
				      GNOME_PARAM_HUMAN_READABLE_NAME, _("Evolution"),
				      NULL);

	if (start_online && start_offline) {
		fprintf (stderr, _("%s: --online and --offline cannot be used together.\n  Use %s --help for more information.\n"),
			 argv[0], argv[0]);
		exit (1);
	}

	if (killev) {
		filename = g_build_filename (EVOLUTION_TOOLSDIR,
					     "killev",
					     NULL);
		execl (filename, "killev", NULL);
		/* Not reached */
		exit (0);
	}

#ifdef G_OS_WIN32
	gtk_rc_parse_string ("gtk-fallback-icon-theme = \"gnome\"");
#endif

#if DEVELOPMENT
	if (force_migrate) {
		destroy_config ();
	}
#endif
	
	setup_segv_redirect ();
	
	if (evolution_debug_log) {
		int fd;

		fd = g_open (evolution_debug_log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd) {
			dup2 (fd, STDOUT_FILENO);
			dup2 (fd, STDERR_FILENO);
			close (fd);
		} else
			g_warning ("Could not set up debugging output file.");
	}

	glade_init ();
	e_cursors_init ();
	e_icon_factory_init ();
	e_passwords_init();

	icon_list = e_icon_factory_get_icon_list ("stock_mail");
	if (icon_list) {
		gtk_window_set_default_icon_list (icon_list);
		g_list_foreach (icon_list, (GFunc) g_object_unref, NULL);
		g_list_free (icon_list);
	}

	if (setup_only)
		exit (0);

	uri_list = NULL;

	if (remaining_args != NULL) {
		const char **p;

		for (p = (const char**)remaining_args; *p != NULL; p++)
			uri_list = g_slist_prepend (uri_list, (char *) *p);
	}
	uri_list = g_slist_reverse (uri_list);
	

	gnome_sound_init ("localhost");

	if (!disable_eplugin) {
		e_plugin_register_type(e_plugin_lib_get_type());
		e_plugin_hook_register_type(es_menu_hook_get_type());
		e_plugin_hook_register_type(es_event_hook_get_type());
#ifdef ENABLE_PROFILING
		e_plugin_hook_register_type(e_profile_event_hook_get_type());
#endif
		e_plugin_hook_register_type(e_plugin_type_hook_get_type());
		e_plugin_load_plugins();
	}

#if DEVELOPMENT
	client = gconf_client_get_default ();
	skip_warning_dialog = gconf_client_get_bool (client, "/apps/evolution/shell/skip_warning_dialog", NULL);
	g_object_unref (client);

	if (!skip_warning_dialog && !getenv ("EVOLVE_ME_HARDER"))
		show_development_warning();
	else
#endif
		g_idle_add (idle_cb, uri_list);	
	
	bonobo_main ();
	
	e_icon_factory_shutdown ();
	g_object_unref (program);
	gnome_sound_shutdown ();
	e_cursors_shutdown ();
#ifdef G_OS_WIN32
	link_shutdown ();
#endif
	return 0;
}
