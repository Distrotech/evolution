#include "calserv.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>

int
try_to_parse (StreamParse *parse, int rsize, gboolean *error, gboolean *cont)
{
  gboolean found_something, is_eol;
  int itmp;
  char *ctmp, *ctmpp, *ctmps, *ctmpnl, *ctmpcr;
  
  found_something = FALSE;

  switch(parse->rs) {
    case RS_ID:
      ctmp = strchr(parse->rdbuf->str, ' ');
      if(ctmp) {
	itmp = ctmp - parse->rdbuf->str;
	parse->rs = RS_NAME;
	parse->curcmd.id = g_strndup(parse->rdbuf->str, itmp);
	g_string_erase(parse->rdbuf, 0, itmp + 1);
	found_something = TRUE;
      }
      break;
    case RS_NAME:
      is_eol = FALSE;
      ctmp = strchr(parse->rdbuf->str, ' ');

      if(!ctmp) {
	ctmp = strchr(parse->rdbuf->str, '\r');
	if(!ctmp) break;

	is_eol = TRUE;
      }
      found_something = TRUE;
      itmp = ctmp - parse->rdbuf->str;

      if(is_eol)
	parse->rs = RS_DONE;
      else {
	parse->rs = RS_ARG;
	parse->curarg = parse->curcmd.args = NULL;
      }

      parse->curcmd.name = g_strndup(parse->rdbuf->str, itmp);
      g_string_erase(parse->rdbuf, 0, itmp + 1);
      break;
    case RS_ARG:
      if(parse->in_literal) {

	if(parse->in_literal <= parse->rdbuf->len) {
	  parse->curarg = cs_cmdarg_new(parse->curarg, NULL);
	  parse->curarg->type = ITEM_STRING;
	  parse->curarg->data = g_strndup(parse->rdbuf->str, parse->in_literal);
	  parse->in_literal = 0;
	  g_string_erase(parse->rdbuf, 0, parse->in_literal + 2 /* CRLF */);
	  found_something = TRUE;
	}

	break;
      }

      switch(*parse->rdbuf->str) {
      case '(': /* open list */
	parse->curarg = cs_cmdarg_new(parse->curarg, NULL);
	if (parse->setptr) {
	  *parse->setptr = parse->curarg;  parse->setptr = NULL;
	}
	parse->curarg->type = ITEM_SUBLIST;
	g_string_erase(parse->rdbuf, 0, 1);
	found_something = TRUE;
	parse->setptr = &parse->curarg->data;
	parse->upargs = g_slist_prepend(parse->upargs, parse->curarg);
	parse->curarg = NULL;
	break;
      case ')': /* close list */
	g_string_erase(parse->rdbuf, 0, 1);
	if(parse->upargs) {
	  parse->curarg = parse->upargs->data;
	  parse->upargs = g_slist_remove(parse->upargs, parse->curarg);
	} else {
	  *error = TRUE;
	  *cont = FALSE;
	  g_warning("Too many close parens. zonk.");
	  return FALSE;
	}
	parse->setptr = NULL;
	found_something = TRUE;
	break;
      case '{': /* literal */
	ctmp = strchr(parse->rdbuf->str + 1, '}');
	if(!ctmp) break;
	if(*(ctmp + 1) != '\r' /* lameness */
	   || sscanf(parse->rdbuf->str + 1, "%d", &parse->in_literal) < 1) {
	  *error = TRUE;
	  cont = FALSE;
	  g_warning("bad literal. zonk.");
	  return FALSE;
	}
	found_something = TRUE;
	g_string_erase(parse->rdbuf, 0, ctmp - parse->rdbuf->str + 3 /* eliminate CR */);
	break;
      case '"':
	ctmp = strchr(parse->rdbuf->str + 1, '"');
	if(!ctmp) break;
	found_something = TRUE;
	parse->curarg = cs_cmdarg_new(parse->curarg, NULL);
	if (parse->setptr) {
	  parse->setptr = NULL;
	  *parse->setptr = parse->curarg;
	}
	parse->curarg->type = ITEM_STRING;
	parse->curarg->data = g_strndup(parse->rdbuf->str + 1, ctmp - parse->rdbuf->str - 1);
	g_string_erase(parse->rdbuf, 0, ctmp - parse->rdbuf->str);
	break;
      case ' ':
	g_string_erase(parse->rdbuf, 0, 1);
	found_something = parse->rdbuf->len;
	parse->in_literal = parse->in_quoted = 0;
	break;
      case '\r':
      case '\n':
	parse->rs = RS_DONE;
	g_string_erase(parse->rdbuf, 0,
		       (*parse->rdbuf->str == '\r')?2:1);
	break;
      default: /* warning, massive code duplication */
	/* it is a parse->rdbuf */
	ctmps = strchr(parse->rdbuf->str, ' ');
	ctmpp = strchr(parse->rdbuf->str, ')');
	ctmpcr = strchr(parse->rdbuf->str, '\r');
	ctmpnl = strchr(parse->rdbuf->str, '\n');

	if(ctmps) ctmp = ctmps;
	else if(ctmpp) ctmp = ctmpp;
	else if(ctmpcr) ctmp = ctmpcr;
	else if(ctmpnl) ctmp = ctmpnl;
	else break;

	if(ctmps) ctmp = MIN(ctmp, ctmps);
	if(ctmpp) ctmp = MIN(ctmp, ctmpp);
	if(ctmpnl) ctmp = MIN(ctmp, ctmpnl);
	if(ctmpcr) ctmp = MIN(ctmp, ctmpcr);

	found_something = TRUE;
	parse->curarg = cs_cmdarg_new(parse->curarg, NULL);
	if (parse->setptr) {
	  *parse->setptr = parse->curarg; parse->setptr = NULL;
	}
	parse->curarg->type = ITEM_STRING;
	parse->curarg->data = g_strndup(parse->rdbuf->str, ctmp - parse->rdbuf->str);
	g_string_erase(parse->rdbuf, 0, ctmp - parse->rdbuf->str);

	break;
      }
      break;
    default:
    }

    *cont = found_something;
    
    if(parse->rs == RS_DONE)
       return TRUE;

    return FALSE;
}

