#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <gtk/gtk.h>
#include "libtoc.h"

#define TOC_HOST "toc.oscar.aol.com"
#define TOC_PORT 21

#define LOGIN_HOST "login.oscar.aol.com"
#define LOGIN_PORT 5190

typedef   enum _SFLAPFrameType    SFLAPFrameType;
typedef struct _SFLAPHeader       SFLAPHeader;
typedef struct _SFLAPSignonHeader SFLAPSignonHeader;

enum _SFLAPFrameType {
	FRAME_TYPE_SIGNON     = 1,
	FRAME_TYPE_DATA       = 2,
	FRAME_TYPE_ERROR      = 3,
	FRAME_TYPE_SIGNOFF    = 4,
	FRAME_TYPE_KEEP_ALIVE = 5
};

struct _SFLAPHeader {
	char asterisk;
	char frame_type;
	short sequence;
	short length;
};

struct _SFLAPSignonHeader {
	SFLAPHeader header;
	char version[4];
	char tlv_tag[2];
	short name_length;
};

static GtkObjectClass *parent_class;

enum SIGNALS {
	SIGNED_IN,
	MESSAGE_IN,
	USER_INFO,
	USER_UPDATE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
toc_connection_destroy(GtkObject *obj)
{
	(* GTK_OBJECT_CLASS(parent_class)->destroy)(obj);
} /* toc_connection_destroy */

typedef void (*GtkSignal_NONE__POINTER_INT_INT_INT_INT)(GtkObject *object,
							gpointer arg1,
							int arg2,
							int arg3,
							int arg4,
							int arg5,
							gpointer user_data);

static void
e_marshal_NONE__POINTER_INT_INT_INT_INT(GtkObject *object,
					GtkSignalFunc func,
					gpointer func_data,
					GtkArg *args)
{
	GtkSignal_NONE__POINTER_INT_INT_INT_INT rfunc;
	rfunc = (GtkSignal_NONE__POINTER_INT_INT_INT_INT) func;
	(*rfunc)(
		object, GTK_VALUE_POINTER(args[0]), GTK_VALUE_INT(args[1]),
		GTK_VALUE_INT(args[2]), GTK_VALUE_INT(args[3]),
		GTK_VALUE_INT(args[4]), func_data);
} /* e_marshal_NONE__POINTER_INT_INT_INT_INT */

static void
toc_connection_class_init(TOCConnectionClass *klass)
{
	GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);

	object_class->destroy = toc_connection_destroy;

	parent_class = gtk_type_class(gtk_object_get_type());

	signals[SIGNED_IN] =
		gtk_signal_new("signed_in",
			       GTK_RUN_LAST,
			       object_class->type,
			       GTK_SIGNAL_OFFSET(
				       TOCConnectionClass, signed_in),
			       gtk_marshal_NONE__INT,
			       GTK_TYPE_NONE, 1,
			       GTK_TYPE_INT);
	signals[MESSAGE_IN] =
		gtk_signal_new("message_in",
			       GTK_RUN_LAST,
			       object_class->type,
			       GTK_SIGNAL_OFFSET(
				       TOCConnectionClass, message_in),
			       gtk_marshal_NONE__POINTER_INT_POINTER,
			       GTK_TYPE_NONE, 3,
			       GTK_TYPE_POINTER,
			       GTK_TYPE_INT,
			       GTK_TYPE_POINTER);

	signals[USER_INFO] = 
		gtk_signal_new("user_info",
			       GTK_RUN_LAST,
			       object_class->type,
			       GTK_SIGNAL_OFFSET(
				       TOCConnectionClass, user_info),
			       gtk_marshal_NONE__POINTER,
			       GTK_TYPE_NONE, 1,
			       GTK_TYPE_POINTER);

	signals[USER_UPDATE] =
		gtk_signal_new("user_update",
			       GTK_RUN_LAST,
			       object_class->type,
			       GTK_SIGNAL_OFFSET(
				       TOCConnectionClass, user_update),
			       e_marshal_NONE__POINTER_INT_INT_INT_INT,
			       GTK_TYPE_NONE, 5,
			       GTK_TYPE_POINTER,
			       GTK_TYPE_INT,
			       GTK_TYPE_INT,
			       GTK_TYPE_INT,
			       GTK_TYPE_INT);

	gtk_object_class_add_signals(object_class, signals, LAST_SIGNAL);
} /* toc_connection_class_init */

