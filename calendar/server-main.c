#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gnome.h>
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

static char *base_dir="/tmp/calendar-server";
static int portnum=7668;

struct poptOption options[] = {
{"basedir", '\0', POPT_ARG_STRING, &base_dir, 0, N_("Base directory"),
N_("DIR")},
{"port", 'p', POPT_ARG_INT, &portnum, 0, N_("Port for server to listen
on"), N_("PORT")},
{NULL, '\0', 0, NULL, 0}
};

int main(int argc, char *argv[])
{
	CSServer *serv;
	int i;
	
	gnomelib_init("gnome-cal-server", "0");
	gnomelib_register_popt_table(options, "gnome-cal-server options");
	poptFreeContext(gnomelib_parse_args(argc, argv, 0));

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
