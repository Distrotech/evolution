 /* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* Evolution Accessibility: ea-gnome-calendar.c
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

#include "ea-gnome-calendar.h"

static void ea_gnome_calendar_class_init (EaGnomeCalendarClass *klass);

static G_CONST_RETURN gchar* ea_gnome_calendar_get_name (AtkObject *accessible);
static G_CONST_RETURN gchar* ea_gnome_calendar_get_description (AtkObject *accessible);
static AtkObject* ea_gnome_calendar_get_parent (AtkObject *accessible);
static gint ea_gnome_get_n_children (AtkObject* obj);
static AtkObject * ea_gnome_ref_child (AtkObject *obj, gint i);

static AtkStateSet* ea_gnome_calendar_ref_state_set (AtkObject *accessible);
static gint ea_gnome_calendar_get_index_in_parent (AtkObject *accessible);

static void atk_component_interface_init (AtkComponentIface *iface);

static void ea_gnome_calendar_get_extents (AtkComponent    *component,
                                           gint            *x,
                                           gint            *y,
                                           gint            *width,
                                           gint            *height,
                                           AtkCoordType    coord_type);

static void ea_gnome_calendar_real_initialize (AtkObject     *obj,
                                               gpointer      data);

static gpointer parent_class = NULL;

GType
ea_gnome_calendar_get_type (void)
{
    static GType type = 0;
    AtkObjectFactory *factory;
    GTypeQuery query;
    GType derived_atk_type;

    if (!type) {
        static const GTypeInfo tinfo = {
            sizeof (EaGnomeCalendarClass),
            (GBaseInitFunc) NULL, /* base init */
            (GBaseFinalizeFunc) NULL, /* base finalize */
            (GClassInitFunc) ea_gnome_calendar_class_init, /* class init */
            (GClassFinalizeFunc) NULL, /* class finalize */
            NULL, /* class data */
            sizeof (EaGnomeCalendar), /* instance size */
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
                                       "EaGnomeCalendar", &tinfo, 0);


        g_type_add_interface_static (type, ATK_TYPE_COMPONENT,
                                     &atk_component_info);
    }

    return type;
}

static void
ea_gnome_calendar_class_init (EaGnomeCalendarClass *klass)
{
    AtkObjectFactory *factory;
    AtkRegistry *default_registry;
    AtkObjectClass *class = ATK_OBJECT_CLASS (klass);

    parent_class = g_type_class_peek_parent (klass);

    /*
      klass->notify_gtk = ea_gnome_calendar_real_notify_gtk;
      klass->focus_gtk = ea_gnome_calendar_real_focus_gtk;

      accessible_class->connect_widget_destroyed =
      ea_gnome_calendar_connect_widget_destroyed;
    */

    class->get_name = ea_gnome_calendar_get_name;
    class->get_description = ea_gnome_calendar_get_description;

    class->get_parent = ea_gnome_calendar_get_parent;
    class->get_index_in_parent = ea_gnome_calendar_get_index_in_parent;
    class->get_n_children = ea_gnome_get_n_children;
    class->ref_child = ea_gnome_ref_child;

    class->ref_state_set = ea_gnome_calendar_ref_state_set;
    class->initialize = ea_gnome_calendar_real_initialize;
}

/**
 * This function  specifies the GtkWidget for which the EaGnomeCalendar
 * was created and specifies a handler to be called when the GtkWidget is
 * destroyed.
 **/
static void 
ea_gnome_calendar_real_initialize (AtkObject *obj,
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
      G_CALLBACK (ea_gnome_calendar_focus_gtk),
      NULL);
      g_signal_connect_after (widget,
      "focus-out-event",
      G_CALLBACK (ea_gnome_calendar_focus_gtk),
      NULL);
      g_signal_connect (widget,
      "notify",
      G_CALLBACK (ea_gnome_calendar_notify_gtk),
      widget);
      atk_component_add_focus_handler (ATK_COMPONENT (accessible),
      ea_gnome_calendar_focus_event);
    */
    /*
     * Add signal handlers for GTK signals required to support property changes
     */
    /*
      g_signal_connect (widget,
      "map",
      G_CALLBACK (ea_gnome_calendar_map_gtk),
      NULL);
      g_signal_connect (widget,
      "unmap",
      G_CALLBACK (ea_gnome_calendar_map_gtk),
      NULL);
      g_object_set_data (G_OBJECT (obj), "atk-component-layer",
      GINT_TO_POINTER (ATK_LAYER_WIDGET));
    */
    obj->role = ATK_ROLE_FILLER;
}

