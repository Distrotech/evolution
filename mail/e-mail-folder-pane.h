/*
 * e-mail-folder-pane.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_MAIL_FOLDER_PANE_H
#define E_MAIL_FOLDER_PANE_H

#include <gtk/gtk.h>
#include <camel/camel.h>
#include <shell/e-shell-backend.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_FOLDER_PANE \
	(e_mail_folder_pane_get_type ())
#define E_MAIL_FOLDER_PANE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_FOLDER_PANE, EMailFolderPane))
#define E_MAIL_FOLDER_PANE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_FOLDER_PANE, EMailFolderPaneClass))
#define E_IS_MAIL_FOLDER_PANE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_FOLDER_PANE))
#define E_IS_MAIL_FOLDER_PANE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_FOLDER_PANE))
#define E_MAIL_FOLDER_PANE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_FOLDER_PANE, EMailFolderPaneClass))

G_BEGIN_DECLS

typedef struct _EMailFolderPane EMailFolderPane;
typedef struct _EMailFolderPaneClass EMailFolderPaneClass;
typedef struct _EMailFolderPanePrivate EMailFolderPanePrivate;

struct _EMailFolderPane {
	GtkVBox parent;
	EMailFolderPanePrivate *priv;
};

struct _EMailFolderPaneClass {
	GtkVBoxClass parent_class;
};

GType		e_mail_folder_pane_get_type	(void);
GtkWidget *	e_mail_folder_pane_new		(EShellBackend *shell_backend);
CamelFolder *	e_mail_folder_pane_get_folder	(EMailFolderPane *folder_pane);
const gchar *	e_mail_folder_pane_get_folder_uri
						(EMailFolderPane *folder_pane);
void		e_mail_folder_pane_set_folder	(EMailFolderPane *folder_pane,
						 CamelFolder *folder,
						 const gchar *folder_uri);
gboolean	e_mail_folder_pane_get_group_by_threads
						(EMailFolderPane *folder_pane);
void		e_mail_folder_pane_set_group_by_threads
						(EMailFolderPane *folder_pane,
						 gboolean group_by_threads);
gboolean	e_mail_folder_pane_get_hide_deleted
						(EMailFolderPane *folder_pane);
void		e_mail_folder_pane_set_hide_deleted
						(EMailFolderPane *folder_pane,
						 gboolean hide_deleted);
GtkWidget *	e_mail_folder_pane_get_message_list
						(EMailFolderPane *folder_pane);
EShellBackend *	e_mail_folder_pane_get_shell_backend
						(EMailFolderPane *folder_pane);

G_END_DECLS

#endif /* E_MAIL_FOLDER_PANE_H */
