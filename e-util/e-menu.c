/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright 2004 Ximian, Inc. (www.ximian.com)
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "e-menu.h"

#include <e-util/e-icon-factory.h>

#include <libgnome/gnome-i18n.h>
#include <bonobo/bonobo-ui-util.h>

struct _EMenuFactory {
	struct _EMenuFactory *next, *prev;

	char *menuid;
	EMenuFactoryFunc factory;
	void *factory_data;
};

struct _menu_node {
	struct _menu_node *next, *prev;

	GSList *menu;
	GDestroyNotify freefunc;
};

struct _pixmap_node {
	struct _pixmap_node *next;
	struct _pixmap_node *prev;

	char *cmd;
	char *pixmap;
};

struct _ui_node {
	struct _ui_node *next;
	struct _ui_node *prev;

	char *appdir;
	char *appname;
	char *filename;
};


struct _EMenuPrivate {
	EDList menus;
	EDList pixmaps;
	EDList uis;
};

static GObjectClass *em_parent;

static void
em_init(GObject *o)
{
	EMenu *emp = (EMenu *)o;
	struct _EMenuPrivate *p;

	p = emp->priv = g_malloc0(sizeof(struct _EMenuPrivate));

	e_dlist_init(&p->menus);
	e_dlist_init(&p->pixmaps);
	e_dlist_init(&p->uis);
}

static void
em_finalise(GObject *o)
{
	EMenu *em = (EMenu *)o;
	struct _EMenuPrivate *p = em->priv;
	struct _menu_node *mnode;
	struct _ui_node *unode;
	struct _pixmap_node *pnode;

	if (em->target)
		e_menu_target_free(em, em->target);
	g_free(em->menuid);

	while ((mnode = (struct _menu_node *)e_dlist_remhead(&p->menus))) {
		if (mnode->freefunc)
			mnode->freefunc(mnode->menu);
		g_free(mnode);
	}

	while ((pnode = (struct _pixmap_node *)e_dlist_remhead(&p->pixmaps))) {
		g_free(pnode->cmd);
		g_free(pnode->pixmap);
		g_free(pnode);
	}

	while ((unode = (struct _ui_node *)e_dlist_remhead(&p->menus))) {
		g_free(unode->appdir);
		g_free(unode->appname);
		g_free(unode->filename);
		g_free(unode);
	}

	g_free(p);

	((GObjectClass *)em_parent)->finalize(o);
}

static void
em_target_free(EMenu *ep, EMenuTarget *t)
{
	g_free(t);
	/* look funny but t has a reference to us */
	g_object_unref(ep);
}

static void
em_class_init(GObjectClass *klass)
{
	printf("EMenu class init %p '%s'\n", klass, g_type_name(((GObjectClass *)klass)->g_type_class.g_type));

	printf("e menu class init\n");
	klass->finalize = em_finalise;
	((EMenuClass *)klass)->target_free = em_target_free;
}

static void
em_base_init(GObjectClass *klass)
{
	/* each class instance must have its own list, it isn't inherited */
	printf("%p: list init\n", klass);
	e_dlist_init(&((EMenuClass *)klass)->factories);
}

GType
e_menu_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMenuClass),
			(GBaseInitFunc)em_base_init, NULL,
			(GClassInitFunc)em_class_init,
			NULL, NULL,
			sizeof(EMenu), 0,
			(GInstanceInitFunc)em_init
		};
		em_parent = g_type_class_ref(G_TYPE_OBJECT);
		type = g_type_register_static(G_TYPE_OBJECT, "EMenu", &info, 0);
	}

	return type;
}

EMenu *e_menu_construct(EMenu *em, const char *menuid)
{
	struct _EMenuFactory *f;
	EMenuClass *klass;

	printf("constructing menu '%s'\n", menuid);

	klass = (EMenuClass *)G_OBJECT_GET_CLASS(em);

	printf("   class is %p '%s'\n", klass, g_type_name(((GObjectClass *)klass)->g_type_class.g_type));

	em->menuid = g_strdup(menuid);

	/* setup the menu itself based on factories */
	f = (struct _EMenuFactory *)klass->factories.head;
	if (f->next == NULL) {
		printf("%p no factories registered on menu\n", klass);
	}

	while (f->next) {
		if (f->menuid == NULL
		    || !strcmp(f->menuid, em->menuid)) {
			printf("  calling factory\n");
			f->factory(em, f->factory_data);
		}
		f = f->next;
	}

	return em;
}

