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

#include <gtk/gtkmenu.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkradiomenuitem.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimage.h>

#include "e-popup.h"

#include <e-util/e-icon-factory.h>

#include <libgnome/gnome-i18n.h>

struct _EPopupFactory {
	struct _EPopupFactory *next, *prev;

	char *menuid;
	EPopupFactoryFunc factory;
	void *factory_data;
};

struct _menu_node {
	struct _menu_node *next, *prev;

	GSList *menu;
	GDestroyNotify freefunc;
};

struct _EPopupPrivate {
	EDList menus;
};

static GObjectClass *ep_parent;

static void
ep_init(GObject *o)
{
	EPopup *emp = (EPopup *)o;
	struct _EPopupPrivate *p;

	p = emp->priv = g_malloc0(sizeof(struct _EPopupPrivate));

	e_dlist_init(&p->menus);
}

static void
ep_finalise(GObject *o)
{
	EPopup *emp = (EPopup *)o;
	struct _EPopupPrivate *p = emp->priv;
	struct _menu_node *mnode, *nnode;

	g_free(emp->menuid);

	mnode = (struct _menu_node *)p->menus.head;
	nnode = mnode->next;
	while (nnode) {
		if (mnode->freefunc)
			mnode->freefunc(mnode->menu);

		g_free(mnode);
		mnode = nnode;
		nnode = nnode->next;
	}

	g_free(p);

	((GObjectClass *)ep_parent)->finalize(o);
}

static void
ep_target_free(EPopup *ep, EPopupTarget *t)
{
	g_free(t);
	/* look funny but t has a reference to us */
	g_object_unref(ep);
}

static void
ep_class_init(GObjectClass *klass)
{
	klass->finalize = ep_finalise;
	((EPopupClass *)klass)->target_free = ep_target_free;
}

GType
e_popup_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EPopupClass),
			NULL, NULL,
			(GClassInitFunc)ep_class_init,
			NULL, NULL,
			sizeof(EPopup), 0,
			(GInstanceInitFunc)ep_init
		};
		ep_parent = g_type_class_ref(G_TYPE_OBJECT);
		type = g_type_register_static(G_TYPE_OBJECT, "EPopup", &info, 0);
	}

	return type;
}

EPopup *e_popup_construct(EPopup *ep, const char *menuid)
{
	ep->menuid = g_strdup(menuid);

	return ep;
}

/**
 * e_popup_add_items:
 * @emp: 
 * @items: 
 * @freefunc: 
 * 
 * Add new EPopupItems to the menu's.  Any with the same path
 * will override previously defined menu items, at menu building
 * time.
 **/
void
e_popup_add_items(EPopup *emp, GSList *items, GDestroyNotify freefunc)
{
	struct _menu_node *node;

	node = g_malloc(sizeof(*node));
	node->menu = items;
	node->freefunc = freefunc;
	e_dlist_addtail(&emp->priv->menus, (EDListNode *)node);
}

/**
 * e_popup_add_static_items:
 * @emp: 
 * @target: Target of this menu.
 * 
 * Will load up any matching menu items from an installed
 * popup factory.  If the menuid of @emp is NULL, then this
 * has no effect.
 *
 **/
void
e_popup_add_static_items(EPopup *emp, EPopupTarget *target)
{
	struct _EPopupFactory *f;
	EPopupClass *klass = (EPopupClass *)G_OBJECT_GET_CLASS(emp);

	if (emp->menuid == NULL || target == NULL)
		return;

	/* setup the menu itself */
	f = (struct _EPopupFactory *)klass->factories.head;
	while (f->next) {
		if (f->menuid == NULL
		    || !strcmp(f->menuid, emp->menuid)) {
			f->factory(emp, target, f->factory_data);
		}
		f = f->next;
	}
}

static int
ep_cmp(const void *ap, const void *bp)
{
	struct _EPopupItem *a = *((void **)ap);
	struct _EPopupItem *b = *((void **)bp);

	return strcmp(a->path, b->path);
}

/**
 * e_popup_create:
 * @menuitems: 
 * @hide_mask: used to hide menu items, not sure of it's utility,
 * since you could just 'not add them' in the first place.  Saves
 * copying logic anyway.
 * @disable_mask: used to disable menu items.
 * 
 * TEMPORARY code to create a menu from a list of items.
 * 
 * The menu items are merged based on their path element, and
 * built into a menu tree.
 *
 * Return value: 
 **/
