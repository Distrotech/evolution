/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * The Evolution Instant Messenger client object.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2001, Ximian, Inc.
 */

#ifndef __E_MESSENGER_H__
#define __E_MESSENGER_H__

#include <libgnome/gnome-defs.h>
#include <gtk/gtktypeutils.h>
#include <gtk/gtkobject.h>

#include <e-messenger-types.h>
#include <e-messenger-identity.h>

BEGIN_GNOME_DECLS

typedef struct _EMessenger        EMessenger;
typedef struct _EMessengerClass   EMessengerClass;
typedef struct _EMessengerPrivate EMessengerPrivate;

struct _EMessenger {
	GtkObject     parent;
	EMessengerPrivate *priv;
};

struct _EMessengerClass {
	GtkObjectClass parent;

	/*
	 * Signals.
	 */
	void (* link_status)     (EMessenger               *messenger,
				  const EMessengerIdentity *identity,
				  gboolean                  connected);

	void (* receive_message) (EMessenger               *messenger,
				  const EMessengerIdentity *identity,
				  const char          	   *sender,
				  const char           	   *message,
				  gboolean            	    autoresponse);
				  
	void (* contact_update) (EMessenger               *messenger,
				 const EMessengerIdentity *identity,
				 const char          	  *contact,
				 gboolean            	   online,
				 EMessengerUserStatus      user_status);
	
};

typedef void (*EMessengerSignonCallback) (EMessenger               *messenger,
					  const EMessengerIdentity *identity,
					  EMessengerSignonError     error,
					  gpointer                  closure);

EMessenger *e_messenger_new       (void);

/*
 * Connection management.
 */
gboolean    e_messenger_signon    (EMessenger               *messenger,
				   EMessengerIdentity       *id,
				   EMessengerSignonCallback  signon_callback,
				   gpointer                  closure);
gboolean    e_messenger_signoff   (EMessenger               *messenger,
				   EMessengerIdentity       *id);

/* Gtk Type System Stuff */
GtkType     e_messenger_get_type  (void);


#define E_MESSENGER_TYPE        (e_messenger_get_type ())
#define E_MESSENGER(o)          (GTK_CHECK_CAST ((o), E_MESSENGER_TYPE, EMessenger))
#define E_MESSENGER_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_MESSENGER_TYPE, EMessengerClass))
#define E_IS_MESSENGER(o)       (GTK_CHECK_TYPE ((o), E_MESSENGER_TYPE))
#define E_IS_MESSENGER_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_MESSENGER_TYPE))

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define gtk_signal_default_marshaller gtk_marshal_NONE__NONE

void gtk_marshal_NONE__POINTER_POINTER_POINTER_BOOL (GtkObject    *object,
						     GtkSignalFunc func,
						     gpointer      func_data,
						     GtkArg       *args);

void gtk_marshal_NONE__POINTER_POINTER_BOOL_INT (GtkObject    *object,
						 GtkSignalFunc func,
						 gpointer      func_data,
						 GtkArg       *args);
#ifdef __cplusplus
}
#endif /* __cplusplus */

END_GNOME_DECLS

#endif /* ! __E_MESSENGER_H__ */
