/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* Evolution Accessibility: ea-day-view-event-factory.c
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

#include <gtk/gtk.h>
#include "ea-day-view-event-factory.h"
#include "ea-day-view-event.h"

static void ea_day_view_event_factory_class_init (EaDayViewEventFactoryClass *klass);

static AtkObject* ea_day_view_event_factory_create_accessible (GObject *obj);

static GType ea_day_view_event_factory_get_accessible_type (void);

GType
ea_day_view_event_factory_get_type (void)
{
  static GType type = 0;

  if (!type) 
  {
    static const GTypeInfo tinfo =
    {
      sizeof (EaDayViewEventFactoryClass),
      (GBaseInitFunc) NULL, /* base init */
      (GBaseFinalizeFunc) NULL, /* base finalize */
      (GClassInitFunc) ea_day_view_event_factory_class_init, /* class init */
      (GClassFinalizeFunc) NULL, /* class finalize */
      NULL, /* class data */
      sizeof (EaDayViewEventFactory), /* instance size */
      0, /* nb preallocs */
      (GInstanceInitFunc) NULL, /* instance init */
      NULL /* value table */
    };
    type = g_type_register_static (
                           ATK_TYPE_OBJECT_FACTORY, 
                           "EaDayViewEventFactory" , &tinfo, 0);
  }

  return type;
}

static void 
ea_day_view_event_factory_class_init (EaDayViewEventFactoryClass *klass)
{
  AtkObjectFactoryClass *class = ATK_OBJECT_FACTORY_CLASS (klass);

  class->create_accessible = ea_day_view_event_factory_create_accessible;
  class->get_accessible_type = ea_day_view_event_factory_get_accessible_type;
}

static AtkObject* 
ea_day_view_event_factory_create_accessible (GObject   *obj)
{
  return ea_day_view_event_new (obj);
}

static GType
ea_day_view_event_factory_get_accessible_type (void)
{
  return EA_TYPE_DAY_VIEW_EVENT;
}
