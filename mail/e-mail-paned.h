/*
 * e-mail-paned.h
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

#ifndef E_MAIL_PANED_H
#define E_MAIL_PANED_H

#include <misc/e-paned.h>
#include <mail/e-mail-folder-pane.h>
#include <mail/e-mail-message-pane.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_PANED \
	(e_mail_paned_get_type ())
#define E_MAIL_PANED(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_PANED, EMailPaned))
#define E_MAIL_PANED_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_PANED, EMailPanedClass))
#define E_IS_MAIL_PANED(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_PANED))
#define E_IS_MAIL_PANED_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_PANED))
#define E_MAIL_PANED_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_PANED, EMailPanedClass))

G_BEGIN_DECLS

typedef struct _EMailPaned EMailPaned;
typedef struct _EMailPanedClass EMailPanedClass;
typedef struct _EMailPanedPrivate EMailPanedPrivate;

struct _EMailPaned {
	EPaned parent;
	EMailPanedPrivate *priv;
};

struct _EMailPanedClass {
	EPanedClass parent_class;

	/* Signals */
	void		(*mark_as_read)		(EMailPaned *paned,
						 const gchar *message_uid);
};

GType		e_mail_paned_get_type		(void);
GtkWidget *	e_mail_paned_new		(EShellBackend *shell_backend);
EShellBackend *	e_mail_paned_get_shell_backend	(EMailPaned *paned);
EMailFolderPane *
		e_mail_paned_get_folder_pane	(EMailPaned *paned);
EMailMessagePane *
		e_mail_paned_get_message_pane	(EMailPaned *paned);
guint		e_mail_paned_get_mark_as_read_delay
						(EMailPaned *paned);
void		e_mail_paned_set_mark_as_read_delay
						(EMailPaned *paned,
						 guint mark_as_read_delay);
gboolean	e_mail_paned_get_mark_as_read_enabled
						(EMailPaned *paned);
void		e_mail_paned_set_mark_as_read_enabled
						(EMailPaned *paned,
						 gboolean mark_as_read_enabled);
void		e_mail_paned_changed		(EMailPaned *paned);

G_END_DECLS

#endif /* E_MAIL_PANED_H */
