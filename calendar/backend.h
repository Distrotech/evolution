#ifndef BACKEND_H
#define BACKEND_H

void      backend_init           (char *base_directory);
Calendar *backend_open_calendar  (char *username);
void      backend_close_calendar (Calendar *cal);

#endif