void
e_menu_add_ui(EMenu *em, const char *appdir, const char *appname, const char *filename)
{
	struct _EMenuPrivate *p = em->priv;
	struct _ui_node *ap = g_malloc0(sizeof(*ap));

	ap->appdir = g_strdup(appdir);
	ap->appname = g_strdup(appname);
	ap->filename = g_strdup(filename);

	e_dlist_addtail(&p->uis, (EDListNode*)ap);
}

void
e_menu_add_pixmap(EMenu *em, const char *cmd, const char *name, int size)
{
	struct _EMenuPrivate *p = em->priv;
	struct _pixmap_node *pn;
	GdkPixbuf *pixbuf;
	char *pixbufstr;

	pixbuf = e_icon_factory_get_icon(name, size);
	if (pixbuf == NULL) {
		g_warning("Unable to load icon '%s'", name);
		return;
	}

	pixbufstr = bonobo_ui_util_pixbuf_to_xml(pixbuf);
	g_object_unref(pixbuf);

	pn = g_malloc0(sizeof(*pn));
	pn->pixmap = pixbufstr;
	pn->cmd = g_strdup(cmd);

	e_dlist_addtail(&p->pixmaps, (EDListNode *)pn);
}

/**
 * e_menu_add_items:
 * @emp: 
 * @items: 
 * @freefunc: 
 * 
 * Add new EMenuItems to the menu's.
 **/
void
e_menu_add_items(EMenu *emp, GSList *items, GDestroyNotify freefunc)
{
	struct _menu_node *node;
	GSList *l;

	for (l=items;l;l=g_slist_next(l))
		((EMenuItem *)l->data)->menu = emp;

	node = g_malloc(sizeof(*node));
	node->menu = items;
	node->freefunc = freefunc;

	e_dlist_addtail(&emp->priv->menus, (EDListNode *)node);
}

static void
em_activate_toggle(BonoboUIComponent *component, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	EMenuItem *item = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	((EMenuToggleActivateFunc)item->activate)(item, state[0] != '0', item->activate_data);
}

static void
em_activate(BonoboUIComponent *uic, void *data, const char *cname)
{
	EMenuItem *item = data;

	((EMenuActivateFunc)item->activate)(item, item->activate_data);
}

void e_menu_activate(EMenu *em, struct _BonoboUIComponent *uic, int act)
{
	struct _EMenuPrivate *p = em->priv;
	struct _menu_node *mw;
	GSList *l;

	if (act) {
		GArray *verbs;
		int i;
		struct _ui_node *ui;

		em->uic = uic;

		for (ui = (struct _ui_node *)p->uis.head;ui->next;ui=ui->next) {
			printf("loading ui file '%s'\n", ui->filename);
			bonobo_ui_util_set_ui(uic, ui->appdir, ui->filename, ui->appname, NULL);
		}

		verbs = g_array_new(TRUE, FALSE, sizeof(BonoboUIVerb));
		for (mw = (struct _menu_node *)p->menus.head;mw->next;mw=mw->next) {
			for (l = mw->menu;l;l=g_slist_next(l)) {
				EMenuItem *item = l->data;
				BonoboUIVerb *verb;

				printf("adding menu verb '%s'\n", item->verb);

				switch (item->type & E_MENU_TYPE_MASK) {
				case E_MENU_ITEM:
					i = verbs->len;
					verbs = g_array_set_size(verbs, i+1);
					verb = &((BonoboUIVerb *)verbs->data)[i];

					verb->cname = item->verb;
					verb->cb = em_activate;
					verb->user_data = item;
					break;
				case E_MENU_TOGGLE:
					bonobo_ui_component_set_prop(uic, item->path, "state", item->type & E_MENU_ACTIVE?"1":"0", NULL);
					bonobo_ui_component_add_listener(uic, item->verb, em_activate_toggle, item);
					break;
				}
			}
		}

		if (verbs->len)
			bonobo_ui_component_add_verb_list(uic, (BonoboUIVerb *)verbs->data);

		g_array_free(verbs, TRUE);
	} else {
		for (mw = (struct _menu_node *)p->menus.head;mw->next;mw=mw->next) {
			for (l = mw->menu;l;l=g_slist_next(l)) {
				EMenuItem *item = l->data;

				bonobo_ui_component_remove_verb(uic, item->verb);
			}
		}

		em->uic = NULL;
	}
}

/**
 * e_menu_update_target:
 * @em: 
 * @tp: Target, after this call the menu owns the target.
 * 
 * Set the target for updating the menu.
 **/