GtkMenu *
e_popup_create_menu(EPopup *emp, guint32 hide_mask, guint32 disable_mask)
{
	struct _EPopupPrivate *p = emp->priv;
	struct _menu_node *mnode, *nnode;
	GPtrArray *items = g_ptr_array_new();
	GSList *l;
	GString *ppath = g_string_new("");
	GtkMenu *topmenu;
	GHashTable *menu_hash = g_hash_table_new(g_str_hash, g_str_equal),
		*group_hash = g_hash_table_new(g_str_hash, g_str_equal);
	/*char *domain = NULL;*/
	int i;

	/* FIXME: need to override old ones with new names */
	mnode = (struct _menu_node *)p->menus.head;
	nnode = mnode->next;
	while (nnode) {
		for (l=mnode->menu; l; l = l->next)
			g_ptr_array_add(items, l->data);
		mnode = nnode;
		nnode = nnode->next;
	}

	qsort(items->pdata, items->len, sizeof(items->pdata[0]), ep_cmp);

	topmenu = (GtkMenu *)gtk_menu_new();
	for (i=0;i<items->len;i++) {
		GtkWidget *label;
		struct _EPopupItem *item = items->pdata[i];
		GtkMenu *thismenu;
		GtkMenuItem *menuitem;
		char *tmp;

		/* for bar's, the mask is exclusive or */
		if (item->mask) {
			if ((item->type & E_POPUP_TYPE_MASK) == E_POPUP_BAR) {
				if ((item->mask & hide_mask) == item->mask)
					continue;
			} else if (item->mask & hide_mask)
				continue;
		}

		g_string_truncate(ppath, 0);
		tmp = strrchr(item->path, '/');
		if (tmp) {
			g_string_append_len(ppath, item->path, tmp-item->path);
			thismenu = g_hash_table_lookup(menu_hash, ppath->str);
			g_assert(thismenu != NULL);
		} else {
			thismenu = topmenu;
		}

		switch (item->type & E_POPUP_TYPE_MASK) {
		case E_POPUP_ITEM:
			if (item->image) {
				GdkPixbuf *pixbuf;
				GtkWidget *image;
				
				pixbuf = e_icon_factory_get_icon ((char *)item->image, E_ICON_SIZE_MENU);
				image = gtk_image_new_from_pixbuf (pixbuf);
				g_object_unref (pixbuf);

				gtk_widget_show(image);
				menuitem = (GtkMenuItem *)gtk_image_menu_item_new();
				gtk_image_menu_item_set_image((GtkImageMenuItem *)menuitem, image);
			} else {
				menuitem = (GtkMenuItem *)gtk_menu_item_new();
			}
			break;
		case E_POPUP_TOGGLE:
			menuitem = (GtkMenuItem *)gtk_check_menu_item_new();
			gtk_check_menu_item_set_active((GtkCheckMenuItem *)menuitem, item->type & E_POPUP_ACTIVE);
			break;
		case E_POPUP_RADIO:
			menuitem = (GtkMenuItem *)gtk_radio_menu_item_new(g_hash_table_lookup(group_hash, ppath->str));
			g_hash_table_insert(group_hash, ppath->str, gtk_radio_menu_item_get_group((GtkRadioMenuItem *)menuitem));
			gtk_check_menu_item_set_active((GtkCheckMenuItem *)menuitem, item->type & E_POPUP_ACTIVE);
			break;
		case E_POPUP_IMAGE:
			menuitem = (GtkMenuItem *)gtk_image_menu_item_new();
			gtk_image_menu_item_set_image((GtkImageMenuItem *)menuitem, item->image);
			break;
		case E_POPUP_SUBMENU: {
			GtkMenu *submenu = (GtkMenu *)gtk_menu_new();

			g_hash_table_insert(menu_hash, item->path, submenu);
			menuitem = (GtkMenuItem *)gtk_menu_item_new();
			gtk_menu_item_set_submenu(menuitem, (GtkWidget *)submenu);
			break; }
		case E_POPUP_BAR:
			/* TODO: double-bar, end-bar stuff? */
			menuitem = (GtkMenuItem *)gtk_separator_menu_item_new();
			break;
		default:
			continue;
		}

		if (item->label) {
			label = gtk_label_new_with_mnemonic(_(item->label));
			gtk_misc_set_alignment((GtkMisc *)label, 0.0, 0.5);
			gtk_widget_show(label);
			gtk_container_add((GtkContainer *)menuitem, label);
		}

		if (item->activate)
			g_signal_connect(menuitem, "activate", item->activate, item->activate_data);

		gtk_menu_shell_append((GtkMenuShell *)thismenu, (GtkWidget *)menuitem);

		if (item->mask & disable_mask)
			gtk_widget_set_sensitive((GtkWidget *)menuitem, FALSE);

		gtk_widget_show((GtkWidget *)menuitem);
	}

	g_string_free(ppath, TRUE);
	g_ptr_array_free(items, TRUE);
	g_hash_table_destroy(menu_hash);
	g_hash_table_destroy(group_hash);

	return topmenu;
}

