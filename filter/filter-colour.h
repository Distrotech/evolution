/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
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
 */

#ifndef _FILTER_COLOUR_H
#define _FILTER_COLOUR_H

#include "filter-element.h"

#define FILTER_COLOUR(obj)	GTK_CHECK_CAST (obj, filter_colour_get_type (), FilterColour)
#define FILTER_COLOUR_CLASS(klass)	GTK_CHECK_CLASS_CAST (klass, filter_colour_get_type (), FilterColourClass)
#define IS_FILTER_COLOUR(obj)      GTK_CHECK_TYPE (obj, filter_colour_get_type ())

typedef struct _FilterColour	FilterColour;
typedef struct _FilterColourClass	FilterColourClass;

struct _FilterColour {
	FilterElement parent;
	struct _FilterColourPrivate *priv;

	guint16 r,g,b,a;
};

struct _FilterColourClass {
	FilterElementClass parent_class;

	/* virtual methods */

	/* signals */
};

guint		filter_colour_get_type	(void);
FilterColour	*filter_colour_new	(void);

/* methods */

#endif /* ! _FILTER_COLOUR_H */

