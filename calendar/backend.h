#ifndef BACKEND_H
#define BACKEND_H

void      backend_init            (char *base_directory);
Calendar *backend_open_calendar   (char *username, char *calendar_name);
void      backend_close_calendar  (Calendar *cal);
GList    *backend_list_users      (void);
void      backend_add_object      (Calendar *calendar, iCalObject *object);
char     *backend_calendar_name   (char *username, char *calendar_name, gboolean must_exist);
int       backend_calendar_create (char *username, char *calendar_name);
void      backend_delete_calendar (char *username, char *calendar_name);
void      backend_close_calendar  (Calendar *calendar);
gboolean  backend_calendar_inuse (char *username, char *calendar_name);

#endif

