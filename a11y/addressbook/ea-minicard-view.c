/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * ea-minicard-view.c
 * Copyright (C) 2000  Ximian, Inc.
 * Author:  Leon Zhang < leon.zhang@sun.com>
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
#include <libgnome/gnome-i18n.h>
#include <string.h>
#include "ea-minicard.h"
#include "ea-minicard-view.h"

static G_CONST_RETURN gchar* ea_minicard_view_get_name (AtkObject *accessible);
static G_CONST_RETURN gchar* ea_minicard_view_get_description (AtkObject *accessible);

static void ea_minicard_view_class_init (EaMinicardViewClass *klass);

static gpointer parent_class = NULL;

GType
ea_minicard_view_get_type (void)
{
	static GType type = 0;
	AtkObjectFactory *factory;
	GTypeQuery query;
	GType derived_atk_type;

	if (!type) {
		static  GTypeInfo tinfo =  {
			sizeof (EaMinicardViewClass),
			(GBaseInitFunc) NULL,  /* base_init */
			(GBaseFinalizeFunc) NULL,  /* base_finalize */
			(GClassInitFunc) ea_minicard_view_class_init,
			(GClassFinalizeFunc) NULL, /* class_finalize */
			NULL,	   /* class_data */
			sizeof (EaMinicardView),
			0,	     /* n_preallocs */
			(GInstanceInitFunc) NULL, /* instance init */
			NULL /* value table */
		};

		/*
		 * Figure out the size of the class and instance
		 * we are run-time deriving from (GailWidget, in this case) */

		factory = atk_registry_get_factory (atk_get_default_registry (),
							GNOME_TYPE_CANVAS_GROUP);
		derived_atk_type = atk_object_factory_get_accessible_type (factory);
		g_type_query (derived_atk_type, &query);

		tinfo.class_size = query.class_size;
		tinfo.instance_size = query.instance_size;

		type = g_type_register_static (derived_atk_type,
						"EaMinicardView", &tinfo, 0);
	}

	return type;
}

static void
ea_minicard_view_class_init (EaMinicardViewClass *klass)
{
	AtkObjectClass *class = ATK_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	class->get_name = ea_minicard_view_get_name;
	class->get_description = ea_minicard_view_get_description;
}

static G_CONST_RETURN gchar*
ea_minicard_view_get_name (AtkObject *accessible)
{
	static gchar name[100];
	GString *new_str = g_string_new (NULL);
	gchar str[10];
	EReflow *reflow;

	g_return_val_if_fail (EA_IS_MINICARD_VIEW(accessible), NULL);
	memset (name, '\0', 100);
	memset (str, '\0', 10);

	reflow = E_REFLOW(atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE(accessible)));
	sprintf (str, "%d", reflow->count);
	g_string_append (new_str, _("current addressbook folder "));
	g_string_append (new_str, (reflow->count) > 1 ? _("have ") : _("has "));
	g_string_append (new_str, str);
	g_string_append (new_str, (reflow->count) > 1 ? _(" cards") : _(" card"));

	strcpy (name, new_str->str);
	g_string_free (new_str, TRUE);

	return name;
}

static G_CONST_RETURN gchar*
ea_minicard_view_get_description (AtkObject *accessible)
{
	g_return_val_if_fail (EA_IS_MINICARD_VIEW(accessible), NULL);
	if (accessible->description)
		return accessible->description;

	return _("evolution addressbook");
}

AtkObject* 
ea_minicard_view_new (GObject *obj)
{
	GObject *object;
	AtkObject *accessible;

	g_return_val_if_fail (E_IS_MINICARD_VIEW(obj), NULL);
	object = g_object_new (EA_TYPE_MINICARD_VIEW, NULL);
	accessible = ATK_OBJECT (object);
	atk_object_initialize (accessible, obj);
	accessible->role = ATK_ROLE_PANEL;
	return accessible;
}
