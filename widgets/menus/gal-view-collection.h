/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * gal-view-collection.h
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _GAL_VIEW_SET_H_
#define _GAL_VIEW_SET_H_

#include <gtk/gtkobject.h>
#include <gal/menus/gal-view-factory.h>
#include <libgnome/gnome-defs.h>

BEGIN_GNOME_DECLS


#define GAL_VIEW_COLLECTION_TYPE        (gal_view_collection_get_type ())
#define GAL_VIEW_COLLECTION(o)          (GTK_CHECK_CAST ((o), GAL_VIEW_COLLECTION_TYPE, GalViewCollection))
#define GAL_VIEW_COLLECTION_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GAL_VIEW_COLLECTION_TYPE, GalViewCollectionClass))
#define GAL_IS_VIEW_COLLECTION(o)       (GTK_CHECK_TYPE ((o), GAL_VIEW_COLLECTION_TYPE))
#define GAL_IS_VIEW_COLLECTION_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GAL_VIEW_COLLECTION_TYPE))

typedef struct GalViewCollectionItem GalViewCollectionItem;

typedef struct {
	GtkObject base;

	GalViewCollectionItem **view_data;
	int view_count;

	GList *factory_list;

	GalViewCollectionItem **removed_view_data;
	int removed_view_count;

	guint loaded : 1;
	guint default_view_built_in : 1;

	char *system_dir;
	char *local_dir;

	char *default_view;

	char *title;
} GalViewCollection;

typedef struct {
	GtkObjectClass parent_class;

	/*
	 * Signals
	 */
	void (*display_view) (GalViewCollection *collection,
			      GalView    *view);
	void (*changed)      (GalViewCollection *collection);
} GalViewCollectionClass;

struct GalViewCollectionItem {
	GalView *view;
	char *id;
	guint changed : 1;
	guint ever_changed : 1;
	guint built_in : 1;
	char *filename;
	char *title;
	char *type;
	GalViewCollection *collection;
	guint view_changed_id;
};

/* Standard functions */
GtkType                gal_view_collection_get_type                 (void);
GalViewCollection     *gal_view_collection_new                      (void);

void                   gal_view_collection_set_title                (GalViewCollection *collection,
								     const char        *title);
/* Set up the view collection.  Call these two functions before ever doing load or save and never call them again. */
void                   gal_view_collection_set_storage_directories  (GalViewCollection *collection,
								     const char        *system_dir,
								     const char        *local_dir);
void                   gal_view_collection_add_factory              (GalViewCollection *collection,
								     GalViewFactory    *factory);

/* Send the display view signal.  This function is deprecated. */
void                   gal_view_collection_display_view             (GalViewCollection *collection,
								     GalView           *view);


/* Query the view collection. */
gint                   gal_view_collection_get_count                (GalViewCollection *collection);
GalView               *gal_view_collection_get_view                 (GalViewCollection *collection,
								     int                n);
GalViewCollectionItem *gal_view_collection_get_view_item            (GalViewCollection *collection,
								     int                n);
int                    gal_view_collection_get_view_index_by_id     (GalViewCollection *collection,
								     const char        *view_id);
char                  *gal_view_collection_get_view_id_by_index     (GalViewCollection *collection,
								     int                n);

/* Manipulate the view collection */
void                   gal_view_collection_append                   (GalViewCollection *collection,
								     GalView           *view);
void                   gal_view_collection_delete_view              (GalViewCollection *collection,
								     int                i);
void                   gal_view_collection_copy_view                (GalViewCollection *collection,
								     int                i);
/* Call set_storage_directories and add factories for anything that
 * might be found there before doing either of these. */
void                   gal_view_collection_load                     (GalViewCollection *collection);
void                   gal_view_collection_save                     (GalViewCollection *collection);
gboolean               gal_view_collection_loaded                   (GalViewCollection *collection);

/* Use factory list to load a GalView file. */
GalView               *gal_view_collection_load_view_from_file      (GalViewCollection *collection,
								     const char        *type,
								     const char        *filename);

/* Returns id of the new view.  These functions are used for
   GalViewInstanceSaveAsDialog. */
const char            *gal_view_collection_append_with_title        (GalViewCollection *collection,
								     const char        *title,
								     GalView           *view);
const char            *gal_view_collection_set_nth_view             (GalViewCollection *collection,
								     int                i,
								     GalView           *view);

const char            *gal_view_collection_get_default_view         (GalViewCollection *collection);
void                   gal_view_collection_set_default_view         (GalViewCollection *collection,
								     const char        *id);


END_GNOME_DECLS


#endif /* _GAL_VIEW_COLLECTION_H_ */
