#include "calserv.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define cs_command_RENAME cs_command_NOOP

static void
cs_command_NOOP(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "%s OK NOOP\r\n", ci->id);
}

static void
cs_command_LOGOUT(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "* BYE ICAP Server thinks you suck anyways\r\n");
  fprintf(cnx->fh, "%s OK LOGOUT Completed\r\n", ci->id);
  cs_connection_unref(cnx); /* give up server list ref */
}

static void
cs_command_CAPABILITY(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "* CAPABILITY "CS_capabilities"\r\n");

  fprintf(cnx->fh, "%s OK CAPABILITY Completed\r\n", ci->id);
}

static void
cs_command_LOGIN(CSConnection *cnx, CSCmdInfo *ci)
{
  char *username, *password;

  username = ci->args->data;
  password = ci->args->next->data;

  if(cs_user_authenticate(cnx, username, password)) {
    fprintf(cnx->fh, "%s NO LOGIN for 31337 d00dz\r\n", ci->id);
  } else {
    fprintf(cnx->fh, "%s OK LOGIN completed\r\n", ci->id);
    if(cnx->authid) g_free(cnx->authid);
    cnx->authid = g_strdup(username);
  }
}

static void
cs_command_switchcals(CSConnection *cnx, CSCmdInfo *ci,
		      gboolean activate_rdonly)
{
  char *calname, *daterange = NULL;
  Calendar *newcal;

  calname = ci->args->data;
  if(ci->args->next)
    daterange = ci->args->next->data;

  if(cs_calendar_authenticate(cnx, calname)) {
    fprintf(cnx->fh, "%s NO %s can't access Calendar store\r\n", ci->id, ci->name);
    return;
  }

  /* XXX todo - date range */
  /* OK we've done all the checks, now make it so */
  if (!strcmp (calname, cnx->authid))
	  calname = NULL;
  
  newcal = backend_open_calendar (cnx->authid, calname);
  if(!newcal) {
    fprintf(cnx->fh, "%s NO %s can't access Calendar store\r\n", ci->id, ci->name);
    return;
  }

  if(cnx->active_cal)
    backend_close_calendar(cnx->active_cal);

  cnx->active_cal = newcal;
  cnx->active_is_readonly = activate_rdonly;
  fprintf(cnx->fh, "* %s EXISTS\r\n", ci->id);
  fprintf(cnx->fh, "* FLAGS ()\r\n");
  fprintf(cnx->fh, "%s OK %s Completed\r\n", ci->id, ci->name);  
}

static void
cs_command_SELECT(CSConnection *cnx, CSCmdInfo *ci)
{
  cs_command_switchcals(cnx, ci, FALSE);
}

static void
cs_command_EXAMINE(CSConnection *cnx, CSCmdInfo *ci)
{
  cs_command_switchcals(cnx, ci, TRUE);
}

static void
cs_command_CREATE(CSConnection *cnx, CSCmdInfo *ci)
{
  char *calname;

  if(!cnx->authid) {
    fprintf(cnx->fh, "%s NO %s login, luser\r\n", ci->id, ci->name);
    return;
  }

  if(!ci->args || ci->args->type != ITEM_STRING) {
    fprintf(cnx->fh, "%s BAD %s invalid args\r\n", ci->id, ci->name);
    return;
  }

  calname = ci->args->data;

  backend_calendar_create(cnx->authid, calname);
  fprintf(cnx->fh, "%s OK %s Calendar store created\r\n", ci->id, ci->name);
}

static void
cs_command_DELETE(CSConnection *cnx, CSCmdInfo *ci)
{
  char *calname;

  if(!cnx->authid) {
    fprintf(cnx->fh, "%s NO %s login, luser\r\n", ci->id, ci->name);
    return;
  }

  calname = ci->args->data;
  if(!calname) {
    fprintf(cnx->fh, "%s BAD %s not enough args\r\n", ci->id, ci->name);
    return;
  }

  if (backend_calendar_inuse (cnx->authid, calname)){
     fprintf (cnx->fh, "%s NO calendar in use\r\n", ci->id);
     return;
  }

  backend_delete_calendar (cnx->authid, calname);
  fprintf(cnx->fh, "%s OK %s Calendar store deleted\r\n", ci->id, ci->name);
}

