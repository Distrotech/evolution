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

#ifndef E_MAIL_STRIPSIG_FILTER_H
#define E_MAIL_STRIPSIG_FILTER_H

#include <camel/camel.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_STRIPSIG_FILTER \
	(e_mail_stripsig_filter_get_type ())
#define E_MAIL_STRIPSIG_FILTER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_STRIPSIG_FILTER, EMailStripSigFilter))
#define E_MAIL_STRIPSIG_FILTER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_STRIPSIG_FILTER, EMailStripSigFilterClass))
#define E_MAIL_IS_STRIPSIG_FILTER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_STRIPSIG_FILTER))
#define E_MAIL_IS_STRIPSIG_FILTER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_STRIPSIG_FILTER))
#define E_MAIL_STRIPSIG_FILTER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_STRIPSIG_FILTER, EMailStripSigFilterClass))

G_BEGIN_DECLS

typedef struct _EMailStripSigFilter EMailStripSigFilter;
typedef struct _EMailStripSigFilterClass EMailStripSigFilterClass;

struct _EMailStripSigFilter {
	CamelMimeFilter parent;

	guint32 midline : 1;
	guint32 text_plain_only : 1;
};

struct _EMailStripSigFilterClass {
	CamelMimeFilterClass parent_class;
};

GType		e_mail_stripsig_filter_get_type	(void);
CamelMimeFilter *
		e_mail_stripsig_filter_new	(gboolean text_plain_only);

G_END_DECLS

#endif /* E_MAIL_STRIPSIG_FILTER_H */
