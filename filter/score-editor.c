/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2001-2002 Ximian Inc.
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>

#include "score-editor.h"
#include "score-rule.h"

#define d(x)

static FilterRule * create_rule(RuleEditor *re);

static void score_editor_class_init (ScoreEditorClass *klass);
static void score_editor_init (ScoreEditor *se);
static void score_editor_finalise (GObject *obj);


static RuleEditorClass *parent_class = NULL;


GtkType
score_editor_get_type (void)
{
	static GtkType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (ScoreEditorClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) score_editor_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (ScoreEditor),
			0,    /* n_preallocs */
			(GInstanceInitFunc) score_editor_init,
		};
		
		type = g_type_register_static (RULE_TYPE_EDITOR, "ScoreEditor", &info, 0);
	}
	
	return type;
}

static void
score_editor_class_init (ScoreEditorClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	RuleEditorClass *re_class = (RuleEditorClass *) klass;
	
	parent_class = g_type_class_ref(rule_editor_get_type ());
	
	gobject_class->finalize = score_editor_finalise;
	
	/* override methods */
	re_class->create_rule = create_rule;
}

static void
score_editor_init (ScoreEditor *se)
{
	;
}

static void
score_editor_finalise (GObject *obj)
{
        G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/**
 * score_editor_new:
 *
 * Create a new ScoreEditor object.
 * 
 * Return value: A new #ScoreEditor object.
 **/
ScoreEditor *
score_editor_new (ScoreContext *sc)
{
	ScoreEditor *se = (ScoreEditor *) g_object_new (SCORE_TYPE_EDITOR, NULL);
	GladeXML *gui;
	GtkWidget *w;
	
	gui = glade_xml_new (FILTER_GLADEDIR "/filter.glade", "rule_editor", NULL);
	rule_editor_construct ((RuleEditor *) se, (RuleContext *) sc, gui, NULL);
	
        w = glade_xml_get_widget (gui, "rule_frame");
	gtk_frame_set_label ((GtkFrame *) w, _("Score Rules"));
	
	g_object_unref (gui);
	
	return se;
}

static FilterRule *
create_rule (RuleEditor *re)
{
	FilterRule *rule = filter_rule_new ();
	FilterPart *part;
	
	/* create a rule with 1 part in it */
	rule = (FilterRule *) score_rule_new ();
	part = rule_context_next_part (re->context, NULL);
	filter_rule_add_part (rule, filter_part_clone (part));
	
	return rule;
}
