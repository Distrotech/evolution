#ifndef LIBTOC_H
#define LIBTOC_H

#include <gtk/gtk.h>

#define TOC_CONNECTION_TYPE             (toc_connection_get_type())
#define TOC_CONNECTION(obj)             (GTK_CHECK_CAST((obj), TOC_CONNECTION_TYPE, TOCConnection))
#define TOC_CONNECTION_CLASS(klass)     (GTK_CHECK_CLASS_CAST((klass), TOC_CONNECTION_TYPE, TOCConnectionClass))
#define IS_TOC_CONNECTION(obj)          (GTK_CHECK_TYPE((obj), TOC_CONNECTION_TYPE))
#define IS_TOC_CONNECTION_CLASS(klass)  (GTK_CHECK_CLASS_TYPE((klass), TOC_CONNECTION_TYPE))
#define TOC_CONNECTION_GET_CLASS(obj)   (TOC_CONNECTION_CLASS(GTK_OBJECT(obj)->klass))

typedef   enum _TOCConnectionStatus    TOCConnectionStatus;
typedef   enum _TOCConnectionUserFlags TOCConnectionUserFlags;
typedef struct _TOCConnection          TOCConnection;
typedef struct _TOCConnectionClass     TOCConnectionClass;

enum _TOCConnectionStatus {
	TOC_CONNECTION_OK,
	TOC_CONNECTION_ERROR
};

enum _TOCConnectionUserFlags {
	USER_FLAGS_NONE              = 0,
	USER_FLAGS_CONNECTED         = 1 << 0,
	USER_FLAGS_ON_AOL            = 1 << 1,
	USER_FLAGS_OSCAR_ADMIN       = 1 << 2,
	USER_FLAGS_OSCAR_UNCONFIRMED = 1 << 3,
	USER_FLAGS_OSCAR_NORMAL      = 1 << 4,
	USER_FLAGS_UNAVAILABLE       = 1 << 5
};

struct _TOCConnection {
	GtkObject parent;

	GIOChannel *channel;
	int seq;

	char *name;
	char *password;
};

struct _TOCConnectionClass {
	GtkObjectClass parent_class;

	void (*signed_in)(
		TOCConnection *conn, TOCConnectionStatus status);
	void (*message_in)(
		TOCConnection *conn, gboolean autoresponse,
		char *name, char *message);
	void (*user_info)(
		TOCConnection *conn, char *info);
	void (*user_update)(
		TOCConnection *conn, char *contact,
		TOCConnectionUserFlags flags, int evil_amount,
		time_t connection_time, int idle_time);
};

GtkType toc_connection_get_type(void);
TOCConnection *toc_connection_new(void);
TOCConnectionStatus toc_connection_signon(TOCConnection *conn, 
					  const char *login,
					  const char *password);
void toc_connection_signoff(TOCConnection *conn);
void toc_connection_set_away(TOCConnection *conn, const char *message);
void toc_connection_set_idle(TOCConnection *conn, int idle);
void toc_connection_send_message(TOCConnection *conn, const char *name, 
				 const char *message);
void toc_connection_get_info(TOCConnection *conn, const char *contact);
void toc_connection_add_buddy(TOCConnection *conn, const char *contact);
void toc_connection_remove_buddy(TOCConnection *conn, const char *contact);
void toc_connection_keepalive(TOCConnection *conn);

#endif /* LIBTOC_H */
