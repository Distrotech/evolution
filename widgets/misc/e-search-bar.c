/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-search-bar.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
 *
 * Authors:
 *  Chris Lahey      <clahey@ximian.com>
 *  Ettore Perazzoli <ettore@ximian.com>
 *  Jon Trowbridge   <trow@ximian.com>

 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <gtk/gtkdrawingarea.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkmain.h>

#include <gal/widgets/e-unicode.h>
#include <gal/widgets/e-gui-utils.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

#include "e-search-bar.h"
#include "e-dropdown-button.h"


enum {
	QUERY_CHANGED,
	MENU_ACTIVATED,

	LAST_SIGNAL
};

static gint esb_signals [LAST_SIGNAL] = { 0, };

static GtkHBoxClass *parent_class = NULL;

/* The arguments we take */
enum {
	ARG_0,
	ARG_ITEM_ID,
	ARG_SUBITEM_ID,
	ARG_TEXT,
};


/* Signals.  */

static void
emit_query_changed (ESearchBar *esb)
{
	if (esb->pending_change) {
		gtk_idle_remove (esb->pending_change);
		esb->pending_change = 0;
	}

	gtk_signal_emit (GTK_OBJECT (esb),
			 esb_signals [QUERY_CHANGED]);
}

static void
emit_menu_activated (ESearchBar *esb, int item)
{
	gtk_signal_emit (GTK_OBJECT (esb),
			 esb_signals [MENU_ACTIVATED],
			 item);
}


/* Callbacks.  */

static void
menubar_activated_cb (GtkWidget *widget, ESearchBar *esb)
{
	int id;

	id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (widget), "EsbMenuId"));

	emit_menu_activated (esb, id);
}

static void
entry_activated_cb (GtkWidget *widget,
		     ESearchBar *esb)
{
	emit_query_changed (esb);
}

static void
subitem_activated_cb (GtkWidget *widget, ESearchBar *esb)
{
	gint id, subid;

	id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (widget), "EsbItemId"));
	subid = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (widget), "EsbSubitemId"));

	esb->item_id = id;
	esb->subitem_id = subid;
	emit_query_changed (esb);
}