static void
ep_popup_done(GtkWidget *w, EPopup *emp)
{
	gtk_widget_destroy(w);
	g_object_unref(emp);
}

static void
ep_target_destroy(EPopupTarget *t)
{
	e_popup_target_free(t->popup, t);
}

/**
 * e_popup_create_menu_once:
 * @emp: EPopup, once the menu is shown, this cannot be
 * considered a valid pointer.
 * @target: If set, the target of the selection.  Static menu
 * items will be added.  The target will be freed once complete.
 * @hide_mask: 
 * @disable_mask: 
 * 
 * Like popup_create_menu, but automatically sets up the menu
 * so that it is destroyed once a selection takes place, and
 * the EPopup is unreffed.
 * 
 * Return value: A menu, to popup.
 **/
GtkMenu *
e_popup_create_menu_once(EPopup *emp, EPopupTarget *target, guint32 hide_mask, guint32 disable_mask)
{
	GtkMenu *menu;

	if (target)
		e_popup_add_static_items(emp, target);

	menu = e_popup_create_menu(emp, hide_mask, disable_mask);

	if (target)
		g_signal_connect_swapped(menu, "selection_done", G_CALLBACK(ep_target_destroy), target);
	g_signal_connect(menu, "selection_done", G_CALLBACK(ep_popup_done), emp);

	return menu;
}

/* ********************************************************************** */

/**
 * e_popup_class_add_factory:
 * @klass:
 * @menuid: 
 * @func: 
 * @data: 
 * 
 * Add a popup factory which will be called to add_items() any
 * extra menu's if wants to do the current PopupTarget.
 *
 * TODO: Make the menuid a pattern?
 * 
 * Return value: A handle to the factory.
 **/
EPopupFactory *
e_popup_class_add_factory(EPopupClass *klass, const char *menuid, EPopupFactoryFunc func, void *data)
{
	struct _EPopupFactory *f = g_malloc0(sizeof(*f));

	f->menuid = g_strdup(menuid);
	f->factory = func;
	f->factory_data = data;
	e_dlist_addtail(&klass->factories, (EDListNode *)f);

	return f;
}

/**
 * e_popup_class_remove_factory:
 * @f: 
 * 
 * Remove a popup factory.
 **/
void
e_popup_class_remove_factory(EPopupClass *klass, EPopupFactory *f)
{
	e_dlist_remove((EDListNode *)f);
	g_free(f->menuid);
	g_free(f);
}

/**
 * e_popup_target_new:
 * @klass: 
 * @type: type, up to implementor
 * @size: 
 * 
 * Allocate a new popup target suitable for this class.
 **/
void *e_popup_target_new(EPopup *ep, int type, size_t size)
{
	EPopupTarget *t;

	g_assert(size >= sizeof(EPopupTarget));

	t = g_malloc0(size);
	t->popup = ep;
	g_object_ref(ep);
	t->type = type;

	return t;
}

/**
 * e_popup_target_free:
 * @ep: 
 * @o: 
 * 
 * Free a target 
 **/
void
e_popup_target_free(EPopup *ep, void *o)
{
	EPopupTarget *t = o;

	((EPopupClass *)G_OBJECT_GET_CLASS(ep))->target_free(ep, t);
}

/* ********************************************************************** */

/* Popup menu plugin handler */

/*
<e-plugin
  class="com.ximian.mail.plugin.popup:1.0"
  id="com.ximian.mail.plugin.popup.item:1.0"
  type="shlib"
  location="/opt/gnome2/lib/camel/1.0/libcamelimap.so"
  name="imap"
  description="IMAP4 and IMAP4v1 mail store">
  <hook class="com.ximian.mail.popupMenu:1.0"
        handler="HandlePopup">
  <menu id="any" target="select">
   <item
    type="item|toggle|radio|image|submenu|bar"
    active
    path="foo/bar"
    label="label"
    icon="foo"
    mask="select_one"
    activate="ep_view_emacs"/>
  </menu>
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
static void *emph_popup_class;
#define emph ((EPopupHook *)eph)

/* must have 1:1 correspondence with e-popup types in order */
static const char * emph_item_types[] = { "item", "toggle", "radio", "image", "submenu", "bar", NULL };

static guint32
emph_mask(xmlNodePtr root, struct _EPopupHookTargetMap *map, const char *prop)
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
emph_popup_activate(void *widget, void *data)
{
	struct _EPopupHookItem *item = data;

	e_plugin_invoke(item->hook->hook.plugin, item->activate, item->target);
}

