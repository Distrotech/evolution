/* Wombat personal information server - main file
 *
 * Author: Nat Friedman <nat@ximian.com>
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-init.h>
#include <liboaf/liboaf.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <bonobo/bonobo-main.h>

#include "pas/pas-book-factory.h"
#include "pas/pas-backend-file.h"

#include "calendar/pcs/cal-factory.h"
#include "calendar/pcs/cal-backend-file.h"

#ifdef HAVE_LDAP
#include "pas/pas-backend-ldap.h"
#endif

#include "wombat-moniker.h"

#define CAL_FACTORY_OAF_ID "OAFIID:GNOME_Evolution_Wombat_CalendarFactory"
#define PAS_BOOK_FACTORY_OAF_ID "OAFIID:GNOME_Evolution_Wombat_ServerFactory"

/* The and addressbook calendar factories */

static CalFactory *cal_factory;
static PASBookFactory *pas_book_factory;

/* Timeout interval in milliseconds for termination */
#define EXIT_TIMEOUT 5000

/* Timeout ID for termination handler */
static guint termination_handler_id;



/* Termination */

/* Termination handler.  Checks if both factories have zero running backends,
 * and if so terminates the program.
 */
static gboolean
termination_handler (gpointer data)
{
	if (cal_factory_get_n_backends (cal_factory) == 0
	    && pas_book_factory_get_n_backends (pas_book_factory) == 0) {
		fprintf (stderr, "termination_handler(): Terminating the Wombat.  Have a nice day.\n");
		gtk_main_quit ();
	}

	termination_handler_id = 0;
	return FALSE;
}

/* Queues a timeout for handling termination of Wombat */
static void
queue_termination (void)
{
	if (termination_handler_id)
		return;

	termination_handler_id = g_timeout_add (EXIT_TIMEOUT, termination_handler, NULL);
}



static void
last_book_gone_cb (PASBookFactory *factory, gpointer data)
{
	queue_termination ();
}

static gboolean
setup_pas (int argc, char **argv)
{
	pas_book_factory = pas_book_factory_new ();

	if (!pas_book_factory)
		return FALSE;

	pas_book_factory_register_backend (
		pas_book_factory, "file", pas_backend_file_new);

#ifdef HAVE_LDAP
	pas_book_factory_register_backend (
		pas_book_factory, "ldap", pas_backend_ldap_new);
#endif

	gtk_signal_connect (GTK_OBJECT (pas_book_factory),
			    "last_book_gone",
			    GTK_SIGNAL_FUNC (last_book_gone_cb),
			    NULL);

	if (!pas_book_factory_activate (pas_book_factory, PAS_BOOK_FACTORY_OAF_ID)) {
		bonobo_object_unref (BONOBO_OBJECT (pas_book_factory));
		pas_book_factory = NULL;
		return FALSE;
	}

	return TRUE;
}



/* Personal calendar server */

/* Callback used when the calendar factory has no more running backends */
static void
last_calendar_gone_cb (CalFactory *factory, gpointer data)
{
	fprintf (stderr, "last_calendar_gone_cb() called!  Queueing termination...\n");
	queue_termination ();
}

/* Creates the calendar factory object and registers it */
static gboolean
setup_pcs (int argc, char **argv)
{
	cal_factory = cal_factory_new ();

	if (!cal_factory) {
		g_message ("setup_pcs(): Could not create the calendar factory");
		return FALSE;
	}

	cal_factory_register_method (cal_factory, "file", CAL_BACKEND_FILE_TYPE);

	if (!cal_factory_oaf_register (cal_factory, CAL_FACTORY_OAF_ID)) {
		bonobo_object_unref (BONOBO_OBJECT (cal_factory));
		cal_factory = NULL;
		return FALSE;
	}

	gtk_signal_connect (GTK_OBJECT (cal_factory),
			    "last_calendar_gone",
			    GTK_SIGNAL_FUNC (last_calendar_gone_cb),
			    NULL);

	return TRUE;
}



static gboolean
setup_config (int argc, char **argv)
{
	BonoboGenericFactory *factory;
	char *oafiid = "OAFIID:Bonobo_Moniker_wombat_Factory";

	factory = bonobo_generic_factory_new_multi (oafiid, 
						    wombat_moniker_factory,
						    NULL);
       
	// bonobo_running_context_auto_exit_unref (BONOBO_OBJECT (factory));
       

	return TRUE;
}

static void
setup_vfs (int argc, char **argv)
{
	if (!gnome_vfs_init ()) {
		g_message (_("setup_vfs(): could not initialize GNOME-VFS"));
		exit (EXIT_FAILURE);
	}
}



static void
init_corba (int *argc, char **argv)
{
	if (gnome_init_with_popt_table ("wombat", VERSION,
					*argc, argv, oaf_popt_options, 0, NULL) != 0) {
		g_message (_("init_corba(): could not initialize GNOME"));
		exit (EXIT_FAILURE);
	}

	oaf_init (*argc, argv);
}

static void
init_bonobo (int *argc, char **argv)
{
	init_corba (argc, argv);

	if (!bonobo_init (CORBA_OBJECT_NIL, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL)) {
		g_message (_("init_bonobo(): could not initialize Bonobo"));
		exit (EXIT_FAILURE);
	}
}

int
main (int argc, char **argv)
{
	gboolean did_pas=FALSE, did_pcs=FALSE, did_config=FALSE;

	bindtextdomain (PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain (PACKAGE);

	g_message ("Starting wombat");

	init_bonobo (&argc, argv);
	setup_vfs (argc, argv);

	/*g_log_set_always_fatal (G_LOG_LEVEL_ERROR |
				G_LOG_LEVEL_CRITICAL |
				G_LOG_LEVEL_WARNING);*/

	if (!( (did_pas = setup_pas (argc, argv))
	       && (did_pcs = setup_pcs (argc, argv))
	       && (did_config = setup_config (argc, argv)))) {

		const gchar *failed = NULL;

		if (!did_pas)
		  failed = "PAS";
		else if (!did_pcs)
		  failed = "PCS";
		else if (!did_config)
		  failed = "Config";

		g_message ("main(): could not initialize Wombat service \"%s\"; terminating", failed);

		if (pas_book_factory) {
			bonobo_object_unref (BONOBO_OBJECT (pas_book_factory));
			pas_book_factory = NULL;
		}

		if (cal_factory) {
			bonobo_object_unref (BONOBO_OBJECT (cal_factory));
			cal_factory = NULL;
		}

		exit (EXIT_FAILURE);
	}

	bonobo_main ();

	bonobo_object_unref (BONOBO_OBJECT (cal_factory));
	cal_factory = NULL;

	bonobo_object_unref (BONOBO_OBJECT (pas_book_factory));
	pas_book_factory = NULL;

	gnome_vfs_shutdown ();

	return 0;
}
