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

#include "ea-day-view.h"

static void ea_day_view_class_init (EaDayViewClass *klass);

static G_CONST_RETURN gchar* ea_day_view_get_name (AtkObject *accessible);
static G_CONST_RETURN gchar* ea_day_view_get_description (AtkObject *accessible);
static AtkObject* ea_day_view_get_parent (AtkObject *accessible);
static gint         ea_day_view_get_n_children      (AtkObject          *obj);
static AtkObject*   ea_day_view_ref_child           (AtkObject          *obj,
                                                     gint               i);
static AtkStateSet* ea_day_view_ref_state_set (AtkObject *accessible);
static gint ea_day_view_get_index_in_parent (AtkObject *accessible);

static void atk_component_interface_init (AtkComponentIface *iface);

static void     ea_day_view_get_extents    (AtkComponent    *component,
                                            gint            *x,
                                            gint            *y,
                                            gint            *width,
                                            gint            *height,
                                            AtkCoordType    coord_type);

static void ea_day_view_real_initialize    (AtkObject     *obj,
                                            gpointer      data);

static gpointer parent_class = NULL;

GType
ea_day_view_get_type (void)
{
    static GType type = 0;
    AtkObjectFactory *factory;
    GTypeQuery query;
    GType derived_atk_type;

    if (!type) {
        static GTypeInfo tinfo = {
            sizeof (EaDayViewClass),
            (GBaseInitFunc) NULL, /* base init */
            (GBaseFinalizeFunc) NULL, /* base finalize */
            (GClassInitFunc) ea_day_view_class_init, /* class init */
            (GClassFinalizeFunc) NULL, /* class finalize */
            NULL, /* class data */
            sizeof (EaDayView), /* instance size */
            0, /* nb preallocs */
            (GInstanceInitFunc) NULL, /* instance init */
            NULL /* value table */
        };

        static const GInterfaceInfo atk_component_info = {
            (GInterfaceInitFunc) atk_component_interface_init,
            (GInterfaceFinalizeFunc) NULL,
            NULL
        };

        /*
         * Figure out the size of the class and instance
         * we are run-time deriving from (GailWidget, in this case)
         */

        factory = atk_registry_get_factory (atk_get_default_registry (),
                                            GTK_TYPE_WIDGET);
        derived_atk_type = atk_object_factory_get_accessible_type (factory);
        g_type_query (derived_atk_type, &query);

        tinfo.class_size = query.class_size;
        tinfo.instance_size = query.instance_size;

        type = g_type_register_static (derived_atk_type,
                                       "EaDayView", &tinfo, 0);


        g_type_add_interface_static (type, ATK_TYPE_COMPONENT,
                                     &atk_component_info);
    }

    return type;
}

static void
ea_day_view_class_init (EaDayViewClass *klass)
{
    AtkObjectFactory *factory;
    AtkRegistry *default_registry;
    AtkObjectClass *class = ATK_OBJECT_CLASS (klass);

    parent_class = g_type_class_peek_parent (klass);

    /*
      klass->notify_gtk = ea_day_view_real_notify_gtk;
      klass->focus_gtk = ea_day_view_real_focus_gtk;

      accessible_class->connect_widget_destroyed = ea_day_view_connect_widget_destroyed;
    */

    class->get_name = ea_day_view_get_name;
    class->get_description = ea_day_view_get_description;
    class->ref_state_set = ea_day_view_ref_state_set;

    class->get_parent = ea_day_view_get_parent;
    class->get_index_in_parent = ea_day_view_get_index_in_parent;
    class->get_n_children = ea_day_view_get_n_children;
    class->ref_child = ea_day_view_ref_child;

    class->initialize = ea_day_view_real_initialize;
}

/**
 * This function  specifies the GtkWidget for which the EaDayView was created 
 * and specifies a handler to be called when the GtkWidget is destroyed.
 **/
static void 
ea_day_view_real_initialize (AtkObject *obj,
                             gpointer  data)
{
    GtkAccessible *accessible;
    GtkWidget *widget;

    g_return_if_fail (GTK_IS_WIDGET (data));

    widget = GTK_WIDGET (data);

    accessible = GTK_ACCESSIBLE (obj);
    accessible->widget = widget;
    gtk_accessible_connect_widget_destroyed (accessible);
    /*
      g_signal_connect_after (widget,
      "focus-in-event",
      G_CALLBACK (ea_day_view_focus_gtk),
      NULL);
      g_signal_connect_after (widget,
      "focus-out-event",
      G_CALLBACK (ea_day_view_focus_gtk),
      NULL);
      g_signal_connect (widget,
      "notify",
      G_CALLBACK (ea_day_view_notify_gtk),
      widget);
      atk_component_add_focus_handler (ATK_COMPONENT (accessible),
      ea_day_view_focus_event);
    */
    /*
     * Add signal handlers for GTK signals required to support property changes
     */
    /*
      g_signal_connect (widget,
      "map",
      G_CALLBACK (ea_day_view_map_gtk),
      NULL);
      g_signal_connect (widget,
      "unmap",
      G_CALLBACK (ea_day_view_map_gtk),
      NULL);
      g_object_set_data (G_OBJECT (obj), "atk-component-layer",
      GINT_TO_POINTER (ATK_LAYER_WIDGET));
    */
    obj->role = ATK_ROLE_CANVAS;
}

