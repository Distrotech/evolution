#include "calserv.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static void
cs_command_NOOP(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  fprintf(cnx->fh, "%s OK NOOP\n", ci->id);
}

static void
cs_command_LOGOUT(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  fprintf(cnx->fh, "* BYE ICAP Server thinks you suck anyways\n");
  fprintf(cnx->fh, "%s OK LOGOUT Completed\n", ci->id);
  cs_connection_destroy(cnx);
}

static void
cs_command_CAPABILITY(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  fprintf(cnx->fh, "* CAPABILITY "CS_capabilities"\n");

  fprintf(cnx->fh, "%s OK CAPABILITY Completed\n", ci->id);
}

static void
cs_command_LOGIN(CSConnection *cnx, CSCmdInfo *ci, char *l)
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
cs_command_switchcals(CSConnection *cnx, CSCmdInfo *ci,
		      gboolean activate_rdonly)
{
  char *ctmp;
  char *calname, *daterange = NULL;
  Calendar *newcal;

  ctmp = ci->rol;

  calname = ctmp;
  if(ctmp) {
    ctmp = strchr(ctmp, ' ');
    if(ctmp) {
      *ctmp = '\0'; ctmp++;
      
      daterange = ctmp;
      ctmp = strchr(ctmp, ' ');
      if(ctmp) {
	fprintf(cnx->fh, "%s BAD %s too many args\n", ci->id, ci->name);
	return;
      }
    }
  }

  if(cs_calendar_authenticate(cnx, calname)) {
    fprintf(cnx->fh, "%s NO %s can't access Calendar store\n", ci->id, ci->name);
    return;
  }

  /* XXX todo - date range */
  /* OK we've done all the checks, now make it so */
  if (!strcmp (calname, cnx->authid))
	  calname = NULL;
  
  newcal = backend_open_calendar (cnx->authid, calname);
  if(!newcal) {
    fprintf(cnx->fh, "%s NO %s can't access Calendar store\n", ci->id, ci->name);
    return;
  }

  if(cnx->active_cal)
    backend_close_calendar(cnx->active_cal);
  cnx->active_cal = newcal;
  cnx->active_is_readonly = activate_rdonly;
  fprintf(cnx->fh, "* %s EXISTS\n", ci->id);
  fprintf(cnx->fh, "* FLAGS ()\n");
  fprintf(cnx->fh, "%s OK %s Completed\n", ci->id, ci->name);  
}

static void
cs_command_SELECT(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  cs_command_switchcals(cnx, ci, FALSE);
}

static void
cs_command_EXAMINE(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  cs_command_switchcals(cnx, ci, TRUE);
}

static void
cs_command_CREATE(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  char *ctmp;
  char *calname;

  if(!cnx->authid) {
    fprintf(cnx->fh, "%s NO %s login, luser\n", ci->id, ci->name);
    return;
  }

  ctmp = ci->rol;

  calname = ctmp;
  if(!calname) {
    fprintf(cnx->fh, "%s BAD %s not enough args\n", ci->id, ci->name);
    return;
  }

  backend_calendar_create(cnx->authid, calname);
  fprintf(cnx->fh, "%s OK %s Calendar store created\n", ci->id, ci->name);
}

static void
cs_command_DELETE(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  char *ctmp;
  char *calname;

  if(!cnx->authid) {
    fprintf(cnx->fh, "%s NO %s login, luser\n", ci->id, ci->name);
    return;
  }

  ctmp = ci->rol;

  calname = ctmp;
  if(!calname) {
    fprintf(cnx->fh, "%s BAD %s not enough args\n", ci->id, ci->name);
    return;
  }

  if (backend_calendar_inuse (cnx->authid, calname)){
     fprintf (cnx->fh, "%s BAD calendar in use\n", ci->id);
     return;
  }

  backend_delete_calendar (cnx->authid, calname);
  fprintf(cnx->fh, "%s OK %s Calendar store deleted\n", ci->id, ci->name);
}

static void
cs_command_LIST(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  char *ctmp;
  char *calname;
  GList *users, *ltmp;

  ctmp = ci->rol;

  calname = ctmp;
  if(!calname) {
    fprintf(cnx->fh, "%s BAD %s not enough args\n", ci->id, ci->name);
    return;
  } else {
    ctmp = strchr(ctmp, ' ');
    if(ctmp) { *ctmp = '\0'; }
  }

  if(strcmp(calname, "<*>")) {
    fprintf(cnx->fh, "%s NO %s failed, we suck at listing.\n", ci->id, ci->name);
    return;
  }

  users = backend_list_users();
  for(ltmp = users; ltmp; ltmp = g_list_next(ltmp)) {
    fprintf(cnx->fh, "* LIST () <%s>\n", ltmp->data);
  }
  g_list_foreach(ltmp, g_free, NULL);
  g_list_free(ltmp);
  fprintf(cnx->fh, "%s OK %s Completed\n", ci->id, ci->name);
}

