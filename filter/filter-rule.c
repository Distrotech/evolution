/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Not Zed <notzed@lostzed.mmc.com.au>
 *           Jeffrey Stedfast <fejj@ximian.com>
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

#include <config.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtktable.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <gal/widgets/e-unicode.h>

#include "filter-rule.h"
#include "filter-context.h"

#define d(x)

static int validate(FilterRule *);
static xmlNodePtr xml_encode (FilterRule *);
static int xml_decode (FilterRule *, xmlNodePtr, RuleContext *);
static void build_code (FilterRule *, GString * out);
static void rule_copy  (FilterRule *dest, FilterRule *src);
static GtkWidget *get_widget (FilterRule * fr, struct _RuleContext *f);

static void filter_rule_class_init (FilterRuleClass * class);
static void filter_rule_init (FilterRule * gspaper);
static void filter_rule_finalise (GtkObject * obj);

#define _PRIVATE(x) (((FilterRule *)(x))->priv)

struct _FilterRulePrivate {
	int frozen;
};

static GtkObjectClass *parent_class;

enum {
	CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

guint
filter_rule_get_type ()
{
	static guint type = 0;
	
	if (!type) {
		GtkTypeInfo type_info = {
			"FilterRule",
			sizeof (FilterRule),
			sizeof (FilterRuleClass),
			(GtkClassInitFunc) filter_rule_class_init,
			(GtkObjectInitFunc) filter_rule_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL
		};
		
		type = gtk_type_unique (gtk_object_get_type(), &type_info);
	}
	
	return type;
}

static void
filter_rule_class_init (FilterRuleClass * class)
{
	GtkObjectClass *object_class;
	
	object_class = (GtkObjectClass *) class;
	parent_class = gtk_type_class (gtk_object_get_type());
	
	object_class->finalize = filter_rule_finalise;
	
	/* override methods */
	class->validate = validate;
	class->xml_encode = xml_encode;
	class->xml_decode = xml_decode;
	class->build_code = build_code;
	class->copy = rule_copy;
	class->get_widget = get_widget;
	
	/* signals */
	signals[CHANGED] =
		gtk_signal_new("changed",
			       GTK_RUN_LAST,
			       object_class->type,
			       GTK_SIGNAL_OFFSET (FilterRuleClass, changed),
			       gtk_marshal_NONE__NONE,
			       GTK_TYPE_NONE, 0);
	
	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);
}

static void
filter_rule_init (FilterRule * o)
{
	o->priv = g_malloc0 (sizeof (*o->priv));
}

static void
filter_rule_finalise (GtkObject * obj)
{
	FilterRule *o = (FilterRule *) obj;

	g_free (o->name);
	g_free (o->source);
	g_list_foreach(o->parts, (GFunc)gtk_object_unref, NULL);
	g_list_free(o->parts);

	g_free(o->priv);
	
	((GtkObjectClass *) (parent_class))->finalize(obj);
}

/**
 * filter_rule_new:
 *
 * Create a new FilterRule object.
 * 
 * Return value: A new #FilterRule object.
 **/
FilterRule *
filter_rule_new ()
{
	FilterRule *o = (FilterRule *) gtk_type_new (filter_rule_get_type());
	
	return o;
}

FilterRule *
filter_rule_clone (FilterRule *base)
{
	FilterRule *rule;
	
	g_assert (IS_FILTER_RULE (base));
	
	rule = gtk_type_new (GTK_OBJECT (base)->klass->type);
	filter_rule_copy (rule, base);
	
	return rule;
}

void
filter_rule_set_name (FilterRule *fr, const char *name)
{
	g_assert (IS_FILTER_RULE (fr));

	if ((fr->name && name && strcmp(fr->name, name) == 0)
	    || (fr->name == NULL && name == NULL))
		return;
	
	g_free (fr->name);
	fr->name = g_strdup (name);

	filter_rule_emit_changed(fr);
}

