/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-combo-button.c
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

#include "e-combo-button.h"

#include <gtk/gtkarrow.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkpixmap.h>
#include <gtk/gtksignal.h>

#include <gal/util/e-util.h>


struct _EComboButtonPrivate {
	GdkPixbuf *icon;

	GtkWidget *icon_pixmap;
	GtkWidget *label;
	GtkWidget *arrow_pixmap;
	GtkWidget *hbox;

	GtkMenu *menu;

	gboolean menu_popped_up;
};


#define SPACING 2


#define PARENT_TYPE gtk_button_get_type ()
static GtkButtonClass *parent_class = NULL;

enum {
	ACTIVATE_DEFAULT,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


/* Utility functions.  */

static GtkWidget *
create_pixmap_widget_from_pixbuf (GdkPixbuf *pixbuf)
{
	GtkWidget *pixmap_widget;
	GdkPixmap *pixmap;
	GdkBitmap *mask;

	gdk_pixbuf_render_pixmap_and_mask (pixbuf, &pixmap, &mask, 128);

	pixmap_widget = gtk_pixmap_new (pixmap, mask);

	gdk_pixmap_unref (pixmap);
	g_object_unref (mask);

	return pixmap_widget;
}

static GtkWidget *
create_empty_pixmap_widget (void)
{
	GtkWidget *pixmap_widget;
	GdkPixbuf *pixbuf;

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);

	pixmap_widget = create_pixmap_widget_from_pixbuf (pixbuf);

	g_object_unref (pixbuf);

	return pixmap_widget;
}

static void
set_icon (EComboButton *combo_button,
	  GdkPixbuf *pixbuf)
{
	EComboButtonPrivate *priv;
	GdkPixmap *pixmap;
	GdkBitmap *mask;

	priv = combo_button->priv;

	if (priv->icon != NULL)
		g_object_unref (priv->icon);

	if (pixbuf == NULL) {
		priv->icon = NULL;
		gtk_widget_hide (priv->icon_pixmap);
		return;
	}

	priv->icon = g_object_ref (pixbuf);

	gdk_pixbuf_render_pixmap_and_mask (pixbuf, &pixmap, &mask, 128);
	gtk_pixmap_set (GTK_PIXMAP (priv->icon_pixmap), pixmap, mask);

	gtk_widget_show (priv->icon_pixmap);

	gdk_pixmap_unref (pixmap);
	gdk_pixmap_unref (mask);
}


/* Paint the borders.  */

static void
paint (EComboButton *combo_button,
       GdkRectangle *area)
{
	EComboButtonPrivate *priv = combo_button->priv;
	GtkShadowType shadow_type;
	int separator_x;

	gdk_window_set_back_pixmap (GTK_WIDGET (combo_button)->window, NULL, TRUE);
	gdk_window_clear_area (GTK_WIDGET (combo_button)->window,
			       area->x, area->y,
			       area->width, area->height);

	/* Only paint the outline if we are in prelight state.  */
	if (GTK_WIDGET_STATE (combo_button) != GTK_STATE_PRELIGHT
	    && GTK_WIDGET_STATE (combo_button) != GTK_STATE_ACTIVE)
		return;

	separator_x = (priv->label->allocation.width
		       + priv->label->allocation.x
		       + priv->arrow_pixmap->allocation.x) / 2;

	if (GTK_WIDGET_STATE (combo_button) == GTK_STATE_ACTIVE)
		shadow_type = GTK_SHADOW_IN;
	else
		shadow_type = GTK_SHADOW_OUT;

	gtk_paint_box (GTK_WIDGET (combo_button)->style,
		       GTK_WIDGET (combo_button)->window,
		       GTK_STATE_PRELIGHT,
		       shadow_type,
		       area,
		       GTK_WIDGET (combo_button),
		       "button",
		       0,
		       0,
		       separator_x,
		       GTK_WIDGET (combo_button)->allocation.height);

	gtk_paint_box (GTK_WIDGET (combo_button)->style,
		       GTK_WIDGET (combo_button)->window,
		       GTK_STATE_PRELIGHT,
		       shadow_type,
		       area,
		       GTK_WIDGET (combo_button),
		       "button",
		       separator_x,
		       0,
		       GTK_WIDGET (combo_button)->allocation.width - separator_x,
		       GTK_WIDGET (combo_button)->allocation.height);
}


/* Callbacks for the associated menu.  */

static void
menu_detacher (GtkWidget *widget,
	       GtkMenu *menu)
{
	EComboButton *combo_button;
	EComboButtonPrivate *priv;

	combo_button = E_COMBO_BUTTON (widget);
	priv = combo_button->priv;

	gtk_signal_disconnect_by_data (GTK_OBJECT (menu), combo_button);
	priv->menu = NULL;
}

