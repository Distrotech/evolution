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
#include <gtk/gtkbox.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkspinbutton.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

#include "score-rule.h"

static xmlNodePtr xml_encode(FilterRule *);
static int xml_decode(FilterRule *, xmlNodePtr, struct _RuleContext *f);
/*static void build_code(FilterRule *, GString *out);*/
static GtkWidget *get_widget(FilterRule *fr, struct _RuleContext *f);

static void score_rule_class_init	(ScoreRuleClass *class);
static void score_rule_init	(ScoreRule *gspaper);
static void score_rule_finalise	(GtkObject *obj);

#define _PRIVATE(x) (((ScoreRule *)(x))->priv)

struct _ScoreRulePrivate {
};

static FilterRuleClass *parent_class;

enum {
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

guint
score_rule_get_type (void)
{
	static guint type = 0;
	
	if (!type) {
		GtkTypeInfo type_info = {
			"ScoreRule",
			sizeof(ScoreRule),
			sizeof(ScoreRuleClass),
			(GtkClassInitFunc)score_rule_class_init,
			(GtkObjectInitFunc)score_rule_init,
			(GtkArgSetFunc)NULL,
			(GtkArgGetFunc)NULL
		};
		
		type = gtk_type_unique (filter_rule_get_type (), &type_info);
	}
	
	return type;
}

static void
score_rule_class_init (ScoreRuleClass *class)
{
	GtkObjectClass *object_class;
	FilterRuleClass *rule_class = (FilterRuleClass *)class;
	
	object_class = (GtkObjectClass *)class;
	parent_class = gtk_type_class (filter_rule_get_type ());
	
	object_class->finalize = score_rule_finalise;
	
	/* override methods */
	rule_class->xml_encode = xml_encode;
	rule_class->xml_decode = xml_decode;
/*	rule_class->build_code = build_code;*/
	rule_class->get_widget = get_widget;
	
	/* signals */
	
	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);
}

static void
score_rule_init (ScoreRule *o)
{
	o->priv = g_malloc0 (sizeof (*o->priv));
}

static void
score_rule_finalise (GtkObject *obj)
{
	ScoreRule *o = (ScoreRule *)obj;
	
	o = o;
	
        ((GtkObjectClass *)(parent_class))->finalize (obj);
}

/**
 * score_rule_new:
 *
 * Create a new ScoreRule object.
 * 
 * Return value: A new #ScoreRule object.
 **/
ScoreRule *
score_rule_new (void)
{
	ScoreRule *o = (ScoreRule *)gtk_type_new (score_rule_get_type ());
	return o;
}

static xmlNodePtr
xml_encode (FilterRule *fr)
{
	ScoreRule *sr = (ScoreRule *)fr;
	xmlNodePtr node, value;
	char number[16];
	
	node = ((FilterRuleClass *)(parent_class))->xml_encode (fr);
	sprintf (number, "%d", sr->score);
	value = xmlNewNode (NULL, "score");
	xmlSetProp (value, "value", number);
	xmlAddChild (node, value);
	
	return node;
}

static int
xml_decode (FilterRule *fr, xmlNodePtr node, struct _RuleContext *f)
{
	ScoreRule *sr = (ScoreRule *)fr;
	xmlNodePtr value;
	int result;
	char *str;
	
	result = ((FilterRuleClass *)(parent_class))->xml_decode (fr, node, f);
	if (result != 0)
		return result;
	
	value = node->childs;
	while (value) {
		if (!strcmp (value->name, "score")) {
			str = xmlGetProp (value, "value");
			sscanf (str, "%d", &sr->score);
			xmlFree (str);
			
			/* score range is -3 to +3 */
			if (sr->score > 3)
				sr->score = 3;
			else if (sr->score < -3)
				sr->score = -3;
		}
		value = value->next;
	}
	
	return 0;
}

/*static void build_code(FilterRule *fr, GString *out)
{
}*/

static void
spin_changed (GtkAdjustment *adj, ScoreRule *sr)
{
	sr->score = adj->value;
}

static GtkWidget *
get_widget (FilterRule *fr, struct _RuleContext *f)
{
	GtkWidget *widget;
	GtkWidget *frame;
	GtkWidget *label;
	GtkWidget *hbox;
	GtkAdjustment *adj;
	ScoreRule *sr = (ScoreRule *)fr;
	GtkWidget *spin;
	
        widget = ((FilterRuleClass *)(parent_class))->get_widget (fr, f);
	
	frame = gtk_frame_new (_("Score"));
	hbox = gtk_hbox_new (FALSE, 3);
	label = gtk_label_new (_("Score"));
	
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 3);
	adj = (GtkAdjustment *)gtk_adjustment_new ((float) sr->score, -3.0, 3.0, 1.0, 1.0, 1.0);
	gtk_signal_connect (GTK_OBJECT (adj), "value_changed", spin_changed, sr);
	
	spin = gtk_spin_button_new (adj, 1.0, 0);
	gtk_box_pack_start (GTK_BOX (hbox), spin, FALSE, FALSE, 3);
	gtk_container_add (GTK_CONTAINER (frame), hbox);
	
	gtk_widget_show_all (frame);
	
	gtk_box_pack_start (GTK_BOX (widget), frame, FALSE, FALSE, 3);
	
	return widget;
}