static void
activate_by_subitems (ESearchBar *esb, gint item_id, ESearchBarSubitem *subitems)
{
	if (subitems == NULL) {
		/* This item uses the entry. */

		/* Remove the menu */
		if (esb->suboption && esb->subitem_id != -1) {
			g_assert (esb->suboption->parent == esb->entry_box);
			g_assert (!esb->entry || esb->entry->parent == NULL);
			gtk_container_remove (GTK_CONTAINER (esb->entry_box), esb->suboption);
		}

		/* Create and add the entry */

		if (esb->entry == NULL) {
			esb->entry = gtk_entry_new();
			gtk_widget_set_usize (esb->entry, 4, -1);
			gtk_object_ref (GTK_OBJECT (esb->entry));
			gtk_signal_connect (GTK_OBJECT (esb->entry), "activate",
					    GTK_SIGNAL_FUNC (entry_activated_cb), esb);
			gtk_container_add (GTK_CONTAINER (esb->entry_box), esb->entry);
			gtk_widget_show(esb->entry);

			esb->subitem_id = -1;
		}

		if (esb->subitem_id == -1) {
			g_assert (esb->entry->parent == esb->entry_box);
			g_assert (!esb->suboption || esb->suboption->parent == NULL);
		} else {
			gtk_container_add (GTK_CONTAINER (esb->entry_box), esb->entry);
			esb->subitem_id = -1;
		}
		
		gtk_entry_set_text (GTK_ENTRY (esb->entry), "");
	} else {
		/* This item uses a submenu */
		GtkWidget *menu;
		GtkWidget *menu_item;
		gint i;

		/* Remove the entry */
		if (esb->entry && esb->subitem_id == -1) {
			g_assert (esb->entry->parent == esb->entry_box);
			g_assert (!esb->suboption || esb->suboption->parent == NULL);
			gtk_container_remove (GTK_CONTAINER (esb->entry_box), esb->entry);
		}

		/* Create and add the menu */

		if (esb->suboption == NULL) {
			esb->suboption = gtk_option_menu_new ();
			gtk_object_ref (GTK_OBJECT (esb->suboption));
			gtk_container_add (GTK_CONTAINER (esb->entry_box), esb->suboption);
			gtk_widget_show (esb->suboption);

			esb->subitem_id = subitems[0].id;
		}

		if (esb->subitem_id != -1) {
			g_assert (esb->suboption->parent == esb->entry_box);
			g_assert (!esb->entry || esb->entry->parent == NULL);
		} else {
			gtk_container_add (GTK_CONTAINER (esb->entry_box), esb->suboption);
			esb->subitem_id = subitems[0].id;
		}

		/* Create the items */

		esb->suboption_menu = menu = gtk_menu_new ();
		for (i = 0; subitems[i].id != -1; ++i) {
			if (subitems[i].text) {
				char *str;

				if (subitems[i].translate)
					str = _(subitems[i].text);
				else
					str = subitems[i].text;

				menu_item = gtk_menu_item_new_with_label (str);
			} else {
				menu_item = gtk_menu_item_new ();
				gtk_widget_set_sensitive (menu_item, FALSE);
			}

			gtk_object_set_data (GTK_OBJECT (menu_item), "EsbItemId",
					     GINT_TO_POINTER (item_id));
			gtk_object_set_data (GTK_OBJECT (menu_item), "EsbSubitemId",
					     GINT_TO_POINTER (subitems[i].id));

			gtk_signal_connect (GTK_OBJECT (menu_item),
					    "activate",
					    GTK_SIGNAL_FUNC (subitem_activated_cb),
					    esb);

			gtk_widget_show (menu_item);
			gtk_menu_append (GTK_MENU (menu), menu_item);
		}

		gtk_option_menu_remove_menu (GTK_OPTION_MENU (esb->suboption));
		gtk_option_menu_set_menu (GTK_OPTION_MENU (esb->suboption), menu);
	}

	if (esb->activate_button)
		gtk_widget_set_sensitive (esb->activate_button, subitems == NULL);
}

static void
option_activated_cb (GtkWidget *widget,
		     ESearchBar *esb)
{
	int id;

	id = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (widget), "EsbChoiceId"));

	activate_by_subitems (esb, id, gtk_object_get_data (GTK_OBJECT (widget), "EsbChoiceSubitems"));

	esb->item_id = id;
	emit_query_changed (esb);
}

static void
activate_button_clicked_cb (GtkWidget *widget, ESearchBar *esb)
{
	emit_query_changed (esb);
}


/* Widgetry creation.  */

/* This function exists to fix the irreparable GtkOptionMenu stupidity.  In
   fact, this lame-ass widget adds a 1-pixel-wide empty border around the
   button for no reason.  So we have add a 1-pixel-wide border around the the
   buttons we have in the search bar to make things look right.  This is done
   through an event box.  */
static GtkWidget *
put_in_spacer_widget (GtkWidget *widget)
{
	GtkWidget *holder;

	holder = gtk_event_box_new ();
	gtk_container_set_border_width (GTK_CONTAINER (holder), 1);
	gtk_container_add (GTK_CONTAINER (holder), widget);

	return holder;
}

static ESearchBarSubitem *
copy_subitems (ESearchBarSubitem *subitems)
{
	gint i, N;
	ESearchBarSubitem *copy;

	if (subitems == NULL)
		return NULL;

	for (N=0; subitems[N].id != -1; ++N);
	copy = g_new (ESearchBarSubitem, N+1);

	for (i=0; i<N; ++i) {
		copy[i].text = g_strdup (subitems[i].text);
		copy[i].id = subitems[i].id;
		copy[i].translate = subitems[i].translate;
	}

	copy[N].text = NULL;
	copy[N].id = -1;

	return copy;
}

