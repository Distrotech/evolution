#include "calserv.h"

/* returns 0 if authentication succeeded, otherwise if not */
gint cs_user_authenticate(CSConnection *cnx,
			  const char *username,
			  const char *password)
{
  g_warning("user authentication NYI, unconditionally succeeding");
  return 0;
}

gint cs_calendar_authenticate(CSConnection *cnx,
			      const char *calendar)
{
  g_warning("calendar authentication NYI, unconditionally succeeding");
  return 0;
}
