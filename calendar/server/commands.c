#include "calserv.h"
#include <string.h>

typedef struct {
  char *id;
  char *rol;
} CSCmdInfo;

static void
cs_command_NOOP(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "%s OK NOOP\n", ci->id);
}

static void
cs_command_LOGOUT(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "* BYE ICAP Server thinks you suck anyways\n");
  fprintf(cnx->fh, "%s OK LOGOUT Completed\n", ci->id);
  cs_connection_destroy(cnx);
}

static void
cs_command_CAPABILITY(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "* CAPABILITY "CS_capabilities"\n");

  fprintf(cnx->fh, "%s OK CAPABILITY Completed\n", ci->id);
}

static void
cs_command_LOGIN(CSConnection *cnx, CSCmdInfo *ci)
{
  char *ctmp;
  char *username, *password;

  ctmp = ci->rol;

  username = ctmp;
  ctmp = strchr(ctmp, ' ');
  g_return_if_fail(ctmp);
  *ctmp = '\0'; ctmp++;

  password = ctmp;
  ctmp = strchr(ctmp, ' ');
  if(ctmp) {
    fprintf(cnx->fh, "%s BAD LOGIN You type worse than raster\n", ci->id);
    return;
  }

  if(cs_user_authenticate(cnx, username, password)) {
    fprintf(cnx->fh, "%s NO LOGIN for 31337 d00dz\n", ci->id);
  } else {
    fprintf(cnx->fh, "%s OK LOGIN completed", ci->id);
    cnx->authid = g_strdup(username);
  }
}

static void
cs_command_SELECT(CSConnection *cnx, CSCmdInfo *ci)
{
}

void
cs_connection_process_command(CSConnection *cnx,
			      char *cmd_id,
			      char *cmd_name,
			      char *rest_of_line)
{
  CSCmdInfo cmdinfo;

  memset(&cmdinfo, '\0', sizeof(cmdinfo));
  cmdinfo.id = cmd_id;
  cmdinfo.rol = rest_of_line;

#define CHECK_CMD(x) if(!strcasecmp(cmd_name, #x)) \
cs_command_##x(cnx, &cmdinfo)

  if(0); /* workaround emacs */
  else CHECK_CMD(NOOP);
  else CHECK_CMD(LOGOUT);
  else CHECK_CMD(CAPABILITY);
  else CHECK_CMD(LOGIN);
  else CHECK_CMD(SELECT);
  else {
    g_warning("Unknown command %s", cmd_name);
    fprintf(cnx->fh, "%s BAD unknown command\n", cmd_id);
  }
}
