#include "calserv.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>

int
try_to_parse (StreamParse *parse, int rsize, gboolean *error, gboolean *cont)
{
  gboolean found_something, is_eol;
  int itmp;
  char *ctmp;
  
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
	parse->literal_left -= rsize;

	if(parse->literal_left <= 0) {
	  parse->curarg->data = g_strndup(parse->rdbuf->str, parse->in_literal);
	  parse->in_literal = parse->literal_left = 0;
	  g_string_erase(parse->rdbuf, 0, parse->in_literal + 2 /* CRLF */);
	  found_something = TRUE;
	}

	break;
      }

      switch(*parse->rdbuf->str) {
      case '(': /* open list */
	parse->curarg = cs_cmdarg_new(NULL, parse->curarg);
	if (!parse->curcmd.args)
		parse->curcmd.args = parse->curarg;
	parse->curarg->type = ITEM_SUBLIST;
	g_string_erase(parse->rdbuf, 0, 1);
	found_something = TRUE;
	break;
      case ')': /* close list */
	if(!parse->curarg) {
	  *error = TRUE;
	  *cont = FALSE;
	  g_warning("Too many close parens. zonk.");
	  return FALSE;
	}
	parse->curarg = parse->curarg->up;
	break;
      case '{': /* literal */
	ctmp = strchr(parse->rdbuf->str + 1, '}');
	if(!ctmp) break;
	if(*(ctmp + 1) != '\r' /* lameness */
	   || sscanf(parse->rdbuf->str + 1, "%d", &parse->in_literal) < 1) {
	  error = TRUE;
	  cont = FALSE;
	  g_warning("bad literal. zonk.");
	  return FALSE;
	}
	found_something = TRUE;
	parse->literal_left = parse->in_literal + 2;
	g_string_erase(parse->rdbuf, 0, ctmp - parse->rdbuf->str + 2 /* eliminate CR */);
	break;
      case '"':
	ctmp = strchr(parse->rdbuf->str + 1, '"');
	if(!ctmp) break;
	found_something = TRUE;
	parse->curarg = cs_cmdarg_new(parse->curarg, NULL);
	if(!parse->curcmd.args)
	  parse->curcmd.args = parse->curarg;
	parse->curarg->type = ITEM_STRING;
	parse->curarg->data = g_strndup(parse->rdbuf->str + 1, ctmp - parse->rdbuf->str - 1);
	g_string_erase(parse->rdbuf, 0, ctmp - parse->rdbuf->str);
	break;
      case ' ':
	found_something = parse->rdbuf->len;
	parse->in_literal = parse->literal_left = parse->in_quoted = 0;
	g_string_erase(parse->rdbuf, 0, 1);
	break;
      case '\r':
	if(parse->rdbuf->len > 2) {
	  parse->rs = RS_DONE;
	  g_string_erase(parse->rdbuf, 0, 2);
	}
	break;
      default: /* warning, massive code duplication */
	/* it is a parse->rdbuf */
	ctmp = strchr(parse->rdbuf->str, ' ');
	if(ctmp) {
	  found_something = TRUE;
	  parse->curarg = cs_cmdarg_new(parse->curarg, NULL);
	  if(!parse->curcmd.args)
	    parse->curcmd.args = parse->curarg;
	  parse->curarg->type = ITEM_STRING;
	  parse->curarg->data = g_strndup(parse->rdbuf->str, ctmp - parse->rdbuf->str);
	  g_string_erase(parse->rdbuf, 0, ctmp - parse->rdbuf->str);
	  break;
	}
	ctmp = strchr(parse->rdbuf->str, ')');
	if(ctmp) {
	  found_something = TRUE;
	  parse->curarg = cs_cmdarg_new(parse->curarg, NULL);
	  if(!parse->curcmd.args)
	    parse->curcmd.args = parse->curarg;
	  parse->curarg->type = ITEM_STRING;
	  parse->curarg->data = g_strndup(parse->rdbuf->str, ctmp - parse->rdbuf->str);
	  parse->curarg = parse->curarg->up;
	  g_string_erase(parse->rdbuf, 0, ctmp - parse->rdbuf->str);
	  break;
	}
	ctmp = strchr(parse->rdbuf->str, '\r');
	if(ctmp) {
	  found_something = TRUE;
	  parse->rs = RS_DONE;
	  parse->curarg = cs_cmdarg_new(parse->curarg, NULL);
	  if(!parse->curcmd.args)
	    parse->curcmd.args = parse->curarg;
	  parse->curarg->type = ITEM_STRING;
	  parse->curarg->data = g_strndup(parse->rdbuf->str, ctmp - parse->rdbuf->str);
	  g_string_erase(parse->rdbuf, 0, ctmp - parse->rdbuf->str);
	  break;
	}
	break;
      }
      break;
    default:
    }

    *cont = found_something;
    
    if(parse->rs == RS_DONE)
       return TRUE;

}