static void
cs_command_LIST(CSConnection *cnx, CSCmdInfo *ci)
{
  char *calname;
  GList *users, *ltmp;

  calname = ci->args->data;

  if(strcmp(calname, "<*>")) {
    fprintf(cnx->fh, "%s NO %s failed, we suck at listing.\r\n", ci->id, ci->name);
    return;
  }

  users = backend_list_users();
  for(ltmp = users; ltmp; ltmp = g_list_next(ltmp)) {
    fprintf(cnx->fh, "* LIST () <%s>\r\n", (char *)ltmp->data);
  }
  g_list_foreach(ltmp, (GFunc)g_free, NULL);
  g_list_free(ltmp);
  fprintf(cnx->fh, "%s OK %s Completed\r\n", ci->id, ci->name);
}

static void
cs_command_LSUB(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "%s NO LSUB subscription not yet implemented\r\n",
	  ci->id);
}

static void
cs_command_SUBSCRIBE(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "%s NO %s subscription not yet implemented\r\n",
	  ci->id, ci->name);
}

static void
cs_command_UNSUBSCRIBE(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "%s NO %s subscription not yet implemented\r\n",
	  ci->id, ci->name);
}

static void
cs_command_APPEND(CSConnection *cnx, CSCmdInfo *ci)
{
  char *calname;
  CSCmdArg *flags;
  char *obj;

  calname = ci->args->data;
  flags = ci->args->next->data;
  obj = ci->args->next->next->data;

  if(!cnx->active_cal) {
    fprintf(cnx->fh, "%s NO %s no current calendar\r\n",
	    ci->id, ci->name);
    return;
  }
  backend_add_object(cnx->active_cal, NULL);
  fprintf(cnx->fh, "%s OK %s completed\r\n", ci->id, ci->name);
}

static void
cs_command_ATTRIBUTE(CSConnection *cnx, CSCmdInfo *ci)
{
  char *calname;
  Calendar *cal;
  CSCmdArg *curitem;

  /*
    fprintf(cnx->fh, "%s NO %s NYI\r\n", ci->id, ci->name);
  */
  calname = ci->args->data;
  cal = backend_open_calendar(cnx->authid, calname);
  fprintf(cnx->fh, "* FETCH (");

  if(ci->args->next->type == ITEM_STRING)
    curitem = ci->args->next;
  else
    curitem = ci->args->next->data;

  for(; curitem; curitem = curitem->next) {
    if(!strcasecmp(curitem->data, "FLAGS")) {
      fprintf(cnx->fh, "FLAGS (");
      if(cal == cnx->active_cal)
	fprintf(cnx->fh, "\\Seen");
      /* support more flags, you moron */
      fprintf(cnx->fh, ")");
    } else if(!strcasecmp(curitem->data, "TYPE")) {
      fprintf(cnx->fh, "TYPE NIL"); /* no flags supported */
    } else if(!strcasecmp(curitem->data, "CSID")) {
      char *uid;

      uid = backend_get_id(cal);
      fprintf(cnx->fh, "CSID %s", uid);
      g_free(uid);
    } else if(!strcasecmp(curitem->data, "COMPONENTS")) {
      const char *compstr = "BEGIN: VCALENDAR\nPRODID:-//Yomomma Software//Pimpit Calendar v0.0//EN\n"
	"VERSION: 2.0\n"
	"BEGIN: VEVENT\n"
	"ATTACH; VALUE=URL:\n"
	"DESCRIPTION; VALUE=TEXT:\n"
	"DTSTART; VALUE=DATE-TIME:\n"
	"DTEND; VALUE=DATE-TIME:\n"
	"STATUS:\n"
	"SUMMARY; VALUE=TEXT:\n"
	"UID:\n"
	"END: VEVENT\n"
	"END: VCALENDAR";
      fprintf(cnx->fh, "COMPONENTS {%d}\n%s\n",
	      strlen(compstr), compstr);
    } else if(!strcasecmp(curitem->data, "TIMEZONE")) {
      /* NYI */
      fprintf(cnx->fh, "TIMEZONE NIL");
    }

    if(curitem->next) fprintf(cnx->fh, " ");
  }
  fprintf(cnx->fh, ")\n"); /* end * FETCH line */

  calendar_unref(cal);
  fprintf(cnx->fh, "%s OK %s completed\r\n", ci->id, ci->name);
}

