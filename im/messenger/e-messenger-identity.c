/*
 * EMesengerIdentity
 *
 * Please see the header file for information about this type.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2001, Ximian, Inc.
 */

#include <e-messenger-identity.h>
#include <glib.h>
#include <string.h>


/**
 * e_messenger_identity_create:
 * @service_type: A string which identifies the type of the instant
 * messaging service to which this identity applies.  Examples are
 * "AIM", "ICQ", etc.
 * @username: The username for this identity.
 * @password: An optional argument containing the password for this
 * identity.  If this identity represents someone whose password you
 * don't know (e.g. someone to whom a message is being sent), set this
 * to NULL.
 *
 * Creates a new EMessengerIdentity, populating it with the provided
 * information.
 */
EMessengerIdentity *
e_messenger_identity_create (const char *service_type,
			     const char *username,
			     const char *password)
{
	EMessengerIdentity *emi;

	g_return_val_if_fail (service_type != NULL, NULL);
	g_return_val_if_fail (username != NULL,     NULL);

	emi = g_new0 (EMessengerIdentity, 1);

	
	*emi = g_strdup_printf ("%s:%s:%s",
				service_type,
				username,
				password == NULL ? "" : password);

	return emi;
}

char *
e_messenger_identity_create_string (const char *service_type,
				    const char *username,
				    const char *password)
{
	EMessengerIdentity *emi;
	char               *emi_string;

	emi = e_messenger_identity_create (
		service_type, username, password);

	emi_string = e_messenger_identity_to_string (emi);

	e_messenger_identity_free (emi);

	return emi_string;
}

EMessengerIdentity *
e_messenger_identity_create_from_string (const char *id_string)
{
	EMessengerIdentity *id;

	g_return_val_if_fail (id_string != NULL, NULL);

	id = g_new0 (EMessengerIdentity, 1);
	*id = (EMessengerIdentity) g_strdup (id_string);

	return id;
}

EMessengerIdentity *
e_messenger_identity_create_from_me_and_username (EMessengerIdentity *me,
						  const char         *username)
{
	EMessengerIdentity *emi;
	char               *service_type;

	service_type = e_messenger_identity_get_service_type (me);

	emi = e_messenger_identity_create (service_type, username, NULL);

	g_free (service_type);

	return emi;
}

char *
e_messenger_identity_to_string (EMessengerIdentity *emi)
{
	return g_strdup ((char *) *emi);
}

gboolean
e_messenger_identity_is_equal (EMessengerIdentity *emi1,
			       EMessengerIdentity *emi2)
{
	char *emi1_username,     *emi2_username;
	char *emi1_password,     *emi2_password;
	char *emi1_service_type, *emi2_service_type;
	gboolean equivalent;

	emi1_username     = e_messenger_identity_get_username (emi1);
	emi1_password     = e_messenger_identity_get_password (emi1);
	emi1_service_type = e_messenger_identity_get_service_type (emi1);

	emi2_username     = e_messenger_identity_get_username (emi2);
	emi2_password     = e_messenger_identity_get_password (emi2);
	emi2_service_type = e_messenger_identity_get_service_type (emi2);

	equivalent = TRUE;

	if (emi1_username != NULL && emi2_username != NULL) {
		if (g_strcasecmp (emi1_username, emi2_username))
			equivalent = FALSE;
	} else {
		if (emi1_username != emi2_username)
			equivalent = FALSE;
	}
	
	if (emi1_password != NULL && emi2_password != NULL) {
		if (strcmp (emi1_password, emi2_password))
			equivalent = FALSE;
	} else {
		if (emi1_password != emi2_password)
			equivalent = FALSE;
	}

	if (emi1_service_type != NULL && emi2_service_type != NULL) {
		if (g_strcasecmp (emi1_service_type, emi2_service_type))
			equivalent = FALSE;
	} else {
		if (emi1_service_type != emi2_service_type)
			equivalent = FALSE;
	}

	g_free (emi1_username);
	g_free (emi1_password);
	g_free (emi1_service_type);
	g_free (emi2_username);
	g_free (emi2_password);
	g_free (emi2_service_type);

	return equivalent;
}

char *
e_messenger_identity_get_service_type (EMessengerIdentity *emi)
{
	char *service_type;
	char *p;

	g_return_val_if_fail (emi != NULL,  NULL);
	g_return_val_if_fail (*emi != NULL, NULL);

	service_type = g_strdup (*emi);

	p = strchr (service_type, ':');

	if (p == NULL) {
		g_warning ("invalid EMessengerIdentity\n");
		g_free (service_type);
		return NULL;
	}

	*p = '\0';

	return service_type;
}

char *
e_messenger_identity_get_username (EMessengerIdentity *emi)
{
	char *username;
	char *p;

	g_return_val_if_fail (emi != NULL,  NULL);
	g_return_val_if_fail (*emi != NULL, NULL);

	p = strchr (*emi, ':');

	if (p == NULL) {
		g_warning ("invalid EMessengerIdentity\n");
		return NULL;
	}

	p++;

	username = g_strdup (p);

	p = strchr (username, ':');

	if (p == NULL) {
		g_warning ("invalid EMessengerIdentity\n");
		g_free (username);
		return NULL;
	}

	*p = '\0';
	
	return username;
}

char *
e_messenger_identity_get_password (EMessengerIdentity *emi)
{
	char *password;
	char *p;

	g_return_val_if_fail (emi != NULL,  NULL);
	g_return_val_if_fail (*emi != NULL, NULL);

	p = strchr (*emi, ':');

	if (p == NULL || *p == '\0') {
		g_warning ("invalid EMessengerIdentity\n");
		return NULL;
	}

	p = strchr (p + 1, ':');

	if (p == NULL || *p == '\0') {
		g_warning ("invalid EMessengerIdentity\n");
		return NULL;
	}

	p++;

	if (*p == '\0')
		return NULL;

	password = g_strdup (p);

	return password;
}

void
e_messenger_identity_free (EMessengerIdentity *emi)
{
	g_return_if_fail (emi != NULL);

	g_free (*emi);
}
