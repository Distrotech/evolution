#ifndef CALSERV_H
#define CALSERV_H 1

#include <config.h>
#include <glib.h>
#include <stdio.h>
#include "calendar.h"
#include "backend.h"
#include "net-parse.h"

#define CS_capabilities "ICAP AUTH=PLAINTEXT"

typedef struct {
  int servfd;
  GList *connections;
  GMainLoop *mainloop;
  GIOChannel *gioc;
} CSServer;

/* in server-io.c */
CSServer *cs_server_new     (void);
void      cs_server_run     (CSServer *server);
void      cs_server_destroy (CSServer *server);

typedef struct {
  int refcount;
  CSServer *serv;
  GIOChannel *gioc;
  int fd;
  FILE *fh;
  char *authid; /* username, if authenticated */

  char wrbuf[1024];

  Calendar *active_cal;
  gboolean active_is_readonly;

  StreamParse parse;
} CSConnection;

/* in server-commands.c */
void cs_connection_process_command(CSConnection *cnx);

/* in server-io.c */
void cs_connection_ref(CSConnection *cnx);
#define cs_connection_destroy cs_connection_unref
void cs_connection_unref(CSConnection *cnx);

typedef char * CSIdentifier;
CSIdentifier cs_identifier_new();
#define cs_identifier_destroy(x) g_destroy(x)

/* in server-auth.c */
/* returns 0 if authentication succeeded, otherwise if not */
gint cs_user_authenticate(CSConnection *cnx,
			  const char *username,
			  const char *password);
gint cs_calendar_authenticate(CSConnection *cnx,
			      const char *calendar);

#endif /* CALSERV_H */