static void
add_dropdown (ESearchBar *esb, ESearchBarItem *items)
{
	GtkWidget *menu = esb->dropdown_menu;
	GtkWidget *item;
	
	if (items->text) {
		char *str;
		str = _(items->text);
		if (str == items->text) {
			/* It may be english string, or utf8 rule name */
			item = e_utf8_gtk_menu_item_new_with_label (GTK_MENU (menu), str);
		} else
			item = gtk_menu_item_new_with_label (str);
	} else {
		item = gtk_menu_item_new();
		gtk_widget_set_sensitive (item, FALSE);
	}
	
	gtk_widget_show (item);
	gtk_menu_append (GTK_MENU (menu), item);
	gtk_object_set_data (GTK_OBJECT (item), "EsbMenuId", GINT_TO_POINTER (items->id));
	gtk_signal_connect (GTK_OBJECT (item), "activate",
			    GTK_SIGNAL_FUNC (menubar_activated_cb),
			    esb);
}

static void
set_dropdown (ESearchBar *esb,
	      ESearchBarItem *items)
{
	GtkWidget *menu;
	GtkWidget *dropdown;
	int i;
	
	menu = esb->dropdown_menu = gtk_menu_new ();
	for (i = 0; items[i].id != -1; i++)
		add_dropdown (esb, items + i);
	
	gtk_widget_show_all (menu);
	
	dropdown = e_dropdown_button_new (_("Sear_ch"), GTK_MENU (menu));
	gtk_widget_show (dropdown);
	
	if (esb->dropdown_holder == NULL) {
		/* See the comment in `put_in_spacer_widget()' to understand
		   why we have to do this.  */
		
		esb->dropdown_holder = put_in_spacer_widget (dropdown);
		esb->dropdown = dropdown;
		gtk_widget_show (esb->dropdown_holder);

		gtk_box_pack_start (GTK_BOX (esb), esb->dropdown_holder, FALSE, FALSE, 0);
	} else {
		gtk_widget_destroy (esb->dropdown);
		esb->dropdown = dropdown;
		gtk_container_add (GTK_CONTAINER (esb->dropdown_holder), esb->dropdown);
	}
}

/* Frees an array of subitem information */
static void
free_subitems (ESearchBarSubitem *subitems)
{
	ESearchBarSubitem *s;

	g_assert (subitems != NULL);

	for (s = subitems; s->id != -1; s++) {
		if (s->text)
			g_free (s->text);
	}

	g_free (subitems);
}

/* Callback used when an option item is destroyed.  We have to destroy its
 * suboption items.
 */
static void
option_item_destroy_cb (GtkObject *object, gpointer data)
{
	ESearchBarSubitem *subitems;

	subitems = data;

	g_assert (subitems != NULL);
	free_subitems (subitems);
	gtk_object_set_data (object, "EsbChoiceSubitems", NULL);
}

