#include "calserv.h"

CSCmdInfo *cs_cmdinfo_dup(CSCmdInfo *ci)
{
  CSCmdInfo *retval;

  retval = g_new0(CSCmdInfo, 1);
  retval->id = g_strdup(ci->id);
  retval->name = g_strdup(ci->name);
  retval->rol = g_strdup(ci->rol);

  return retval;
}

void
cs_cmdinfo_destroy(CSCmdInfo *ci)
{
  g_free(ci->id);
  g_free(ci->name);
  g_free(ci->rol);
  g_free(ci);
}
