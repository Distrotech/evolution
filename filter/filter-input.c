/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <regex.h>
#include <gtk/gtkobject.h>
#include <gtk/gtkwidget.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <gal/widgets/e-unicode.h>

#include "filter-input.h"
#include "e-util/e-sexp.h"

#define d(x)

static gboolean validate (FilterElement *fe);
static void xml_create(FilterElement *fe, xmlNodePtr node);
static xmlNodePtr xml_encode(FilterElement *fe);
static int xml_decode(FilterElement *fe, xmlNodePtr node);
static GtkWidget *get_widget(FilterElement *fe);
static void build_code(FilterElement *fe, GString *out, struct _FilterPart *ff);
static void format_sexp(FilterElement *, GString *);

static void filter_input_class_init	(FilterInputClass *class);
static void filter_input_init	(FilterInput *gspaper);
static void filter_input_finalise	(GtkObject *obj);

#define _PRIVATE(x) (((FilterInput *)(x))->priv)

struct _FilterInputPrivate {
};

static FilterElementClass *parent_class;

enum {
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

guint
filter_input_get_type (void)
{
	static guint type = 0;
	
	if (!type) {
		GtkTypeInfo type_info = {
			"FilterInput",
			sizeof(FilterInput),
			sizeof(FilterInputClass),
			(GtkClassInitFunc)filter_input_class_init,
			(GtkObjectInitFunc)filter_input_init,
			(GtkArgSetFunc)NULL,
			(GtkArgGetFunc)NULL
		};
		
		type = gtk_type_unique(filter_element_get_type (), &type_info);
	}
	
	return type;
}

static void
filter_input_class_init (FilterInputClass *class)
{
	GtkObjectClass *object_class;
	FilterElementClass *filter_element = (FilterElementClass *)class;

	object_class = (GtkObjectClass *)class;
	parent_class = gtk_type_class(filter_element_get_type ());

	object_class->finalize = filter_input_finalise;

	/* override methods */
	filter_element->validate = validate;
	filter_element->xml_create = xml_create;
	filter_element->xml_encode = xml_encode;
	filter_element->xml_decode = xml_decode;
	filter_element->get_widget = get_widget;
	filter_element->build_code = build_code;
	filter_element->format_sexp = format_sexp;

	/* signals */

	gtk_object_class_add_signals(object_class, signals, LAST_SIGNAL);
}

static void
filter_input_init (FilterInput *o)
{
	o->priv = g_malloc0 (sizeof (*o->priv));
}

static void
filter_input_finalise (GtkObject *obj)
{
	FilterInput *o = (FilterInput *)obj;

	xmlFree (o->type);
	g_list_foreach(o->values, (GFunc)g_free, NULL);
	g_list_free(o->values);

	g_free(o->priv);
	
        ((GtkObjectClass *)(parent_class))->finalize(obj);
}

/**
 * filter_input_new:
 *
 * Create a new FilterInput object.
 * 
 * Return value: A new #FilterInput object.
 **/
FilterInput *
filter_input_new (void)
{
	FilterInput *o = (FilterInput *)gtk_type_new(filter_input_get_type ());
	return o;
}

FilterInput *
filter_input_new_type_name (const char *type)
{
	FilterInput *o = filter_input_new ();
	o->type = xmlStrdup (type);
	
	d(printf("new type %s = %p\n", type, o));
	return o;
}

void
filter_input_set_value (FilterInput *fi, const char *value)
{
	GList *l;
	
	l = fi->values;
	while (l) {
		g_free (l->data);
		l = g_list_next (l);
	}
	g_list_free (fi->values);
	
	fi->values = g_list_append (NULL, g_strdup (value));
}

static gboolean
validate (FilterElement *fe)
{
	FilterInput *fi = (FilterInput *)fe;
	gboolean valid = TRUE;
	
	if (!strcmp (fi->type, "regex")) {
		regex_t regexpat;        /* regex patern */
		gint regerr;
		char *text;
		
		text = fi->values->data;
		
		regerr = regcomp (&regexpat, text, REG_EXTENDED | REG_NEWLINE | REG_ICASE);
		if (regerr) {
			GtkWidget *dialog;
			gchar *regmsg, *errmsg;
			size_t reglen;
			
			/* regerror gets called twice to get the full error string 
			   length to do proper posix error reporting */
			reglen = regerror (regerr, &regexpat, 0, 0);
			regmsg = g_malloc0 (reglen + 1);
			regerror (regerr, &regexpat, regmsg, reglen);
			
			errmsg = g_strdup_printf (_("Error in regular expression '%s':\n%s"),
						  text, regmsg);
			g_free (regmsg);
			
			dialog = gnome_ok_dialog (errmsg);
			
			gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
			
			g_free (errmsg);
			valid = FALSE;
		}
		
		regfree (&regexpat);
	}
	
	return valid;
}

static void
xml_create (FilterElement *fe, xmlNodePtr node)
{
	/* parent implementation */
        ((FilterElementClass *)(parent_class))->xml_create(fe, node);
	
}

static xmlNodePtr
xml_encode (FilterElement *fe)
{
	xmlNodePtr value;
	GList *l;
	FilterInput *fi = (FilterInput *)fe;
	char *type;
	
	type = fi->type ? fi->type : "string";
	
	d(printf ("Encoding %s as xml\n", type));
	
	value = xmlNewNode (NULL, "value");
	xmlSetProp (value, "name", fe->name);
	xmlSetProp (value, "type", type);
	l = fi->values;
	while (l) {
                xmlNodePtr cur;
		char *str = l->data;
		char *encstr;
		
                cur = xmlNewChild (value, NULL, type, NULL);
		encstr = e_utf8_xml1_encode (str);
		xmlNodeSetContent (cur, encstr);
		g_free (encstr);
                l = g_list_next (l);
	}
	
	return value;
}

static int
xml_decode (FilterElement *fe, xmlNodePtr node)
{
	FilterInput *fi = (FilterInput *)fe;
	char *name, *str, *type;
	xmlNodePtr n;
	
	name = xmlGetProp (node, "name");
	type = xmlGetProp (node, "type");
	
	d(printf("Decoding %s from xml %p\n", type, fe));
	d(printf ("Name = %s\n", name));
	xmlFree (fe->name);
	fe->name = name;
	xmlFree (fi->type);
	fi->type = type;
	n = node->childs;
	while (n) {
		if (!strcmp (n->name, type)) {
			gchar *decstr;
			str = xmlNodeGetContent (n);
			if (str) {
				decstr = e_utf8_xml1_decode (str);
				xmlFree (str);
				d(printf ("  '%s'\n", decstr));
				fi->values = g_list_append (fi->values, decstr);
			}
		} else {
			g_warning ("Unknown node type '%s' encountered decoding a %s\n", n->name, type);
		}
		n = n->next;
	}
	
	return 0;
}

static void
entry_changed (GtkEntry *entry, FilterElement *fe)
{
	char *new;
	FilterInput *fi = (FilterInput *)fe;
	GList *l;
	
	new = e_utf8_gtk_entry_get_text(entry);
	
	/* NOTE: entry only supports a single value ... */
	l = fi->values;
	while (l) {
		g_free (l->data);
		l = g_list_next (l);
	}
	
	g_list_free (fi->values);
	
	fi->values = g_list_append (NULL, new);
}

static GtkWidget *
get_widget (FilterElement *fe)
{
	GtkWidget *entry;
	FilterInput *fi = (FilterInput *)fe;
	
	entry = gtk_entry_new ();
	if (fi->values && fi->values->data) {
		e_utf8_gtk_entry_set_text (GTK_ENTRY (entry), fi->values->data);
	}
	
	gtk_signal_connect (GTK_OBJECT (entry), "changed", entry_changed, fe);
	
	return entry;
}

static void
build_code (FilterElement *fe, GString *out, struct _FilterPart *ff)
{
	return;
}

static void
format_sexp (FilterElement *fe, GString *out)
{
	GList *l;
	FilterInput *fi = (FilterInput *)fe;
	
	l = fi->values;
	while (l) {
		e_sexp_encode_string (out, l->data);
		l = g_list_next (l);
	}
}
