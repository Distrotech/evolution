#include "calserv.h"

/* returns 0 if authentication succeeded, otherwise if not */
gint cs_user_authenticate(CSConnection *cnx,
			  const char *username,
			  const char *password)
{
  g_warning("Authentication NYI, unconditionally succeeding");
  return 0;
}
