#include <config.h>
#include <stdio.h>
#include <glib.h>
#include "calendar.h"

void
ical_object_try_alarms (iCalObject *obj)
{
	/* We do not need to try the alarms on the server */
}

void
alarm_kill (iCalObject *object)
{
	/* The server never schedules alarms */
}


int
main (int argc, char *argv [])
{
	printf ("It does not do anything yet\n");

	backend_init ("/tmp");
	return 0;
}