static void
emph_popup_factory(EPopup *emp, EPopupTarget *target, void *data)
{
	struct _EPopupHookMenu *menu = data;
	GSList *l, *menus = NULL;

	printf("popup factory called %s mask %08x\n", menu->id?menu->id:"all menus", target->mask);

	if (target->type != menu->target_type)
		return;

	l = menu->items;
	while (l) {
		struct _EPopupHookItem *item = l->data;

		item->target = target;
		printf("  adding menyu item '%s' %08x\n", item->item.label, item->item.mask);
		menus = g_slist_prepend(menus, item);
		l = l->next;
	}

	if (menus)
		e_popup_add_items(emp, menus, (GDestroyNotify)g_slist_free);
}

static void
emph_free_item(struct _EPopupHookItem *item)
{
	g_free(item->item.path);
	g_free(item->item.label);
	g_free(item->item.image);
	g_free(item->activate);
	g_free(item);
}

static void
emph_free_menu(struct _EPopupHookMenu *menu)
{
	g_slist_foreach(menu->items, (GFunc)emph_free_item, NULL);
	g_slist_free(menu->items);

	g_free(menu->id);
	g_free(menu);
}

static struct _EPopupHookItem *
emph_construct_item(EPluginHook *eph, EPopupHookMenu *menu, xmlNodePtr root, struct _EPopupHookTargetMap *map)
{
	struct _EPopupHookItem *item;

	printf("  loading menu item\n");
	item = g_malloc0(sizeof(*item));
	if ((item->item.type = emph_index(root, emph_item_types, "type")) == -1
	    || item->item.type == E_POPUP_IMAGE)
		goto error;
	item->item.path = get_xml_prop(root, "path");
	item->item.label = get_xml_prop(root, "label");
	item->item.image = get_xml_prop(root, "icon");
	item->item.mask = emph_mask(root, map, "mask");
	item->activate = get_xml_prop(root, "activate");

	item->item.activate = G_CALLBACK(emph_popup_activate);
	item->item.activate_data = item;
	item->hook = emph;

	printf("   path=%s\n", item->item.path);
	printf("   label=%s\n", item->item.label);

	return item;
error:
	printf("error!\n");
	emph_free_item(item);
	return NULL;
}

static struct _EPopupHookMenu *
emph_construct_menu(EPluginHook *eph, xmlNodePtr root)
{
	struct _EPopupHookMenu *menu;
	xmlNodePtr node;
	struct _EPopupHookTargetMap *map;
	EPopupHookClass *klass = (EPopupHookClass *)G_OBJECT_GET_CLASS(eph);
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
			struct _EPopupHookItem *item;

			item = emph_construct_item(eph, menu, node, map);
			if (item)
				menu->items = g_slist_append(menu->items, item);
		}
		node = node->next;
	}

	return menu;
error:
	emph_free_menu(menu);
	return NULL;
}

static int
emph_construct(EPluginHook *eph, EPlugin *ep, xmlNodePtr root)
{
	xmlNodePtr node;

	printf("loading popup hook\n");

	if (((EPluginHookClass *)emph_parent_class)->construct(eph, ep, root) == -1)
		return -1;

	node = root->children;
	while (node) {
		if (strcmp(node->name, "menu") == 0) {
			struct _EPopupHookMenu *menu;

			menu = emph_construct_menu(eph, node);
			if (menu) {
				e_popup_class_add_factory(emph_popup_class, menu->id, emph_popup_factory, menu);
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
	((GObjectClass *)klass)->finalize = emph_finalise;
	klass->construct = emph_construct;

	/* this is actually an abstract implementation but list it anyway */
	klass->id = "com.ximian.evolution.popup:1.0";

	((EPopupHookClass *)klass)->target_map = g_hash_table_new(g_str_hash, g_str_equal);
}

GType
e_popup_hook_get_type(void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof(EPopupHookClass), NULL, NULL, (GClassInitFunc) emph_class_init, NULL, NULL,
			sizeof(EPopupHook), 0, (GInstanceInitFunc) NULL,
		};

		emph_parent_class = g_type_class_ref(e_plugin_hook_get_type());
		emph_popup_class = g_type_class_ref(e_popup_get_type());
		type = g_type_register_static(e_plugin_hook_get_type(), "EPopupHook", &info, 0);
	}
	
	return type;
}

void e_popup_hook_class_add_target_map(EPopupHookClass *klass, EPopupHookTargetMap *map)
{
	g_hash_table_insert(klass->target_map, map->type, map);
}
