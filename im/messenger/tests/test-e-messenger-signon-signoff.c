/* This test succeeds if it does not abort or exit (1); */

#include <gnome.h>
#include <liboaf/liboaf.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>
#include <messenger/e-messenger.h>
#include <Messenger.h>

static void
signon_callback (EMessenger               *messenger,
		 const EMessengerIdentity *identity,
		 EMessengerSignonError     error,
		 gpointer                  closure)
{
	EMessengerIdentity *orig_identity = closure;

	g_assert (messenger != NULL);
	g_assert (E_IS_MESSENGER (messenger));
	g_assert (e_messenger_identity_is_equal (orig_identity, identity));

	printf ("SignonCallback: error: [%s]\n", e_messenger_signon_error_to_string (error));

	g_assert (error == E_MESSENGER_SIGNON_ERROR_NONE);

	exit (0);
}

static gboolean
run_test (gpointer data)
{
	char               **argv = data;
	EMessenger          *messenger;
	EMessengerIdentity  *identity;
	int                  pwlen;
	gboolean             retval;

	messenger = e_messenger_new ();
	g_assert (messenger != NULL);

	pwlen = strlen (argv [3]);

	identity = e_messenger_identity_create (argv [1], argv [2], argv [3]);
	g_assert (identity != NULL);
	
	/*
	 * Zero the password so it doesn't show up in ps anymore
	 * (imperfect but better than nothing).
	 */
	memset (argv [3], pwlen, sizeof (char));

	retval = e_messenger_signon (messenger, identity, signon_callback, identity);
	g_assert (retval);

	return FALSE;
} /* run_test */

int
main(int argc, char *argv[])
{
	CORBA_ORB orb;

	if (argc < 4) {
		printf ("Usage: %s <service> <signon> <password>\n", argv [0]);
		printf ("\tService types: AIM\n");
		exit (1);
	}

	gnome_init_with_popt_table (
		"test-e-messenger-signon-signoff", "0.0", argc, argv,
		oaf_popt_options, 0, NULL);

	orb = oaf_init (argc, argv);

	if (! bonobo_init (orb, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL)) {
		g_error ("Failed initializing Bonobo.");
		exit (1);
	}

	gtk_idle_add (run_test, argv);

	bonobo_main ();

	return 0;
}
