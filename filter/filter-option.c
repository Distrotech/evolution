/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>

#include <string.h>
#include <glib.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkoptionmenu.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <gal/widgets/e-unicode.h>

#include "filter-option.h"
#include "filter-part.h"
#include "e-util/e-sexp.h"

#define d(x)

static void xml_create(FilterElement *fe, xmlNodePtr node);
static xmlNodePtr xml_encode(FilterElement *fe);
static int xml_decode(FilterElement *fe, xmlNodePtr node);
static FilterElement *clone(FilterElement *fe);
static GtkWidget *get_widget(FilterElement *fe);
static void build_code(FilterElement *fe, GString *out, struct _FilterPart *ff);
static void format_sexp(FilterElement *, GString *);

static void filter_option_class_init	(FilterOptionClass *class);
static void filter_option_init	(FilterOption *gspaper);
static void filter_option_finalise	(GtkObject *obj);

#define _PRIVATE(x) (((FilterOption *)(x))->priv)

struct _FilterOptionPrivate {
};

static FilterElementClass *parent_class;

enum {
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

guint
filter_option_get_type (void)
{
	static guint type = 0;
	
	if (!type) {
		GtkTypeInfo type_info = {
			"FilterOption",
			sizeof(FilterOption),
			sizeof(FilterOptionClass),
			(GtkClassInitFunc)filter_option_class_init,
			(GtkObjectInitFunc)filter_option_init,
			(GtkArgSetFunc)NULL,
			(GtkArgGetFunc)NULL
		};
		
		type = gtk_type_unique (filter_element_get_type (), &type_info);
	}
	
	return type;
}

static void
filter_option_class_init (FilterOptionClass *class)
{
	GtkObjectClass *object_class;
	FilterElementClass *filter_element = (FilterElementClass *)class;
	
	object_class = (GtkObjectClass *)class;
	parent_class = gtk_type_class(filter_element_get_type ());

	object_class->finalize = filter_option_finalise;
	
	/* override methods */
	filter_element->xml_create = xml_create;
	filter_element->xml_encode = xml_encode;
	filter_element->xml_decode = xml_decode;
	filter_element->clone = clone;
	filter_element->get_widget = get_widget;
	filter_element->build_code = build_code;
	filter_element->format_sexp = format_sexp;
	
	/* signals */
	
	gtk_object_class_add_signals(object_class, signals, LAST_SIGNAL);
}

static void
filter_option_init (FilterOption *o)
{
	o->priv = g_malloc0 (sizeof (*o->priv));
}

static void
free_option(struct _filter_option *o, void *data)
{
	g_free(o->title);
	xmlFree (o->value);
	g_free(o->code);
	g_free(o);
}

static void
filter_option_finalise (GtkObject *obj)
{
	FilterOption *o = (FilterOption *)obj;

	g_list_foreach(o->options, (GFunc)free_option, NULL);
	g_list_free(o->options);
	
        ((GtkObjectClass *)(parent_class))->finalize(obj);
}

/**
 * filter_option_new:
 *
 * Create a new FilterOption object.
 * 
 * Return value: A new #FilterOption object.
 **/
FilterOption *
filter_option_new (void)
{
	FilterOption *o = (FilterOption *)gtk_type_new (filter_option_get_type ());
	return o;
}

static struct _filter_option *
find_option (FilterOption *fo, const char *name)
{
	GList *l = fo->options;
	struct _filter_option *op;
	
	while (l) {
		op = l->data;
		if (!strcmp (name, op->value)) {
			return op;
		}
		l = g_list_next (l);
	}
	
	return NULL;
}

void
filter_option_set_current (FilterOption *option, const char *name)
{
	g_assert(IS_FILTER_OPTION(option));
	
	option->current = find_option (option, name);
}

static void
xml_create (FilterElement *fe, xmlNodePtr node)
{
	FilterOption *fo = (FilterOption *)fe;
	xmlNodePtr n, work;
	struct _filter_option *op;
	
	/* parent implementation */
        ((FilterElementClass *)(parent_class))->xml_create(fe, node);
	
	n = node->childs;
	while (n) {
		if (!strcmp (n->name, "option")) {
			op = g_malloc0 (sizeof (*op));
			op->value = xmlGetProp (n, "value");
			work = n->childs;
			while (work) {
				if (!strcmp (work->name, "title")) {
					if (!op->title) {
						gchar *str, *decstr;
						str = xmlNodeGetContent (work);
						decstr = e_utf8_xml1_decode (str);
						if (str) xmlFree (str);
						op->title = decstr;
					}
				} else if (!strcmp (work->name, "code")) {
					if (!op->code) {
						gchar *str, *decstr;
						str = xmlNodeGetContent (work);
						decstr = e_utf8_xml1_decode (str);
						if (str) xmlFree (str);
						op->code = decstr;
					}
				}
				work = work->next;
			}
			d(printf ("creating new option:\n title %s\n value %s\n code %s\n",
				  op->title, op->value, op->code ? op->code : "none"));
			fo->options = g_list_append (fo->options, op);
			if (fo->current == NULL)
				fo->current = op;
		} else {
			g_warning ("Unknown xml node within optionlist: %s\n", n->name);
		}
		n = n->next;
	}
}

static xmlNodePtr
xml_encode (FilterElement *fe)
{
	xmlNodePtr value;
	FilterOption *fo = (FilterOption *)fe;
	
	d(printf ("Encoding option as xml\n"));
	value = xmlNewNode (NULL, "value");
	xmlSetProp (value, "name", fe->name);
	xmlSetProp (value, "type", "option");
	if (fo->current) {
		xmlSetProp (value, "value", fo->current->value);
	}
	
	return value;
}

static int
xml_decode (FilterElement *fe, xmlNodePtr node)
{
	FilterOption *fo = (FilterOption *)fe;
	char *value;
	
	d(printf ("Decoding option from xml\n"));
	xmlFree (fe->name);
	fe->name = xmlGetProp (node, "name");
	value = xmlGetProp (node, "value");
	if (value) {
		fo->current = find_option (fo, value);
		xmlFree (value);
	} else {
		fo->current = NULL;
	}
	return 0;
}

static void
option_changed (GtkWidget *widget, FilterElement *fe)
{
	FilterOption *fo = (FilterOption *)fe;
	
	fo->current = gtk_object_get_data (GTK_OBJECT (widget), "option");
}

static GtkWidget *
get_widget (FilterElement *fe)
{
	FilterOption *fo = (FilterOption *)fe;
	GtkWidget *menu;
	GtkWidget *omenu;
	GtkWidget *item;
	GtkWidget *first = NULL;
	GList *l = fo->options;
	struct _filter_option *op;
	int index = 0, current = 0;
	
	menu = gtk_menu_new ();
	while (l) {
		op = l->data;
		item = gtk_menu_item_new_with_label (_(op->title));
		gtk_object_set_data (GTK_OBJECT (item), "option", op);
		gtk_signal_connect (GTK_OBJECT (item), "activate", option_changed, fe);
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);
		if (op == fo->current) {
			current = index;
			first = item;
		} else if (!first) {
			first = item;
		}
		
		l = g_list_next (l);
		index++;
	}
	