static void
menu_deactivate_callback (GtkMenuShell *menu_shell,
			  void *data)
{
	EComboButton *combo_button;
	EComboButtonPrivate *priv;

	combo_button = E_COMBO_BUTTON (data);
	priv = combo_button->priv;

	priv->menu_popped_up = FALSE;

	GTK_BUTTON (combo_button)->button_down = FALSE;
	GTK_BUTTON (combo_button)->in_button = FALSE;
	gtk_button_leave (GTK_BUTTON (combo_button));
	gtk_button_clicked (GTK_BUTTON (combo_button));
}

static void
menu_position_func (GtkMenu *menu,
		    gint *x_return,
		    gint *y_return,
		    gboolean *push_in,
		    void *data)
{
	EComboButton *combo_button;
	GtkAllocation *allocation;

	combo_button = E_COMBO_BUTTON (data);
	allocation = & (GTK_WIDGET (combo_button)->allocation);

	gdk_window_get_origin (GTK_WIDGET (combo_button)->window, x_return, y_return);

	*y_return += allocation->height;
}


/* GtkObject methods.  */

static void
impl_destroy (GtkObject *object)
{
	EComboButton *combo_button;
	EComboButtonPrivate *priv;

	combo_button = E_COMBO_BUTTON (object);
	priv = combo_button->priv;

	if (priv) {
		if (priv->arrow_pixmap != NULL) {
			gtk_widget_destroy (priv->arrow_pixmap);
			priv->arrow_pixmap = NULL;
		}
		
		if (priv->icon != NULL) {
			g_object_unref (priv->icon);
			priv->icon = NULL;
		}

		g_free (priv);
		combo_button->priv = NULL;
	}
	
	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* GtkWidget methods.  */

static int
impl_button_press_event (GtkWidget *widget,
			 GdkEventButton *event)
{
	EComboButton *combo_button;
	EComboButtonPrivate *priv;

	combo_button = E_COMBO_BUTTON (widget);
	priv = combo_button->priv;

	if (event->type == GDK_BUTTON_PRESS && 
	    (event->button == 1 || event->button == 3)) {
		GTK_BUTTON (widget)->button_down = TRUE;

		if (event->button == 3 || 
		    event->x >= priv->arrow_pixmap->allocation.x) {
			/* User clicked on the right side: pop up the menu.  */
			gtk_button_pressed (GTK_BUTTON (widget));

			priv->menu_popped_up = TRUE;
			gtk_menu_popup (GTK_MENU (priv->menu), NULL, NULL,
					menu_position_func, combo_button,
					event->button, event->time);
		} else {
			/* User clicked on the left side: just behave like a
			   normal button (i.e. not a toggle).  */
			gtk_button_pressed (GTK_BUTTON (widget));
		}
	}

	return TRUE;
}

static int
impl_leave_notify_event (GtkWidget *widget,
			 GdkEventCrossing *event)
{
	EComboButton *combo_button;
	EComboButtonPrivate *priv;

	combo_button = E_COMBO_BUTTON (widget);
	priv = combo_button->priv;

	/* This is to override the standard behavior of deactivating the button
	   when the pointer gets out of the widget, in the case in which we
	   have just popped up the menu.  Otherwise, the button would look as
	   inactive when the menu is popped up.  */
	if (! priv->menu_popped_up)
		return (* GTK_WIDGET_CLASS (parent_class)->leave_notify_event) (widget, event);

	return FALSE;
}

static int
impl_expose_event (GtkWidget *widget,
		   GdkEventExpose *event)
{
	GtkBin *bin;
	GdkEventExpose child_event;

	if (! GTK_WIDGET_DRAWABLE (widget))
		return FALSE;

	bin = GTK_BIN (widget);
      
	paint (E_COMBO_BUTTON (widget), &event->area);

	child_event = *event;
	if (bin->child && GTK_WIDGET_NO_WINDOW (bin->child) &&
	    gtk_widget_intersect (bin->child, &event->area, &child_event.area))
		gtk_container_propagate_expose (GTK_CONTAINER (widget), bin->child, &child_event);

	return FALSE;
}


/* GtkButton methods.  */

static void
impl_released (GtkButton *button)
{
	EComboButton *combo_button;
	EComboButtonPrivate *priv;

	combo_button = E_COMBO_BUTTON (button);
	priv = combo_button->priv;

	/* Massive cut & paste from GtkButton here...  The only change in
	   behavior here is that we want to emit ::activate_default when not
	   the menu hasn't been popped up.  */

	if (button->button_down) {
		int new_state;

		button->button_down = FALSE;

		if (button->in_button) {
			gtk_button_clicked (button);

			if (! priv->menu_popped_up)
				gtk_signal_emit (GTK_OBJECT (button), signals[ACTIVATE_DEFAULT]);
		}

		new_state = (button->in_button ? GTK_STATE_PRELIGHT : GTK_STATE_NORMAL);

		if (GTK_WIDGET_STATE (button) != new_state) {
			gtk_widget_set_state (GTK_WIDGET (button), new_state);

			/* We _draw () instead of queue_draw so that if the
			   operation blocks, the label doesn't vanish.  */
			gtk_widget_draw (GTK_WIDGET (button), NULL);
		}
	}
}


static void
class_init (GtkObjectClass *object_class)
{
	GtkWidgetClass *widget_class;
	GtkButtonClass *button_class;

	parent_class = g_type_class_ref(PARENT_TYPE);

	object_class->destroy = impl_destroy;

	widget_class = GTK_WIDGET_CLASS (object_class);
	widget_class->button_press_event = impl_button_press_event;
	widget_class->leave_notify_event = impl_leave_notify_event;
	widget_class->expose_event       = impl_expose_event;

	button_class = GTK_BUTTON_CLASS (object_class);
	button_class->released = impl_released;

	signals[ACTIVATE_DEFAULT] = gtk_signal_new ("activate_default",
						    GTK_RUN_FIRST,
						    GTK_CLASS_TYPE (object_class),
						    G_STRUCT_OFFSET (EComboButtonClass, activate_default),
						    gtk_marshal_NONE__NONE,
						    GTK_TYPE_NONE, 0);
}

static void
init (EComboButton *combo_button)
{
	EComboButtonPrivate *priv;

	priv = g_new (EComboButtonPrivate, 1);
	combo_button->priv = priv;

	priv->hbox = gtk_hbox_new (FALSE, SPACING);
	gtk_container_add (GTK_CONTAINER (combo_button), priv->hbox);
	gtk_widget_show (priv->hbox);

	priv->icon_pixmap = create_empty_pixmap_widget ();
	gtk_box_pack_start (GTK_BOX (priv->hbox), priv->icon_pixmap, TRUE, TRUE, 0);
	gtk_widget_show (priv->icon_pixmap);

	priv->label = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (priv->hbox), priv->label, TRUE, TRUE,
			    2 * GTK_WIDGET (combo_button)->style->xthickness);
	gtk_widget_show (priv->label);

	priv->arrow_pixmap = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_NONE);
	gtk_box_pack_start (GTK_BOX (priv->hbox), priv->arrow_pixmap, TRUE, TRUE,
			    GTK_WIDGET (combo_button)->style->xthickness);
	gtk_widget_show (priv->arrow_pixmap);

	priv->icon           = NULL;
	priv->menu           = NULL;
	priv->menu_popped_up = FALSE;
}