AtkObject* 
ea_day_view_new (GtkWidget *widget)
{
    GObject *object;
    AtkObject *accessible;

    g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

    object = g_object_new (EA_TYPE_DAY_VIEW, NULL);

    accessible = ATK_OBJECT (object);
    atk_object_initialize (accessible, widget);

    printf ("EA-day-view created\n");

    return accessible;
}

static G_CONST_RETURN gchar*
ea_day_view_get_name (AtkObject *accessible)
{
    EDayView *day_view;

    g_return_val_if_fail (EA_IS_DAY_VIEW (accessible), NULL);

    day_view = E_DAY_VIEW (GTK_ACCESSIBLE (accessible)->widget);

    if (!accessible->name) {
        GnomeCalendarViewType view_type;

        view_type = gnome_calendar_get_view (day_view->calendar);

        if (view_type == GNOME_CAL_WORK_WEEK_VIEW)
            atk_object_set_name(accessible, "work week view");
        else
            atk_object_set_name(accessible, "day view view");
    }
    return accessible->name;
}

static G_CONST_RETURN gchar*
ea_day_view_get_description (AtkObject *accessible)
{
    EDayView *day_view;

    g_return_val_if_fail (EA_IS_DAY_VIEW (accessible), NULL);

    day_view = E_DAY_VIEW (GTK_ACCESSIBLE (accessible)->widget);

    if (!accessible->description) {
        GnomeCalendarViewType view_type;

        view_type = gnome_calendar_get_view (day_view->calendar);

        if (view_type == GNOME_CAL_WORK_WEEK_VIEW)
            atk_object_set_description(accessible,
                                       "calendar view for a work week");
        else
            atk_object_set_description(accessible,
                                       "calendar view for one or more days");
    }
    return accessible->description;
}

static AtkObject* 
ea_day_view_get_parent (AtkObject *accessible)
{
    EDayView *day_view;
    GnomeCalendar *gnomeCalendar;

    g_return_val_if_fail (EA_IS_DAY_VIEW (accessible), NULL);

    day_view = E_DAY_VIEW (GTK_ACCESSIBLE (accessible)->widget);

    gnomeCalendar = day_view->calendar;

    return gtk_widget_get_accessible (GTK_WIDGET(gnomeCalendar));
}

static gint
ea_day_view_get_n_children (AtkObject *accessible)
{
    EDayView *day_view;
	gint day;
    gint child_num = 0;

    g_return_val_if_fail (EA_IS_DAY_VIEW (accessible), -1);

    day_view = E_DAY_VIEW (GTK_ACCESSIBLE (accessible)->widget);

    child_num += day_view->long_events->len;

	for (day = 0; day < day_view->days_shown; day++) {
		child_num += day_view->events[day]->len;
    }

    return child_num;
}

static AtkObject *
ea_day_view_ref_child (AtkObject *accessible, gint i)
{
    EDayView *day_view;
    gint child_num;
	gint day;
    AtkObject *atk_object = NULL;
	EDayViewEvent *event = NULL;

    g_return_val_if_fail (EA_IS_DAY_VIEW (accessible), NULL);

    child_num = atk_object_get_n_accessible_children (accessible);
    if (child_num <= 0 || i < 0 || i >= child_num)
        return NULL;

    day_view = E_DAY_VIEW (GTK_ACCESSIBLE (accessible)->widget);

    /* a long event */
    if (i < day_view->long_events->len) {
        event = &g_array_index (day_view->long_events,
                                EDayViewEvent, i);
	}
    else {
        i -= day_view->long_events->len;
        day = 0;
        while (i >= day_view->events[day]->len) {
            i -= day_view->events[day]->len;
            ++day;
        }

        event = &g_array_index (day_view->events[day],
                                EDayViewEvent, i);
    }
    printf ("child event for index=%d is %p\n", i, event);
    if (event) {
        /* event is not created as gobject???!!!
           atk_object = atk_gobject_accessible_for_object (G_OBJECT (event));
        */
        atk_object = ea_day_view_event_new (event);
        g_object_ref (atk_object);
    }
    return atk_object;
}

static AtkStateSet*
ea_day_view_ref_state_set (AtkObject *accessible)
{
    return ATK_OBJECT_CLASS (parent_class)->ref_state_set (accessible);
}

static gint
ea_day_view_get_index_in_parent (AtkObject *accessible)
{
    return 0;
}

static void 
atk_component_interface_init (AtkComponentIface *iface)
{
    g_return_if_fail (iface != NULL);

    /*
     * Use default implementation for contains and get_position
     */

    /*
      iface->add_focus_handler = ea_day_view_add_focus_handler;
    */
    iface->get_extents = ea_day_view_get_extents;

    /*
      iface->get_size = ea_day_view_get_size;
      iface->get_layer = ea_day_view_get_layer;
      iface->grab_focus = ea_day_view_grab_focus;
      iface->remove_focus_handler = ea_day_view_remove_focus_handler;
      iface->set_extents = ea_day_view_set_extents;
      iface->set_position = ea_day_view_set_position;
      iface->set_size = ea_day_view_set_size;
    */
}

static void 
ea_day_view_get_extents (AtkComponent   *component,
                         gint           *x,
                         gint           *y,
                         gint           *width,
                         gint           *height,
                         AtkCoordType   coord_type)
{
    AtkComponentIface *atk_component_iface =
        g_type_interface_peek (parent_class, ATK_TYPE_COMPONENT);

    if (atk_component_iface)
        atk_component_iface->get_extents(component, x, y, width, height,
                                         coord_type);

}
