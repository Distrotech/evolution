#ifndef CALSERV_H
#define CALSERV_H 1

#include <glib.h>
#include <stdio.h>
#include "calendar.h"

#define CS_capabilities "ICAP AUTH=PLAINTEXT"
typedef struct {
  int servfd;
  GList *connections;
  GMainLoop *mainloop;
  GIOChannel *gioc;
} CSServer;
/* in server-io.c */
CSServer *cs_server_new(void);
void cs_server_run(CSServer *server);
void cs_server_destroy(CSServer *server);

/* for bandying around info on a command */
typedef struct {
  char *id;
  char *name;
  char *rol;
  gpointer cmd_specific_data;
} CSCmdInfo;
CSCmdInfo *cs_cmdinfo_dup(CSCmdInfo *ci);
void cs_cmdinfo_destroy(CSCmdInfo *ci);

typedef struct {
  CSServer *serv;
  GIOChannel *gioc;
  int fd;
  FILE *fh;
  GString *rdbuf;
  char *authid; /* username, if authenticated */

  gint reading_literal; /* 0 if not currently reading */
  char wrbuf[1024];

  Calendar *active_cal;
  gboolean active_is_readonly;

  CSCmdInfo *curcmd;
} CSConnection;

/* in server-commands.c */
void cs_connection_process_line(CSConnection *cnx, char *line);
static void cs_connection_process_literal(CSConnection *cnx, char *line);

/* in server-io.c */
void cs_connection_destroy(CSConnection *cnx);

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
