/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Authors: Michel Zucchi <notzed@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
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

#ifndef __E_CONFIG_H__
#define __E_CONFIG_H__

#include <glib-object.h>
#include "e-util/e-msgport.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

/* This is a config window management/merging class. */

typedef struct _EConfig EConfig;
typedef struct _EConfigClass EConfigClass;

typedef struct _EConfigItem EConfigItem;
typedef struct _EConfigFactory EConfigFactory;
typedef struct _EConfigTarget EConfigTarget;

typedef void (*EConfigFactoryFunc)(EConfig *ec, EConfigTarget *t, void *data);

typedef void (*EConfigItemsFunc)(EConfig *ec, GSList *items, void *data);

typedef struct _GtkWidget * (*EConfigItemFactoryFunc)(EConfig *ec, EConfigItem *, struct _GtkWidget *parent, void *data);

/* ok so this is all a bit bogussy
   we need to map to glade stuff instead */

/* Add types?
   if no factory, setup appropriate container ?
   if factory, then assume that returns the right sort of object?
   what about pages ?
*/

enum _e_config_t {
	E_CONFIG_BOOK,
	E_CONFIG_PAGE,
	E_CONFIG_SECTION,
	E_CONFIG_ITEM,
};

/* all items are in a flat structure
   it is up to the caller to setup the items so once sorted, a page preceeds a section preceeds any items. */
struct _EConfigItem {
	enum _e_config_t type;
	char *path;		/* absolute path, must sort asci-lexographically into the right spot */
	char *label;
	EConfigItemFactoryFunc factory;
	void *user_data;
	EConfig *config;	/* set to parent always */
};

/* base config target type, think more of a 'context' */
struct _EConfigTarget {
	struct _EConfig *config;
	struct _GtkWidget *widget;	/* used if you need a parent toplevel, if available */

	guint32 type;

	/* implementation fields follow, depends on window type */
};

/* The object */
struct _EConfig {
	GObject object;

	struct _EConfigPrivate *priv;

	char *id;

	EConfigTarget *target;

	struct _GtkWidget *widget; /* the generated widget */
};

struct _EConfigClass {
	GObjectClass object_class;

	EDList factories;

	void (*target_free)(EConfig *ep, EConfigTarget *t);
};

GType e_config_get_type(void);

/* Static class methods */
EConfigFactory *e_config_class_add_factory(EConfigClass *klass, const char *menuid, EConfigFactoryFunc func, void *data);
void e_config_class_remove_factory(EConfigClass *klass, EConfigFactory *f);

EConfig *e_config_construct(EConfig *, const char *menuid);
EConfig *e_config_new(const char *menuid);

void e_config_add_items(EConfig *, GSList *items, EConfigItemsFunc commitfunc, EConfigItemsFunc abortfunc, EConfigItemsFunc freefunc, void *data);

struct _GtkWidget *e_config_create_widget(EConfig *, EConfigTarget *);

void e_config_abort(EConfig *);
void e_config_commit(EConfig *);

void *e_config_target_new(EConfig *, int type, size_t size);
void e_config_target_free(EConfig *, void *);

/* ********************************************************************** */

/* config plugin target, they are closely integrated */

/* To implement a basic config plugin, you just need to subclass
   this and initialise the class target type tables */

#include "e-util/e-plugin.h"

typedef struct _EConfigHookItem EConfigHookItem;
typedef struct _EConfigHookGroup EConfigHookGroup;
typedef struct _EConfigHook EConfigHook;
typedef struct _EConfigHookClass EConfigHookClass;

typedef struct _EPluginHookTargetMap EConfigHookTargetMap;
typedef struct _EPluginHookTargetKey EConfigHookTargetMask;

typedef struct _EConfigHookItemFactoryData EConfigHookItemFactoryData;

typedef void (*EConfigHookFunc)(struct _EPlugin *plugin, EConfigTarget *target);
typedef void (*EConfigHookItemFactoryFunc)(struct _EPlugin *plugin, EConfigHookItemFactoryData *data);

struct _EConfigHookItemFactoryData {
	EConfigItem *item;
	EConfigTarget *target;
	struct _GtkWidget *parent;
};

struct _EConfigHookItem {
	EConfigItem item;

	struct _EConfigHook *hook; /* parent pointer */
	char *factory;		/* factory handler */
};

struct _EConfigHookGroup {
	struct _EConfigHook *hook; /* parent pointer */
	char *id;		/* target menu id for these menu items */
	int target_type;	/* target type of this menu */
	GSList *items;		/* items to add to menu */
	char *commit;		/* commit handler, if set */
	char *abort;		/* abort handler, if set */
};

struct _EConfigHook {
	EPluginHook hook;

	GSList *menus;
};

struct _EConfigHookClass {
	EPluginHookClass hook_class;

	/* EConfigHookTargetMap by .type */
	GHashTable *target_map;
	/* the config class these configs's belong to */
	EConfigClass *config_class;
};

GType e_config_hook_get_type(void);

/* for implementors */
void e_config_hook_class_add_target_map(EConfigHookClass *klass, const EConfigHookTargetMap *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E_CONFIG_H__ */
