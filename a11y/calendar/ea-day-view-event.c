/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* Evolution Accessibility: ea-day-view.c
 *
 * Copyright (C) 2003 Ximian, Inc.
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
 * Author: Bolian Yin <bolian.yin@sun.com> Sun Microsystem Inc., 2003
 *
 */

#include "ea-day-view-event.h"
#include <gal/e-text/e-text.h>

static void ea_day_view_event_class_init (EaDayViewEventClass *klass);

static G_CONST_RETURN gchar* ea_day_view_event_get_name (AtkObject *accessible);
static G_CONST_RETURN gchar* ea_day_view_event_get_description (AtkObject *accessible);
static AtkObject* ea_day_view_event_get_parent (AtkObject *accessible);
static AtkStateSet* ea_day_view_event_ref_state_set (AtkObject *accessible);
static gint ea_day_view_event_get_index_in_parent (AtkObject *accessible);

static void atk_component_interface_init (AtkComponentIface *iface);

static void     ea_day_view_event_get_extents    (AtkComponent    *component,
                                                  gint            *x,
                                                  gint            *y,
                                                  gint            *width,
                                                  gint            *height,
                                                  AtkCoordType    coord_type);

static void ea_day_view_event_real_initialize    (AtkObject     *obj,
                                                  gpointer      data);

static gpointer impl_inherit_class = NULL;

GType
ea_day_view_event_get_type (void)
{
    static GType type = 0;

    if (!type) {
        static const GTypeInfo tinfo = {
            sizeof (EaDayViewEventClass),
            (GBaseInitFunc) NULL, /* base init */
            (GBaseFinalizeFunc) NULL, /* base finalize */
            (GClassInitFunc) ea_day_view_event_class_init, /* class init */
            (GClassFinalizeFunc) NULL, /* class finalize */
            NULL, /* class data */
            sizeof (EaDayViewEvent), /* instance size */
            0, /* nb preallocs */
            (GInstanceInitFunc) NULL, /* instance init */
            NULL /* value table */
        };

        static const GInterfaceInfo atk_component_info = {
            (GInterfaceInitFunc) atk_component_interface_init,
            (GInterfaceFinalizeFunc) NULL,
            NULL
        };

        type = g_type_register_static (ATK_TYPE_GOBJECT_ACCESSIBLE,
                                       "EaDayViewEvent", &tinfo, 0);
        g_type_add_interface_static (type, ATK_TYPE_COMPONENT,
                                     &atk_component_info);
    }

    return type;
}

static void
ea_day_view_event_class_init (EaDayViewEventClass *klass)
{
    AtkObjectFactory *factory;
    AtkRegistry *default_registry;
    GType impl_inherit_type;
    AtkObjectClass *class = ATK_OBJECT_CLASS (klass);

    default_registry = atk_get_default_registry ();
    factory = atk_registry_get_factory (default_registry,
                                        E_TYPE_TEXT);
    impl_inherit_type = atk_object_factory_get_accessible_type (factory);
    impl_inherit_class = g_type_class_ref (impl_inherit_type);

    /*
      klass->notify_gtk = ea_day_view_event_real_notify_gtk;
      klass->focus_gtk = ea_day_view_event_real_focus_gtk;

      accessible_class->connect_widget_destroyed = ea_day_view_event_connect_widget_destroyed;
    */

    class->get_name = ea_day_view_event_get_name;
    class->get_description = ea_day_view_event_get_description;
    class->get_parent = ea_day_view_event_get_parent;
    /*
      class->ref_state_set = ea_day_view_event_ref_state_set;
    */
    class->get_index_in_parent = ea_day_view_event_get_index_in_parent;
    class->initialize = ea_day_view_event_real_initialize;
}

static void
ea_day_view_event_real_initialize (AtkObject *obj,
                                   gpointer  data)
{
    EaDayViewEvent *ea_event;

    ea_event = EA_DAY_VIEW_EVENT (obj);
    ea_event->event = (EDayViewEvent *) (data);

    /* initialize the accessible object of e_text with
     * the e_text of current event
     */
    ATK_OBJECT_CLASS (impl_inherit_class)->initialize (obj,
                                                       ea_event->event->canvas_item);

    obj->role = ATK_ROLE_TEXT;
}

AtkObject* 
ea_day_view_event_new (GObject *obj)
{
    EDayViewEvent *event;
    AtkObject *object;

    event = (EDayViewEvent *)obj;
    object = ATK_OBJECT(g_object_new (EA_TYPE_DAY_VIEW_EVENT, NULL));
    atk_object_initialize (object, event);

    printf ("a day view event created\n");

    return object;
}

