#ifndef CALSERV_H
#define CALSERV_H 1

#include <glib.h>

typedef struct {
  int servfd;
  GList *connections;
  GMainLoop *mainloop;
  GIOChannel *gioc;
} CSServer;
CSServer *cs_server_new(void);
void cs_server_run(CSServer *server);
void cs_server_destroy(CSServer *server);

typedef struct {
  CSServer *serv;
  GIOChannel *gioc;
  int fd;
} CSConnection;
void cs_connection_accept(gpointer data, GIOCondition cond,
			  CSServer *server);
void cs_connection_destroy(CSConnection *cnx);

typedef char * CSIdentifier;
CSIdentifier cs_identifier_new();
#define cs_identifier_destroy(x) g_destroy(x)

#endif /* CALSERV_H */