static void
toc_connection_init(TOCConnection *conn)
{
	conn->channel = NULL;
} /* toc_connection_init */

GtkType
toc_connection_get_type(void)
{
	static GtkType type = 0;

	if (!type) {
		GtkTypeInfo info = {
			"TOCConnection",
			sizeof(TOCConnection),
			sizeof(TOCConnectionClass),
			(GtkClassInitFunc) toc_connection_class_init,
			(GtkObjectInitFunc) toc_connection_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};
		type = gtk_type_unique(gtk_object_get_type(), &info);
	}

	return type;
} /* toc_connection_get_type */

TOCConnection *
toc_connection_new(void)
{
	TOCConnection *conn;

	conn = gtk_type_new(toc_connection_get_type());

	return conn;
} /* toc_connection_new */

static GByteArray *
sflap_encode(TOCConnection *conn, SFLAPFrameType type, char *data)
{
	SFLAPHeader header;
	GByteArray *buf;
	int len;

	if (type == FRAME_TYPE_SIGNON) {
		g_warning("Use sflap_signon_encode instead.");
		return NULL;
	}

	if (type == FRAME_TYPE_KEEP_ALIVE)
		len = 0;
	else
		len = strlen(data) + 1;


	header.asterisk = '*';
	header.frame_type = type;
	header.sequence = htons(conn->seq++ & 0xffff);
	header.length = htons((short)len);

	buf = g_byte_array_new();
	buf = g_byte_array_append(buf, (guint8 *)&header, sizeof(header));
	if (len)
		buf = g_byte_array_append(buf, data, len);

	return buf;
} /* sflap_encode */

static GByteArray *
sflap_signon_encode(TOCConnection *conn, const char *name)
{
	SFLAPSignonHeader header;
	GByteArray *buf;
	
	header.header.asterisk = '*';
	header.header.frame_type = FRAME_TYPE_SIGNON;
	header.header.sequence = htons(conn->seq++);
	header.header.length = htons((short) (strlen(name)+8));
	header.version[0] = 0;
	header.version[1] = 0;
	header.version[2] = 0;
	header.version[3] = 1;
	header.tlv_tag[0] = 0;
	header.tlv_tag[1] = 1;
	header.name_length = htons((short) strlen(name));

	buf = g_byte_array_new();
	buf = g_byte_array_append(buf, (guint8 *)&header, sizeof(header));
	buf = g_byte_array_append(buf, name, strlen(name));

	return buf;
} /* sflap_signon_encode */

static char *
roast(const char *password)
{
	int len;
	char roast[] = "Tic/Toc";
	char *roasted;
	int i;

	len = strlen(password);

	roasted = g_malloc0(strlen(password)*2+2);

	snprintf(roasted, 3, "0x");
	for (i = 0; i < len; i++) {
		snprintf(&roasted[2*i+2], 3, "%02x", 
			 password[i] ^ roast[i % strlen(roast)]);
	}
	roasted[2*i+2] = '\0';

	return roasted;
} /* roast */

static GIOChannel *
connect_address(char *hostname, int port)
{
	GIOChannel *channel;
	struct hostent *host;
	unsigned int addr;
	struct sockaddr_in sin;
	int fd;

	host = gethostbyname(hostname);
	if (!host)
		return NULL;
	addr = ((struct in_addr *) host->h_addr)->s_addr;
	sin.sin_addr.s_addr = addr;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		return NULL;
	
	if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		return NULL;

	channel = g_io_channel_unix_new(fd);

	return channel;
} /* connect_address */

static void
sflap_send(TOCConnection *conn, SFLAPFrameType frame_type, char *data)
{
	char *req;
	GByteArray *d;
	int bytes;

	printf("Debug OUT: %s\n", data);

	req = g_strdup(data);
	d = sflap_encode(conn, frame_type, req);
	g_io_channel_write(conn->channel, d->data, d->len, &bytes);
	g_byte_array_free(d, TRUE);
} /* sflap_send */

static gboolean
read_http(GIOChannel *channel, GIOCondition cond, TOCConnection *conn)
{
	int bytes;
	char buf[8192];
	int i;
	char *message;

	g_io_channel_read(channel, buf, 8192, &bytes);
	for (i = 0; i < bytes && buf[i] != '<'; i++);
	if (buf[i]) {
		message = g_strdup(&buf[i]);
		gtk_signal_emit(
			GTK_OBJECT(conn), signals[USER_INFO], message);
		g_free(message);
	}

	g_io_channel_close(channel);

	return FALSE;
} /* read_http */

