/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GAL_GUI_UTILS_H
#define GAL_GUI_UTILS_H

#include <gtk/gtkmenu.h>
#include <gtk/gtkwindow.h>

#include <libgnomeui/gnome-messagebox.h>

G_BEGIN_DECLS

void  e_popup_menu                    (GtkMenu      *menu,
				       GdkEvent     *event);
void  e_auto_kill_popup_menu_on_hide  (GtkMenu      *menu);
void  e_notice                        (GtkWindow    *window,
				       const char   *type,
				       const char   *format,
				       ...);
void  e_container_foreach_leaf        (GtkContainer *container,
				       GtkCallback   callback,
				       gpointer      closure);
void  e_container_focus_nth_entry     (GtkContainer *container,
				       int           n);
gint  e_container_change_tab_order    (GtkContainer *container,
				       GList        *widgets);

G_END_DECLS

#endif /* GAL_GUI_UTILS_H */
