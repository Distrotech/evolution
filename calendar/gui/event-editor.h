/* Evolution calendar - Event editor dialog
 *
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Authors: Miguel de Icaza <miguel@helixcode.com>
 *          Federico Mena-Quintero <federico@helixcode.com>
 *          Seth Alves <alves@helixcode.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __EVENT_EDITOR_DIALOG_H__
#define __EVENT_EDITOR_DIALOG_H__

#include <libgnome/gnome-defs.h>
#include <gtk/gtkobject.h>
#include "gnome-cal.h"



#define TYPE_EVENT_EDITOR            (event_editor_get_type ())
#define EVENT_EDITOR(obj)            (GTK_CHECK_CAST ((obj), TYPE_EVENT_EDITOR, EventEditor))
#define EVENT_EDITOR_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), TYPE_EVENT_EDITOR,	\
				      EventEditorClass))
#define IS_EVENT_EDITOR(obj)         (GTK_CHECK_TYPE ((obj), TYPE_EVENT_EDITOR))
#define IS_EVENT_EDITOR_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), TYPE_EVENT_EDITOR))

typedef struct _EventEditor EventEditor;
typedef struct _EventEditorClass EventEditorClass;
typedef struct _EventEditorPrivate EventEditorPrivate;

struct _EventEditor {
	GtkObject object;

	/* Private data */
	EventEditorPrivate *priv;
};

struct _EventEditorClass {
	GtkObjectClass parent_class;
};


GtkType event_editor_get_type (void);
EventEditor *event_editor_construct (EventEditor *ee);

EventEditor *event_editor_new (void);

void event_editor_set_cal_client (EventEditor *ee, CalClient *client);
CalClient *event_editor_get_cal_client (EventEditor *ee);

void event_editor_set_event_object (EventEditor *ee, CalComponent *comp);

void event_editor_focus (EventEditor *ee);

void event_editor_update_widgets (EventEditor *ee);


GtkWidget *make_date_edit (void);
GtkWidget *make_spin_button (int val, int low, int high);



#endif /* __EVENT_EDITOR_DIALOG_H__ */
