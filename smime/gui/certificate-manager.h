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
 *		Chris Toshok <toshok@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef _CERTIFICATE_MANAGER_H_
#define _CERTIFICATE_MANAGER_H

#include <gtk/gtk.h>
#include <shell/e-shell.h>
#include <widgets/misc/e-preferences-window.h>

/* Standard GObject macros */
#define E_TYPE_CERT_MANAGER_CONFIG \
	(e_cert_manager_config_get_type ())
#define E_CERT_MANAGER_CONFIG(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CERT_MANAGER_CONFIG, ECertManagerConfig))
#define E_CERT_MANAGER_CONFIG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CERT_MANAGER_CONFIG, ECertManagerConfigClass))
#define E_IS_CERT_MANAGER_CONFIG(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CERT_MANAGER_CONFIG))
#define E_IS_CERT_MANAGER_CONFIG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CERT_MANAGER_CONFIG))
#define E_CERT_MANAGER_CONFIG_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CERT_MANAGER_CONFIG, ECertManagerConfigClass))

typedef struct _ECertManagerConfig ECertManagerConfig;
typedef struct _ECertManagerConfigPrivate ECertManagerConfigPrivate;
typedef struct _ECertManagerConfigClass ECertManagerConfigClass;

struct _ECertManagerConfig {
	GtkBox parent;
	ECertManagerConfigPrivate *priv;
};

struct _ECertManagerConfigClass {
	GtkBoxClass parent_class;
};

G_BEGIN_DECLS

GType	  e_cert_manager_config_get_type (void) G_GNUC_CONST;

GtkWidget *e_cert_manager_config_new (EPreferencesWindow *window);


G_END_DECLS

#endif /* _CERTIFICATE_MANAGER_H_ */
