/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifndef __EM_FOLDER_TREE_H__
#define __EM_FOLDER_TREE_H__

#include <gtk/gtkvbox.h>
#include <camel/camel-store.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EM_TYPE_FOLDER_TREE            (em_folder_tree_get_type ())
#define EM_FOLDER_TREE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EM_TYPE_FOLDER_TREE, EMFolderTree))
#define EM_FOLDER_TREE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EM_TYPE_FOLDER_TREE, EMFolderTreeClass))
#define EM_IS_FOLDER_TREE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EM_TYPE_FOLDER_TREE))
#define EM_IS_FOLDER_TREE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EM_TYPE_FOLDER_TREE))
#define EM_FOLDER_TREE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), EM_TYPE_FOLDER_TREE, EMFolderTreeClass))

typedef struct _EMFolderTree EMFolderTree;
typedef struct _EMFolderTreeClass EMFolderTreeClass;

struct _EMFolderTree {
	GtkVBox parent_object;
	
	struct _EMFolderTreePrivate *priv;
};

struct _EMFolderTreeClass {
	GtkVBoxClass parent_class;
	
	/* signals */
	void (* folder_selected) (EMFolderTree *emft, const char *path, const char *uri);
};


GType em_folder_tree_get_type (void);

GtkWidget *em_folder_tree_new (void);

void em_folder_tree_add_store (EMFolderTree *emft, CamelStore *store, const char *display_name);
void em_folder_tree_remove_store (EMFolderTree *emft, CamelStore *store);

void em_folder_tree_set_selected (EMFolderTree *emft, const char *uri);
const char *em_folder_tree_get_selected (EMFolderTree *emft);
const char *em_folder_tree_get_selected_path (EMFolderTree *emft);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EM_FOLDER_TREE_H__ */
