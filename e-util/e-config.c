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

#include <gtk/gtknotebook.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>

#include "e-config.h"

#include <e-util/e-icon-factory.h>

#include <libgnome/gnome-i18n.h>

struct _EConfigFactory {
	struct _EConfigFactory *next, *prev;

	char *id;
	EConfigFactoryFunc factory;
	void *factory_data;
};

struct _menu_node {
	struct _menu_node *next, *prev;

	GSList *menu;
	EConfigItemsFunc free;
	EConfigItemsFunc abort;
	EConfigItemsFunc commit;
	void *data;
};

struct _widget_node {
	struct _widget_node *next, *prev;

	struct _menu_node *context;
	EConfigItem *item;
	struct _GtkWidget *widget; /* widget created by the factory, if any */
};

struct _EConfigPrivate {
	EDList menus;
	EDList widgets;
};

static GObjectClass *ep_parent;

static void
ep_init(GObject *o)
{
	EConfig *emp = (EConfig *)o;
	struct _EConfigPrivate *p;

	p = emp->priv = g_malloc0(sizeof(struct _EConfigPrivate));

	e_dlist_init(&p->menus);
	e_dlist_init(&p->widgets);
}

static void
ep_finalise(GObject *o)
{
	EConfig *emp = (EConfig *)o;
	struct _EConfigPrivate *p = emp->priv;
	struct _menu_node *mnode;
	struct _widget_node *wn;

	g_free(emp->id);

	while ((mnode = (struct _menu_node *)e_dlist_remhead(&p->menus))) {
		if (mnode->free)
			mnode->free(emp, mnode->menu, mnode->data);

		g_free(mnode);
	}

	while ( (wn = (struct _widget_node *)e_dlist_remhead(&p->widgets)) ) {
		g_free(wn);
	}

	g_free(p);

	((GObjectClass *)ep_parent)->finalize(o);
}

static void
ep_target_free(EConfig *ep, EConfigTarget *t)
{
	g_free(t);
	g_object_unref(ep);
}

static void
ep_class_init(GObjectClass *klass)
{
	printf("EConfig class init %p '%s'\n", klass, g_type_name(((GObjectClass *)klass)->g_type_class.g_type));

	klass->finalize = ep_finalise;
	((EConfigClass *)klass)->target_free = ep_target_free;
}

static void
ep_base_init(GObjectClass *klass)
{
	e_dlist_init(&((EConfigClass *)klass)->factories);
}

GType
e_config_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EConfigClass),
			(GBaseInitFunc)ep_base_init, NULL,
			(GClassInitFunc)ep_class_init, NULL, NULL,
			sizeof(EConfig), 0,
			(GInstanceInitFunc)ep_init
		};
		ep_parent = g_type_class_ref(G_TYPE_OBJECT);
		type = g_type_register_static(G_TYPE_OBJECT, "EConfig", &info, 0);
	}

	return type;
}

EConfig *e_config_construct(EConfig *ep, const char *id)
{
	ep->id = g_strdup(id);

	return ep;
}

EConfig *e_config_new(const char *id)
{
	EConfig *ec = g_object_new(e_config_get_type(), NULL);

	return e_config_construct(ec, id);
}

/**
 * e_config_add_items:
 * @emp: 
 * @items: 
 * @freefunc: 
 * 
 * Add new EConfigItems to the menu's.  Any with the same path
 * will override previously defined menu items, at menu building
 * time.
 **/
void
e_config_add_items(EConfig *ec, GSList *items, EConfigItemsFunc commitfunc, EConfigItemsFunc abortfunc, EConfigItemsFunc freefunc, void *data)
{
	struct _menu_node *node;
	GSList *l;

	for (l=items;l;l=g_slist_next(l))
		((EConfigItem *)l->data)->config = ec;

	node = g_malloc(sizeof(*node));
	node->menu = items;
	node->commit = commitfunc;
	node->abort = abortfunc;
	node->free = freefunc;
	node->data = data;
	e_dlist_addtail(&ec->priv->menus, (EDListNode *)node);
}