static time_t
parse_time(char **str)
{
  char *ctmp = *str;
  struct tm parsed_time;
  time_t retval;
  long saved_timezone = timezone;

  if(!isdigit(*ctmp)) return 0;
  if(sscanf(ctmp, "%.4d%.2d%.2dT%.2d%.2d%.2dZ",
	    &parsed_time.tm_year,
	    &parsed_time.tm_mon,
	    &parsed_time.tm_mday,
	    &parsed_time.tm_hour,
	    &parsed_time.tm_min,
	    &parsed_time.tm_sec) != 6) {
    return 0;
  }
  *str += 16;

  parsed_time.tm_isdst = 0;
  
  timezone = 0;
  retval = mktime(&parsed_time);
  timezone = saved_timezone;

  return retval;
}

static time_t
parse_time_period(char **str)
{
  time_t retval;
  char *ctmp = *str;
  struct tm parsed_time;
  long saved_timezone = timezone;
  unsigned long curval;
  
  if(!isdigit(*ctmp)) return 0;
  memset(&parsed_time, '\0', sizeof(parsed_time));

  timezone = 0;
  retval = mktime(&parsed_time);
  timezone = saved_timezone;
  
  while(isdigit(*ctmp)) {
    curval = strtoul(ctmp, &ctmp, 10);
    switch(tolower(*ctmp)) {
    case 'h':
      parsed_time.tm_hour = curval;
      break;
    case 'm':
      parsed_time.tm_min = curval;
      break;
    default:
      g_warning("Unknown time period modifier %c", *ctmp);
    }
  }

  return retval;
}

time_t
parse_time_range(CSCmdArg *arg, time_t *end_time)
{
  time_t retval;
  char *dat;

  dat = arg->data;

  retval = parse_time(&dat);
  if(end_time) {
    if(*dat == '/') {
      dat++;
      if(*dat == 'P') {
	dat++;
	*end_time = parse_time_period(&dat) + retval;
      } else
	*end_time = parse_time(&dat);
    } else
      *end_time = (time_t)-1;
  }

  return retval;
}

void
cs_cmdarg_dump(CSCmdArg *arg, int indent_level)
{
  int i;

  if(!arg) return;

  for(i = 0; i < indent_level; i++)
    g_print(" ");

  switch(arg->type) {
  case ITEM_SUBLIST:
    g_print("(\n");
    cs_cmdarg_dump((CSCmdArg *)arg->data, indent_level + 2);
    for(i = 0; i < indent_level; i++)
      g_print(" ");
    g_print(")\n");
    break;
  case ITEM_STRING:
    g_print("\"%s\"\n", (char *)arg->data);
    break;
  default:
    g_print("!!!! ITEM_UNKNOWN\n");
  }

  if(arg->next)
    cs_cmdarg_dump(arg->next, indent_level);
}