AtkObject* 
ea_gnome_calendar_new (GtkWidget *widget)
{
    GObject *object;
    AtkObject *accessible;

    g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

    object = g_object_new (EA_TYPE_GNOME_CALENDAR, NULL);

    accessible = ATK_OBJECT (object);
    atk_object_initialize (accessible, widget);

    printf ("EA-gnome-calendar created\n");

    return accessible;
}

static G_CONST_RETURN gchar*
ea_gnome_calendar_get_name (AtkObject *accessible)
{
    if (!accessible->name)
        atk_object_set_name(accessible, "Gnome Calendar");
    return accessible->name;
}

static G_CONST_RETURN gchar*
ea_gnome_calendar_get_description (AtkObject *accessible)
{
    if (!accessible->description)
        atk_object_set_description(accessible,
                                   "Gnome Calendar");
    return accessible->description;
}

static AtkObject* 
ea_gnome_calendar_get_parent (AtkObject *accessible)
{
    return ATK_OBJECT_CLASS (parent_class)->get_parent (accessible);
}

static gint
ea_gnome_calendar_get_index_in_parent (AtkObject *accessible)
{
    return ATK_OBJECT_CLASS (parent_class)->get_index_in_parent (accessible);
}

static gint
ea_gnome_get_n_children (AtkObject* obj)
{
    g_return_val_if_fail (EA_IS_GNOME_CALENDAR (obj), 0);

    return 3;
}

static AtkObject *
ea_gnome_ref_child (AtkObject *obj, gint i)
{
    AtkObject * child = NULL;
    GnomeCalendar * calendarWidget;
    GtkWidget *childWidget;

    g_return_val_if_fail (EA_IS_GNOME_CALENDAR (obj), NULL);
    if (i < 0 || i >2 )
        return NULL;

    calendarWidget = GTK_ACCESSIBLE (obj)->widget;

    switch (i) {
    case 0:
        /* for the day/week view */
        childWidget = gnome_calendar_get_current_view_widget (calendarWidget);
        child = gtk_widget_get_accessible (childWidget);
        atk_object_set_parent (child, obj);
        break;
    case 1:
        /* for calendar */
        childWidget = gnome_calendar_get_e_calendar_widget (calendarWidget);
        child = gtk_widget_get_accessible (childWidget);
        break;
    case 2:
        /* for todo list */
        childWidget = gnome_calendar_get_task_pad (calendarWidget);
        child = gtk_widget_get_accessible (childWidget);
        break;
    default:
        break;
    }
    if (child)
        g_object_ref(child);
    return child;
}

static AtkStateSet*
ea_gnome_calendar_ref_state_set (AtkObject *accessible)
{
    return ATK_OBJECT_CLASS (parent_class)->ref_state_set (accessible);
}

static void 
atk_component_interface_init (AtkComponentIface *iface)
{
    g_return_if_fail (iface != NULL);

    /*
     * Use default implementation for contains and get_position
     */

    /*
      iface->add_focus_handler = ea_gnome_calendar_add_focus_handler;
    */
    iface->get_extents = ea_gnome_calendar_get_extents;

    /*
      iface->get_size = ea_gnome_calendar_get_size;
      iface->get_layer = ea_gnome_calendar_get_layer;
      iface->grab_focus = ea_gnome_calendar_grab_focus;
      iface->remove_focus_handler = ea_gnome_calendar_remove_focus_handler;
      iface->set_extents = ea_gnome_calendar_set_extents;
      iface->set_position = ea_gnome_calendar_set_position;
      iface->set_size = ea_gnome_calendar_set_size;
    */
}

static void 
ea_gnome_calendar_get_extents (AtkComponent   *component,
                               gint           *x,
                               gint           *y,
                               gint           *width,
                               gint           *height,
                               AtkCoordType   coord_type)
{
    AtkComponentIface *atk_component_iface =
        g_type_interface_peek (parent_class, ATK_TYPE_COMPONENT);

    if (atk_component_iface)
        atk_component_iface->get_extents(component, x, y, width, height, coord_type);

}