static gboolean
read_data(GIOChannel *channel, GIOCondition cond, TOCConnection *conn)
{
	int bytes;
	GIOError err;
	SFLAPHeader header;
	char *in;
	char *out;
	int len;

	err = g_io_channel_read(
		channel, (char *)&header, sizeof(header), &bytes);
	in = g_malloc0(ntohs(header.length));
	err = g_io_channel_read(channel, in, ntohs(header.length), &bytes);
	printf("Debug  IN: %s\n", in);

	if (header.frame_type == FRAME_TYPE_SIGNON) {
		printf("Signon header.\n");
		g_free(in);

		out = g_strdup_printf(
			"toc_signon %s %d %s %s english \"Evolution\"",
			LOGIN_HOST, LOGIN_PORT, conn->name, conn->password);
		sflap_send(conn, FRAME_TYPE_DATA, out);
		g_free(out);

		return TRUE;
	}
	else if (header.frame_type == FRAME_TYPE_SIGNOFF) {
		printf("Signoff header.\n");

		return TRUE;
	}

	len = strlen(in);

	if (len >= 7 && strncmp(in, "SIGN_ON", 7) == 0) {
		/* TOC *really* *really* wants you to add a buddy before you
		   can sign yourself in. We don't want to add any real users
		   here, so adding ourself and removing seems to work */

		out = g_strdup_printf("toc_add_buddy %s", conn->name);
		sflap_send(conn, FRAME_TYPE_DATA, out);
		g_free(out);

		out = g_strdup_printf("toc_remove_buddy %s", conn->name);
		sflap_send(conn, FRAME_TYPE_DATA, out);
		g_free(out);

		out = "toc_add_permit ";
		sflap_send(conn, FRAME_TYPE_DATA, out);

		out = "toc_add_deny ";
		sflap_send(conn, FRAME_TYPE_DATA, out);

		out = "toc_init_done";
		sflap_send(conn, FRAME_TYPE_DATA, out);

		gtk_signal_emit(
			GTK_OBJECT(conn), signals[SIGNED_IN], 
			TOC_CONNECTION_OK);
	}
	else if (len >= 5 && strncmp(in, "IM_IN", 5) == 0) {
		/* IM_IN:<Source User>:<Auto Response T/F>:<Message> */

		char **v;

		v = g_strsplit(in, ":", 3);
		printf("Message from \"%s\"%s: %s\n", v[1], 
		       v[2][0] == 'T' ? " (Auto Response)" : "", v[3]);
		gtk_signal_emit(
			GTK_OBJECT(conn), signals[MESSAGE_IN], v[1],
			v[2][0] == 'T' ? TRUE : FALSE, v[3]);
		g_strfreev(v);
			
	}
	else if (len >= 12 && strncmp(in, "UPDATE_BUDDY", 12) == 0) {
		/* UPDATE_BUDDY:<Buddy User>:<Online? T/F>:<Evil Amount>:
		 * <Signon Time>:<IdleTime>:<UC>
		 */

		char **v;
		TOCConnectionUserFlags flags = USER_FLAGS_NONE;
		int evil, idle;
		time_t conn_time;

		v = g_strsplit(in, ":", 6);

		if (v[2][0] == 'T')
			flags |= USER_FLAGS_CONNECTED;

		evil = atoi(v[3]);
		conn_time = atoi(v[4]);
		idle = atoi(v[5]);

		if (v[6][0] == 'A')
			flags |= USER_FLAGS_ON_AOL;

		if (v[6][1] == 'A')
			flags |= USER_FLAGS_OSCAR_ADMIN;
		else if (v[6][1] == 'U')
			flags |= USER_FLAGS_OSCAR_UNCONFIRMED;
		else if (v[6][1] == 'O')
			flags |= USER_FLAGS_OSCAR_NORMAL;

		if (v[6][2] == 'U')
			flags |= USER_FLAGS_UNAVAILABLE;

		gtk_signal_emit(
			GTK_OBJECT(conn), signals[USER_UPDATE], v[1], 
			flags, evil, conn_time, idle);
		g_strfreev(v);
	}
	else if (len >= 8 && strncmp(in, "GOTO_URL", 8) == 0) {
		char **v;
		GIOChannel *channel;
		char *req;
		int bytes;

		v = g_strsplit(in, ":", 2);
		channel = connect_address(TOC_HOST, TOC_PORT);
		req = g_strdup_printf("GET /%s HTTP/1.0\r\n\r\n", v[2]);
		g_io_channel_write(channel, req, strlen(req), &bytes);
		g_free(req);
		g_io_add_watch(channel, G_IO_IN, (GIOFunc) read_http, conn);
	}
	else if (strncmp(in, "ERROR", 5) == 0) {
		/* ERROR:<Error Code> */
		printf("Error: %s\n", in);

		return FALSE;
	}
	else {
		printf("Don't know how to handle \"%s\"\n", in);
	}

	return TRUE;
} /* read_data */