static void
cs_command_LSUB(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  fprintf(cnx->fh, "%s NO LSUB subscription not yet implemented\n",
	  ci->id);
}

static void
cs_command_SUBSCRIBE(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  fprintf(cnx->fh, "%s NO %s subscription not yet implemented\n",
	  ci->id, ci->name);
}

static void
cs_command_UNSUBSCRIBE(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  fprintf(cnx->fh, "%s NO %s subscription not yet implemented\n",
	  ci->id, ci->name);
}

static void
cs_command_APPEND(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  char *ctmp;
  char *calname, *flags;

  ctmp = ci->rol;

  calname = ctmp;
  ctmp = strchr(ctmp, ' ');
  g_return_if_fail(ctmp);
  *ctmp = '\0'; ctmp++;

  flags = ctmp;
  ctmp = strchr(ctmp, ' ');
  g_return_if_fail(ctmp);
  *ctmp = '\0'; ctmp++;

  if(sscanf(ctmp, "{%d}", &cnx->reading_literal) < 1) {
    fprintf(cnx->fh, "%s BAD %s sscanf failed, blah\n",
	    ci->id, ci->name);
    return;
  }

  cnx->curcmd = cs_cmdinfo_dup(ci);
}

static void
cs_literal_APPEND(CSConnection *cnx, CSCmdInfo *ci, char *l)
{
  if(!cnx->active_cal) {
    fprintf(cnx->fh, "%s NO %s no current calendar\n",
	    ci->id, ci->name);
    return;
  }
  backend_add_object(cnx->active_cal, NULL);
  fprintf(cnx->fh, "%s OK %s completed\n", ci->id, ci->name);
  cs_cmdinfo_destroy(cnx->curcmd); cnx->curcmd = NULL;
}

void
cs_connection_process_literal(CSConnection *cnx, char *line)
{
  g_return_if_fail(cnx->curcmd);

#define CHECK_LIT(x) if(!strcasecmp(cnx->curcmd->name, #x)) \
cs_literal_##x(cnx, cnx->curcmd, line)

  CHECK_LIT(APPEND);
  else {
    g_warning("Unknown command %s", line);
    fprintf(cnx->fh, "%s BAD unknown literal handler\n", cnx->curcmd->name);
  }
}

void
cs_connection_process_line(CSConnection *cnx, char *l)
{
  char *ctmp;
  char *cmd_id, *cmd_name;
  char *origl;
  CSCmdInfo cmdinfo, *ciptr;

  origl = alloca(strlen(l) + 1);
  strcpy(origl, l);

  if(cnx->curcmd)
    ciptr = cnx->curcmd;
  else {
    memset(&cmdinfo, '\0', sizeof(cmdinfo));
    ciptr = &cmdinfo;

    ctmp = l;
    
    cmdinfo.id = ctmp;
    ctmp = strchr(ctmp, ' ');
    g_return_if_fail(ctmp);
    *ctmp = '\0';
    ctmp++;
    
    cmdinfo.name = ctmp;
    ctmp = strchr(ctmp, ' ');
    if(ctmp) {
      *ctmp = '\0';
      ctmp++;
      cmdinfo.rol = ctmp;
    }
  }

#define CHECK_CMD(x) if(!strcasecmp(cmd_name, #x)) \
cs_command_##x(cnx, ciptr, origl)

  CHECK_CMD(NOOP);
  else CHECK_CMD(LOGOUT);
  else CHECK_CMD(CAPABILITY);
  else CHECK_CMD(LOGIN);
  else CHECK_CMD(SELECT);
  else CHECK_CMD(EXAMINE);
  else CHECK_CMD(CREATE);
  else CHECK_CMD(DELETE);
  else CHECK_CMD(LIST);
  else CHECK_CMD(LSUB);
  else CHECK_CMD(SUBSCRIBE);
  else CHECK_CMD(UNSUBSCRIBE);
  else {
    g_warning("Unknown command %s", cmd_name);
    fprintf(cnx->fh, "%s BAD unknown command\n", ciptr->id);
  }
}