void e_menu_update_target(EMenu *em, void *tp)
{
	struct _EMenuPrivate *p = em->priv;
	EMenuTarget *t = tp;
	guint32 mask = ~0;
	struct _menu_node *mw;
	GSList *l;

	if (em->target)
		e_menu_target_free(em, em->target);

	/* if we unset the target, should we disable/hide all the menu items? */
	em->target = t;
	if (t == NULL)
		return;

	mask = t->mask;

	/* canna do any more capt'n */
	if (em->uic == NULL)
		return;

	for (mw = (struct _menu_node *)p->menus.head;mw->next;mw=mw->next) {
		for (l = mw->menu;l;l=g_slist_next(l)) {
			EMenuItem *item = l->data;
			int state;

			printf("checking item '%s' mask %08x against target %08x\n", item->verb, item->enable, mask);

			state = (item->enable & mask) == 0;
			bonobo_ui_component_set_prop(em->uic, item->path, "sensitive", state?"1":"0", NULL);
			/* visible? */
		}
	}
}

/* ********************************************************************** */

/**
 * e_menu_class_add_factory:
 * @klass:
 * @menuid: 
 * @func: 
 * @data: 
 * 
 * Add a menu factory which will be called to add_items() any
 * extra menu's for a given menu.
 *
 * TODO: Make the menuid a pattern?
 * 
 * Return value: A handle to the factory.
 **/
EMenuFactory *
e_menu_class_add_factory(EMenuClass *klass, const char *menuid, EMenuFactoryFunc func, void *data)
{
	struct _EMenuFactory *f = g_malloc0(sizeof(*f));

	printf("%p adding factory '%s' to class '%s'\n", klass, menuid, g_type_name(((GObjectClass *)klass)->g_type_class.g_type));

	f->menuid = g_strdup(menuid);
	f->factory = func;
	f->factory_data = data;
	e_dlist_addtail(&klass->factories, (EDListNode *)f);

	/* setup the menu itself based on factories */
	{
		struct _EMenuFactory *j;

		j = (struct _EMenuFactory *)klass->factories.head;
		if (j->next == NULL) {
			printf("%p no factories registered on menu???\n", klass);
		}
	}

	return f;
}

/**
 * e_menu_class_remove_factory:
 * @f: 
 * 
 * Remove a popup factory.
 **/
void
e_menu_class_remove_factory(EMenuClass *klass, EMenuFactory *f)
{
	e_dlist_remove((EDListNode *)f);
	g_free(f->menuid);
	g_free(f);
}

/**
 * e_menu_target_new:
 * @klass: 
 * @type: type, up to implementor
 * @size: 
 * 
 * Allocate a new menu target suitable for this class.
 **/
void *e_menu_target_new(EMenu *ep, int type, size_t size)
{
	EMenuTarget *t;

	g_assert(size >= sizeof(EMenuTarget));

	t = g_malloc0(size);
	t->menu = ep;
	g_object_ref(ep);
	t->type = type;

	return t;
}

/**
 * e_menu_target_free:
 * @ep: 
 * @o: 
 * 
 * Free a target 
 **/
void
e_menu_target_free(EMenu *ep, void *o)
{
	EMenuTarget *t = o;

	((EMenuClass *)G_OBJECT_GET_CLASS(ep))->target_free(ep, t);
}

/* ********************************************************************** */

/* Main menu plugin handler */

/* NB: This has significant overlap with EPopupHook */

/*
<e-plugin
  class="com.ximian.mail.plugin.popup:1.0"
  id="com.ximian.mail.plugin.popup.item:1.0"
  type="shlib"
  location="/opt/gnome2/lib/camel/1.0/libcamelimap.so"
  name="imap"
  description="Main menu plugin">
  <hook class="com.ximian.evolution.bonobomenu:1.0">
  <menu id="any" target="select" view="com.ximian.mail">
   <ui file="ui file1"/>
   <ui file="ui file2"/>
   <pixmap command="command" pixmap="stockname" size="menu|button|small_toolbar|large_toolbar|dnd|dialog"/>
   <item
    type="item|toggle"
    verb="verb"
    enable="select_one"
    visible="select_one"
    activate="doactivate"/>
   </menu>
  </hook>
  </extension>

*/

static char *
get_xml_prop(xmlNodePtr node, const char *id)
{
	char *p = xmlGetProp(node, id);
	char *out = NULL;

	if (p) {
		out = g_strdup(p);
		xmlFree(p);
	}

	return out;
}

static void *emph_parent_class;
#define emph ((EMenuHook *)eph)