static void
e_config_add_static_items(EConfig *ec, EConfigTarget *t)
{
	struct _EConfigFactory *f;
	EConfigClass *klass = (EConfigClass *)G_OBJECT_GET_CLASS(ec);

	f = (struct _EConfigFactory *)klass->factories.head;
	while (f->next) {
		if (f->id == NULL
		    || !strcmp(f->id, ec->id)) {
			f->factory(ec, t, f->factory_data);
		}
		f = f->next;
	}
}

static int
ep_cmp(const void *ap, const void *bp)
{
	struct _widget_node *a = *((void **)ap);
	struct _widget_node *b = *((void **)bp);

	return strcmp(a->item->path, b->item->path);
}

GtkWidget *
e_config_create_widget(EConfig *emp, EConfigTarget *target)
{
	struct _EConfigPrivate *p = emp->priv;
	struct _menu_node *mnode;
	GPtrArray *items = g_ptr_array_new();
	GSList *l;
	/*char *domain = NULL;*/
	GtkWidget *book = NULL, *page = NULL, *section = NULL;
	int pageno = 0, sectionno = 0, itemno = 0;
	int i;

	emp->target = target;
	e_config_add_static_items(emp, target);

	/* FIXME: need to override old ones with new names */
	for (mnode = (struct _menu_node *)p->menus.head;mnode->next;mnode=mnode->next)
		for (l=mnode->menu; l; l = l->next) {
			struct _EConfigItem *item = l->data;
			struct _widget_node *wn = g_malloc0(sizeof(*wn));

			wn->item = item;
			wn->context = mnode;
			g_ptr_array_add(items, wn);
		}

	qsort(items->pdata, items->len, sizeof(items->pdata[0]), ep_cmp);

	for (i=0;i<items->len;i++) {
		struct _widget_node *wn = items->pdata[i];
		struct _EConfigItem *item = wn->item;
		GtkWidget *w;

		/* Not sure we need to keep this stuff */
		e_dlist_addtail(&p->widgets, (EDListNode *)wn);

		switch (item->type) {
		case E_CONFIG_BOOK:
			g_assert(book == NULL);
			if (item->factory)
				book = item->factory(emp, item, NULL, wn->context->data);
			else {
				book = gtk_notebook_new();
				gtk_widget_show(book);
			}
			wn->widget = book;
			break;
		case E_CONFIG_PAGE:
			if (item->factory) {
				page = item->factory(emp, item, book, wn->context->data);
			} else {
				w = gtk_label_new(item->label);
				gtk_widget_show(w);
				page = gtk_vbox_new(FALSE, 12);
				gtk_widget_show(page);
				gtk_notebook_insert_page((GtkNotebook *)book, page, w, pageno);
			}
			pageno++;
			sectionno = 0;
			section = NULL;
			wn->widget = page;
			break;
		case E_CONFIG_SECTION:
			if (item->factory) {
				section = item->factory(emp, item, page, wn->context->data);
			} else {
				GtkWidget *frame = gtk_vbox_new(FALSE, 6);

				gtk_widget_show(frame);
				if (item->label) {
					char *txt;
					GtkWidget *hbox;

					w = gtk_label_new("");
					gtk_widget_show(w);
					txt = g_strdup_printf("<span weight=\"bold\">%s</span>", item->label);
					gtk_label_set_markup((GtkLabel *)w, txt);
					g_free(txt);
					gtk_misc_set_alignment((GtkMisc *)w, 0.0, 0.5);
					gtk_box_pack_start((GtkBox *)frame, w, FALSE, FALSE, 0);

					hbox = gtk_hbox_new(FALSE, 12);
					gtk_widget_show(hbox);
					w = gtk_label_new("");
					gtk_widget_show(w);
					gtk_box_pack_start((GtkBox *)hbox, w, FALSE, FALSE, 0);
					gtk_box_pack_start((GtkBox *)frame, hbox, FALSE, FALSE, 0);

					section = gtk_vbox_new(FALSE, 6);
					gtk_box_pack_start((GtkBox *)hbox, section, FALSE, FALSE, 0);
					gtk_widget_show(section);
				} else {
					section = frame;
				}

				gtk_box_pack_start((GtkBox *)page, frame, FALSE, FALSE, 0);
			}
			sectionno++;
			itemno = 0;
			wn->widget = section;
			break;
		case E_CONFIG_ITEM:
			if (item->factory) {
				wn->widget = item->factory(emp, item, section, wn->context->data);
			}
			itemno++;
			break;
		}
	}

	g_ptr_array_free(items, TRUE);

	emp->widget = (GtkWidget *)book;

	if (book) {
		/* make this depend on flags?? */
		if (gtk_notebook_get_n_pages((GtkNotebook *)book) == 1) {
			gtk_notebook_set_show_tabs((GtkNotebook *)book, FALSE);
			gtk_notebook_set_show_border((GtkNotebook *)book, FALSE);
		}
		g_object_set_data_full((GObject *)book, "e-config", emp, g_object_unref);
	}

	return (GtkWidget *)book;
}