static void
cs_command_FREEBUSY(CSConnection *cnx, CSCmdInfo *ci)
{
  char *t_start, *t_end;
  GSList *calendars = NULL, *ltmp;
  Calendar *curcal;
  time_t tm_start, tm_end;
  GString *tmpstr = g_string_new(NULL);

  if(ci->args->type == ITEM_STRING) {
    calendars = g_slist_prepend(calendars,
				backend_open_calendar(cnx->authid,
						      ci->args->data));
  } else { /* ITEM_LIST */
    CSCmdArg *curitem;

    for(curitem = ci->args->data; curitem; curitem = curitem->next) {
      g_assert(curitem->type == ITEM_STRING);

      curcal = backend_open_calendar(cnx->authid,
				     curitem->data);
      if(!curcal) {
	fprintf(cnx->fh, "%s BAD %s Invalid calendar name\r\n",
		ci->id, ci->name);
	goto errout;
      }

      calendars = g_slist_prepend(calendars, curcal);
    }
  }

  t_start = ci->args->next->data;
  t_end = ci->args->next->next->data;
  tm_start = parse_time_range(ci->args->next, NULL);
  tm_end = parse_time_range(ci->args->next->next, NULL);

  for(ltmp = calendars; ltmp; ltmp = g_slist_next(ltmp)) {
    GList *evs, *lltmp;

    curcal = ltmp->data;
    fprintf(cnx->fh, "CSNAME %s ICAL ", curcal->title);

    evs = calendar_get_events_in_range(curcal, tm_start, tm_end);
    g_string_sprintf(tmpstr,
		     "BEGIN: VCALENDAR\n"
		     "PRODID:-//blah//blum//EN\n"
		     "BEGIN: VFREEBUSY\n"
		     "ATTENDEE: %s\n"
		     "DTSTART: %s\nDTEND: %s\n"
		     "FREEBUSY; VALUE=PERIOD-START: ",
		     curcal->title, t_start, t_end);
    for(lltmp = evs; lltmp; lltmp = g_list_next(lltmp)) {
      struct tm *tt;
      tt = gmtime(&((CalendarObject *)lltmp->data)->ev_start);
      g_string_sprintfa(tmpstr, "%.4d%.2d%.2dT%.2d%.2d%.2dZ",
			tt->tm_year, tt->tm_mon,
			tt->tm_mday, tt->tm_hour, tt->tm_min,
			tt->tm_sec);
      tt = gmtime(&((CalendarObject *)lltmp->data)->ev_end);
      g_string_sprintfa(tmpstr, "/%.4d%.2d%.2dT%.2d%.2d%.2dZ%s",
			tt->tm_year, tt->tm_mon,
			tt->tm_mday, tt->tm_hour, tt->tm_min,
			tt->tm_sec,
			lltmp->next?", ":"");

    }
    g_string_sprintfa(tmpstr, "END: VFREEBUSY\nEND: VCALENDAR");
    fprintf(cnx->fh, "{%d}\n%s\n", tmpstr->len, tmpstr->str);
  }
  fprintf(cnx->fh, ")\n");

  fprintf(cnx->fh, "%s OK %s completed\r\n", ci->id, ci->name);

  errout:
  g_slist_foreach(calendars, (GFunc)calendar_unref, NULL);
  g_slist_free(calendars);
  g_string_free(tmpstr, TRUE);
}

static void
cs_command_FETCH(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "%s NO %s NYI\r\n", ci->id, ci->name);
}

static void
cs_command_STORE(CSConnection *cnx, CSCmdInfo *ci)
{
  fprintf(cnx->fh, "%s NO %s NYI\r\n", ci->id, ci->name);
}

typedef struct {
  int argtype;
  guchar is_required;
} ArgDef;

typedef struct {
  const char *name;
  const ArgDef *args;
  void (*handler)(CSConnection *cnx, CSCmdInfo *ci);
} CmdDef;

#define END_ARGS {ITEM_UNKNOWN, 0}
static ArgDef NO_ARGS[] = {END_ARGS};
static ArgDef LOGIN_ARGS[] = {{ITEM_STRING, 1},
			      {ITEM_STRING, 1},
			      END_ARGS};