static void
set_option (ESearchBar *esb, ESearchBarItem *items)
{
	GtkWidget *menu;
	GtkRequisition dropdown_requisition;
	GtkRequisition option_requisition;
	int i;

	if (esb->option) {
		gtk_widget_destroy (esb->option_menu);
	} else {
		esb->option = gtk_option_menu_new ();
		gtk_widget_show (esb->option);
		gtk_box_pack_start (GTK_BOX (esb), esb->option, FALSE, FALSE, 0);
	}

	esb->option_menu = menu = gtk_menu_new ();
	for (i = 0; items[i].id != -1; i++) {
		GtkWidget *item;
		ESearchBarSubitem *subitems = NULL;

		if (items[i].text) {
			char *str;
			str = _(items[i].text);
			if (str == items[i].text) {
				/* It may be english string, or utf8 rule name */
				item = e_utf8_gtk_menu_item_new_with_label (GTK_MENU (menu), str);
			} else
				item = gtk_menu_item_new_with_label (str);
		} else {
			item = gtk_menu_item_new ();
			gtk_widget_set_sensitive (item, FALSE);
		}

		gtk_menu_append (GTK_MENU (menu), item);

		gtk_object_set_data (GTK_OBJECT (item), "EsbChoiceId", GINT_TO_POINTER(items[i].id));

		if (items[i].subitems != NULL) {
			subitems = copy_subitems (items[i].subitems);
			gtk_object_set_data (GTK_OBJECT (item), "EsbChoiceSubitems", subitems);
			gtk_signal_connect (GTK_OBJECT (item), "destroy",
					    GTK_SIGNAL_FUNC (option_item_destroy_cb), subitems);
		}

		if (i == 0)
			activate_by_subitems (esb, items[i].id, subitems);

		gtk_signal_connect (GTK_OBJECT (item), "activate",
				    GTK_SIGNAL_FUNC (option_activated_cb),
				    esb);
	}

	gtk_widget_show_all (menu);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (esb->option), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (esb->option), 0);

	gtk_widget_set_sensitive (esb->option, TRUE);

	/* Set the minimum height of this widget to that of the dropdown
           button, for a better look.  */
	g_assert (esb->dropdown != NULL);

	gtk_widget_size_request (esb->dropdown, &dropdown_requisition);
	gtk_widget_size_request (esb->option, &option_requisition);

	gtk_container_set_border_width (GTK_CONTAINER (esb->dropdown), GTK_CONTAINER (esb->option)->border_width);
}

static void
add_activate_button (ESearchBar *esb)
{
	GtkWidget *label;
	GtkWidget *holder;

	label = gtk_label_new (_("Find Now"));
	gtk_misc_set_padding(GTK_MISC(label), 2, 0);
	gtk_widget_show (label);
	
	/* See the comment in `put_in_spacer_widget()' to understand
	   why we have to do this.  */
	
	esb->activate_button = gtk_button_new ();
	gtk_widget_show (esb->activate_button);
	gtk_container_add (GTK_CONTAINER (esb->activate_button), label);
	
	holder = put_in_spacer_widget (esb->activate_button);
	gtk_widget_show (holder);
	
	gtk_signal_connect (GTK_OBJECT (esb->activate_button), "clicked",
			    GTK_SIGNAL_FUNC (activate_button_clicked_cb), esb);
	
	gtk_box_pack_start (GTK_BOX (esb), holder, FALSE, FALSE, 0);
}

static int
find_id (GtkWidget *menu, int idin, const char *type, GtkWidget **widget)
{
	GList *l = GTK_MENU_SHELL (menu)->children;
	int row = -1, i = 0, id;

	if (widget)
		*widget = NULL;
	while (l) {
		id = GPOINTER_TO_INT (gtk_object_get_data (l->data, type));
		if (id == idin) {
			row = i;
			if (widget)
				*widget = l->data;
			break;
		}
		i++;
		l = l->next;
	}
	return row;
}


/* GtkObject methods.  */

static void
impl_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	ESearchBar *esb = E_SEARCH_BAR (object);

	switch (arg_id) {
	case ARG_ITEM_ID:
		GTK_VALUE_ENUM (*arg) = e_search_bar_get_item_id (esb);
		break;

	case ARG_SUBITEM_ID:
		GTK_VALUE_ENUM (*arg) = e_search_bar_get_subitem_id (esb);
		break;

	case ARG_TEXT:
		GTK_VALUE_STRING (*arg) = e_search_bar_get_text (esb);
		break;

	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

static void
impl_set_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	ESearchBar *esb = E_SEARCH_BAR(object);
	
	switch (arg_id) {
	case ARG_ITEM_ID:
		e_search_bar_set_item_id (esb, GTK_VALUE_ENUM (*arg));
		break;

	case ARG_SUBITEM_ID:
		e_search_bar_set_subitem_id (esb, GTK_VALUE_ENUM (*arg));
		break;
		
	case ARG_TEXT:
		e_search_bar_set_text (esb, GTK_VALUE_STRING (*arg));
		break;
		
	default:
		break;
	}
}

