#ifndef CALSERV_H
#define CALSERV_H 1

#include <glib.h>
#include <stdio.h>

#define CS_capabilities "ICAP AUTH=PLAINTEXT"
typedef struct {
  int servfd;
  GList *connections;
  GMainLoop *mainloop;
  GIOChannel *gioc;
} CSServer;
/* in calserv.c */
CSServer *cs_server_new(void);
void cs_server_run(CSServer *server);
void cs_server_destroy(CSServer *server);

typedef struct {
  CSServer *serv;
  GIOChannel *gioc;
  int fd;
  FILE *fh;
  GString *rdbuf;
  char *authid; /* username, if authenticated */

  GHashTable *curcmds; /* pending commands */
  gint reading_literal; /* 0 if not currently reading */
  char wrbuf[1024];
} CSConnection;

/* in commands.c */
void cs_connection_process_command(CSConnection *cnx, char *cmd_id,
				   char *cmd_name, char *rest_of_line);
/* in calserv.c */
void cs_connection_destroy(CSConnection *cnx);

typedef char * CSIdentifier;
CSIdentifier cs_identifier_new();
#define cs_identifier_destroy(x) g_destroy(x)

/* in auth.c */
/* returns 0 if authentication succeeded, otherwise if not */
gint cs_user_authenticate(CSConnection *cnx,
			  const char *username,
			  const char *password);

#endif /* CALSERV_H */
