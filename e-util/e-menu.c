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

struct _item_node {
	struct _item_node *next;

	EMenuItem *item;
	struct _menu_node *menu;
};

struct _menu_node {
	struct _menu_node *next, *prev;

	EMenu *parent;

	GSList *menu;
	EMenuItemsFunc freefunc;
	void *data;

	struct _item_node *items;
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
		struct _item_node *inode;

		if (mnode->freefunc)
			mnode->freefunc(em, mnode->menu, mnode->data);

		inode = mnode->items;
		while (inode) {
			struct _item_node *nnode = inode->next;

			g_free(inode);
			inode = nnode;
		}

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

/**
 * e_menu_get_type:
 * 
 * Standard GObject type function.  Used to subclass this type only.
 * 
 * Return value: The EMenu object type.
 **/
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

/**
 * e_menu_construct:
 * @em: An instantiated but uninitislied EPopup.
 * @menuid: The unique identifier for this menu.
 * 
 * Construct the base menu instance based on the parameters.
 * 
 * Return value: Returns @em.
 **/
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

/**
 * e_menu_add_ui:
 * @em: An initialised EMenu.
 * @appdir: Application directory passed eo bonobo_ui_util_set_ui().
 * @appname: Application name passed to bonobo_ui_util_set_ui().
 * @filename: Filename of BonoboUI XML file, passed to
 * bonobo_ui_util_set_ui().
 * 
 * Add a BonoboUI file to the list which will be loaded when the
 * parent control is activated.  @appdir, @appname and @filename will
 * be passed unaltered to bonobo_ui_util_set_ui().
 **/
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

/**
 * e_menu_add_pixmap:
 * @em: An initialised EMenu.
 * @cmd: The command to which this pixmap should be associated.
 * @name: The name of the pixmap, an icon theme name or stock icon
 * name or full pathname of an icon file.
 * @size: The e-icon-factory icon size.
 * 
 * Adds a pixmap descriptor to the menu @em.  The icon @name will be
 * converted to an xml string and set on the UIComponent when the view
 * is activated.  This is used to allow stock pixmap names to be used
 * in the UI files.
 **/
void
e_menu_add_pixmap(EMenu *em, const char *cmd, const char *name, int size)
{
	struct _EMenuPrivate *p = em->priv;
	struct _pixmap_node *pn;
	GdkPixbuf *pixbuf;

	pixbuf = e_icon_factory_get_icon(name, size);
	if (pixbuf == NULL) {
		g_warning("Unable to load icon '%s'", name);
		return;
	}

	pn = g_malloc0(sizeof(*pn));
	pn->pixmap = bonobo_ui_util_pixbuf_to_xml(pixbuf);
	pn->cmd = g_strdup(cmd);

	g_object_unref(pixbuf);

	e_dlist_addtail(&p->pixmaps, (EDListNode *)pn);
}

/**
 * e_menu_add_items:
 * @emp: An initialised EMenu.
 * @items: A list of EMenuItems or derived structures defining a group
 * of menu items for this menu.
 * @freefunc: If supplied, called when the menu items are no longer needed.
 * @data: user-data passed to @freefunc and activate callbacks.
 * 
 * Add new EMenuItems to the menu's.  This may be called any number of
 * times before the menu is first activated to hook onto any of the
 * menu items defined for that view.
 **/
void
e_menu_add_items(EMenu *emp, GSList *items, EMenuItemsFunc freefunc, void *data)
{
	struct _menu_node *node;
	GSList *l;

	node = g_malloc(sizeof(*node));
	node->parent = emp;
	node->menu = items;
	node->freefunc = freefunc;
	node->data = data;

	for (l=items;l;l=g_slist_next(l)) {
		struct _item_node *inode = g_malloc0(sizeof(*inode));
		EMenuItem *item = l->data;

		inode->item = item;
		inode->menu = node;
		inode->next = node->items;
		node->items = inode;
	}

	e_dlist_addtail(&emp->priv->menus, (EDListNode *)node);
}

static void
em_activate_toggle(BonoboUIComponent *component, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	struct _item_node *inode = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	((EMenuToggleActivateFunc)inode->item->activate)(inode->menu->parent, inode->item, state[0] != '0', inode->menu->data);
}

static void
em_activate(BonoboUIComponent *uic, void *data, const char *cname)
{
	struct _item_node *inode = data;

	((EMenuActivateFunc)inode->item->activate)(inode->menu->parent, inode->item, inode->menu->data);
}

/**
 * e_menu_activate:
 * @em: An initialised EMenu.
 * @uic: The BonoboUI component for this views menu's.
 * @act: If %TRUE, then the control is being activated.
 * 
 * This is called by the owner of the component, control, or view to
 * pass on the activate or deactivate control signals.  If the view is
 * being activated then the callbacks and menu items are setup,
 * otherwise they are removed.
 *
 * This should always be called in the strict sequence of activate, then
 * deactivate, repeated any number of times.
 **/
void e_menu_activate(EMenu *em, struct _BonoboUIComponent *uic, int act)
{
	struct _EMenuPrivate *p = em->priv;
	struct _menu_node *mw;
	GSList *l;

	if (act) {
		GArray *verbs;
		int i;
		struct _ui_node *ui;
		struct _pixmap_node *pn;

		em->uic = uic;

		for (ui = (struct _ui_node *)p->uis.head;ui->next;ui=ui->next) {
			printf("loading ui file '%s'\n", ui->filename);
			bonobo_ui_util_set_ui(uic, ui->appdir, ui->filename, ui->appname, NULL);
		}

		verbs = g_array_new(TRUE, FALSE, sizeof(BonoboUIVerb));
		for (mw = (struct _menu_node *)p->menus.head;mw->next;mw=mw->next) {
			struct _item_node *inode;

			for (inode = mw->items; inode; inode=inode->next) {
				EMenuItem *item = inode->item;
				BonoboUIVerb *verb;

				printf("adding menu verb '%s'\n", item->verb);

				switch (item->type & E_MENU_TYPE_MASK) {
				case E_MENU_ITEM:
					i = verbs->len;
					verbs = g_array_set_size(verbs, i+1);
					verb = &((BonoboUIVerb *)verbs->data)[i];

					verb->cname = item->verb;
					verb->cb = em_activate;
					verb->user_data = inode;
					break;
				case E_MENU_TOGGLE:
					bonobo_ui_component_set_prop(uic, item->path, "state", item->type & E_MENU_ACTIVE?"1":"0", NULL);
					bonobo_ui_component_add_listener(uic, item->verb, em_activate_toggle, inode);
					break;
				}
			}
		}

		if (verbs->len)
			bonobo_ui_component_add_verb_list(uic, (BonoboUIVerb *)verbs->data);

		g_array_free(verbs, TRUE);

		/* TODO: maybe we only need to do this once? */
		for (pn = (struct _pixmap_node *)p->pixmaps.head;pn->next;pn=pn->next)
			bonobo_ui_component_set_prop(uic, pn->cmd, "pixmap", pn->pixmap, NULL);
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
 * @em: An initialised EMenu.
 * @tp: Target, after this call the menu owns the target.
 * 
 * Change the target for the menu.  Once the target is changed, the
 * sensitivity state of the menu items managed by @em is re-evaluated
 * and the physical menu's updated to reflect it.
 *
 * This is used by the owner of the menu and view to update the menu
 * system based on user input or changed system state.
 **/
void e_menu_update_target(EMenu *em, void *tp)
{
	struct _EMenuPrivate *p = em->priv;
	EMenuTarget *t = tp;
	guint32 mask = ~0;
	struct _menu_node *mw;
	GSList *l;

	if (em->target && em->target != t)
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
 * @klass: An EMenuClass type to which this factory applies.
 * @menuid: The identifier of the menu for this factory, or NULL to be
 * called on all menus.
 * @func: An EMenuFactoryFunc callback.
 * @data: Callback data for @func.
 * 
 * Add a menu factory which will be called when the menu @menuid is
 * created.  The factory is free to add new items as it wishes to the
 * menu provided in the callback.
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
 * @klass: Class on which the factory was originally added.
 * @f: Factory handle.
 * 
 * Remove a popup factory.  This must only be called once, and must
 * only be called using a valid factory handle @f.  After this call,
 * @f is undefined.
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
 * @ep: An EMenu to which this target applies.
 * @type: Target type, up to implementation.
 * @size: Size of memory to allocate.  Must be >= sizeof(EMenuTarget).
 * 
 * Allocate a new menu target suitable for this class.  @size is used
 * to specify the actual target size, which may vary depending on the
 * implementing class.
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
 * @ep: EMenu on which the target was allocated.
 * @o: Tareget to free.
 * 
 * Free a target.
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

static void *emph_parent_class;
#define emph ((EMenuHook *)eph)

/* must have 1:1 correspondence with e-menu types in order */
static const EPluginHookTargetKey emph_item_types[] = {
	{ "item", E_MENU_ITEM },
	{ "toggle", E_MENU_TOGGLE },
	{ "radio", E_MENU_RADIO },
	{ 0 }
};

/* 1:1 with e-icon-factory sizes */
static const EPluginHookTargetKey emph_pixmap_sizes[] = {
	{ "menu", 0 },
	{ "button", 1},
	{ "small_toolbar", 2},
	{ "large_toolbar", 3},
	{ "dnd", 4},
	{ "dialog", 5},
	{ 0 }
};

static void
emph_menu_activate(EMenu *em, EMenuItem *item, void *data)
{
	EMenuHook *hook = data;

	printf("invoking plugin hook '%s' %p\n", (char *)item->user_data, em->target);

	e_plugin_invoke(hook->hook.plugin, item->user_data, em->target);
}

static void
emph_menu_toggle_activate(EMenu *em, EMenuItem *item, int state, void *data)
{
	EMenuHook *hook = data;

	/* FIXME: where does the toggle state go? */
	printf("invoking plugin hook '%s' %p\n", (char *)item->user_data, em->target);

	e_plugin_invoke(hook->hook.plugin, item->user_data, em->target);
}

static void
emph_menu_factory(EMenu *emp, void *data)
{
	struct _EMenuHookMenu *menu = data;
	GSList *l;

	printf("menu factory, adding %d items\n", g_slist_length(menu->items));

	if (menu->items)
		e_menu_add_items(emp, menu->items, NULL, menu->hook);

	for (l = menu->uis;l;l=g_slist_next(l))
		e_menu_add_ui(emp, "/tmp", "evolution-mail", (char *)l->data);

	/* FIXME: pixmaps? */
}

static void
emph_free_item(struct _EMenuItem *item)
{
	g_free(item->path);
	g_free(item->verb);
	g_free(item->user_data);
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

static struct _EMenuItem *
emph_construct_item(EPluginHook *eph, EMenuHookMenu *menu, xmlNodePtr root, EMenuHookTargetMap *map)
{
	struct _EMenuItem *item;

	printf("  loading menu item\n");
	item = g_malloc0(sizeof(*item));
	item->type = e_plugin_hook_id(root, emph_item_types, "type");
	item->path = e_plugin_xml_prop(root, "path");
	item->verb = e_plugin_xml_prop(root, "verb");
	item->visible = e_plugin_hook_mask(root, map->mask_bits, "visible");
	item->enable = e_plugin_hook_mask(root, map->mask_bits, "enable");
	item->user_data = e_plugin_xml_prop(root, "activate");
	if ((item->type & E_MENU_TYPE_MASK) == E_MENU_TOGGLE)
		item->activate = G_CALLBACK(emph_menu_toggle_activate);
	else
		item->activate = G_CALLBACK(emph_menu_activate);

	if (item->type == -1 || item->user_data == NULL)
		goto error;

	printf("   path=%s\n", item->path);
	printf("   verb=%s\n", item->verb);

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
	pixmap->command = e_plugin_xml_prop(root, "command");
	pixmap->name = e_plugin_xml_prop(root, "pixmap");
	pixmap->size = e_plugin_hook_id(root, emph_pixmap_sizes, "size");

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
	EMenuHookTargetMap *map;
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
	menu->id = e_plugin_xml_prop(root, "id");
	node = root->children;
	while (node) {
		if (0 == strcmp(node->name, "item")) {
			struct _EMenuItem *item;

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

/**
 * e_menu_hook_get_type:
 * 
 * Standard GObject function to get the object type.  Used to subclass
 * EMenuHook.
 * 
 * Return value: The type of the menu hook class.
 **/
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

/**
 * e_menu_hook_class_add_target_map:
 * @klass: The derived EMenuHook class.
 * @map: A map used to describe a single EMenuTarget for this class.
 * 
 * Adds a target map to a concrete derived class of EMenu.  The target
 * map enumerates a single target type, and the enable mask bit names,
 * so that the type can be loaded automatically by the EMenu class.
 **/
void e_menu_hook_class_add_target_map(EMenuHookClass *klass, const EMenuHookTargetMap *map)
{
	g_hash_table_insert(klass->target_map, (void *)map->type, (void *)map);
}
