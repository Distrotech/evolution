/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* em-folder-selection-button.c
 *
 * Copyright (C) 2003  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#include <config.h>

#include "em-folder-selection-button.h"

#include "mail-component.h"
#include "em-folder-selection.h"
#include "em-marshal.h"

#include <gal/util/e-util.h>

#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkhbox.h>


#define PARENT_TYPE gtk_button_get_type ()
static GtkButtonClass *parent_class = NULL;


struct _EMFolderSelectionButtonPrivate {
	GtkWidget *icon;
	GtkWidget *label;

	CamelFolder *selected_folder;

	char *title;
	char *caption;
};

enum {
	SELECTED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };


/* Utility functions.  */

static void
set_contents_unselected (EMFolderSelectionButton *button)
{	
	gtk_image_set_from_pixbuf (GTK_IMAGE (button->priv->icon), NULL);
	gtk_label_set_text (GTK_LABEL (button->priv->label), _("<click here to select a folder>"));
}

static void
set_contents (EMFolderSelectionButton *button)
{
	EMFolderSelectionButtonPrivate *priv = button->priv;
	CamelStore *store;
	EStorage *storage;
	EFolder *folder;
	const char *storage_name;
	char *path;
	char *label;

	if (priv->selected_folder == NULL) {
		set_contents_unselected (button);
		return;
	}

	store = camel_folder_get_parent_store (priv->selected_folder);
	storage = mail_component_lookup_storage (mail_component_peek (), store);

	if (storage == NULL) {
		set_contents_unselected (button);
		return;
	}

	storage_name = e_storage_get_name (storage);

	path = g_strconcat ("/", camel_folder_get_full_name (priv->selected_folder), NULL);
	folder = e_storage_get_folder (storage, path);
	g_free (path);

	if (folder == NULL) {
		set_contents_unselected (button);
		return;
	}

	/* EPFIXME icon */

	label = g_strdup_printf (_("\"%s\" in \"%s\""), e_folder_get_name (folder), e_storage_get_name (storage));
	gtk_label_set_text (GTK_LABEL (priv->label), label);
	g_free (label);
}

static void
set_selection (EMFolderSelectionButton *button,
	       CamelFolder *folder)
{
	if (button->priv->selected_folder == folder)
		return;

	if (button->priv->selected_folder != NULL)
		camel_object_unref (CAMEL_OBJECT (button->priv->selected_folder));

	if (folder != NULL) {
		camel_object_ref (CAMEL_OBJECT (folder));
		button->priv->selected_folder = folder;
	}

	set_contents (button);
}


/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	EMFolderSelectionButtonPrivate *priv = EM_FOLDER_SELECTION_BUTTON (object)->priv;

	if (priv->selected_folder != NULL) {
		camel_object_unref (CAMEL_OBJECT (priv->selected_folder));
		priv->selected_folder = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	EMFolderSelectionButtonPrivate *priv = EM_FOLDER_SELECTION_BUTTON (object)->priv;

	g_free (priv->title);
	g_free (priv->caption);
	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/* GtkButton methods.  */

static void
impl_clicked (GtkButton *button)
{
	EMFolderSelectionButtonPrivate *priv = EM_FOLDER_SELECTION_BUTTON (button)->priv;
	GtkWidget *toplevel;
	CamelFolder *folder;

	if (GTK_BUTTON_CLASS (parent_class)->clicked != NULL)
		(* GTK_BUTTON_CLASS (parent_class)->clicked) (button);

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
	folder = em_folder_selection_run_dialog (toplevel ? GTK_WINDOW (toplevel) : NULL,
						 priv->title,
						 priv->caption,
						 priv->selected_folder);

	em_folder_selection_button_set_selection (EM_FOLDER_SELECTION_BUTTON (button), folder);

	g_signal_emit (button, signals[SELECTED], 0, folder);
}


/* Initialization.  */

static void
class_init (EMFolderSelectionButtonClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkButtonClass *button_class = GTK_BUTTON_CLASS (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	button_class->clicked = impl_clicked;

	parent_class = g_type_class_peek_parent (class);

	signals[SELECTED] = g_signal_new ("selected",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (EMFolderSelectionButtonClass, selected),
					  NULL, NULL,
					  em_marshal_NONE__POINTER,
					  G_TYPE_NONE, 1,
					  G_TYPE_POINTER);
}

static void
init (EMFolderSelectionButton *folder_selection_button)
{
	EMFolderSelectionButtonPrivate *priv;
	GtkWidget *box;

	priv = g_new0 (EMFolderSelectionButtonPrivate, 1);
	folder_selection_button->priv = priv;

	box = gtk_hbox_new (FALSE, 4);

	priv->icon = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (box), priv->icon, FALSE, TRUE, 0);

	priv->label = gtk_label_new ("");
	gtk_label_set_justify (GTK_LABEL (priv->label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment (GTK_MISC (priv->label), 0.0, 0.0);
	gtk_box_pack_start (GTK_BOX (box), priv->label, TRUE, TRUE, 0);

	gtk_widget_show_all (box);
	gtk_container_add (GTK_CONTAINER (folder_selection_button), box);

	set_contents (folder_selection_button);
}


/* Public API.  */

GtkWidget *
em_folder_selection_button_new (const char *title,
				const char *caption)
{
	EMFolderSelectionButton *button = g_object_new (EM_TYPE_FOLDER_SELECTION_BUTTON, NULL);

	button->priv->title = g_strdup (title);
	button->priv->caption = g_strdup (caption);

	return GTK_WIDGET (button);
}


void
em_folder_selection_button_set_selection  (EMFolderSelectionButton *button,
					   CamelFolder *folder)
{
	g_return_if_fail (EM_IS_FOLDER_SELECTION_BUTTON (button));
	g_return_if_fail (folder == NULL || CAMEL_IS_FOLDER (folder));

	set_selection (button, folder);
}


CamelFolder *
em_folder_selection_button_get_selection  (EMFolderSelectionButton *button)
{
	g_return_val_if_fail (EM_IS_FOLDER_SELECTION_BUTTON (button), NULL);

	return button->priv->selected_folder;
}


E_MAKE_TYPE (em_folder_selection_button, "EMFolderSelectionButton", EMFolderSelectionButton, class_init, init, PARENT_TYPE)