/* must have 1:1 correspondence with e-popup types in order */
static const char * emph_item_types[] = { "item", "toggle", "radio", "image", "submenu", "bar", NULL };
/* 1:1 with e-icon-factory sizes */
static const char * emph_pixmap_sizes[] = { "menu", "button", "small_toolbar", "large_toolbar", "dnd", "dialog", NULL };

static guint32
emph_mask(xmlNodePtr root, struct _EMenuHookTargetMap *map, const char *prop)
{
	char *val, *p, *start, c;
	guint32 mask = 0;

	val = xmlGetProp(root, prop);
	if (val == NULL)
		return 0;

	printf(" mask '%s' = ", val);

	p = val;
	do {
		start = p;
		while (*p && *p != ',')
			p++;
		c = *p;
		*p = 0;
		if (start != p) {
			int i;

			for (i=0;map->mask_bits[i].key;i++) {
				if (!strcmp(map->mask_bits[i].key, start)) {
					mask |= map->mask_bits[i].mask;
					break;
				}
			}
		}
		*p++ = c;
	} while (c);

	xmlFree(val);

	printf("%08x\n", mask);

	return mask;
}

static int
emph_index(xmlNodePtr root, const char **vals, const char *prop)
{
	int i = 0;
	char *val;

	val = xmlGetProp(root, prop);
	if (val == NULL) {
		printf(" can't find prop '%s'\n", prop);
		return -1;
	}

	printf("looking up index of '%s'", val);

	while (vals[i]) {
		if (!strcmp(vals[i], val)) {
			printf(" = %d\n", i);
			xmlFree(val);
			return i;
		}
		i++;
	}

	printf(" not found\n");

	xmlFree(val);
	return -1;
}

static void
emph_menu_activate(void *widget, void *data)
{
	struct _EMenuHookItem *item = data;

	printf("invoking plugin hook '%s' %p\n", item->activate, item->item.menu->target);

	e_plugin_invoke(item->hook->hook.plugin, item->activate, item->item.menu->target);
}

static void
emph_menu_factory(EMenu *emp, void *data)
{
	struct _EMenuHookMenu *menu = data;
	GSList *l;

	printf("menu factory, adding %d items\n", g_slist_length(menu->items));

	/* FIXME: we need to copy the items */
	if (menu->items)
		e_menu_add_items(emp, menu->items, NULL);

	for (l = menu->uis;l;l=g_slist_next(l))
		e_menu_add_ui(emp, "/tmp", "evolution-mail", (char *)l->data);

	/* FIXME: pixmaps? */
}

static void
emph_free_item(struct _EMenuHookItem *item)
{
	g_free(item->item.path);
	g_free(item->item.verb);
	g_free(item->activate);
	g_free(item);
}

static void
emph_free_pixmap(struct _EMenuHookPixmap *pixmap)
{
	g_free(pixmap->command);
	g_free(pixmap->name);
	g_free(pixmap);
}

static void
emph_free_menu(struct _EMenuHookMenu *menu)
{
	g_slist_foreach(menu->items, (GFunc)emph_free_item, NULL);
	g_slist_free(menu->items);

	g_free(menu->id);
	g_free(menu);
}

static struct _EMenuHookItem *
emph_construct_item(EPluginHook *eph, EMenuHookMenu *menu, xmlNodePtr root, struct _EMenuHookTargetMap *map)
{
	struct _EMenuHookItem *item;

	printf("  loading menu item\n");
	item = g_malloc0(sizeof(*item));
	if ((item->item.type = emph_index(root, emph_item_types, "type")) == -1
	    || item->item.type == E_MENU_IMAGE)
		goto error;
	item->item.path = get_xml_prop(root, "path");
	item->item.verb = get_xml_prop(root, "verb");
	item->item.mask = emph_mask(root, map, "mask");
	item->item.enable = emph_mask(root, map, "enable");
	item->activate = get_xml_prop(root, "activate");

	item->item.activate = G_CALLBACK(emph_menu_activate);
	item->item.activate_data = item;
	item->hook = emph;

	printf("   path=%s\n", item->item.path);
	printf("   verb=%s\n", item->item.verb);

	return item;
error:
	printf("error!\n");
	emph_free_item(item);
	return NULL;
}

static struct _EMenuHookPixmap *
emph_construct_pixmap(EPluginHook *eph, EMenuHookMenu *menu, xmlNodePtr root)
{
	struct _EMenuHookPixmap *pixmap;

	printf("  loading menu pixmap\n");
	pixmap = g_malloc0(sizeof(*pixmap));
	pixmap->command = get_xml_prop(root, "command");
	pixmap->name = get_xml_prop(root, "pixmap");
	pixmap->size = emph_index(root, emph_pixmap_sizes, "size");

	if (pixmap->command == NULL || pixmap->name == NULL || pixmap->size == -1)
		goto error;

	return pixmap;
error:
	printf("error!\n");
	emph_free_pixmap(pixmap);
	return NULL;
}