void
filter_rule_set_source (FilterRule *fr, const char *source)
{
	g_assert (IS_FILTER_RULE (fr));

	if ((fr->source && source && strcmp(fr->source, source) == 0)
	    || (fr->source == NULL && source == NULL))
		return;
	
	g_free (fr->source);
	fr->source = g_strdup (source);

	filter_rule_emit_changed(fr);
}

int
filter_rule_validate (FilterRule *fr)
{
	g_assert (IS_FILTER_RULE (fr));
	
	return ((FilterRuleClass *) ((GtkObject *) fr)->klass)->validate (fr);
}

static int
validate (FilterRule *fr)
{
	int valid = TRUE;
	GList *parts;
	
	if (!fr->name || !*fr->name) {
		GtkWidget *dialog;
		
		dialog = gnome_ok_dialog (_("You must name this filter."));
		
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
		
		return FALSE;
	}
	
	/* validate rule parts */
	parts = fr->parts;
	while (parts && valid) {
		valid = filter_part_validate ((FilterPart *)parts->data);
		parts = parts->next;
	}
	
	return valid;
}

xmlNodePtr
filter_rule_xml_encode (FilterRule *fr)
{
	g_assert (IS_FILTER_RULE (fr));
	
	return ((FilterRuleClass *) ((GtkObject *) fr)->klass)->xml_encode (fr);
}

static xmlNodePtr
xml_encode (FilterRule *fr)
{
	xmlNodePtr node, set, work;
	GList *l;
	
	node = xmlNewNode (NULL, "rule");
	switch (fr->grouping) {
	case FILTER_GROUP_ALL:
		xmlSetProp (node, "grouping", "all");
		break;
	case FILTER_GROUP_ANY:
		xmlSetProp (node, "grouping", "any");
		break;
	}
	
	if (fr->source) {
		xmlSetProp (node, "source", fr->source);
	} else {
		/* set to the default filter type */
		xmlSetProp (node, "source", "incoming");
	}
	
	if (fr->name) {
		char *encstr;
		
		work = xmlNewNode (NULL, "title");
		encstr = e_utf8_xml1_encode (fr->name);
		xmlNodeSetContent (work, encstr);
		g_free (encstr);
		xmlAddChild (node, work);
	}
	
	set = xmlNewNode (NULL, "partset");
	xmlAddChild (node, set);
	l = fr->parts;
	while (l) {
		work = filter_part_xml_encode ((FilterPart *) l->data);
		xmlAddChild (set, work);
		l = g_list_next (l);
	}
	
	return node;
}

static void
load_set (xmlNodePtr node, FilterRule *fr, RuleContext *f)
{
	xmlNodePtr work;
	char *rulename;
	FilterPart *part;
	
	work = node->childs;
	while (work) {
		if (!strcmp (work->name, "part")) {
			rulename = xmlGetProp (work, "name");
			part = rule_context_find_part (f, rulename);
			if (part) {
				part = filter_part_clone (part);
				filter_part_xml_decode (part, work);
				filter_rule_add_part (fr, part);
			} else {
				g_warning ("cannot find rule part '%s'\n", rulename);
			}
			xmlFree (rulename);
		} else {
			g_warning ("Unknwon xml node in part: %s", work->name);
		}
		work = work->next;
	}
}

int
filter_rule_xml_decode (FilterRule *fr, xmlNodePtr node, RuleContext *f)
{
	int res;

	g_assert (IS_FILTER_RULE (fr));
	g_assert (IS_RULE_CONTEXT (f));
	g_assert (node != NULL);

	fr->priv->frozen++;	
	res = ((FilterRuleClass *) ((GtkObject *) fr)->klass)->xml_decode (fr, node, f);
	fr->priv->frozen--;

	filter_rule_emit_changed(fr);

	return res;
}