static void
impl_destroy (GtkObject *object)
{
	ESearchBar *esb = E_SEARCH_BAR (object);
	
	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (object));
	
	/* These two we do need to unref, because we explicitly hold
	   references to them. */
	if (esb->entry)
		gtk_object_unref (GTK_OBJECT (esb->entry));
	if (esb->suboption)
		gtk_object_unref (GTK_OBJECT (esb->suboption));
	
	if (esb->pending_change) {
		gtk_idle_remove (esb->pending_change);
		esb->pending_change = 0;
	}
	
	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		GTK_OBJECT_CLASS (parent_class)->destroy (object);
}


static void
class_init (ESearchBarClass *klass)
{
	GtkObjectClass *object_class;
	
	object_class = GTK_OBJECT_CLASS (klass);
	
	parent_class = gtk_type_class (gtk_hbox_get_type ());
	
	object_class->set_arg = impl_set_arg;
	object_class->get_arg = impl_get_arg;
	object_class->destroy = impl_destroy;
	
	klass->set_menu = set_dropdown;
	klass->set_option = set_option;
	
	gtk_object_add_arg_type ("ESearchBar::item_id", GTK_TYPE_ENUM,
				 GTK_ARG_READWRITE, ARG_ITEM_ID);
	gtk_object_add_arg_type ("ESearchBar::subitem_id", GTK_TYPE_ENUM,
				 GTK_ARG_READWRITE, ARG_SUBITEM_ID);
	gtk_object_add_arg_type ("ESearchBar::text", GTK_TYPE_STRING,
				 GTK_ARG_READWRITE, ARG_TEXT);
	
	esb_signals [QUERY_CHANGED] =
		gtk_signal_new ("query_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ESearchBarClass, query_changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);
	
	esb_signals [MENU_ACTIVATED] =
		gtk_signal_new ("menu_activated",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ESearchBarClass, menu_activated),
				gtk_marshal_NONE__INT,
				GTK_TYPE_NONE, 1, GTK_TYPE_INT);
	
	gtk_object_class_add_signals (object_class, esb_signals, LAST_SIGNAL);
}

static void
init (ESearchBar *esb)
{
	esb->dropdown      = NULL;
	esb->option        = NULL;
	esb->entry         = NULL;
	
	esb->item_id = 0;
	esb->subitem_id = 0;
}


/* Object construction.  */

static gint
idle_change_hack (gpointer ptr)
{
	ESearchBar *esb = E_SEARCH_BAR (ptr);
	esb->pending_change = 0;
	emit_query_changed (esb);
	return FALSE;
}

void
e_search_bar_construct (ESearchBar *search_bar,
			ESearchBarItem *menu_items,
			ESearchBarItem *option_items)
{
	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));
	g_return_if_fail (menu_items != NULL);
	g_return_if_fail (option_items != NULL);
	
	gtk_box_set_spacing (GTK_BOX (search_bar), 1);

	e_search_bar_set_menu (search_bar, menu_items);

	search_bar->entry_box = gtk_hbox_new (0, FALSE);

	e_search_bar_set_option (search_bar, option_items);

	gtk_widget_show (search_bar->entry_box);
	gtk_box_pack_start (GTK_BOX(search_bar), search_bar->entry_box, TRUE, TRUE, 0);

	add_activate_button (search_bar);

	/* 
	 * If the default choice for the option menu has subitems, then we need to
	 * activate the search immediately.  However, the developer won't have
	 * connected to the changed signal until after the object is constructed,
	 * so we can't emit here.  Thus we launch a one-shot idle function that will
	 * emit the changed signal, so that the proper callback will get invoked.
	 */
	if (search_bar->subitem_id >= 0) {
		gtk_widget_set_sensitive (search_bar->activate_button, FALSE);

		search_bar->pending_change = gtk_idle_add (idle_change_hack, search_bar);
	}
}

void
e_search_bar_set_menu (ESearchBar *search_bar, ESearchBarItem *menu_items)
{
	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));
	g_return_if_fail (menu_items != NULL);
	
	((ESearchBarClass *)((GtkObject *)search_bar)->klass)->set_menu (search_bar, menu_items);
}