void e_config_abort(EConfig *ec)
{
	struct _EConfigPrivate *p = ec->priv;
	struct _menu_node *mnode;

	for (mnode = (struct _menu_node *)p->menus.head;mnode->next;mnode=mnode->next)
		if (mnode->abort)
			mnode->abort(ec, mnode->menu, mnode->data);
}

void e_config_commit(EConfig *ec)
{
	struct _EConfigPrivate *p = ec->priv;
	struct _menu_node *mnode;

	for (mnode = (struct _menu_node *)p->menus.head;mnode->next;mnode=mnode->next)
		if (mnode->commit)
			mnode->commit(ec, mnode->menu, mnode->data);
}

/* ********************************************************************** */

/**
 * e_config_class_add_factory:
 * @klass:
 * @id: 
 * @func: 
 * @data: 
 * 
 * Add a config factory which will be called to add_items() any
 * extra menu's if wants to do the current ConfigTarget.
 *
 * TODO: Make the id a pattern?
 * 
 * Return value: A handle to the factory.
 **/
EConfigFactory *
e_config_class_add_factory(EConfigClass *klass, const char *id, EConfigFactoryFunc func, void *data)
{
	struct _EConfigFactory *f = g_malloc0(sizeof(*f));

	f->id = g_strdup(id);
	f->factory = func;
	f->factory_data = data;
	e_dlist_addtail(&klass->factories, (EDListNode *)f);

	return f;
}

/**
 * e_config_class_remove_factory:
 * @f: 
 * 
 * Remove a config factory.
 **/
void
e_config_class_remove_factory(EConfigClass *klass, EConfigFactory *f)
{
	e_dlist_remove((EDListNode *)f);
	g_free(f->id);
	g_free(f);
}

/**
 * e_config_target_new:
 * @klass: 
 * @type: type, up to implementor
 * @size: 
 * 
 * Allocate a new config target suitable for this class.
 **/
void *e_config_target_new(EConfig *ep, int type, size_t size)
{
	EConfigTarget *t;

	g_assert(size >= sizeof(EConfigTarget));

	t = g_malloc0(size);
	t->config = ep;
	g_object_ref(ep);
	t->type = type;

	return t;
}

/**
 * e_config_target_free:
 * @ep: 
 * @o: 
 * 
 * Free a target 
 **/
void
e_config_target_free(EConfig *ep, void *o)
{
	EConfigTarget *t = o;

	((EConfigClass *)G_OBJECT_GET_CLASS(ep))->target_free(ep, t);
}

/* ********************************************************************** */

/* Config menu plugin handler */

/*
<e-plugin
  class="com.ximian.mail.plugin.config:1.0"
  id="com.ximian.mail.plugin.config.item:1.0"
  type="shlib"
  location="/opt/gnome2/lib/camel/1.0/libcamelimap.so"
  name="imap"
  description="IMAP4 and IMAP4v1 mail store">
  <hook class="com.ximian.mail.configMenu:1.0"
        handler="HandleConfig">
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

static void *emph_parent_class;
#define emph ((EConfigHook *)eph)

/* must have 1:1 correspondence with e-config types in order */
static const EPluginHookTargetKey ech_item_types[] = {
	{ "page", E_CONFIG_PAGE },
	{ "section", E_CONFIG_SECTION },
	{ "item", E_CONFIG_ITEM },
	{ 0 },
};

