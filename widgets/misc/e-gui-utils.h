#ifndef GAL_GUI_UTILS_H
#define GAL_GUI_UTILS_H

#include <gtk/gtkmenu.h>
#include <gtk/gtkwindow.h>

#include <libgnomeui/gnome-messagebox.h>

void  e_popup_menu                   (GtkMenu *menu, GdkEventButton *event);
void  e_auto_kill_popup_menu_on_hide (GtkMenu *menu);
void  e_notice                       (GtkWindow *window, const char *type, const char *format, ...);
void e_container_foreach_leaf        (GtkContainer *container,
				      GtkCallback   callback,
				      gpointer      closure);
gint e_container_change_tab_order    (GtkContainer *container,
				      GList        *widgets);

#endif /* GAL_GUI_UTILS_H */