void
e_search_bar_add_menu (ESearchBar *search_bar, ESearchBarItem *menu_item)
{
	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));
	g_return_if_fail (menu_item != NULL);
	
	add_dropdown (search_bar, menu_item);
}

void
e_search_bar_set_option (ESearchBar *search_bar, ESearchBarItem *option_items)
{
	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));
	g_return_if_fail (option_items != NULL);
	
	((ESearchBarClass *)((GtkObject *)search_bar)->klass)->set_option (search_bar, option_items);
}

/**
 * e_search_bar_set_suboption:
 * @search_bar: A search bar.
 * @option_id: Identifier of the main option menu item under which the subitems
 * are to be set.
 * @subitems: Array of subitem information.
 * 
 * Sets the items for the secondary option menu of a search bar.
 **/
void
e_search_bar_set_suboption (ESearchBar *search_bar, int option_id, ESearchBarSubitem *subitems)
{
	int row;
	GtkWidget *item;
	ESearchBarSubitem *old_subitems;
	ESearchBarSubitem *new_subitems;

	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));

	row = find_id (search_bar->option_menu, option_id, "EsbChoiceId", &item);
	g_return_if_fail (row != -1);
	g_assert (item != NULL);

	old_subitems = gtk_object_get_data (GTK_OBJECT (item), "EsbChoiceSubitems");
	if (old_subitems) {
		/* This was connected in set_option() */
		gtk_signal_disconnect_by_data (GTK_OBJECT (item), old_subitems);
		free_subitems (old_subitems);
		gtk_object_set_data (GTK_OBJECT (item), "EsbChoiceSubitems", NULL);
	}

	if (subitems) {
		new_subitems = copy_subitems (subitems);
		gtk_object_set_data (GTK_OBJECT (item), "EsbChoiceSubitems", new_subitems);
		gtk_signal_connect (GTK_OBJECT (item), "destroy",
				    GTK_SIGNAL_FUNC (option_item_destroy_cb), new_subitems);
	} else
		new_subitems = NULL;

	if (search_bar->item_id == option_id)
		activate_by_subitems (search_bar, option_id, new_subitems);
}

GtkWidget *
e_search_bar_new (ESearchBarItem *menu_items,
		  ESearchBarItem *option_items)
{
	GtkWidget *widget;
	
	g_return_val_if_fail (menu_items != NULL, NULL);
	g_return_val_if_fail (option_items != NULL, NULL);
	
	widget = GTK_WIDGET (gtk_type_new (e_search_bar_get_type ()));
	
	e_search_bar_construct (E_SEARCH_BAR (widget), menu_items, option_items);
	
	return widget;
}

void
e_search_bar_set_menu_sensitive (ESearchBar *esb, int id, gboolean state)
{
	int row;
	GtkWidget *widget;
	
	row = find_id (esb->dropdown_menu, id, "EsbMenuId", &widget);
	if (row != -1)
		gtk_widget_set_sensitive (widget, state);
}

GtkType
e_search_bar_get_type (void)
{
	static GtkType type = 0;
	
	if (!type) {
		static const GtkTypeInfo info = {
			"ESearchBar",
			sizeof (ESearchBar),
			sizeof (ESearchBarClass),
			(GtkClassInitFunc) class_init,
			(GtkObjectInitFunc) init,
			/* reserved_1 */ NULL,
		       	/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		
		type = gtk_type_unique (gtk_hbox_get_type (), &info);
	}
	
	return type;
}

/**
 * e_search_bar_set_item_id:
 * @search_bar: A search bar.
 * @id: Identifier of the item to set.
 * 
 * Sets the active item in the options menu of a search bar.
 **/
void
e_search_bar_set_item_id (ESearchBar *search_bar, int id)
{
	int row;

	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));

	row = find_id (search_bar->option_menu, id, "EsbChoiceId", NULL);
	g_return_if_fail (row != -1);

	search_bar->item_id = id;
	gtk_option_menu_set_history (GTK_OPTION_MENU (search_bar->option), row);
	emit_query_changed (search_bar);
}

