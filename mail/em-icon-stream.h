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
 *		Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef EM_ICON_STREAM_H
#define EM_ICON_STREAM_H

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EM_ICON_STREAM_TYPE     (em_icon_stream_get_type ())
#define EM_ICON_STREAM(obj)     (CAMEL_CHECK_CAST((obj), EM_ICON_STREAM_TYPE, EMIconStream))
#define EM_ICON_STREAM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), EM_ICON_STREAM_TYPE, EMIconStreamClass))
#define EM_IS_ICON_STREAM(o)    (CAMEL_CHECK_TYPE((o), EM_ICON_STREAM_TYPE))

struct _GtkHTML;
struct _GtkIconStream;

#include "mail/em-sync-stream.h"

typedef struct _EMIconStream {
	EMSyncStream sync;

	unsigned int width, height;
	guint destroy_id;
	struct _GdkPixbufLoader *loader;
	struct _GtkImage *image;
	char *key;

	guint keep:1;
} EMIconStream;

typedef struct {
	EMSyncStreamClass parent_class;
} EMIconStreamClass;

CamelType    em_icon_stream_get_type (void);
CamelStream *em_icon_stream_new(GtkImage *image, const char *key, unsigned int maxwidth, unsigned int maxheight, int keep);

struct _GdkPixbuf *em_icon_stream_get_image(const char *key, unsigned int maxwidth, unsigned int maxheight);
int em_icon_stream_is_resized(const char *key, unsigned int maxwidth, unsigned int maxheight);

void em_icon_stream_clear_cache(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* EM_ICON_STREAM_H */