static int
xml_decode (FilterRule *fr, xmlNodePtr node, RuleContext *f)
{
	xmlNodePtr work;
	char *grouping;
	char *source;
	
	if (fr->name) {
		g_free (fr->name);
		fr->name = NULL;
	}
	
	grouping = xmlGetProp (node, "grouping");
	if (!strcmp (grouping, "any"))
		fr->grouping = FILTER_GROUP_ANY;
	else
		fr->grouping = FILTER_GROUP_ALL;
	xmlFree (grouping);
	
	g_free (fr->source);
	source = xmlGetProp (node, "source");
	if (source) {
		fr->source = g_strdup (source);
		xmlFree (source);
	} else {
		/* default filter type */
		fr->source = g_strdup ("incoming");
	}
	
	work = node->childs;
	while (work) {
		if (!strcmp (work->name, "partset")) {
			load_set (work, fr, f);
		} else if (!strcmp (work->name, "title") || !strcmp (work->name, "_title")) {
			if (!fr->name) {
				char *str, *decstr;

				str = xmlNodeGetContent (work);
				decstr = e_utf8_xml1_decode (str);
				if (str)
					xmlFree (str);
				fr->name = decstr;
			}
		}
		work = work->next;
	}
	
	return 0;
}

static void
rule_copy (FilterRule *dest, FilterRule *src)
{
	GList *node;
	
	g_free (dest->name);
	dest->name = g_strdup (src->name);
	
	g_free (dest->source);
	dest->source = g_strdup (src->source);
	
	dest->grouping = src->grouping;
	
	if (dest->parts) {
		g_list_foreach (dest->parts, (GFunc) gtk_object_unref, NULL);
		g_list_free (dest->parts);
		dest->parts = NULL;
	}
	
	node = src->parts;
	while (node) {
		FilterPart *part = node->data;
		
		gtk_object_ref (GTK_OBJECT (part));
		dest->parts = g_list_append (dest->parts, part);
		node = node->next;
	}
}

void
filter_rule_copy (FilterRule *dest, FilterRule *src)
{
	g_assert (IS_FILTER_RULE (dest));
	g_assert (IS_FILTER_RULE (src));
	
	((FilterRuleClass *) ((GtkObject *) dest)->klass)->copy (dest, src);

	filter_rule_emit_changed(dest);
}

void
filter_rule_add_part (FilterRule *fr, FilterPart *fp)
{
	g_assert (IS_FILTER_RULE (fr));
	g_assert (IS_FILTER_PART (fp));
	
	fr->parts = g_list_append (fr->parts, fp);

	filter_rule_emit_changed(fr);
}

void
filter_rule_remove_part (FilterRule *fr, FilterPart *fp)
{
	g_assert (IS_FILTER_RULE (fr));
	g_assert (IS_FILTER_PART (fp));
	
	fr->parts = g_list_remove (fr->parts, fp);

	filter_rule_emit_changed(fr);
}

void
filter_rule_replace_part (FilterRule *fr, FilterPart *fp, FilterPart *new)
{
	GList *l;
	
	g_assert (IS_FILTER_RULE (fr));
	g_assert (IS_FILTER_PART (fp));
	g_assert (IS_FILTER_PART (new));
	
	l = g_list_find (fr->parts, fp);
	if (l) {
		l->data = new;
	} else {
		fr->parts = g_list_append (fr->parts, new);
	}

	filter_rule_emit_changed(fr);
}

void
filter_rule_build_code (FilterRule *fr, GString *out)
{
	g_assert (IS_FILTER_RULE (fr));
	g_assert (out != NULL);
	
	((FilterRuleClass *) ((GtkObject *) fr)->klass)->build_code (fr, out);
	
	d(printf ("build_code: [%s](%d)", out->str, out->len));
}

void
filter_rule_emit_changed(FilterRule *fr)
{
	g_assert (IS_FILTER_RULE (fr));

	if (fr->priv->frozen == 0)
		gtk_signal_emit((GtkObject *)fr, signals[CHANGED]);
}

static void
build_code (FilterRule *fr, GString *out)
{
	switch (fr->grouping) {
	case FILTER_GROUP_ALL:
		g_string_append (out, " (and\n  ");
		break;
	case FILTER_GROUP_ANY:
		g_string_append (out, " (or\n  ");
		break;
	default:
		g_warning ("Invalid grouping");
	}
	
	filter_part_build_code_list (fr->parts, out);
	g_string_append (out, ")\n");
}