/**
 * e_search_bar_get_item_id:
 * @search_bar: A search bar.
 * 
 * Queries the currently selected item in the options menu of a search bar.
 * 
 * Return value: Identifier of the selected item in the options menu.
 **/
int
e_search_bar_get_item_id (ESearchBar *search_bar)
{
	g_return_val_if_fail (search_bar != NULL, -1);
	g_return_val_if_fail (E_IS_SEARCH_BAR (search_bar), -1);
	
	return search_bar->item_id;
}

void
e_search_bar_set_subitem_id (ESearchBar *search_bar, int id)
{
	int row;

	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));

	row = find_id (search_bar->suboption_menu, id, "EsbSubitemId", NULL);
	g_return_if_fail (row != -1);

	search_bar->subitem_id = id;
	gtk_option_menu_set_history (GTK_OPTION_MENU (search_bar->suboption), row);
	emit_query_changed (search_bar);
}

/**
 * e_search_bar_get_subitem_id:
 * @search_bar: A search bar.
 * 
 * Queries the currently selected item in the suboptions menu of a search bar.
 * 
 * Return value: Identifier of the selected item in the suboptions menu.
 * If the search bar currently contains an entry rather than a a suboption menu,
 * a value less than zero is returned.
 **/
int
e_search_bar_get_subitem_id (ESearchBar *search_bar)
{
	g_return_val_if_fail (search_bar != NULL, -1);
	g_return_val_if_fail (E_IS_SEARCH_BAR (search_bar), -1);
	
	return search_bar->subitem_id;
}

/**
 * e_search_bar_set_ids:
 * @search_bar: A search bar.
 * @item_id: Identifier of the item to set.
 * @subitem_id: Identifier of the subitem to set.
 * 
 * Sets the item and subitem ids for a search bar.  This is intended to switch
 * to an item that has subitems.
 **/
void
e_search_bar_set_ids (ESearchBar *search_bar, int item_id, int subitem_id)
{
	int item_row;
	GtkWidget *item_widget;
	ESearchBarSubitem *subitems;

	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));

	item_row = find_id (search_bar->option_menu, item_id, "EsbChoiceId", &item_widget);
	g_return_if_fail (item_row != -1);
	g_assert (item_widget != NULL);

	subitems = gtk_object_get_data (GTK_OBJECT (item_widget), "EsbChoiceSubitems");
	g_return_if_fail (subitems != NULL);

	search_bar->item_id = item_id;
	gtk_option_menu_set_history (GTK_OPTION_MENU (search_bar->option), item_row);

	activate_by_subitems (search_bar, item_id, subitems);
	e_search_bar_set_subitem_id (search_bar, subitem_id);
}

/**
 * e_search_bar_set_text:
 * @search_bar: A search bar.
 * @text: Text to set in the search bar's entry line.
 * 
 * Sets the text string inside the entry line of a search bar.
 **/
void
e_search_bar_set_text (ESearchBar *search_bar, const char *text)
{
	g_return_if_fail (search_bar != NULL);
	g_return_if_fail (E_IS_SEARCH_BAR (search_bar));

	e_utf8_gtk_editable_set_text (GTK_EDITABLE (search_bar->entry), text);
	emit_query_changed (search_bar);
}

/**
 * e_search_bar_get_text:
 * @search_bar: A search bar.
 * 
 * Queries the text of the entry line in a search bar.
 * 
 * Return value: The text string that is in the entry line of the search bar.
 * This must be freed using g_free().  If a suboption menu is active instead
 * of an entry, NULL is returned.
 **/
char *
e_search_bar_get_text (ESearchBar *search_bar)
{
	g_return_val_if_fail (search_bar != NULL, NULL);
	g_return_val_if_fail (E_IS_SEARCH_BAR (search_bar), NULL);
	
	return search_bar->subitem_id < 0 ? e_utf8_gtk_editable_get_text (GTK_EDITABLE (search_bar->entry)) : NULL;
}
