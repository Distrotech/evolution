/* Evolution calendar - Delegate selector dialog
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: JP Rosevear <jpr@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#ifndef __E_DELEGATE_DIALOG_H__
#define __E_DELEGATE_DIALOG_H__

#include <libgnome/gnome-defs.h>
#include <gtk/gtkwidget.h>



#define E_TYPE_DELEGATE_DIALOG       (e_delegate_dialog_get_type ())
#define E_DELEGATE_DIALOG(obj)       (GTK_CHECK_CAST ((obj), E_TYPE_DELEGATE_DIALOG, EDelegateDialog))
#define E_DELEGATE_DIALOG_CLASS(klass) (GTK_CHECK_CLASS_CAST ((klass), E_TYPE_DELEGATE_DIALOG,	\
				      EDelegateDialogClass))
#define E_IS_DELEGATE_DIALOG(obj)    (GTK_CHECK_TYPE ((obj), E_TYPE_DELEGATE_DIALOG))
#define E_IS_DELEGATE_DIALOG_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), E_TYPE_DELEGATE_DIALOG))


typedef struct _EDelegateDialog		EDelegateDialog;
typedef struct _EDelegateDialogClass	EDelegateDialogClass;
typedef struct _EDelegateDialogPrivate	EDelegateDialogPrivate;

struct _EDelegateDialog {
	GtkObject object;

	/* Private data */
	EDelegateDialogPrivate *priv;
};

struct _EDelegateDialogClass {
	GtkObjectClass parent_class;
};

GtkType          e_delegate_dialog_get_type          (void);

EDelegateDialog* e_delegate_dialog_construct         (EDelegateDialog *etd,
						      const char      *name,
						      const char      *address);

EDelegateDialog* e_delegate_dialog_new               (const char      *name,
						      const char      *address);

char*            e_delegate_dialog_get_delegate      (EDelegateDialog *etd);

char*            e_delegate_dialog_get_delegate_name (EDelegateDialog *etd);

void             e_delegate_dialog_set_delegate      (EDelegateDialog *etd,
						      const char      *address);

GtkWidget*       e_delegate_dialog_get_toplevel      (EDelegateDialog *etd);




#endif /* __E_DELEGATE_DIALOG_H__ */
