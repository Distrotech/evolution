/*
 * anjal-shell-backend.h
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

#ifndef ANJAL_SHELL_BACKEND_H
#define ANJAL_SHELL_BACKEND_H

#include <shell/e-shell-backend.h>

#include <camel/camel-folder.h>
#include <camel/camel-store.h>
#include <e-util/e-signature-list.h>
#include <libedataserver/e-account-list.h>

/* Standard GObject macros */
#define ANJAL_TYPE_SHELL_BACKEND \
	(anjal_shell_backend_get_type ())
#define ANJAL_SHELL_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), ANJAL_TYPE_SHELL_BACKEND, AnjalShellBackend))
#define ANJAL_SHELL_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), ANJAL_TYPE_SHELL_BACKEND, AnjalShellBackendClass))
#define E_IS_MAIL_SHELL_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), ANJAL_TYPE_SHELL_BACKEND))
#define ANJAL_IS_SHELL_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), ANJAL_TYPE_SHELL_BACKEND))
#define ANJAL_SHELL_BACKEND_GET_CLASS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), ANJAL_TYPE_SHELL_BACKEND, AnjalShellBackendClass))

G_BEGIN_DECLS

typedef struct _AnjalShellBackend AnjalShellBackend;
typedef struct _AnjalShellBackendClass AnjalShellBackendClass;
typedef struct _AnjalShellBackendPrivate AnjalShellBackendPrivate;

struct _AnjalShellBackend {
	EShellBackend parent;
	AnjalShellBackendPrivate *priv;
};

struct _AnjalShellBackendClass {
	EShellBackendClass parent_class;
};

GType		anjal_shell_backend_get_type	(void);
void		anjal_shell_backend_register_type
					(GTypeModule *type_module);

/* XXX Find a better place for this function. */
GSList *	e_mail_labels_get_filter_options(void);

G_END_DECLS

#endif /* ANJAL_SHELL_BACKEND_H */