static G_CONST_RETURN gchar*
ea_day_view_event_get_name (AtkObject *accessible)
{
    if (!accessible->name)
        atk_object_set_name(accessible, "day view event");
    return accessible->name;
}

static G_CONST_RETURN gchar*
ea_day_view_event_get_description (AtkObject *accessible)
{
    if (!accessible->description)
        atk_object_set_description(accessible,
                                   "day view event");
    return accessible->description;
}

static AtkObject* 
ea_day_view_event_get_parent (AtkObject *accessible)
{
    AtkGObjectAccessible *atk_gobj;
    GObject *g_obj;
    GnomeCanvasItem *canvas_item;
    GnomeCanvas *canvas;
    GtkWidget *day_view;

    g_return_val_if_fail (EA_IS_DAY_VIEW_EVENT (accessible), NULL);
    atk_gobj = ATK_GOBJECT_ACCESSIBLE (accessible);

    g_obj = atk_gobject_accessible_get_object (atk_gobj);
    if (g_obj == NULL)
        /* Object is defunct */
        return NULL;

    /* canvas_item is the e_text for the event */
    /* canvas_item->canvas is the ECanvas for day view */
    /* parent of canvas_item->canvas is the EDayView, our target widget */
    canvas_item = GNOME_CANVAS_ITEM (g_obj);
    canvas = canvas_item->canvas;
    day_view = gtk_widget_get_parent (GTK_WIDGET(canvas));
    if (!E_IS_DAY_VIEW (day_view))
        return NULL;

    return gtk_widget_get_accessible (day_view);
}

static AtkStateSet*
ea_day_view_event_ref_state_set (AtkObject *accessible)
{
    return ATK_OBJECT_CLASS (impl_inherit_class)->ref_state_set (accessible);
}

static gint
ea_day_view_event_get_index_in_parent (AtkObject *accessible)
{
    GtkAccessible *ea_parent;
    EaDayViewEvent *ea_event;
    EDayView *day_view;

	EDayViewEvent *event;
	gint day, event_num;

    g_return_val_if_fail (EA_IS_DAY_VIEW_EVENT (accessible), -1);
    ea_event = EA_DAY_VIEW_EVENT (accessible);
    if (!ea_event->event)
        return -1;

    ea_parent = GTK_ACCESSIBLE (atk_object_get_parent (accessible));
    if (!ea_parent)
        return -1;

    day_view = E_DAY_VIEW (ea_parent->widget);

    /* the long event comes first in the order */
	for (event_num = day_view->long_events->len - 1; event_num >= 0;
	     event_num--) {
		event = &g_array_index (day_view->long_events,
                                EDayViewEvent, event_num);
        if (ea_event->event == event)
            return event_num;

	}

	for (day = 0; day < day_view->days_shown; day++) {
		for (event_num = day_view->events[day]->len - 1; event_num >= 0;
		     event_num--) {
			event = &g_array_index (day_view->events[day],
                                    EDayViewEvent, event_num);
            if (ea_event->event == event)
                return day_view->long_events->len + event_num;
        }
    }

    return -1;
}

static void 
atk_component_interface_init (AtkComponentIface *iface)
{
    g_return_if_fail (iface != NULL);

    /*
     * Use default implementation for contains and get_position
     */

    /*
      iface->add_focus_handler = ea_day_view_event_add_focus_handler;
    */
    iface->get_extents = ea_day_view_event_get_extents;

    /*
      iface->get_size = ea_day_view_event_get_size;
      iface->get_layer = ea_day_view_event_get_layer;
      iface->grab_focus = ea_day_view_event_grab_focus;
      iface->remove_focus_handler = ea_day_view_event_remove_focus_handler;
      iface->set_extents = ea_day_view_event_set_extents;
      iface->set_position = ea_day_view_event_set_position;
      iface->set_size = ea_day_view_event_set_size;
    */
}

static void 
ea_day_view_event_get_extents (AtkComponent   *component,
                               gint           *x,
                               gint           *y,
                               gint           *width,
                               gint           *height,
                               AtkCoordType   coord_type)
{
    AtkComponentIface *atk_component_iface =
        g_type_interface_peek (impl_inherit_class, ATK_TYPE_COMPONENT);

    if (atk_component_iface)
        atk_component_iface->get_extents(component, x, y, width, height,
                                         coord_type);

}
