
#include <string.h>
#include <stdlib.h>

#include <gtk/gtkmenu.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkradiomenuitem.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimage.h>

#include <glib.h>

#include "em-popup.h"
#include "e-util/e-msgport.h"

struct _menu_node {
	struct _menu_node *next, *prev;

	GSList *menu;
	GDestroyNotify freefunc;
};

struct _EMPopupPrivate {
	EDList menus;
};

static GObjectClass *emp_parent;

static void
emp_init(GObject *o)
{
	EMPopup *emp = (EMPopup *)o;
	struct _EMPopupPrivate *p;

	p = emp->priv = g_malloc0(sizeof(struct _EMPopupPrivate));

	e_dlist_init(&p->menus);
}

static void
emp_finalise(GObject *o)
{
	EMPopup *emp = (EMPopup *)o;
	struct _EMPopupPrivate *p = emp->priv;
	struct _menu_node *mnode, *nnode;

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

	((GObjectClass *)emp_parent)->finalize(o);
}

static void
emp_class_init(GObjectClass *klass)
{
	klass->finalize = emp_finalise;
}

GType
em_popup_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMPopupClass),
			NULL, NULL,
			(GClassInitFunc)emp_class_init,
			NULL, NULL,
			sizeof(EMPopup), 0,
			(GInstanceInitFunc)emp_init
		};
		emp_parent = g_type_class_ref(G_TYPE_OBJECT);
		type = g_type_register_static(G_TYPE_OBJECT, "EMPopup", &info, 0);
	}

	return type;
}

EMPopup *em_popup_new(void)
{
	EMPopup *emp = g_object_new(em_popup_get_type(), 0);

	return emp;
}

void
em_popup_add_items(EMPopup *emp, GSList *items, GDestroyNotify freefunc)
{
	struct _menu_node *node;

	node = g_malloc(sizeof(*node));
	node->menu = items;
	node->freefunc = freefunc;
	e_dlist_addtail(&emp->priv->menus, (EDListNode *)node);
}

static int
emp_cmp(const void *ap, const void *bp)
{
	struct _EMPopupItem *a = *((void **)ap);
	struct _EMPopupItem *b = *((void **)bp);

	return strcmp(a->path, b->path);
}

/**
 * em_popup_create:
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
em_popup_create_menu(EMPopup *emp, guint32 hide_mask, guint32 disable_mask)
{
	struct _EMPopupPrivate *p = emp->priv;
	struct _menu_node *mnode, *nnode;
	GPtrArray *items = g_ptr_array_new();
	GSList *l;
	GString *ppath = g_string_new("");
	GtkMenu *topmenu;
	GHashTable *menu_hash = g_hash_table_new(g_str_hash, g_str_equal),
		*group_hash = g_hash_table_new(g_str_hash, g_str_equal);
	/*char *domain = NULL;*/
	int i;

	mnode = (struct _menu_node *)p->menus.head;
	nnode = mnode->next;
	while (nnode) {
		for (l=mnode->menu; l; l = l->next)
			g_ptr_array_add(items, l->data);
		mnode = nnode;
		nnode = nnode->next;
	}

	qsort(items->pdata, items->len, sizeof(items->pdata[0]), emp_cmp);

	topmenu = (GtkMenu *)gtk_menu_new();
	for (i=0;i<items->len;i++) {
		GtkWidget *label;
		struct _EMPopupItem *item = items->pdata[i];
		GtkMenu *thismenu;
		GtkMenuItem *menuitem;
		char *tmp;

		/* for bar's, the mask is exclusive or */
		if (item->mask) {
			if ((item->type & EM_POPUP_TYPE_MASK) == EM_POPUP_BAR) {
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

		switch (item->type & EM_POPUP_TYPE_MASK) {
		case EM_POPUP_ITEM:
			if (item->image) {
				char *path;
				GtkWidget *image;

				path = g_build_filename(EVOLUTION_IMAGES, (char *)item->image, NULL);
				image = gtk_image_new_from_file(path);
				g_free(path);

				gtk_widget_show(image);
				menuitem = (GtkMenuItem *)gtk_image_menu_item_new();
				gtk_image_menu_item_set_image((GtkImageMenuItem *)menuitem, image);
			} else {
				menuitem = (GtkMenuItem *)gtk_menu_item_new();
			}
			break;
		case EM_POPUP_TOGGLE:
			menuitem = (GtkMenuItem *)gtk_check_menu_item_new();
			gtk_check_menu_item_set_active((GtkCheckMenuItem *)menuitem, item->type & EM_POPUP_ACTIVE);
			break;
		case EM_POPUP_RADIO:
			menuitem = (GtkMenuItem *)gtk_radio_menu_item_new(g_hash_table_lookup(group_hash, ppath->str));
			g_hash_table_insert(group_hash, ppath->str, gtk_radio_menu_item_get_group((GtkRadioMenuItem *)menuitem));
			gtk_check_menu_item_set_active((GtkCheckMenuItem *)menuitem, item->type & EM_POPUP_ACTIVE);
			break;
		case EM_POPUP_IMAGE:
			menuitem = (GtkMenuItem *)gtk_image_menu_item_new();
			gtk_image_menu_item_set_image((GtkImageMenuItem *)menuitem, item->image);
			break;
		case EM_POPUP_SUBMENU: {
			GtkMenu *submenu = (GtkMenu *)gtk_menu_new();

			g_hash_table_insert(menu_hash, item->path, submenu);
			menuitem = (GtkMenuItem *)gtk_menu_item_new();
			gtk_menu_item_set_submenu(menuitem, (GtkWidget *)submenu);
			break; }
		case EM_POPUP_BAR:
			/* TODO: double-bar, end-bar stuff? */
			menuitem = (GtkMenuItem *)gtk_separator_menu_item_new();
			break;
		default:
			continue;
		}

		if (item->label) {
			label = gtk_label_new_with_mnemonic(item->label);
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
emp_popup_done(GtkWidget *w, EMPopup *emp)
{
	gtk_widget_destroy(w);
	g_object_unref(emp);
}

/**
 * em_popup_create_menu_once:
 * @emp: EMPopup, once the menu is shown, this cannot be
 * considered a valid pointer.
 * @hide_mask: 
 * @disable_mask: 
 * 
 * Like popup_create_menu, but automatically sets up the menu
 * so that it is destroyed once a selection takes place, and
 * the EMPopup is unreffed.
 * 
 * Return value: A menu, to popup.
 **/
GtkMenu *
em_popup_create_menu_once(EMPopup *emp, guint32 hide_mask, guint32 disable_mask)
{
	GtkMenu *menu = em_popup_create_menu(emp, hide_mask, disable_mask);

	g_signal_connect(menu, "selection_done", G_CALLBACK(emp_popup_done), emp);

	return menu;
}
