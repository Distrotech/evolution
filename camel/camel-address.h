/*
 *  Copyright (C) 2000 Helix Code Inc.
 *
 *  Authors: Michael Zucchi <notzed@helixcode.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _CAMEL_ADDRESS_H
#define _CAMEL_ADDRESS_H

#include <camel/camel-object.h>

#define CAMEL_ADDRESS(obj)         CAMEL_CHECK_CAST (obj, camel_address_get_type (), CamelAddress)
#define CAMEL_ADDRESS_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_address_get_type (), CamelAddressClass)
#define IS_CAMEL_ADDRESS(obj)      CAMEL_CHECK_TYPE (obj, camel_address_get_type ())

typedef struct _CamelAddressClass CamelAddressClass;

struct _CamelAddress {
	CamelObject parent;

	GPtrArray *addresses;

	struct _CamelAddressPrivate *priv;
};

struct _CamelAddressClass {
	CamelObjectClass parent_class;

	int   (*decode)		(CamelAddress *, const char *raw);
	char *(*encode)		(CamelAddress *);

	void  (*remove)		(CamelAddress *, int index);
};

guint		camel_address_get_type	(void);
CamelAddress   *camel_address_new	(void);

int	        camel_address_decode	(CamelAddress *, const char *);
char	       *camel_address_encode	(CamelAddress *);

void		camel_address_remove	(CamelAddress *, int index);

#endif /* ! _CAMEL_ADDRESS_H */
