#include <gnome.h>
#include "libtoc.h"

typedef struct {
	TOCConnection *conn;
	const char *contact;
} TestInfo;

static void
user_update_cb(TOCConnection *conn, char *contact,
	       TOCConnectionUserFlags flags, int evil_amount,
	       time_t connection_time, int idle_time)
{
	printf("CALLBACK: User update: %s, flags: %d, evil: %d, ct: %d, idle: %d\n", contact, flags, evil_amount, (int) connection_time, idle_time);
}

static gboolean
go_go_gadget(TestInfo *info)
{
	toc_connection_add_buddy(info->conn, info->contact);

	toc_connection_send_message(info->conn, info->contact, "you rule");

	return FALSE;
} /* go_go_gadget */

int
main(int argc, char *argv[])
{
	TestInfo info;

	if (argc < 4) {
		printf("Usage: test-backend <signon> <password> "
		       "<contact to message>\n");
		exit(1);
	}

	gnome_init("test-program", "0.0", argc, argv);

	info.conn = toc_connection_new();
	info.contact = argv[3];

	toc_connection_signon(info.conn, argv[1], argv[2]);

	gtk_signal_connect(GTK_OBJECT(info.conn), "user_update",
			   GTK_SIGNAL_FUNC(user_update_cb), NULL);

	gtk_timeout_add(5000, (GtkFunction) go_go_gadget, &info);
	
	gtk_main();

	return 0;
} /* main */
