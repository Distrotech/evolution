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

#include <glib.h>
#include <string.h>

#include "e-messenger-identity.h"

/*
 * FIXME: This file lacks preconditions.
 */

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

EMessengerIdentity *
e_messenger_identity_copy (const EMessengerIdentity *emi)
{
	EMessengerIdentity *emi_copy;

	g_return_val_if_fail (emi != NULL,                                 NULL);
	g_return_val_if_fail (e_messenger_identity_check_invariants (emi), NULL);

	emi_copy = g_new0 (EMessengerIdentity, 1);

	*emi_copy = g_strdup (*emi);

	return emi_copy;
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
e_messenger_identity_create_from_me_and_username (const EMessengerIdentity *me,
						  const char         *username)
{
	EMessengerIdentity *emi;
	char               *service_type;

	g_return_val_if_fail (me != NULL,       NULL);
	g_return_val_if_fail (username != NULL, NULL);

	service_type = e_messenger_identity_get_service_type (me);

	emi = e_messenger_identity_create (service_type, username, NULL);

	g_free (service_type);

	return emi;
}

char *
e_messenger_identity_to_string (const EMessengerIdentity *emi)
{
	char *service_type;
	char *username;
	char *emi_string;

	g_return_val_if_fail (emi != NULL, NULL);

	service_type = e_messenger_identity_get_service_type (emi);
	username     = e_messenger_identity_get_username (emi);

	emi_string = g_strdup_printf ("%s:%s:", service_type, username);

	g_free (service_type);
	g_free (username);

	return emi_string;
}

/**
 * e_messenger_identity_is_equal:
 *
 * Does not compare passwords.
 */
gboolean
e_messenger_identity_is_equal (const EMessengerIdentity *emi1,
			       const EMessengerIdentity *emi2)
{
	char *emi1_username,     *emi2_username;
	char *emi1_password,     *emi2_password;
	char *emi1_service_type, *emi2_service_type;
	gboolean equivalent;

	/*
	 * FIXME: Is this the correct way to handle NULL args?
	 */
	if ((emi1 == emi2) && (emi1 == NULL))
		return TRUE;

	if ((emi1 == NULL || emi2 == NULL) && (emi1 != emi2))
		return FALSE;


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
e_messenger_identity_get_service_type (const EMessengerIdentity *emi)
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
e_messenger_identity_get_username (const EMessengerIdentity *emi)
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
e_messenger_identity_get_password (const EMessengerIdentity *emi)
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

gboolean
e_messenger_identity_check_invariants (const EMessengerIdentity *emi)
{
	char *service_type;
	char *username;
	char *password;
	char *p;

	/*
	 * A NULL EMI is a valid EMI.
	 */
	if (emi == NULL)
		return TRUE;

	if (*emi == NULL)
		return FALSE;

	/*
	 * Verify that the string is of the form:
	 *     "service_type:username:password"
	 */
	p = strchr ((char *) *emi, ':');
	if (p == NULL)
		return FALSE;

	p ++;
	if (*p == '\0')
		return FALSE;

	p = strchr (p, ':');
	if (p == NULL)
		return FALSE;

	p ++;
	if (*p == '\0')
		return FALSE;

	/*
	 * Verify that we can extract the pieces of the identity and
	 * that they are valid.
	 */

	/*
	 * Service type.
	 */
	service_type = e_messenger_identity_get_service_type (emi);

	if (service_type == NULL)
		return FALSE;

	if (strlen (service_type) == 0) {
		g_free (service_type);
		return FALSE;
	}

	g_free (service_type); 

	/*
	 * Username.
	 */
	username = e_messenger_identity_get_username (emi);

	if (username == NULL)
		return FALSE;

	if (strlen (username) == 0) {
		g_free (username);
		return FALSE;
	}

	g_free (username);

	/*
	 * Password.
	 */
	password = e_messenger_identity_get_password (emi);

	if ((password != NULL) && strlen (password) == 0) {
		g_free (password);
		return FALSE;
	}

	g_free (password);

	return TRUE;
}

void
e_messenger_identity_free (EMessengerIdentity *emi)
{
	g_return_if_fail (emi != NULL);

	g_free (*emi);
}
