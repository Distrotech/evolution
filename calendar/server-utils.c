#include "calserv.h"

CSCmdArg *
cs_cmdarg_new(CSCmdArg *prev, CSCmdArg *parent)
{
  CSCmdArg *retval = g_new0(CSCmdArg, 1);

  prev->next = retval;
  if(parent)
    retval->up = parent;
  else if(prev)
    retval->up = prev->up;

  return retval;
}

gint
cs_cmdarg_nargs(CSCmdArg *arglist)
{
  return 1 + cs_cmdarg_nargs(arglist->next);
}

void
cs_cmdarg_destroy(CSCmdArg *arg)
{
  switch(arg->type) {
  case ITEM_STRING:
    g_free(arg->data); break;
  case ITEM_SUBLIST:
    cs_cmdarg_destroy(arg->data); break;
  default:
    g_warning("Unknown arg type %d", arg->type);
  }
  cs_cmdarg_destroy(arg->next);
  g_free(arg);
}