static void
match_all (GtkWidget *widget, FilterRule *fr)
{
	fr->grouping = FILTER_GROUP_ALL;
}

static void
match_any (GtkWidget *widget, FilterRule *fr)
{
	fr->grouping = FILTER_GROUP_ANY;
}

struct _part_data {
	FilterRule *fr;
	RuleContext *f;
	FilterPart *part;
	GtkWidget *partwidget, *container;
};

static void
option_activate (GtkMenuItem *item, struct _part_data *data)
{
	FilterPart *part = gtk_object_get_data (GTK_OBJECT (item), "part");
	FilterPart *newpart;
	
	/* dont update if we haven't changed */
	if (!strcmp (part->title, data->part->title))
		return;
	
	/* here we do a widget shuffle, throw away the old widget/rulepart,
	   and create another */
	if (data->partwidget)
		gtk_container_remove (GTK_CONTAINER (data->container), data->partwidget);
	
	newpart = filter_part_clone (part);
	filter_rule_replace_part (data->fr, data->part, newpart);
	gtk_object_unref (GTK_OBJECT (data->part));
	data->part = newpart;
	data->partwidget = filter_part_get_widget (newpart);
	if (data->partwidget)
		gtk_box_pack_start (GTK_BOX (data->container), data->partwidget, FALSE, FALSE, 0);
	
	gtk_object_set_data (GTK_OBJECT (data->container), "part", newpart);
}

static GtkWidget *
get_rule_part_widget (RuleContext *f, FilterPart *newpart, FilterRule *fr)
{
	FilterPart *part = NULL;
	GtkWidget *menu;
	GtkWidget *item;
	GtkWidget *omenu;
	GtkWidget *hbox;
	GtkWidget *p;
	int index = 0, current = 0;
	struct _part_data *data;
	
	data = g_malloc0 (sizeof (*data));
	data->fr = fr;
	data->f = f;
	data->part = newpart;
	
	hbox = gtk_hbox_new (FALSE, 0);
	/* only set to automatically clean up the memory */
	gtk_object_set_data_full (GTK_OBJECT (hbox), "data", data, g_free);
	
	p = filter_part_get_widget (newpart);
	
	data->partwidget = p;
	data->container = hbox;
	
	menu = gtk_menu_new ();
	/* sigh, this is a little ugly */
	while ((part = rule_context_next_part (f, part))) {
		item = gtk_menu_item_new_with_label (_(part->title));
		gtk_object_set_data (GTK_OBJECT (item), "part", part);
		gtk_signal_connect (GTK_OBJECT (item), "activate", option_activate, data);
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);
		if (!strcmp (newpart->title, part->title)) {
			current = index;
		}
		index++;
	}
	
	omenu = gtk_option_menu_new ();
	gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (omenu), current);
	gtk_widget_show (omenu);
	
	gtk_box_pack_start (GTK_BOX (hbox), omenu, FALSE, FALSE, 0);
	if (p) {
		gtk_box_pack_start (GTK_BOX (hbox), p, FALSE, FALSE, 0);
	}
	gtk_widget_show_all (hbox);
	
	return hbox;
}

struct _rule_data {
	FilterRule *fr;
	RuleContext *f;
	GtkWidget *parts;
};

static void
less_parts (GtkWidget *button, struct _rule_data *data)
{
	FilterPart *part;
	GtkWidget *rule;
	
	if (g_list_length (data->fr->parts) < 2)
		return;
	
	rule = gtk_object_get_data (GTK_OBJECT (button), "rule");
	part = gtk_object_get_data (GTK_OBJECT (rule), "part");
	
	/* remove the part from the list */
	filter_rule_remove_part (data->fr, part);
	gtk_object_unref (GTK_OBJECT (part));
	
	/* and from the display */
	gtk_container_remove (GTK_CONTAINER (data->parts), rule);
	gtk_container_remove (GTK_CONTAINER (data->parts), button);
}