	omenu = gtk_option_menu_new ();
	gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), menu);
	
	if (first)
		gtk_signal_emit_by_name (GTK_OBJECT (first), "activate", fe);
	
	gtk_option_menu_set_history (GTK_OPTION_MENU (omenu), current);
	
	return omenu;
}

static void
build_code (FilterElement *fe, GString *out, struct _FilterPart *ff)
{
	FilterOption *fo = (FilterOption *)fe;
	
	d(printf ("building option code %p, current = %p\n", fo, fo->current));
	
	if (fo->current && fo->current->code) {
		filter_part_expand_code (ff, fo->current->code, out);
	}
}

static void
format_sexp (FilterElement *fe, GString *out)
{
	FilterOption *fo = (FilterOption *)fe;
	
	if (fo->current) {
		e_sexp_encode_string (out, fo->current->value);
	}
}

static FilterElement *
clone (FilterElement *fe)
{
	FilterOption *fo = (FilterOption *)fe, *new;
	GList *l;
	struct _filter_option *fn, *op;
	
	d(printf ("cloning option\n"));
	
        new = FILTER_OPTION (((FilterElementClass *)(parent_class))->clone(fe));
	l = fo->options;
	while (l) {
		op = l->data;
		fn = g_malloc (sizeof (*fn));
		d(printf ("  option %s\n", op->title));
		fn->title = g_strdup (op->title);
		fn->value = xmlStrdup (op->value);
		if (op->code)
			fn->code = g_strdup (op->code);
		else
			fn->code = NULL;
		new->options = g_list_append (new->options, fn);
		l = g_list_next (l);
		
		if (new->current == NULL)
			new->current = fn;
	}
	
	d(printf ("cloning option code %p, current = %p\n", new, new->current));
	
	return (FilterElement *)new;
}
