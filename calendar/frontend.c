#include <config.h>
#include <sys/types.h>
#include <dirent.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h>
#include "calendar.h"
#include "backend.h"

int
calendar_get_id (Calendar *cal)
{
	if (cal->server){
		g_warning ("Should ask the calendar server here to get me a unique ID\n");
	} else {
		id = gnome_config_get_int ("/calendar/Calendar/ID=0");
		id++;
		gnome_config_set_int ("/calendar/Calendar/ID", id);
		
		return id++;
	}
}

		 
