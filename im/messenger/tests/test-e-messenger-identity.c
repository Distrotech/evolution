/*
 * This test succeeds if it does not throw an assertion.
 */

#include <e-messenger-identity.h>
#include <glib.h>
#include <string.h>

static void
test1 (void)
{
	EMessengerIdentity *emi;
	char *service_type, *username, *password;

	emi = e_messenger_identity_create ("aim", "natfriedman", NULL);
	g_assert (emi != NULL);

	service_type = e_messenger_identity_get_service_type (emi);
	g_assert (! strcmp (service_type, "aim"));

	username = e_messenger_identity_get_username (emi);
	g_assert (! strcmp (username, "natfriedman"));
	
	password = e_messenger_identity_get_password (emi);
	g_assert (password == NULL);

	g_free (service_type);
	g_free (username);
	e_messenger_identity_free (emi);
}

static void
test2 (void)
{
	EMessengerIdentity *emi;
	EMessengerIdentity *emi2;
	char *emi_string;

	emi = e_messenger_identity_create ("icq", "29834239847928", NULL);
	g_assert (emi != NULL);

	emi_string = e_messenger_identity_to_string (emi);
	g_assert (emi_string != NULL);

	emi2 = e_messenger_identity_create_from_string (emi_string);
	g_assert (emi2 != NULL);

	g_assert (e_messenger_identity_is_equal (emi, emi2));

	e_messenger_identity_free (emi);
	e_messenger_identity_free (emi2);
	g_free (emi_string);
}

static void
test3 (void)
{
	EMessengerIdentity *emi;

	emi = e_messenger_identity_create (NULL, "natfriedman", NULL);
	g_assert (emi == NULL);

	emi = e_messenger_identity_create ("aim", NULL, NULL);
	g_assert (emi == NULL);

	emi = e_messenger_identity_create_from_string (NULL);
	g_assert (emi == NULL);

	e_messenger_identity_free (emi);
}


static void
test4 (void)
{
	EMessengerIdentity *emi;
	EMessengerIdentity *emi2;

	emi = e_messenger_identity_create ("aim", "natfriedman", NULL);
	g_assert (emi != NULL);

	emi2 = e_messenger_identity_create ("AIm", "NATfRiedMan", NULL);
	g_assert (emi2 != NULL);

	g_assert (e_messenger_identity_is_equal (emi, emi2));

	e_messenger_identity_free (emi);
	e_messenger_identity_free (emi2);
}

static void
test5 (void)
{
	EMessengerIdentity *emi;
	EMessengerIdentity *emi2;

	emi = e_messenger_identity_create ("aim", "natfriedman", "sekret");
	g_assert (emi != NULL);

	emi2 = e_messenger_identity_create ("AIm", "NATfRiedMan", "secret");
	g_assert (emi2 != NULL);

	g_assert (! e_messenger_identity_is_equal (emi, emi2));

	e_messenger_identity_free (emi);
	e_messenger_identity_free (emi2);
}

static void
test6 (void)
{
	EMessengerIdentity *emi;
	EMessengerIdentity *emi2;
	char               *str;

	emi = e_messenger_identity_create ("aim", "natfriedman", "sekret");
	g_assert (emi != NULL);

	emi2 = e_messenger_identity_create_from_me_and_username (emi, "eojwahs");
	g_assert (emi2 != NULL);

	str = e_messenger_identity_get_service_type (emi2);
	g_assert (! strcmp (str, "aim"));

	g_free (str);

	str = e_messenger_identity_get_username (emi2);
	g_assert (! strcmp (str, "eojwahs"));

	g_free (str);

	e_messenger_identity_free (emi);
	e_messenger_identity_free (emi2);
}

static void
test7 (void)
{
	EMessengerIdentity *emi;
	EMessengerIdentity *emi2;
	char               *str;

	emi = e_messenger_identity_create (NULL, "natfriedman", "sekret");
	g_assert (emi == NULL);

	emi2 = e_messenger_identity_create_from_me_and_username (NULL, "eojwahs");
	g_assert (emi2 == NULL);

	str = e_messenger_identity_get_service_type (NULL);
	g_assert (str == NULL);
}

int
main (int argc, char **argv)
{
	test1 ();
	test2 ();
	test3 ();
	test4 ();
	test5 ();
	test6 ();
	test7 ();
}