static void
attach_rule (GtkWidget *rule, struct _rule_data *data, FilterPart *part, int row)
{
	GtkWidget *remove;
	GtkWidget *pixmap;
	
	gtk_table_attach (GTK_TABLE (data->parts), rule, 0, 1, row, row + 1,
			  GTK_EXPAND | GTK_FILL, 0, 0, 0);
	
	pixmap = gnome_stock_new_with_icon (GNOME_STOCK_PIXMAP_REMOVE);
	remove = gnome_pixmap_button (pixmap, _("Remove"));
	gtk_object_set_data (GTK_OBJECT (remove), "rule", rule);
	gtk_object_set_data (GTK_OBJECT (rule), "part", part);
	/*gtk_button_set_relief (GTK_BUTTON (remove), GTK_RELIEF_NONE);*/
	gtk_signal_connect (GTK_OBJECT (remove), "clicked", less_parts, data);
	gtk_table_attach (GTK_TABLE (data->parts), remove, 1, 2, row, row + 1,
			  0, 0, 0, 0);
	gtk_widget_show (remove);
}

static void
more_parts (GtkWidget *button, struct _rule_data *data)
{
	FilterPart *new;
	
	/* first make sure that the last part is ok */
	if (data->fr->parts) {
		FilterPart *part;
		GList *l;
		
		l = g_list_last (data->fr->parts);
		part = l->data;
		if (!filter_part_validate (part))
			return;
	}
	
	/* create a new rule entry, use the first type of rule */
	new = rule_context_next_part (data->f, NULL);
	if (new) {
		GtkWidget *w;
		int rows;
		
		new = filter_part_clone (new);
		filter_rule_add_part (data->fr, new);
		w = get_rule_part_widget (data->f, new, data->fr);
		
		rows = GTK_TABLE (data->parts)->nrows;
		gtk_table_resize (GTK_TABLE (data->parts), rows + 1, 2);
		attach_rule (w, data, new, rows);
	}
}

static void
name_changed (GtkEntry *entry, FilterRule *fr)
{
	g_free (fr->name);
	fr->name = e_utf8_gtk_entry_get_text (entry);
}

GtkWidget *
filter_rule_get_widget (FilterRule *fr, struct _RuleContext *f)
{
	return ((FilterRuleClass *) ((GtkObject *) fr)->klass)->get_widget(fr, f);
}

static void
grab_focus (GtkWidget *entry, gpointer data)
{
	gtk_widget_grab_focus (entry);
}

