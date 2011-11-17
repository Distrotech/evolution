/*
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
 * Authors:
 *		David Trowbridge <trowbrds@cs.colorado.edu>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifndef __EAB_CONFIG_H__
#define __EAB_CONFIG_H__

#include <libedataserver/e-source.h>
#include "e-util/e-config.h"

#define EAB_TYPE_CONFIG (eab_config_get_type ())
#define EAB_CONFIG(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), EAB_TYPE_CONFIG, EABConfig))

G_BEGIN_DECLS

typedef struct _EABConfig EABConfig;
typedef struct _EABConfigClass EABConfigClass;
typedef struct _EABConfigPrivate EABConfigPrivate;

struct _EABConfig {
	EConfig config;

	EABConfigPrivate *priv;
};

struct _EABConfigClass {
	EConfigClass config_class;
};

enum _eab_config_target_t {
	EAB_CONFIG_TARGET_SOURCE,
	EAB_CONFIG_TARGET_PREFS
};

typedef struct _EABConfigTargetSource EABConfigTargetSource;

struct _EABConfigTargetSource {
	EConfigTarget target;

	ESource *source;
};

typedef struct _EABConfigTargetPrefs EABConfigTargetPrefs;

struct _EABConfigTargetPrefs {
	EConfigTarget target;

	/* preferences are global from GSettings */
	GSettings *settings;
};

typedef struct _EConfigItem EABConfigItem;

GType eab_config_get_type (void);
EABConfig *eab_config_new (gint type, const gchar *menuid);

EABConfigTargetSource *eab_config_target_new_source (EABConfig *ecp, ESource *source);
EABConfigTargetPrefs *eab_config_target_new_prefs (EABConfig *ecp, GConfClient *gconf);

G_END_DECLS

#endif