void
e_combo_button_construct (EComboButton *combo_button)
{
	EComboButtonPrivate *priv;

	g_return_if_fail (combo_button != NULL);
	g_return_if_fail (E_IS_COMBO_BUTTON (combo_button));

	priv = combo_button->priv;
	g_return_if_fail (priv->menu == NULL);

	GTK_WIDGET_UNSET_FLAGS (combo_button, GTK_CAN_FOCUS);

	gtk_button_set_relief (GTK_BUTTON (combo_button), GTK_RELIEF_NONE);
}

GtkWidget *
e_combo_button_new (void)
{
	EComboButton *new;

	new = gtk_type_new (e_combo_button_get_type ());
	e_combo_button_construct (new);

	return GTK_WIDGET (new);
}


void
e_combo_button_set_icon (EComboButton *combo_button,
			 GdkPixbuf *pixbuf)
{
	g_return_if_fail (combo_button != NULL);
	g_return_if_fail (E_IS_COMBO_BUTTON (combo_button));

	set_icon (combo_button, pixbuf);
}

void
e_combo_button_set_label (EComboButton *combo_button,
			  const char *label)
{
	EComboButtonPrivate *priv;

	g_return_if_fail (combo_button != NULL);
	g_return_if_fail (E_IS_COMBO_BUTTON (combo_button));
	g_return_if_fail (label != NULL);

	priv = combo_button->priv;

	if (label == NULL)
		label = "";

	gtk_label_parse_uline (GTK_LABEL (priv->label), label);
}

void
e_combo_button_set_menu (EComboButton *combo_button,
			 GtkMenu *menu)
{
	EComboButtonPrivate *priv;

	g_return_if_fail (combo_button != NULL);
	g_return_if_fail (E_IS_COMBO_BUTTON (combo_button));
	g_return_if_fail (menu != NULL);
	g_return_if_fail (GTK_IS_MENU (menu));

	priv = combo_button->priv;

	if (priv->menu != NULL)
		gtk_menu_detach (priv->menu);

	priv->menu = menu;
	if (menu == NULL)
		return;

	gtk_menu_attach_to_widget (menu, GTK_WIDGET (combo_button), menu_detacher);

	g_signal_connect((menu), "deactivate",
			    G_CALLBACK (menu_deactivate_callback),
			    combo_button);
}


E_MAKE_TYPE (e_combo_button, "EComboButton", EComboButton, class_init, init, PARENT_TYPE)