static void
ech_config_factory(EConfig *emp, EConfigTarget *target, void *data)
{
	struct _EConfigHookMenu *menu = data;

	printf("config factory called %s\n", menu->id?menu->id:"all menus");

	if (target->type != menu->target_type)
		return;

/*	if (menu->items)
	e_config_add_items(emp, menu->items, NULL);*/
}

static void
emph_free_item(struct _EConfigHookItem *item)
{
	g_free(item->item.path);
	g_free(item->item.label);
	g_free(item->factory);
	g_free(item);
}

static void
emph_free_menu(struct _EConfigHookMenu *menu)
{
	g_slist_foreach(menu->items, (GFunc)emph_free_item, NULL);
	g_slist_free(menu->items);

	g_free(menu->id);
	g_free(menu);
}

static struct _GtkWidget *
ech_config_widget_factory(EConfig *ec, EConfigItem *item, GtkWidget *parent, void *data)
{
	EConfigHookItem *hitem = (EConfigHookItem *)item;

	/* FIXME: marshal args into a struct for plugin callback */

	return (struct _GtkWidget *)e_plugin_invoke(hitem->hook->hook.plugin, hitem->factory, ec->target);
}

static struct _EConfigHookItem *
emph_construct_item(EPluginHook *eph, EConfigHookMenu *menu, xmlNodePtr root, EConfigHookTargetMap *map)
{
	struct _EConfigHookItem *item;

	printf("  loading menu item\n");
	item = g_malloc0(sizeof(*item));
	if ((item->item.type = e_plugin_hook_id(root, ech_item_types, "type")) == -1)
		goto error;
	item->item.path = e_plugin_xml_prop(root, "path");
	item->item.label = e_plugin_xml_prop(root, "label");
	item->factory = e_plugin_xml_prop(root, "factory");

	item->item.factory = ech_config_widget_factory;
	/* item->item.user_data = 0; */ /* FIXME: ?? */
	item->hook = emph;

	printf("   path=%s\n", item->item.path);
	printf("   label=%s\n", item->item.label);

	return item;
error:
	printf("error!\n");
	emph_free_item(item);
	return NULL;
}

static struct _EConfigHookMenu *
emph_construct_menu(EPluginHook *eph, xmlNodePtr root)
{
	struct _EConfigHookMenu *menu;
	xmlNodePtr node;
	EConfigHookTargetMap *map;
	EConfigHookClass *klass = (EConfigHookClass *)G_OBJECT_GET_CLASS(eph);
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
	menu->id = e_plugin_xml_prop(root, "id");
	node = root->children;
	while (node) {
		if (0 == strcmp(node->name, "item")) {
			struct _EConfigHookItem *item;

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
	EConfigClass *klass;

	printf("loading config hook\n");

	if (((EPluginHookClass *)emph_parent_class)->construct(eph, ep, root) == -1)
		return -1;

	klass = ((EConfigHookClass *)G_OBJECT_GET_CLASS(eph))->config_class;

	node = root->children;
	while (node) {
		if (strcmp(node->name, "menu") == 0) {
			struct _EConfigHookMenu *menu;

			menu = emph_construct_menu(eph, node);
			if (menu) {
				e_config_class_add_factory(klass, menu->id, ech_config_factory, menu);
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
	klass->id = "com.ximian.evolution.config:1.0";

	printf("EConfigHook: init class %p '%s'\n", klass, g_type_name(((GObjectClass *)klass)->g_type_class.g_type));

	((EConfigHookClass *)klass)->target_map = g_hash_table_new(g_str_hash, g_str_equal);
	((EConfigHookClass *)klass)->config_class = g_type_class_ref(e_config_get_type());
}

GType
e_config_hook_get_type(void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof(EConfigHookClass), NULL, NULL, (GClassInitFunc) emph_class_init, NULL, NULL,
			sizeof(EConfigHook), 0, (GInstanceInitFunc) NULL,
		};

		emph_parent_class = g_type_class_ref(e_plugin_hook_get_type());
		type = g_type_register_static(e_plugin_hook_get_type(), "EConfigHook", &info, 0);
	}
	
	return type;
}

void e_config_hook_class_add_target_map(EConfigHookClass *klass, const EConfigHookTargetMap *map)
{
	g_hash_table_insert(klass->target_map, (void *)map->type, (void *)map);
}