static struct _EMenuHookMenu *
emph_construct_menu(EPluginHook *eph, xmlNodePtr root)
{
	struct _EMenuHookMenu *menu;
	xmlNodePtr node;
	struct _EMenuHookTargetMap *map;
	EMenuHookClass *klass = (EMenuHookClass *)G_OBJECT_GET_CLASS(eph);
	char *tmp;

	printf(" loading menu\n");
	menu = g_malloc0(sizeof(*menu));

	tmp = xmlGetProp(root, "target");
	if (tmp == NULL)
		goto error;
	map = g_hash_table_lookup(klass->target_map, tmp);
	xmlFree(tmp);
	if (map == NULL)
		goto error;

	menu->target_type = map->id;
	menu->id = get_xml_prop(root, "id");
	node = root->children;
	while (node) {
		if (0 == strcmp(node->name, "item")) {
			struct _EMenuHookItem *item;

			item = emph_construct_item(eph, menu, node, map);
			if (item)
				menu->items = g_slist_append(menu->items, item);
		} else if (0 == strcmp(node->name, "ui")) {
			tmp = xmlGetProp(node, "file");
			if (tmp) {
				menu->uis = g_slist_append(menu->uis, g_strdup(tmp));
				g_free(tmp);
			}
		} else if (0 == strcmp(node->name, "pixmap")) {
			struct _EMenuHookPixmap *pixmap;

			pixmap = emph_construct_pixmap(eph, menu, node);
			if (pixmap)
				menu->pixmaps = g_slist_append(menu->pixmaps, pixmap);
		}
		node = node->next;
	}

	return menu;
error:
	printf("error loading menu hook\n");
	emph_free_menu(menu);
	return NULL;
}

static int
emph_construct(EPluginHook *eph, EPlugin *ep, xmlNodePtr root)
{
	xmlNodePtr node;
	EMenuClass *klass;

	printf("loading menu hook\n");

	if (((EPluginHookClass *)emph_parent_class)->construct(eph, ep, root) == -1)
		return -1;

	klass = ((EMenuHookClass *)G_OBJECT_GET_CLASS(eph))->menu_class;

	node = root->children;
	while (node) {
		if (strcmp(node->name, "menu") == 0) {
			struct _EMenuHookMenu *menu;

			menu = emph_construct_menu(eph, node);
			if (menu) {
				printf(" plugin adding factory %p\n", klass);
				e_menu_class_add_factory(klass, menu->id, emph_menu_factory, menu);
				emph->menus = g_slist_append(emph->menus, menu);
			}
		}

		node = node->next;
	}

	eph->plugin = ep;

	return 0;
}

static void
emph_finalise(GObject *o)
{
	EPluginHook *eph = (EPluginHook *)o;

	g_slist_foreach(emph->menus, (GFunc)emph_free_menu, NULL);
	g_slist_free(emph->menus);

	((GObjectClass *)emph_parent_class)->finalize(o);
}

static void
emph_class_init(EPluginHookClass *klass)
{
	printf("EMenuHook class init %p '%s'\n", klass, g_type_name(((GObjectClass *)klass)->g_type_class.g_type));

	((GObjectClass *)klass)->finalize = emph_finalise;
	klass->construct = emph_construct;

	/* this is actually an abstract implementation but list it anyway */
	klass->id = "com.ximian.evolution.bonobomenu:1.0";

	((EMenuHookClass *)klass)->target_map = g_hash_table_new(g_str_hash, g_str_equal);
	((EMenuHookClass *)klass)->menu_class = g_type_class_ref(e_menu_get_type());
}

GType
e_menu_hook_get_type(void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof(EMenuHookClass), NULL, NULL, (GClassInitFunc) emph_class_init, NULL, NULL,
			sizeof(EMenuHook), 0, (GInstanceInitFunc) NULL,
		};

		emph_parent_class = g_type_class_ref(e_plugin_hook_get_type());
		type = g_type_register_static(e_plugin_hook_get_type(), "EMenuHook", &info, 0);
	}
	
	return type;
}

void e_menu_hook_class_add_target_map(EMenuHookClass *klass, EMenuHookTargetMap *map)
{
	g_hash_table_insert(klass->target_map, map->type, map);
}
