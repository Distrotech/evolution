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

#include <gtk/gtkobject.h>

#include "vfolder-context.h"
#include "vfolder-rule.h"

static void vfolder_context_class_init	(VfolderContextClass *class);
static void vfolder_context_init	(VfolderContext *gspaper);
static void vfolder_context_finalise	(GtkObject *obj);

#define _PRIVATE(x) (((VfolderContext *)(x))->priv)

struct _VfolderContextPrivate {
};

static RuleContextClass *parent_class;

enum {
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

guint
vfolder_context_get_type (void)
{
	static guint type = 0;
	
	if (!type) {
		GtkTypeInfo type_info = {
			"VfolderContext",
			sizeof(VfolderContext),
			sizeof(VfolderContextClass),
			(GtkClassInitFunc)vfolder_context_class_init,
			(GtkObjectInitFunc)vfolder_context_init,
			(GtkArgSetFunc)NULL,
			(GtkArgGetFunc)NULL
		};
		
		type = gtk_type_unique(rule_context_get_type (), &type_info);
	}
	
	return type;
}

static void
vfolder_context_class_init (VfolderContextClass *class)
{
	GtkObjectClass *object_class;
	
	object_class = (GtkObjectClass *)class;
	parent_class = gtk_type_class(rule_context_get_type ());

	object_class->finalize = vfolder_context_finalise;
	/* override methods */

	/* signals */

	gtk_object_class_add_signals(object_class, signals, LAST_SIGNAL);
}

static void
vfolder_context_init (VfolderContext *o)
{
	o->priv = g_malloc0(sizeof(*o->priv));

	rule_context_add_part_set((RuleContext *)o, "partset", filter_part_get_type(),
				  rule_context_add_part, rule_context_next_part);

	rule_context_add_rule_set((RuleContext *)o, "ruleset", vfolder_rule_get_type(),
				  rule_context_add_rule, rule_context_next_rule);
}

static void
vfolder_context_finalise(GtkObject *obj)
{
	VfolderContext *o = (VfolderContext *)obj;

	o = o;

        ((GtkObjectClass *)(parent_class))->finalize(obj);
}

/**
 * vfolder_context_new:
 *
 * Create a new VfolderContext object.
 * 
 * Return value: A new #VfolderContext object.
 **/
VfolderContext *
vfolder_context_new(void)
{
	VfolderContext *o = (VfolderContext *)gtk_type_new(vfolder_context_get_type ());
	return o;
}
