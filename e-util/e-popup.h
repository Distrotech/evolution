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

#ifndef __E_POPUP_H__
#define __E_POPUP_H__

#include <glib-object.h>
#include "e-util/e-msgport.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

/* This is an abstract popup menu management/merging class.

   To implement your own popup menu system, just create your own
   target types and implement the target free method. */

typedef struct _EPopup EPopup;
typedef struct _EPopupClass EPopupClass;

typedef struct _EPopupItem EPopupItem;
typedef struct _EPopupFactory EPopupFactory; /* anonymous type */
typedef struct _EPopupTarget EPopupTarget;

typedef void (*EPopupActivateFunc)(EPopupItem *item, void *data);
typedef void (*EPopupFactoryFunc)(EPopup *emp, EPopupTarget *target, void *data);

/* Menu item descriptions */
enum _e_popup_t {
	E_POPUP_ITEM = 0,
	E_POPUP_TOGGLE,
	E_POPUP_RADIO,
	E_POPUP_IMAGE,
	E_POPUP_SUBMENU,
	E_POPUP_BAR,
	E_POPUP_TYPE_MASK = 0xffff,
	E_POPUP_ACTIVE = 0x10000,
};

/* FIXME: activate passes back no context data apart from that provided.
   FIXME: It should pass the target at the least.  The menu widget is useless */
struct _EPopupItem {
	enum _e_popup_t type;
	char *path;		/* absolute path! must sort ascii-lexographically into the right spot */
	char *label;
	GCallback activate;	/* EPopupActivateFunc */
	void *activate_data;
	void *image;		/* char* for item type, GtkWidget * for image type */
	guint32 mask;		/* visibility mask */
	guint32 enable;		/* sensitivity mask, unimplemented */
	EPopup *popup;		/* parent, set by epopup */
};

/* base popup target type */
struct _EPopupTarget {
	struct _EPopup *popup;	/* used for virtual methods */

	struct _GtkWidget *widget;	/* used if you need a parent toplevel, if available */
	guint32 type;		/* targe type, for implementors */

	guint32 mask;		/* depends on type, visibility mask */

	/* implementation fields follow */
};

/* The object */
struct _EPopup {
	GObject object;

	struct _EPopupPrivate *priv;

	char *menuid;

	EPopupTarget *target;
};

struct _EPopupClass {
	GObjectClass object_class;

	EDList factories;

	void (*target_free)(EPopup *ep, EPopupTarget *t);
};

GType e_popup_get_type(void);

/* Static class methods */
EPopupFactory *e_popup_class_add_factory(EPopupClass *klass, const char *menuid, EPopupFactoryFunc func, void *data);
void e_popup_class_remove_factory(EPopupClass *klass, EPopupFactory *f);

EPopup *e_popup_construct(EPopup *, const char *menuid);

void e_popup_add_items(EPopup *, GSList *items, GDestroyNotify freefunc);
void e_popup_add_static_items(EPopup *emp, EPopupTarget *target);
/* do not call e_popup_create_menu, it can leak structures if not used right */
struct _GtkMenu *e_popup_create_menu(EPopup *, EPopupTarget *, guint32 hide_mask, guint32 disable_mask);
struct _GtkMenu *e_popup_create_menu_once(EPopup *emp, EPopupTarget *, guint32 hide_mask, guint32 disable_mask);

void *e_popup_target_new(EPopup *, int type, size_t size);
void e_popup_target_free(EPopup *, void *);

/* ********************************************************************** */

/* popup plugin target, they are closely integrated */

/* To implement a basic popup menu plugin, you just need to subclass
   this and initialise the class target type tables */

#include "e-util/e-plugin.h"

typedef struct _EPopupHookItem EPopupHookItem;
typedef struct _EPopupHookMenu EPopupHookMenu;
typedef struct _EPopupHook EPopupHook;
typedef struct _EPopupHookClass EPopupHookClass;

typedef struct _EPopupHookTargetMap EPopupHookTargetMap;
typedef struct _EPopupHookTargetMask EPopupHookTargetMask;

typedef void (*EPopupHookFunc)(struct _EPlugin *plugin, EPopupTarget *target);

struct _EPopupHookItem {
	EPopupItem item;

	struct _EPopupHook *hook; /* parent pointer */
	char *activate;		/* activate handler */
};

struct _EPopupHookMenu {
	struct _EPopupHook *hook; /* parent pointer */
	char *id;		/* target menu id for these menu items */
	int target_type;	/* target type of this menu */
	GSList *items;		/* items to add to menu */
};

/* target map, maps target types to possible values for it */
struct _EPopupHookTargetMap {
	char *type;
	int id;
	/* null terminated array of EPopupHookTargetMask's */
	struct _EPopupHookTargetMask *mask_bits;
};

/* maps a mask name in xml to bit(s) */
struct _EPopupHookTargetMask {
	char *key;
	guint32 mask;
};

struct _EPopupHook {
	EPluginHook hook;

	GSList *menus;
};

struct _EPopupHookClass {
	EPluginHookClass hook_class;

	/* EPopupHookTargetMap by .type */
	GHashTable *target_map;
	/* the popup class these popups's belong to */
	EPopupClass *popup_class;
};

GType e_popup_hook_get_type(void);

/* for implementors */
void e_popup_hook_class_add_target_map(EPopupHookClass *klass, EPopupHookTargetMap *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E_POPUP_H__ */
