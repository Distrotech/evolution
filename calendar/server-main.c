#include <glib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "calserv.h"

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

int main(int argc, char *argv[])
{
	CSServer *serv;
	char *base_dir = NULL;
	int i;
	
	for (i = 1; i < argc; i++){
		if (g_str_equal (argv [i], "-d")){
			i++;
			base_dir = argv [i];
			continue;
		}
	}
	if (!base_dir){
		g_warning ("Using a temporary directory to run!\n");
		base_dir = "/tmp/calendar-server";
		mkdir (base_dir, 0700);
	}
	
	backend_init (base_dir);
	
	serv = cs_server_new ();
	g_return_val_if_fail(serv, 1);
	
	cs_server_run(serv);
	
	cs_server_destroy(serv);
	
	return 0;
}
