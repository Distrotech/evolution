#ifndef CALSERV_H
#define CALSERV_H 1

#include <config.h>
#include <glib.h>
#include <stdio.h>
#include "calendar.h"
#include "backend.h"

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

/* for bandying around info on a command */
typedef struct _CSCmdArg CSCmdArg;
struct _CSCmdArg {
  enum { ITEM_UNKNOWN=0, ITEM_STRING, ITEM_SUBLIST } type;
  gpointer data;
  CSCmdArg *next, *up;
};
CSCmdArg *cs_cmdarg_new(CSCmdArg *prev, CSCmdArg *parent);
void cs_cmdarg_destroy(CSCmdArg *arg);

typedef struct {
  char *id;
  char *name;
  CSCmdArg *args;
} CSCmdInfo;

typedef struct {
  CSServer *serv;
  GIOChannel *gioc;
  int fd;
  FILE *fh;
  GString *rdbuf;
  char *authid; /* username, if authenticated */

  char wrbuf[1024];

  Calendar *active_cal;
  gboolean active_is_readonly;

  /* read state */
  enum { RS_ID=0, RS_NAME, RS_ARG, RS_DONE } rs;
  gint in_literal, literal_left, in_quoted; /* 0 if not currently reading */
  CSCmdArg *curarg;
  CSCmdInfo curcmd;
} CSConnection;

/* in server-commands.c */
void cs_connection_process_command(CSConnection *cnx);

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
