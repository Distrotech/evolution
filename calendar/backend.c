#include <config.h>
#include <sys/types.h>
#include <dirent.h>
#include <glib.h>
#include <fcntl.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
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

char *
backend_get_id(Calendar *cal)
{
	FILE *inf;
	char *fname;
	char buf[32];
	
	fname = g_copy_strings (cal->filename, ".uid", NULL);
	inf = fopen (fname, "r");
	g_free (fname);
	if (!inf)
		return NULL;
	fgets (buf, sizeof (buf), inf);
	fclose (inf);
	
	return g_strdup (g_strchomp (g_strstrip (buf)));
}

static void
calendar_assign_id(const char *filename)
{
	FILE *inf;
	unsigned long lastid;
	char *fname;

	lastid = gnome_config_get_int("/calendar/Calendar/ID=0");

	fname = g_copy_strings(filename, ".uid", NULL);
	inf = fopen(fname, "w");
	g_free(fname);
	if(!inf) return;
	fprintf(inf, "%.16lu\n", lastid);
	fclose(inf);
	lastid++;

	gnome_config_set_int("/calendar/Calendar/ID", lastid);	
	gnome_config_sync();
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
		calendar_name = "default.calendar";
	
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
	
	if (calendar_path_is_directory (cal_file)) {
		char *p = g_basename (cal_file);

		*(p-1) = 0;
		xmkdir (cal_file);
	} else {
		int fd;
		
		fd = open (cal_file, O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if (fd != -1)
			close (fd);
	}
	calendar_assign_id (cal_file);
	g_free (cal_file);
	return 0;
}

void
backend_verify_default (char *username)
{
	char *user_dir;
	
	g_return_if_fail (username != NULL);
	user_dir = g_concat_dir_and_file (base_directory, username);
	if (!g_file_exists (user_dir)){
		xmkdir (user_dir);
		backend_calendar_create (username, "default.calendar");
	}
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
		char *user_dir, *def_calendar;
		struct stat s;

		user_dir = g_concat_dir_and_file (base_directory, dent->d_name);
		def_calendar = g_concat_dir_and_file (user_dir, "default.calendar");

		if (stat (def_calendar, &s) == 0)
			list = g_list_prepend (list, g_strdup (dent->d_name));

		g_free (def_calendar);
		g_free (user_dir);
	}
	
	closedir (dir);

	return list;
}

GList *
backend_list_user_calendars (char *username)
{
	struct dirent *dent;
	DIR *dir;
	GList *list = NULL;
	char *user_dir;

	user_dir = g_concat_dir_and_file (base_directory, username);

	dir = opendir (user_dir);
	if (!dir){
		g_free (user_dir);
		return NULL;
	}

	while ((dent = readdir (dir)) != NULL){
		const int extsize = sizeof (".calendar")-1;
		int len = strlen (dent->d_name);

		if (len < extsize)
			continue;

		if (strcmp (dent->d_name + len - extsize, ".calendar") == 0){
			char *full, *name, *p;

			name = g_strdup (dent->d_name);
			p = strstr (name, ".calendar");
			*p = 0;
			full = g_copy_strings ("<", username, ">/", name, NULL);
			g_free (name);
			
			list = g_list_prepend (list, full);
		}
	}
	g_free (user_dir);

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
	g_return_if_fail (calendar != NULL);
	g_return_if_fail (object != NULL);
}

Calendar *
backend_calendar_inuse (char *username, char *calendar_name)
{
	Calendar *ret_val;
	char *cal_file;

	g_return_val_if_fail (username != NULL, FALSE);
	g_return_val_if_fail (calendar_name != NULL, FALSE);
	
	cal_file = backend_calendar_name (username, calendar_name, FALSE);
	ret_val = calendar_loaded (cal_file);
	g_free (cal_file);

	return ret_val;
}
