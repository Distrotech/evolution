#include <gnome.h>
#include <liboaf/liboaf.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>
#include <messenger/e-messenger-listener.h>
#include <Messenger.h>

static gboolean
go_go_gadget(gpointer data)
{
	char **argv = data;
	CORBA_Environment ev;
	Bonobo_Unknown corba_object;
	GNOME_Evolution_Messenger_BackendDispatcher dispatcher;
	EMessengerListener *listener;
	int pwlen;

	listener = e_messenger_listener_new ();

	CORBA_exception_init (&ev);

	corba_object = oaf_activate_from_id(
		"OAFIID:GNOME_Evolution_Messenger_BackendDispatcher",
		0, NULL, &ev);

	if (BONOBO_EX(&ev)) {
		g_error("Exception activating");
	}
	
	dispatcher = Bonobo_Unknown_queryInterface(
		corba_object, 
		"IDL:GNOME/Evolution/Messenger/BackendDispatcher:1.0", &ev);

	if (BONOBO_EX(&ev)) {
		g_error("Exception on queryInterface");
	}

	pwlen = strlen (argv [2]);

	GNOME_Evolution_Messenger_BackendDispatcher_signon(
		dispatcher, "AIM", argv[1], argv[2],
		BONOBO_OBJREF (listener), &ev);

	/*
	 * Zero the password so it doesn't show up in ps anymore
	 * (imperfect but better than nothing).
	 */
	memset (argv [2], pwlen, sizeof (char));

	if (BONOBO_EX(&ev)) {
		g_error("Exception on signon");
	}

	CORBA_exception_free (&ev);

	return FALSE;
} /* go_go_gadget */

int
main(int argc, char *argv[])
{
	CORBA_ORB orb;

	if (argc < 3) {
		printf("Usage: test-pms <signon> <password>\n");
		exit(1);
	}

	gnome_init_with_popt_table(
		"test-pms", "0.0", argc, argv,
		oaf_popt_options, 0, NULL);
	orb = oaf_init(argc, argv);

	if (!bonobo_init(orb, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL))
		g_error("bonobo didn't init");

	gtk_idle_add(go_go_gadget, argv);

	bonobo_main();

	return 0;
} /* main */