static GtkWidget *
get_widget (FilterRule *fr, struct _RuleContext *f)
{
	GtkWidget *vbox, *parts, *inframe;
	GtkWidget *hbox;
	GtkWidget *add, *pixmap;
	GtkWidget *w;
	GtkWidget *menu, *item, *omenu;
	GtkWidget *frame;
	GtkWidget *name;
	GtkWidget *label;
	GtkWidget *scrolledwindow;
	GtkObject *hadj, *vadj;
	GList *l;
	FilterPart *part;
	struct _rule_data *data;
	gint rows, i = 0;
	
	/* this stuff should probably be a table, but the
	   rule parts need to be a vbox */
	vbox = gtk_vbox_new (FALSE, 3);
	
	label = gtk_label_new (_("Rule name: "));
	name = gtk_entry_new ();
	
	if (!fr->name) {
		fr->name = g_strdup (_("Untitled"));
		gtk_entry_set_text (GTK_ENTRY (name), fr->name);
		/* FIXME: do we want the following code in the future? */
		/*gtk_editable_select_region (GTK_EDITABLE (name), 0, -1);*/
	} else {
		e_utf8_gtk_entry_set_text (GTK_ENTRY (name), fr->name);
	}
	
	/* evil kludgy hack because gtk sucks */
	gtk_signal_connect (GTK_OBJECT (name), "realize", grab_focus, name);
	
	hbox = gtk_hbox_new (FALSE, 3);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), name, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (name), "changed", name_changed, fr);
	
	frame = gtk_frame_new (_("If"));
	inframe = gtk_vbox_new (FALSE, 3);
	gtk_container_add (GTK_CONTAINER (frame), inframe);
	
	/* this is the parts table, it should probably be inside a scrolling list */
	rows = g_list_length (fr->parts);
	parts = gtk_table_new (rows, 2, FALSE);
	
	/* data for the parts part of the display */
	data = g_malloc0 (sizeof (*data));
	data->f = f;
	data->fr = fr;
	data->parts = parts;
	
	/* only set to automatically clean up the memory */
	gtk_object_set_data_full (GTK_OBJECT (vbox), "data", data, g_free);
	
	hbox = gtk_hbox_new (FALSE, 3);
	label = gtk_label_new (_("Execute actions"));
	
	menu = gtk_menu_new ();
	
	item = gtk_menu_item_new_with_label (_("if all criteria are met"));
	gtk_signal_connect (GTK_OBJECT (item), "activate", match_all, fr);
	gtk_menu_append (GTK_MENU (menu), item);
	gtk_widget_show (item);
	
	item = gtk_menu_item_new_with_label (_("if any criteria are met"));
	gtk_signal_connect (GTK_OBJECT (item), "activate", match_any, fr);
	gtk_menu_append (GTK_MENU (menu), item);
	gtk_widget_show (item);
	
	omenu = gtk_option_menu_new ();
	gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (omenu), fr->grouping == FILTER_GROUP_ALL ? 0 : 1);
	gtk_widget_show (omenu);
	
	pixmap = gnome_stock_new_with_icon (GNOME_STOCK_PIXMAP_ADD);
	add = gnome_pixmap_button (pixmap, _("Add criterion"));
	gtk_signal_connect (GTK_OBJECT (add), "clicked", more_parts, data);
	gtk_box_pack_start (GTK_BOX (hbox), add, FALSE, FALSE, 3);
	
	gtk_box_pack_end (GTK_BOX (hbox), omenu, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	
	gtk_box_pack_start (GTK_BOX (inframe), hbox, FALSE, FALSE, 0);
	
	l = fr->parts;
	while (l) {
		part = l->data;
		d(printf ("adding rule %s\n", part->title));
		w = get_rule_part_widget (f, part, fr);
		attach_rule (w, data, part, i++);
		l = g_list_next (l);
	}
	
	hadj = gtk_adjustment_new (0.0, 0.0, 1.0, 1.0, 1.0, 1.0);
	vadj = gtk_adjustment_new (0.0, 0.0, 1.0, 1.0, 1.0, 1.0);
	scrolledwindow = gtk_scrolled_window_new (GTK_ADJUSTMENT (hadj), GTK_ADJUSTMENT (vadj));
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolledwindow), parts);
	
	gtk_box_pack_start (GTK_BOX (inframe), scrolledwindow, TRUE, TRUE, 0);
	
	/*gtk_box_pack_start (GTK_BOX (inframe), parts, FALSE, FALSE, 3); */
	
	gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 0);
	
	gtk_widget_show_all (vbox);
	
	return vbox;
}

FilterRule *
filter_rule_next_list (GList * l, FilterRule * last, const char *source)
{
	GList *node = l;
	
	if (last != NULL) {
		node = g_list_find (node, last);
		if (node == NULL)
			node = l;
		else
			node = g_list_next (node);
	}
	
	if (source) {
		while (node) {
			FilterRule *rule = node->data;
			
			if (rule->source && strcmp (rule->source, source) == 0)
				break;
			node = g_list_next (node);
		}
	}
	
	if (node)
		return node->data;
	
	return NULL;
}

FilterRule *
filter_rule_find_list (GList * l, const char *name, const char *source)
{
	while (l) {
		FilterRule *rule = l->data;
		
		if (strcmp (rule->name, name) == 0)
			if (source == NULL || (rule->source != NULL && strcmp (rule->source, source) == 0))
				return rule;
		l = g_list_next (l);
	}
	
	return NULL;
}

#ifdef FOR_TRANSLATIONS_ONLY

static char *list[] = {
  N_("incoming"), N_("outgoing")
};
#endif