static ArgDef SELECT_ARGS[] = {{ITEM_STRING, 1},
			       {ITEM_STRING, 0},
			       END_ARGS};
static ArgDef CREATE_ARGS[] = {{ITEM_STRING, 1},
			       END_ARGS};
static ArgDef RENAME_ARGS[] = {{ITEM_STRING, 1},
			       {ITEM_STRING, 1},
			       END_ARGS};
static ArgDef LIST_ARGS[] = {{ITEM_STRING, 1},
			     END_ARGS};
static ArgDef SUBSCRIBE_ARGS[] = {{ITEM_STRING, 1},
				  END_ARGS};
static ArgDef APPEND_ARGS[] = {{ITEM_STRING, 1},
			       {ITEM_SUBLIST, 1},
			       {ITEM_STRING, 1},
			       END_ARGS};
static ArgDef ATTRIBUTE_ARGS[] = {{ITEM_STRING, 1},
				  {ITEM_SUBLIST, 1},
				  END_ARGS};
static ArgDef FREEBUSY_ARGS[] = {{ITEM_SUBLIST, 1},
				 {ITEM_STRING, 1},
				 {ITEM_STRING, 1},
				 END_ARGS};
static ArgDef FETCH_ARGS[] = {{ITEM_STRING, 1},
			      {ITEM_SUBLIST, 1},
			      END_ARGS};
static ArgDef STORE_ARGS[] = {{ITEM_STRING, 1},
			      {ITEM_STRING, 1},
			      {ITEM_SUBLIST, 1},
			      END_ARGS};

static const CmdDef commands[] = {
  {"NOOP", NO_ARGS, cs_command_NOOP},
  {"LOGOUT", NO_ARGS, cs_command_LOGOUT},
  {"CAPABILITY", NO_ARGS, cs_command_CAPABILITY},
  {"LOGIN", LOGIN_ARGS, cs_command_LOGIN},
  {"SELECT", SELECT_ARGS, cs_command_SELECT},
  {"EXAMINE", SELECT_ARGS, cs_command_EXAMINE},
  {"CREATE", CREATE_ARGS, cs_command_CREATE},
  {"DELETE", CREATE_ARGS, cs_command_DELETE},
  {"RENAME", RENAME_ARGS, cs_command_RENAME},
  {"LIST", LIST_ARGS, cs_command_LIST},
  {"LSUB", LIST_ARGS, cs_command_LSUB},
  {"SUBSCRIBE", SUBSCRIBE_ARGS, cs_command_SUBSCRIBE},
  {"UNSUBSCRIBE", SUBSCRIBE_ARGS, cs_command_UNSUBSCRIBE},
  {"APPEND", APPEND_ARGS, cs_command_APPEND},
  {"ATTRIBUTE", ATTRIBUTE_ARGS, cs_command_ATTRIBUTE},
  {"FREEBUSY", FREEBUSY_ARGS, cs_command_FREEBUSY},
  {"FETCH", FETCH_ARGS, cs_command_FETCH},
  {"STORE", STORE_ARGS, cs_command_STORE},
  {NULL, NULL, NULL}
};

void
cs_connection_process_command(CSConnection *cnx)
{
  int i, j;
  CSCmdArg *arg;

  for(i = 0; commands[i].name; i++) {
    if(!strcasecmp(commands[i].name, cnx->parse.curcmd.name)) {
      /* check args */
      for(j = 0, arg = cnx->parse.curcmd.args;
	  commands[i].args[j].argtype && arg; arg = arg->next, j++) {
	if(commands[i].args[j].argtype != arg->type
	   && commands[i].args[j].argtype == ITEM_STRING)
	  goto errout;
      }
      if(commands[i].args[j].argtype) goto errout; /* too many args */
      if(!arg && commands[i].args[j].is_required) goto errout; /* too few */

      /* do it */
      commands[i].handler(cnx, &cnx->parse.curcmd);
    }
  }

  return;

 errout:
  g_warning("Unknown command %s", cnx->parse.curcmd.name);
  fprintf(cnx->fh, "%s BAD unknown command\r\n", cnx->parse.curcmd.id);
}
