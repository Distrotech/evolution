/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Authors: Michel Zucchi <notzed@ximian.com>
 *
 *  Copyright 2004 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __EM_CONFIG_H__
#define __EM_CONFIG_H__

#include <glib-object.h>

#include "e-util/e-config.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

typedef struct _EMConfig EMConfig;
typedef struct _EMConfigClass EMConfigClass;

/* Current target description */
/* Types of popup tagets */
enum _em_config_target_t {
	EM_CONFIG_TARGET_FOLDER,
};

typedef struct _EMConfigTargetFolder EMConfigTargetFolder;

struct _EMConfigTargetFolder {
	EConfigTarget target;

	struct _CamelFolder *folder;
	char *uri;
};

typedef struct _EConfigItem EMConfigItem;

/* The object */
struct _EMConfig {
	EConfig config;

	struct _EMConfigPrivate *priv;
};

struct _EMConfigClass {
	EConfigClass config_class;
};

GType em_config_get_type(void);

EMConfig *em_config_new(const char *menuid);

EMConfigTargetFolder *em_config_target_new_folder(EMConfig *emp, struct _CamelFolder *folder, const char *uri);

/* ********************************************************************** */

typedef struct _EMConfigHook EMConfigHook;
typedef struct _EMConfigHookClass EMConfigHookClass;

struct _EMConfigHook {
	EConfigHook hook;
};

struct _EMConfigHookClass {
	EConfigHookClass hook_class;
};

GType em_config_hook_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EM_CONFIG_H__ */
