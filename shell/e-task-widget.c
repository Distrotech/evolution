/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-task-widget.c
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

#include "e-task-widget.h"

#include <gtk/gtkframe.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkpixmap.h>
#include <gtk/gtktooltips.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

#include <gal/util/e-util.h>


#define SPACING 2

#define PARENT_TYPE (gtk_event_box_get_type ())
static GtkEventBoxClass *parent_class = NULL;

struct _ETaskWidgetPrivate {
	char *component_id;

	GtkTooltips *tooltips;

	GdkPixbuf *icon_pixbuf;
	GtkWidget *label;
	GtkWidget *pixmap;
};


/* GtkObject methods.  */

static void
impl_destroy (GtkObject *object)
{
	ETaskWidget *task_widget;
	ETaskWidgetPrivate *priv;

	task_widget = E_TASK_WIDGET (object);
	priv = task_widget->priv;

	g_free (priv->component_id);

	gtk_object_unref (GTK_OBJECT (priv->tooltips));

	gdk_pixbuf_unref (priv->icon_pixbuf);

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


static void
class_init (GtkObjectClass *object_class)
{
	parent_class = gtk_type_class (PARENT_TYPE);

	object_class->destroy = impl_destroy;
}

static void
init (ETaskWidget *task_widget)
{
	ETaskWidgetPrivate *priv;

	priv = g_new (ETaskWidgetPrivate, 1);

	priv->component_id = NULL;
	priv->tooltips     = NULL;
	priv->icon_pixbuf  = NULL;
	priv->label        = NULL;
	priv->pixmap       = NULL;

	task_widget->priv = priv;
}


void
e_task_widget_construct (ETaskWidget *task_widget,
			 GdkPixbuf *icon_pixbuf,
			 const char *component_id,
			 const char *information)
{
	ETaskWidgetPrivate *priv;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	GtkWidget *box;
	GtkWidget *frame;

	g_return_if_fail (task_widget != NULL);
	g_return_if_fail (E_IS_TASK_WIDGET (task_widget));
	g_return_if_fail (icon_pixbuf != NULL);
	g_return_if_fail (component_id != NULL);
	g_return_if_fail (information != NULL);

	priv = task_widget->priv;

	priv->component_id = g_strdup (component_id);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (task_widget), frame);
	gtk_widget_show (frame);

	box = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (frame), box);
	gtk_widget_show (box);

	gtk_widget_set_usize (box, 1, -1);

	priv->icon_pixbuf = gdk_pixbuf_ref (icon_pixbuf);

	gdk_pixbuf_render_pixmap_and_mask (icon_pixbuf, &pixmap, &mask, 128);

	priv->pixmap = gtk_pixmap_new (pixmap, mask);
	gtk_widget_show (priv->pixmap);
	gtk_box_pack_start (GTK_BOX (box), priv->pixmap, FALSE, TRUE, 0);

	priv->label = gtk_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (priv->label), 0.0, 0.5);
	gtk_widget_show (priv->label);
	gtk_box_pack_start (GTK_BOX (box), priv->label, TRUE, TRUE, 0);

	gdk_pixmap_unref (pixmap);
	gdk_bitmap_unref (mask);

	priv->tooltips = gtk_tooltips_new ();

	e_task_widget_update (task_widget, information, -1.0);
}

GtkWidget *
e_task_widget_new (GdkPixbuf *icon_pixbuf,
		   const char *component_id,
		   const char *information)
{
	ETaskWidget *task_widget;

	g_return_val_if_fail (icon_pixbuf != NULL, NULL);
	g_return_val_if_fail (information != NULL, NULL);

	task_widget = gtk_type_new (e_task_widget_get_type ());
	e_task_widget_construct (task_widget, icon_pixbuf, component_id, information);

	return GTK_WIDGET (task_widget);
}


void
e_task_widget_update (ETaskWidget *task_widget,
		      const char *information,
		      double completion)
{
	ETaskWidgetPrivate *priv;
	char *text;

	g_return_if_fail (task_widget != NULL);
	g_return_if_fail (E_IS_TASK_WIDGET (task_widget));
	g_return_if_fail (information != NULL);

	priv = task_widget->priv;

	if (completion < 0.0) {
		text = g_strdup_printf (_("%s (...)"), information);
	} else {
		int percent_complete;

		percent_complete = (int) (completion * 100.0 + .5);
		text = g_strdup_printf (_("%s (%d%% complete)"), information, percent_complete);
	}

	gtk_label_set_text (GTK_LABEL (priv->label), text);

	gtk_tooltips_set_tip (priv->tooltips, GTK_WIDGET (task_widget), text, NULL);

	g_free (text);
}

void
e_task_wiget_alert (ETaskWidget *task_widget)
{
	g_return_if_fail (task_widget != NULL);
	g_return_if_fail (E_IS_TASK_WIDGET (task_widget));
}

void
e_task_wiget_unalert (ETaskWidget *task_widget)
{
	g_return_if_fail (task_widget != NULL);
	g_return_if_fail (E_IS_TASK_WIDGET (task_widget));
}
	

const char *
e_task_widget_get_component_id  (ETaskWidget *task_widget)
{
	g_return_val_if_fail (task_widget != NULL, NULL);
	g_return_val_if_fail (E_IS_TASK_WIDGET (task_widget), NULL);

	return task_widget->priv->component_id;
}


E_MAKE_TYPE (e_task_widget, "ETaskWidget", ETaskWidget, class_init, init, PARENT_TYPE)
