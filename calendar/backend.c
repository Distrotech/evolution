#include <config.h>
#include <sys/types.h>
#include <dirent.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h>
#include "calendar.h"
#include "backend.h"

static char *base_directory;

void
backend_init (char *base_directory)
{
	base_directory = base_directory;
}

Calendar *
backend_open_calendar (char *username)
{
	char *fullname;
	Calendar *cal;

	g_return_val_if_fail (username != NULL, NULL);
	
	fullname = g_concat_dir_and_file (base_directory, username);

	cal = calendar_new ("");
	calendar_load (cal, fullname);
	
	g_free (fullname);

	return cal;
}

void
backend_close_calendar (Calendar *calendar)
{
	g_return_if_fail (calendar != NULL);
	
	calendar_save (calendar, NULL);
	calendar_destroy (calendar);
}

/**
 * backend_list_users:
 *
 * Returns a GList containing g_strduped names of the existing
 * calendars on this server
 */
GList *
backend_list_users (void)
{
	DIR *dir;
	struct dirent *dent;
	GList *list;
	
	list = NULL;
	dir = opendir (base_directory);
	if (!dir)
		return NULL;

	
	while ((dent = readdir (dir)) != NULL){
		int len = strlen (dent->d_name);

		if (len < sizeof (".calendar"))
			continue;
		
		if (strcmp (dent->d_name + len - sizeof (".calendar"), ".calendar") != 0)
			continue;

		list = g_list_prepend (list, g_strdup (dent->d_name));
	}
	
	closedir (dir);
}

