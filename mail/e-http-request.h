/*
 * e-http-request.h
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
 */

#ifndef E_HTTP_REQUEST_H
#define E_HTTP_REQUEST_H

#define LIBSOUP_USE_UNSTABLE_REQUEST_API

#include <libsoup/soup.h>
#include <libsoup/soup-request.h>

/* Standard GObject macros */
#define E_TYPE_HTTP_REQUEST \
	(e_http_request_get_type ())
#define E_HTTP_REQUEST(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_HTTP_REQUEST, EHTTPRequest))
#define E_HTTP_REQUEST_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_HTTP_REQUEST, EHTTPRequestClass))
#define E_IS_HTTP_REQUEST(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_HTTP_REQUEST))
#define E_IS_HTTP_REQUEST_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_HTTP_REQUEST))
#define E_HTTP_REQUEST_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_HTTP_REQUEST, EHTTPRequestClass))

G_BEGIN_DECLS

typedef struct _EHTTPRequest EHTTPRequest;
typedef struct _EHTTPRequestClass EHTTPRequestClass;
typedef struct _EHTTPRequestPrivate EHTTPRequestPrivate;

struct _EHTTPRequest {
	SoupRequest parent;
	EHTTPRequestPrivate *priv;
};

struct _EHTTPRequestClass {
	SoupRequestClass parent;
};

GType		e_http_request_get_type		(void) G_GNUC_CONST;

G_END_DECLS

#endif /* E_HTTP_REQUEST_H */
