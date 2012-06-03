/*
 * e-mail-signature-editor.h
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

#ifndef E_MAIL_SIGNATURE_EDITOR_H
#define E_MAIL_SIGNATURE_EDITOR_H

#include <gtkhtml-editor.h>
#include <misc/e-focus-tracker.h>
#include <libedataserver/e-source-registry.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_SIGNATURE_EDITOR \
	(e_mail_signature_editor_get_type ())
#define E_MAIL_SIGNATURE_EDITOR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_SIGNATURE_EDITOR, EMailSignatureEditor))
#define E_MAIL_SIGNATURE_EDITOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_SIGNATURE_EDITOR, EMailSignatureEditorClass))
#define E_IS_MAIL_SIGNATURE_EDITOR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_SIGNATURE_EDITOR))
#define E_IS_MAIL_SIGNATURE_EDITOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_SIGNATURE_EDITOR))
#define E_MAIL_SIGNATURE_EDITOR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_SIGNATURE_EDITOR, EMailSignatureEditorClass))

G_BEGIN_DECLS

typedef struct _EMailSignatureEditor EMailSignatureEditor;
typedef struct _EMailSignatureEditorClass EMailSignatureEditorClass;
typedef struct _EMailSignatureEditorPrivate EMailSignatureEditorPrivate;

struct _EMailSignatureEditor {
	GtkhtmlEditor parent;
	EMailSignatureEditorPrivate *priv;
};

struct _EMailSignatureEditorClass {
	GtkhtmlEditorClass parent_class;
};

GType		e_mail_signature_editor_get_type
						(void) G_GNUC_CONST;
GtkWidget *	e_mail_signature_editor_new	(ESourceRegistry *registry,
						 ESource *source);
EFocusTracker *	e_mail_signature_editor_get_focus_tracker
						(EMailSignatureEditor *editor);
ESourceRegistry *
		e_mail_signature_editor_get_registry
						(EMailSignatureEditor *editor);
ESource *	e_mail_signature_editor_get_source
						(EMailSignatureEditor *editor);
void		e_mail_signature_editor_commit	(EMailSignatureEditor *editor,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_mail_signature_editor_commit_finish
						(EMailSignatureEditor *editor,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* E_MAIL_SIGNATURE_EDITOR_H */
