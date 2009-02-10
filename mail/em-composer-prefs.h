/*
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
 *
 * Authors:
 *		Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef EM_COMPOSER_PREFS_H
#define EM_COMPOSER_PREFS_H

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <shell/e-shell.h>

/* Standard GObject macros */
#define EM_TYPE_COMPOSER_PREFS \
	(em_composer_prefs_get_type ())
#define EM_COMPOSER_PREFS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EM_TYPE_COMPOSER_PREFS, EMComposerPrefs))
#define EM_COMPOSER_PREFS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EM_TYPE_COMPOSER_PREFS, EMComposerPrefsClass))
#define EM_IS_COMPOSER_PREFS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EM_TYPE_COMPOSER_PREFS))
#define EM_IS_COMPOSER_PREFS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EM_TYPE_COMPOSER_PREFS))
#define EM_COMPOSER_PREFS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EM_TYPE_COMPOSER_PREFS, EMComposerPrefsClass))

G_BEGIN_DECLS

typedef struct _EMComposerPrefs EMComposerPrefs;
typedef struct _EMComposerPrefsClass EMComposerPrefsClass;

struct _ESignature;

struct _EMComposerPrefs {
	GtkVBox parent;

	GladeXML *gui;

	/* General tab */

	/* Default Behavior */
	GtkOptionMenu *charset;

	GtkTreeModel *language_model;

	/* Keyboard Shortcuts */
	GtkOptionMenu *shortcuts_type;

	/* Signatures */
	struct _GtkHTML *sig_preview;
};

struct _EMComposerPrefsClass {
	GtkVBoxClass parent_class;
};

GType		em_composer_prefs_get_type	(void);
GtkWidget *	em_composer_prefs_new		(EShell *shell);
void		em_composer_prefs_new_signature (GtkWindow *parent,
						 gboolean html_mode);

/* needed by global config */
#define EM_COMPOSER_PREFS_CONTROL_ID \
	"OAFIID:GNOME_Evolution_Mail_ComposerPrefs_ConfigControl:" BASE_VERSION

G_END_DECLS

#endif /* EM_COMPOSER_PREFS_H */
