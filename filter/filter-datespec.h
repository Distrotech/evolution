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

#ifndef _FILTER_DATESPEC_H
#define _FILTER_DATESPEC_H

#include <time.h>
#include "filter-element.h"

#define FILTER_DATESPEC(obj)	GTK_CHECK_CAST (obj, filter_datespec_get_type (), FilterDatespec)
#define FILTER_DATESPEC_CLASS(klass)	GTK_CHECK_CLASS_CAST (klass, filter_datespec_get_type (), FilterDatespecClass)
#define IS_FILTER_DATESPEC(obj)      GTK_CHECK_TYPE (obj, filter_datespec_get_type ())

typedef struct _FilterDatespec	FilterDatespec;
typedef struct _FilterDatespecClass	FilterDatespecClass;

typedef enum _FilterDatespec_type { FDST_NOW, FDST_SPECIFIED, FDST_X_AGO, FDST_UNKNOWN } FilterDatespec_type;

struct _FilterDatespec {
	FilterElement parent;
	struct _FilterDatespecPrivate *priv;

	FilterDatespec_type type;

	/* either a timespan, an absolute time, or 0
	 * depending on type -- the above mapping to
	 * (X_AGO, SPECIFIED, NOW)
	 */

	time_t value;
};

struct _FilterDatespecClass {
	FilterElementClass parent_class;

	/* virtual methods */

	/* signals */
};

guint		filter_datespec_get_type	(void);
FilterDatespec	*filter_datespec_new	(void);

/* methods */

#endif /* ! _FILTER_DATESPEC_H */

