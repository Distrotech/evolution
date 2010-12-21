/*
 * e-picture-gallery.h
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
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef E_PICTURE_GALLERY_H
#define E_PICTURE_GALLERY_H

#include <gtk/gtk.h>

/* Standard GObject macros */
#define E_TYPE_PICTURE_GALLERY \
	(e_picture_gallery_get_type ())
#define E_PICTURE_GALLERY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_PICTURE_GALLERY, EPictureGallery))
#define E_PICTURE_GALLERY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_PICTURE_GALLERY, EPictureGalleryClass))
#define E_IS_PICTURE_GALLERY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_PICTURE_GALLERY))
#define E_IS_PICTURE_GALLERY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_PICTURE_GALLERY))
#define E_PICTURE_GALLERY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_PICTURE_GALLERY, EPictureGalleryClass))

G_BEGIN_DECLS

typedef struct _EPictureGallery EPictureGallery;
typedef struct _EPictureGalleryClass EPictureGalleryClass;
typedef struct _EPictureGalleryPrivate EPictureGalleryPrivate;

struct _EPictureGallery {
	GtkIconView parent;
	EPictureGalleryPrivate *priv;
};

struct _EPictureGalleryClass {
	GtkIconViewClass parent_class;
};

GType		e_picture_gallery_get_type	(void);
GtkWidget *	e_picture_gallery_new		(const gchar *path);
const gchar *	e_picture_gallery_get_path	(EPictureGallery *gallery);

G_END_DECLS

#endif /* E_PICTURE_GALLERY_H */
