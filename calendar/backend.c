#include <config.h>
#include <sys/types.h>
#include <dirent.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h>
#include <sys/stat.h>
#include <unistd.h>
#include "calendar.h"
#include "backend.h"

static char *base_directory;

void
backend_init (char *my_base_directory)
{
	base_directory = my_base_directory;
}

/**
 * xmkdir:
 * @path: creates a directory creating any missing components
 */
static void
xmkdir (char *path)
{
	char *last;

	if (g_str_equal (path, base_directory))
		return;
	
	last = g_basename (path);
	*(last-1) = 0;
	xmkdir (path);
	*(last-1) = '/';
	mkdir (path, 0777);
}


int
calendar_get_id (Calendar *cal)
{
	static int id;

	
	g_warning ("This is broken.  We need to get id from a file on the disk (relative to %s)\n", base_directory);
	return id++;
}

/**
 * backend_calendar_name:
 * @username: user owning the calendar
 * @calendar_name: the name of the calendar to get, or NULL for the default
 * @must_exist: if set, this will only return the name if the calendar exists
 *
 * Returns the filename of the calendar, or NULL if it does not exist and
 * must_exist was specified.
 */
char *
backend_calendar_name (char *username, char *calendar_name, gboolean must_exist)
{
	char *user_dir, *cal_file;

	if (!calendar_name)
		calendar_name = "default.vcf";
	
	user_dir = g_concat_dir_and_file (base_directory, username);
	cal_file = g_concat_dir_and_file (user_dir, calendar_name);
	g_free (user_dir);

	if (must_exist){
		if (g_file_exists (calendar_name))
			return cal_file;
		else {
			g_free (cal_file);
			return NULL;
		}
	}
	return cal_file;
}

static gboolean
calendar_path_is_directory (char *cal_file)
{
	if (cal_file [strlen (cal_file)-1] == '/')
		return TRUE;
	else
		return FALSE;
}

/**
 * backend_calendar_create:
 * @username: the user to which this calendar belongs
 * @calendar_name: the name of the calendar to create
 *
 * Returns 0 on success, -1 otherwise
 */
int 
backend_calendar_create (char *username, char *calendar_name)
{
	char *cal_file;

	g_return_val_if_fail (username != NULL, -1);
	g_return_val_if_fail (calendar_name != NULL, -1);
	
	cal_file = backend_calendar_name (username, calendar_name, FALSE);
	
	if (!calendar_path_is_directory (cal_file)){
		char *p = g_basename (cal_file);

		*(p-1) = 0;
		xmkdir (cal_file);
	} else {
		truncate (cal_file, 0);
	}
	g_free (cal_file);
	return 0;
}

Calendar *
backend_open_calendar (char *username, char *calendar_name)
{
	char *cal_file;
	Calendar *cal;

	g_return_val_if_fail (username != NULL, NULL);

	cal_file = backend_calendar_name (username, calendar_name, TRUE);
	if (!cal_file)
		return NULL;
	
	cal = calendar_get (cal_file);
	g_free (cal_file);

	return cal;
}

void
backend_close_calendar (Calendar *calendar)
{
	g_return_if_fail (calendar != NULL);
	
	calendar_save (calendar, NULL);
	calendar_unref (calendar);
}

void
backend_delete_calendar (char *username, char *calendar_name)
{
	char *cal_file;
	
	g_return_if_fail (username != NULL);
	g_return_if_fail (calendar_name != NULL);

	cal_file = backend_calendar_name (username, calendar_name, TRUE);
	if (cal_file){
		if (g_file_test (cal_file, G_FILE_TEST_ISDIR))
			rmdir (cal_file);
		else
			unlink (cal_file);
	}
	g_free (cal_file);
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

	return list;
}

/**
 * backend_add_object:
 * @calendar: Our calendar;
 * @object: a calendar object we received from the CUA
 *
 * Add an object that has been received from the Calendar User Agent
 * into the database.
 *
 * If the list of attendees is not empty, we have to schedule appointments
 * for all those people. 
 */
void
backend_add_object (Calendar *calendar, iCalObject *object)
{
	
}

gboolean
backend_calendar_inuse (char *username, char *calendar_name)
{
	gboolean ret_val;
	char *cal_file;

	g_return_val_if_fail (username != NULL, FALSE);
	g_return_val_if_fail (cal_file != NULL, FALSE);
	
	cal_file = backend_calendar_name (username, calendar_name, FALSE);
	ret_val = calendar_loaded (cal_file);
	g_free (cal_file);

	return ret_val;
}
