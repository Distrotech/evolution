/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * A client-side GtkObject which exposes the Evolution:IM:Listener
 * interface.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2001, Ximian, Inc.
 */

#ifndef __E_MESSENGER_LISTENER_H__
#define __E_MESSENGER_LISTENER_H__

#include <libgnome/gnome-defs.h>
#include <bonobo/bonobo-object.h>

#include <Messenger.h>
#include <e-messenger-types.h>
#include <e-messenger-identity.h>

BEGIN_GNOME_DECLS

typedef struct _EMessengerListener EMessengerListener;
typedef struct _EMessengerListenerClass EMessengerListenerClass;
typedef struct _EMessengerListenerPrivate EMessengerListenerPrivate;

struct _EMessengerListener {
	BonoboObject               parent;
	EMessengerListenerPrivate *priv;
};

struct _EMessengerListenerClass {
	BonoboObjectClass parent;

	/*
	 * Signals
	 */
	void (*messages_queued) (void);
};

typedef enum {
	/* Async responses */
	SignonResponse,

	/* Async events */
	ReceiveMessageEvent,
	ContactUpdateEvent
} EMessengerListenerMessageType;

typedef struct {
	EMessengerListenerMessageType type;

	/* The ID to which this message corresponds. */
	EMessengerIdentity                *id;

	/* For SignonResponse */
	EMessengerSignonError              signon_error;
	GNOME_Evolution_Messenger_Backend  backend;

	/* For LinkStatusEvent */
	gboolean connected;

	/*
	 * For events about other people, such as ReceiveMessage and
	 * ContactUpdate.
	 */
	EMessengerIdentity *who;

	/* For ReceiveMessage */
	char               *message;
	gboolean            autoresponse;

	/* For ContactUpdate */
	EMessengerUserStatus  user_status;
	gboolean              online;
} EMessengerListenerMessage;

EMessengerListener        *e_messenger_listener_new            (void);
int                        e_messenger_listener_check_pending  (EMessengerListener *listener);
EMessengerListenerMessage *e_messenger_listener_pop_message    (EMessengerListener *listener);
GtkType                    e_messenger_listener_get_type       (void);
void                       e_messenger_listener_stop           (EMessengerListener *listener);

POA_GNOME_Evolution_Messenger_Listener__epv *e_messenger_listener_get_epv (void);

#define E_MESSENGER_LISTENER_TYPE        (e_messenger_listener_get_type ())
#define E_MESSENGER_LISTENER(o)          (GTK_CHECK_CAST ((o), E_MESSENGER_LISTENER_TYPE, EMessengerListener))
#define E_MESSENGER_LISTENER_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_MESSENGER_LISTENER_TYPE, EMessengerListenerClass))
#define E_IS_MESSENGER_LISTENER(o)       (GTK_CHECK_TYPE ((o), E_MESSENGER_LISTENER_TYPE))
#define E_IS_MESSENGER_LISTENER_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_MESSENGER_LISTENER_TYPE))

END_GNOME_DECLS

#endif /* ! __E_MESSENGER_LISTENER_H__ */