static char *
aim_encode(const char *s)
{
	char *o;
	int i;
	int j;

	o = g_malloc0(strlen(s)*2);
	for (i = 0, j = 0; i < strlen(s); i++) {
		switch (s[i]) {
		case '$':
		case '{':
		case '}':
		case '[':
		case ']':
		case '(':
		case ')':
		case '\"':
		case '\\':
			o[j++] = '\\';
		default:
			o[j++] = s[i];
		}
	}

	return o;
} /* aim_encode */

void
toc_connection_keepalive(TOCConnection *conn)
{
	sflap_send(conn, FRAME_TYPE_KEEP_ALIVE, NULL);
} /* toc_connection_keepalive */

void
toc_connection_add_buddy(TOCConnection *conn, const char *contact)
{
	char *req;

	req = g_strdup_printf("toc_add_buddy %s", contact);
	sflap_send(conn, FRAME_TYPE_DATA, req);
	g_free(req);
} /* toc_connection_add_buddy */

void
toc_connection_remove_buddy(TOCConnection *conn, const char *contact)
{
	char *req;

	req = g_strdup_printf("toc_remove_buddy %s", contact);
	sflap_send(conn, FRAME_TYPE_DATA, req);
	g_free(req);
} /* toc_connection_remove_buddy */

void
toc_connection_get_info(TOCConnection *conn, const char *contact)
{
	char *req;

	req = g_strdup_printf("toc_get_info %s", contact);
	sflap_send(conn, FRAME_TYPE_DATA, req);
	g_free(req);
} /* toc_connection_get_info */

void
toc_connection_send_message(TOCConnection *conn, const char *name, 
			    const char *message)
{
	char *req;
	char *em;

	em = aim_encode(message);
	req = g_strdup_printf("toc_send_im %s \"%s\"", name, em);
	g_free(em);
	sflap_send(conn, FRAME_TYPE_DATA, req);
	g_free(req);
} /* toc_connection_send_message */
	
void
toc_connection_set_idle(TOCConnection *conn, int idle)
{
	char *req;

	req = g_strdup_printf("toc_set_idle %d", idle);
	sflap_send(conn, FRAME_TYPE_DATA, req);
	g_free(req);
} /* toc_connection_set_idle */

void
toc_connection_set_away(TOCConnection *conn, const char *message)
{
	char *req;

	if (message)
		req = g_strdup_printf("toc_set_away \"%s\"", message);
	else
		req = g_strdup("toc_set_away");
	sflap_send(conn, FRAME_TYPE_DATA, req);
	g_free(req);
} /* toc_connection_set_away */

void
toc_connection_signoff(TOCConnection *conn)
{
	g_io_channel_close(conn->channel);
	conn->channel = NULL;
	conn->seq = 0;
	g_free(conn->name);
	g_free(conn->password);
	conn->name = NULL;
	conn->password = NULL;
} /* toc_connection_signoff */

TOCConnectionStatus
toc_connection_signon(TOCConnection *conn, const char *name, 
		      const char *password)
{
	int bytes;
	GByteArray *signon;

	g_return_val_if_fail(conn, TOC_CONNECTION_ERROR);

	conn->name = g_strdup(name);
	conn->password = roast(password);

	conn->channel = connect_address(TOC_HOST, TOC_PORT);
	if (!conn->channel)
		return TOC_CONNECTION_ERROR;

	g_io_channel_write(
		conn->channel, "FLAPON\r\n\r\n", 10, &bytes);

	signon = sflap_signon_encode(conn, name);
	g_io_channel_write(conn->channel, signon->data, signon->len, &bytes);
	g_byte_array_free(signon, FALSE);
	g_io_add_watch(conn->channel, G_IO_IN, (GIOFunc) read_data, conn);

	return TOC_CONNECTION_OK;
} /* toc_connection_signon */
