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

#ifndef __E_MENU_H__
#define __E_MENU_H__

#include <glib-object.h>
#include "e-util/e-msgport.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

/* This is an abstract popup menu management/merging class.

   To implement your own popup menu system, just create your own
   target types and implement the target free method. */

typedef struct _EMenu EMenu;
typedef struct _EMenuClass EMenuClass;

typedef struct _EMenuItem EMenuItem;
typedef struct _EMenuFactory EMenuFactory; /* anonymous type */
typedef struct _EMenuTarget EMenuTarget;

typedef void (*EMenuFactoryFunc)(EMenu *emp, void *data);
typedef void (*EMenuActivateFunc)(EMenuItem *, void *data);
typedef void (*EMenuToggleActivateFunc)(EMenuItem *, int state, void *data);

/* Menu item descriptions */
enum _e_menu_t {
	E_MENU_ITEM = 0,
	E_MENU_TOGGLE,
	E_MENU_RADIO,
	E_MENU_IMAGE,
	E_MENU_SUBMENU,
	E_MENU_BAR,
	E_MENU_TYPE_MASK = 0xffff,
	E_MENU_ACTIVE = 0x10000,
};

struct _EMenuItem {
	enum _e_menu_t type;
	char *path;		/* full path?  can we just create it from verb? */
	char *verb;		/* command verb */
	GCallback activate;	/* depends on type, the bonobo activate callback */
	void *activate_data;
	guint32 mask;		/* is visible mask, FIXME: rename to visible */
	guint32 enable;		/* is enable mask */
	struct _EMenu *menu;	/* used by e_menu_add_items & callbacks */
};

/* base popup target type */
struct _EMenuTarget {
	struct _EMenu *menu;	/* used for virtual methods */

	struct _GtkWidget *widget;	/* used if you need a parent toplevel, if available */
	guint32 type;		/* for implementors */

	guint32 mask;		/* enable/visible mask */
	
	/* implementation fields follow */
};

/* The object */
struct _EMenu {
	GObject object;

	struct _EMenuPrivate *priv;

	char *menuid;

	struct _BonoboUIComponent *uic;

	EMenuTarget *target;
};

struct _EMenuClass {
	GObjectClass object_class;

	EDList factories;

	void (*target_free)(EMenu *ep, EMenuTarget *t);
};

GType e_menu_get_type(void);

/* Static class methods */
EMenuFactory *e_menu_class_add_factory(EMenuClass *klass, const char *menuid, EMenuFactoryFunc func, void *data);
void e_menu_class_remove_factory(EMenuClass *klass, EMenuFactory *f);

EMenu *e_menu_construct(EMenu *menu, const char *menuid);

void e_menu_add_ui(EMenu *, const char *appdir, const char *appname, const char *filename);
void e_menu_add_pixmap(EMenu *, const char *cmd, const char *name, int size);

void e_menu_add_items(EMenu *, GSList *items, GDestroyNotify freefunc);

void e_menu_activate(EMenu *, struct _BonoboUIComponent *uic, int act);
void e_menu_update_target(EMenu *, void *);

void *e_menu_target_new(EMenu *, int type, size_t size);
void e_menu_target_free(EMenu *, void *);

/* ********************************************************************** */

/* menu plugin, they are closely integrated */

/* To implement a basic menu plugin, you just need to subclass
   this and initialise the class target type tables */

#include "e-util/e-plugin.h"

typedef struct _EMenuHookItem EMenuHookItem;
typedef struct _EMenuHookMenu EMenuHookMenu;
typedef struct _EMenuHook EMenuHook;
typedef struct _EMenuHookClass EMenuHookClass;

typedef struct _EPluginHookTargetMap EMenuHookTargetMap;
typedef struct _EPluginHookTargetKey EMenuHookTargetMask;

typedef void (*EMenuHookFunc)(struct _EPlugin *plugin, EMenuTarget *target);

struct _EMenuHookItem {
	EMenuItem item;

	struct _EMenuHook *hook; /* parent pointer */
	char *activate;		/* activate handler */
};

struct _EMenuHookPixmap {
	char *command;
	char *name;
	int size;
};

struct _EMenuHookMenu {
	struct _EMenuHook *hook; /* parent pointer */
	char *id;		/* target menu id for these menu items */
	int target_type;	/* target type, not used */
	GSList *items;		/* items to add to menu */
	GSList *uis;		/* ui files */
	GSList *pixmaps;	/* pixmap descriptors */
};

struct _EMenuHook {
	EPluginHook hook;

	GSList *menus;
};

struct _EMenuHookClass {
	EPluginHookClass hook_class;

	/* EMenuHookTargetMap by .type */
	GHashTable *target_map;
	/* the menu class these menu's belong to */
	EMenuClass *menu_class;
};

GType e_menu_hook_get_type(void);

/* for implementors */
void e_menu_hook_class_add_target_map(EMenuHookClass *klass, const EMenuHookTargetMap *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E_MENU_H__ */
