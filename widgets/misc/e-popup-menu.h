/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef E_POPUP_MENU_H
#define E_POPUP_MENU_H

#include <gtk/gtkmenu.h>
#include <gtk/gtkwidget.h>

G_BEGIN_DECLS

#define E_POPUP_SEPARATOR  { "", NULL, (NULL), NULL,  0 }
#define E_POPUP_TERMINATOR { NULL, NULL, (NULL), NULL,  0 }

typedef struct _EPopupMenu EPopupMenu;

struct _EPopupMenu {
	char *name;
	char *pixname;
	GtkSignalFunc fn;
	EPopupMenu *submenu;
	guint32 disable_mask;
};

GtkMenu *e_popup_menu_create  (EPopupMenu     *menu_list,
			       guint32         disable_mask,
			       guint32         hide_mask,
			       void           *closure);

void     e_popup_menu_run     (EPopupMenu     *menu_list,
			       GdkEvent       *event,
			       guint32         disable_mask,
			       guint32         hide_mask,
			       void           *closure);

G_END_DECLS

#endif /* E_POPUP_MENU_H */
