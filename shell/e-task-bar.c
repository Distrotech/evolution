/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-task-bar.c
 *
 * Copyright (C) 2001  Ximian, Inc.
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
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-task-bar.h"

#include <gal/util/e-util.h>


#define PARENT_TYPE gtk_hbox_get_type ()
static GtkHBoxClass *parent_class = NULL;


/* WARNING: Ugly hack starts here.  */

#define MAX_ACTIVITIES_PER_COMPONENT 2

static void
reduce_displayed_activities_per_component (ETaskBar *task_bar)
{
	GHashTable *component_ids_hash;
	GtkBox *box;
	GList *p;

	component_ids_hash = g_hash_table_new (g_str_hash, g_str_equal);

	box = GTK_BOX (task_bar);

	for (p = box->children; p != NULL; p = p->next) {
		GtkBoxChild *child;
		const char *component_id;
		void *hash_item;

		child = (GtkBoxChild *) p->data;
		component_id = e_task_widget_get_component_id (E_TASK_WIDGET (child->widget));

		hash_item = g_hash_table_lookup (component_ids_hash, component_id);

		if (hash_item == NULL) {
			gtk_widget_show (child->widget);
			g_hash_table_insert (component_ids_hash, (void *) component_id, GINT_TO_POINTER (1));
		} else {
			int num_items;

			num_items = GPOINTER_TO_INT (hash_item);
			g_assert (num_items <= MAX_ACTIVITIES_PER_COMPONENT);

			if (num_items == MAX_ACTIVITIES_PER_COMPONENT) {
				gtk_widget_hide (child->widget);
			} else {
				num_items ++;
				gtk_widget_show (child->widget);
				g_hash_table_insert (component_ids_hash, (void *) component_id, GINT_TO_POINTER (num_items));
			}
		}
	}

	g_hash_table_destroy (component_ids_hash);
}


static void
class_init (GtkObjectClass *object_class)
{
	parent_class = g_type_class_ref(PARENT_TYPE);
}

static void
init (ETaskBar *task_bar)
{
	/* Nothing to do here.  */
}


void
e_task_bar_construct (ETaskBar *task_bar)
{
	g_return_if_fail (task_bar != NULL);
	g_return_if_fail (E_IS_TASK_BAR (task_bar));

	/* Nothing to do here.  */
}

GtkWidget *
e_task_bar_new (void)
{
	ETaskBar *task_bar;

	task_bar = g_object_new (e_task_bar_get_type (), NULL);
	e_task_bar_construct (task_bar);

	return GTK_WIDGET (task_bar);
}

void
e_task_bar_prepend_task (ETaskBar *task_bar,
			 ETaskWidget *task_widget)
{
	GtkBoxChild *child_info;
	GtkBox *box;

	g_return_if_fail (task_bar != NULL);
	g_return_if_fail (E_IS_TASK_BAR (task_bar));
	g_return_if_fail (task_widget != NULL);
	g_return_if_fail (E_IS_TASK_WIDGET (task_widget));

	/* Hah hah.  GTK+ sucks.  This is adapted from `gtkhbox.c'.  */

	child_info = g_new (GtkBoxChild, 1);
	child_info->widget = GTK_WIDGET (task_widget);
	child_info->padding = 0;
	child_info->expand = TRUE;
	child_info->fill = TRUE;
	child_info->pack = GTK_PACK_START;

	box = GTK_BOX (task_bar);

	box->children = g_list_prepend (box->children, child_info);

	gtk_widget_set_parent (GTK_WIDGET (task_widget), GTK_WIDGET (task_bar));

	if (GTK_WIDGET_REALIZED (task_bar))
		gtk_widget_realize (GTK_WIDGET (task_widget));

	if (GTK_WIDGET_VISIBLE (task_bar) && GTK_WIDGET_VISIBLE (task_widget)) {
		if (GTK_WIDGET_MAPPED (task_bar))
			gtk_widget_map (GTK_WIDGET (task_widget));
		gtk_widget_queue_resize (GTK_WIDGET (task_widget));
	}

	reduce_displayed_activities_per_component (task_bar);
}

void
e_task_bar_remove_task (ETaskBar *task_bar,
			int n)
{
	ETaskWidget *task_widget;

	g_return_if_fail (task_bar != NULL);
	g_return_if_fail (E_IS_TASK_BAR (task_bar));
	g_return_if_fail (n >= 0);

	task_widget = e_task_bar_get_task_widget (task_bar, n);
	gtk_widget_destroy (GTK_WIDGET (task_widget));

	reduce_displayed_activities_per_component (task_bar);
}
	
ETaskWidget *
e_task_bar_get_task_widget (ETaskBar *task_bar,
			    int n)
{
	GtkBoxChild *child_info;

	g_return_val_if_fail (task_bar != NULL, NULL);
	g_return_val_if_fail (E_IS_TASK_BAR (task_bar), NULL);

	child_info = (GtkBoxChild *) g_list_nth (GTK_BOX (task_bar)->children, n)->data;

	return E_TASK_WIDGET (child_info->widget);
}


E_MAKE_TYPE (e_task_bar, "ETaskBar", ETaskBar, class_init, init, PARENT_TYPE)
