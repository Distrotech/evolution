/*
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
 *
 * Authors:
 *		Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef EM_ACCOUNT_PREFS_H
#define EM_ACCOUNT_PREFS_H

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <table/e-table.h>

/* Standard GObject macros */
#define EM_TYPE_ACCOUNT_PREFS \
	(em_account_prefs_get_type ())
#define EM_ACCOUNT_PREFS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EM_TYPE_ACCOUNT_PREFS, EMAccountPrefs))
#define EM_ACCOUNT_PREFS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EM_TYPE_ACCOUNT_PREFS, EMAccountPrefsClass))
#define EM_IS_ACCOUNT_PREFS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EM_TYPE_ACCOUNT_PREFS))
#define EM_IS_ACCOUNT_PREFS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EM_TYPE_ACCOUNT_PREFS))
#define EM_ACCOUNT_PREFS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EM_TYPE_ACCOUNT_PREFS, EMAccountPrefsClass))

G_BEGIN_DECLS

typedef struct _EMAccountPrefs EMAccountPrefs;
typedef struct _EMAccountPrefsClass EMAccountPrefsClass;

struct _EMAccountPrefs {
	GtkVBox parent_object;

	GladeXML *gui;

	GtkWidget *druid;
	GtkWidget *editor;

	GtkTreeView *table;

	GtkButton *mail_add;
	GtkButton *mail_edit;
	GtkButton *mail_delete;
	GtkButton *mail_default;

	guint destroyed : 1;
	guint changed : 1;
};

struct _EMAccountPrefsClass {
	GtkVBoxClass parent_class;
};

GType		em_account_prefs_get_type	(void);
GtkWidget *	em_account_prefs_new		(void);

G_END_DECLS

#endif /* EM_ACCOUNT_PREFS_H */
