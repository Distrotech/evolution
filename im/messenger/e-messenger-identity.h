/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * EMessengerIdentity
 *
 * This type identifies a given user signon.  It is composed of:
 *
 *     - A service type (AIM, ICQ, etc).
 *     - A username.
 *
 * EMessengerIdentity is not a real GtkObject; it's just a typedef
 * with some helper functions.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2001, Ximian, Inc.
 */

#ifndef __E_MESSENGER_IDENTITY_H__
#define __E_MESSENGER_IDENTITY_H__

#include <libgnome/gnome-defs.h>
#include <glib.h>

BEGIN_GNOME_DECLS

typedef char * EMessengerIdentity;

EMessengerIdentity *e_messenger_identity_create                      (const char *service_type,
							              const char *username,
							              const char *password);
EMessengerIdentity *e_messenger_identity_create_from_string          (const char *id_string);
EMessengerIdentity *e_messenger_identity_create_from_me_and_username (EMessengerIdentity *me,
								      const char         *username);
char               *e_messenger_identity_to_string                   (EMessengerIdentity *emi);
gboolean            e_messenger_identity_is_equal                    (EMessengerIdentity *emi1,
							              EMessengerIdentity *emi2);
char               *e_messenger_identity_get_service_type            (EMessengerIdentity *emi);
char               *e_messenger_identity_get_username                (EMessengerIdentity *emi);
char               *e_messenger_identity_get_password                (EMessengerIdentity *emi);
void                e_messenger_identity_free                        (EMessengerIdentity *emi);

END_GNOME_DECLS

#endif /* ! __E_MESSENGER_IDENTITY_H__ */
