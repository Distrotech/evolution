/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-minicard-label.c
 * Copyright (C) 2000  Ximian, Inc.
 * Author: Chris Lahey <clahey@ximian.com>
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

#include "e-minicard-label.h"

#include <gtk/gtksignal.h>
#include <libgnomeui/gnome-canvas-rect-ellipse.h>
#include <gal/util/e-util.h>
#include <gal/e-text/e-text.h>
#include <gal/widgets/e-canvas.h>
#include <gal/widgets/e-canvas-utils.h>
#include <gdk/gdkkeysyms.h>

static void e_minicard_label_init		(EMinicardLabel		 *card);
static void e_minicard_label_class_init	(EMinicardLabelClass	 *klass);
static void e_minicard_label_set_arg (GtkObject *o, GtkArg *arg, guint arg_id);
static void e_minicard_label_get_arg (GtkObject *object, GtkArg *arg, guint arg_id);
static gboolean e_minicard_label_event (GnomeCanvasItem *item, GdkEvent *event);
static void e_minicard_label_realize (GnomeCanvasItem *item);
static void e_minicard_label_unrealize (GnomeCanvasItem *item);
static void e_minicard_label_reflow(GnomeCanvasItem *item, int flags);

static void e_minicard_label_resize_children( EMinicardLabel *e_minicard_label );

static GnomeCanvasGroupClass *parent_class = NULL;

/* The arguments we take */
enum {
	ARG_0,
	ARG_WIDTH,
	ARG_HEIGHT,
	ARG_HAS_FOCUS,
	ARG_FIELD,
	ARG_FIELDNAME,
	ARG_TEXT_MODEL,
	ARG_MAX_FIELD_NAME_WIDTH,
	ARG_EDITABLE
};

GtkType
e_minicard_label_get_type (void)
{
  static GtkType minicard_label_type = 0;

  if (!minicard_label_type)
    {
      static const GtkTypeInfo minicard_label_info =
      {
        "EMinicardLabel",
        sizeof (EMinicardLabel),
        sizeof (EMinicardLabelClass),
        (GtkClassInitFunc) e_minicard_label_class_init,
        (GtkObjectInitFunc) e_minicard_label_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      minicard_label_type = gtk_type_unique (gnome_canvas_group_get_type (), &minicard_label_info);
    }

  return minicard_label_type;
}

static void
e_minicard_label_class_init (EMinicardLabelClass *klass)
{
  GtkObjectClass *object_class;
  GnomeCanvasItemClass *item_class;

  object_class = (GtkObjectClass*) klass;
  item_class = (GnomeCanvasItemClass *) klass;

  parent_class = gtk_type_class (gnome_canvas_group_get_type ());
  
  gtk_object_add_arg_type ("EMinicardLabel::width", GTK_TYPE_DOUBLE, 
			   GTK_ARG_READWRITE, ARG_WIDTH); 
  gtk_object_add_arg_type ("EMinicardLabel::height", GTK_TYPE_DOUBLE, 
			   GTK_ARG_READABLE, ARG_HEIGHT);
  gtk_object_add_arg_type ("EMinicardLabel::has_focus", GTK_TYPE_BOOL, 
			   GTK_ARG_READWRITE, ARG_HAS_FOCUS);
  gtk_object_add_arg_type ("EMinicardLabel::field", GTK_TYPE_STRING, 
			   GTK_ARG_READWRITE, ARG_FIELD);
  gtk_object_add_arg_type ("EMinicardLabel::fieldname", GTK_TYPE_STRING, 
			   GTK_ARG_READWRITE, ARG_FIELDNAME);
  gtk_object_add_arg_type ("EMinicardLabel::text_model", GTK_TYPE_OBJECT,
			   GTK_ARG_READWRITE, ARG_TEXT_MODEL);
  gtk_object_add_arg_type ("EMinicardLabel::max_field_name_length", GTK_TYPE_DOUBLE,
			   GTK_ARG_READWRITE, ARG_MAX_FIELD_NAME_WIDTH);
  gtk_object_add_arg_type ("EMinicardLabel::editable", GTK_TYPE_BOOL, 
			   GTK_ARG_READWRITE, ARG_EDITABLE);
 
  object_class->set_arg = e_minicard_label_set_arg;
  object_class->get_arg = e_minicard_label_get_arg;
  /*  object_class->destroy = e_minicard_label_destroy; */
  
  /* GnomeCanvasItem method overrides */
  item_class->realize     = e_minicard_label_realize;
  item_class->unrealize   = e_minicard_label_unrealize;
  item_class->event       = e_minicard_label_event;
}

static void
e_minicard_label_init (EMinicardLabel *minicard_label)
{
  minicard_label->width = 10;
  minicard_label->height = 10;
  minicard_label->rect = NULL;
  minicard_label->fieldname = NULL;
  minicard_label->field = NULL;

  minicard_label->max_field_name_length = -1;

  e_canvas_item_set_reflow_callback(GNOME_CANVAS_ITEM(minicard_label), e_minicard_label_reflow);
}

static void
e_minicard_label_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	EMinicardLabel *e_minicard_label;

	item = GNOME_CANVAS_ITEM (o);
	e_minicard_label = E_MINICARD_LABEL (o);
	
	switch (arg_id){
	case ARG_WIDTH:
		e_minicard_label->width = GTK_VALUE_DOUBLE (*arg);
		e_minicard_label_resize_children(e_minicard_label);
		e_canvas_item_request_reflow (item);
		break;
	case ARG_HAS_FOCUS:
		if (e_minicard_label->field && (GTK_VALUE_ENUM(*arg) != E_FOCUS_NONE))
			e_canvas_item_grab_focus(e_minicard_label->field, FALSE);
		break;
	case ARG_FIELD:
		gnome_canvas_item_set( e_minicard_label->field, "text", GTK_VALUE_STRING (*arg), NULL );
		break;
	case ARG_FIELDNAME:
		gnome_canvas_item_set( e_minicard_label->fieldname, "text", GTK_VALUE_STRING (*arg), NULL );
		break;
	case ARG_TEXT_MODEL:
		gnome_canvas_item_set( e_minicard_label->field, "model", GTK_VALUE_OBJECT (*arg), NULL);
		break;
	case ARG_MAX_FIELD_NAME_WIDTH:
		e_minicard_label->max_field_name_length = GTK_VALUE_DOUBLE (*arg);
		break;
	case ARG_EDITABLE:
		e_minicard_label->editable = GTK_VALUE_BOOL (*arg);
		gtk_object_set (GTK_OBJECT (e_minicard_label->field), "editable", e_minicard_label->editable, NULL);
		break;
	}
}

static void
e_minicard_label_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	EMinicardLabel *e_minicard_label;
	char *temp;
	ETextModel *tempmodel;

	e_minicard_label = E_MINICARD_LABEL (object);

	switch (arg_id) {
	case ARG_WIDTH:
		GTK_VALUE_DOUBLE (*arg) = e_minicard_label->width;
		break;
	case ARG_HEIGHT:
		GTK_VALUE_DOUBLE (*arg) = e_minicard_label->height;
		break;
	case ARG_HAS_FOCUS:
		GTK_VALUE_ENUM (*arg) = e_minicard_label->has_focus ? E_FOCUS_CURRENT : E_FOCUS_NONE;
		break;
	case ARG_FIELD:
		gtk_object_get( GTK_OBJECT( e_minicard_label->field ), "text", &temp, NULL );
		GTK_VALUE_STRING (*arg) = temp;
		break;
	case ARG_FIELDNAME:
		gtk_object_get( GTK_OBJECT( e_minicard_label->fieldname ), "text", &temp, NULL );
		GTK_VALUE_STRING (*arg) = temp;
		break;
	case ARG_TEXT_MODEL:
		gtk_object_get( GTK_OBJECT( e_minicard_label->field ), "model", &tempmodel, NULL );
		GTK_VALUE_OBJECT (*arg) = GTK_OBJECT(tempmodel);
		break;
	case ARG_MAX_FIELD_NAME_WIDTH:
		GTK_VALUE_DOUBLE (*arg) = e_minicard_label->max_field_name_length;
		break;
	case ARG_EDITABLE:
		GTK_VALUE_BOOL (*arg) = e_minicard_label->editable;
		break;
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

static void
e_minicard_label_realize (GnomeCanvasItem *item)
{
	if (GNOME_CANVAS_ITEM_CLASS( parent_class )->realize)
	  (* GNOME_CANVAS_ITEM_CLASS( parent_class )->realize) (item);

	e_canvas_item_request_reflow(item);
	
	if (!item->canvas->aa)
	  {
	  }
}

void
e_minicard_label_construct (GnomeCanvasItem *item)
{
	EMinicardLabel *e_minicard_label;
	GnomeCanvasGroup *group;
	GdkFont *font;

	font = ((GtkWidget *) item->canvas)->style->font;

	e_minicard_label = E_MINICARD_LABEL (item);
	group = GNOME_CANVAS_GROUP( item );

	e_minicard_label->rect =
	  gnome_canvas_item_new( group,
				 gnome_canvas_rect_get_type(),
				 "x1", (double) 0,
				 "y1", (double) 0,
				 "x2", (double) e_minicard_label->width - 1,
				 "y2", (double) e_minicard_label->height - 1,
				 "outline_color", NULL,
				 NULL );
	e_minicard_label->fieldname =
	  gnome_canvas_item_new( group,
				 e_text_get_type(),
				 "anchor", GTK_ANCHOR_NW,
				 "clip_width", (double) ( e_minicard_label->width / 2 - 4 ),
				 "clip", TRUE,
				 "use_ellipsis", TRUE,
				 "font_gdk", font,
				 "fill_color", "black",
				 "draw_background", FALSE,
				 NULL );
	e_canvas_item_move_absolute(e_minicard_label->fieldname, 2, 1);

	e_minicard_label->field =
	  gnome_canvas_item_new( group,
				 e_text_get_type(),
				 "anchor", GTK_ANCHOR_NW,
				 "clip_width", (double) ( ( e_minicard_label->width + 1 ) / 2 - 4 ),
				 "clip", TRUE,
				 "use_ellipsis", TRUE,
				 "font_gdk", font,
				 "fill_color", "black",
				 "editable", e_minicard_label->editable,
				 "draw_background", FALSE,
				 NULL );
	e_canvas_item_move_absolute(e_minicard_label->field, ( e_minicard_label->width / 2 + 2), 1);

	e_canvas_item_request_reflow(item);
}

static void
e_minicard_label_unrealize (GnomeCanvasItem *item)
{
  EMinicardLabel *e_minicard_label;

  e_minicard_label = E_MINICARD_LABEL (item);

  if (!item->canvas->aa)
    {
    }

  if (GNOME_CANVAS_ITEM_CLASS( parent_class )->unrealize)
    (* GNOME_CANVAS_ITEM_CLASS( parent_class )->unrealize) (item);
}

static gboolean
e_minicard_label_event (GnomeCanvasItem *item, GdkEvent *event)
{
  EMinicardLabel *e_minicard_label;
 
  e_minicard_label = E_MINICARD_LABEL (item);

  switch( event->type )
    {
    case GDK_KEY_PRESS:
	    if (event->key.keyval == GDK_Escape) {
		    GnomeCanvasItem *parent;

		    e_text_cancel_editing (E_TEXT (e_minicard_label->field));

		    parent = GNOME_CANVAS_ITEM (e_minicard_label)->parent;
		    if (parent)
			    e_canvas_item_grab_focus(parent, FALSE);
	    }
	    break;
    case GDK_FOCUS_CHANGE:
      {
	GdkEventFocus *focus_event = (GdkEventFocus *) event;
	if ( focus_event->in )
	  {
	    gnome_canvas_item_set( e_minicard_label->rect, 
				   "outline_color", "grey50", 
				   "fill_color", "grey90",
				   NULL );
	    e_minicard_label->has_focus = TRUE;
	  }
	else
	  {
	    gnome_canvas_item_set( e_minicard_label->rect, 
				   "outline_color", NULL, 
				   "fill_color", NULL,
				   NULL );
	    e_minicard_label->has_focus = FALSE;
	  }
      }
      break;
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE: 
    case GDK_MOTION_NOTIFY:
    case GDK_ENTER_NOTIFY:
    case GDK_LEAVE_NOTIFY: {
	    gboolean return_val;
#if 0
	    GnomeCanvasItem *field;
	    ArtPoint p;
	    double inv[6], affine[6];
	    
	    field = e_minicard_label->field;
	    art_affine_identity (affine);
	    
	    if (field->xform != NULL) {
		    if (field->object.flags & GNOME_CANVAS_ITEM_AFFINE_FULL) {
			    art_affine_multiply (affine, affine, field->xform);
		    } else {
			    affine[4] += field->xform[0];
			    affine[5] += field->xform[1];
		    }
	    }
	    
	    art_affine_invert (inv, affine);
	    switch(event->type) {
	    case GDK_MOTION_NOTIFY:
		    p.x = event->motion.x;
		    p.y = event->motion.y;
		    art_affine_point (&p, &p, inv);
		    event->motion.x = p.x;
		    event->motion.y = p.y;
		    break;
	    case GDK_BUTTON_PRESS:
	    case GDK_BUTTON_RELEASE:
		    p.x = event->button.x;
		    p.y = event->button.y;
		    art_affine_point (&p, &p, inv);
		    event->button.x = p.x;
		    event->button.y = p.y;
		    break;
	    case GDK_ENTER_NOTIFY:
	    case GDK_LEAVE_NOTIFY:
		    p.x = event->crossing.x;
		    p.y = event->crossing.y;
		    art_affine_point (&p, &p, inv);
		    event->crossing.x = p.x;
		    event->crossing.y = p.y;
		    break;
	    default:
		    break;
	    }
#endif	    
	    gtk_signal_emit_by_name(GTK_OBJECT(e_minicard_label->field), "event", event, &return_val);
	    return return_val;
	    break;
    }
    default:
	    break;
    }
  
  if (GNOME_CANVAS_ITEM_CLASS( parent_class )->event)
    return (* GNOME_CANVAS_ITEM_CLASS( parent_class )->event) (item, event);
  else
    return 0;
}

static void
e_minicard_label_resize_children(EMinicardLabel *e_minicard_label)
{
	double left_width;
	if (e_minicard_label->max_field_name_length != -1 && ((e_minicard_label->width / 2) - 4 > e_minicard_label->max_field_name_length))
		left_width = e_minicard_label->max_field_name_length;
	else
		left_width = e_minicard_label->width / 2 - 4;

	gnome_canvas_item_set( e_minicard_label->fieldname,
			       "clip_width", (double) ( left_width ),
			       NULL );
	gnome_canvas_item_set( e_minicard_label->field,
			       "clip_width", (double) ( e_minicard_label->width - 8 - left_width ),
			       NULL );
}

static void
e_minicard_label_reflow(GnomeCanvasItem *item, int flags)
{
	EMinicardLabel *e_minicard_label = E_MINICARD_LABEL(item);
	
	gint old_height;
	gdouble text_height;
	gdouble left_width;

	old_height = e_minicard_label->height;

	gtk_object_get(GTK_OBJECT(e_minicard_label->fieldname), 
		       "text_height", &text_height,
		       NULL);

	e_minicard_label->height = text_height;


	gtk_object_get(GTK_OBJECT(e_minicard_label->field), 
		       "text_height", &text_height,
		       NULL);

	if (e_minicard_label->height < text_height)
		e_minicard_label->height = text_height;
	e_minicard_label->height += 3;

	gnome_canvas_item_set( e_minicard_label->rect,
			       "x2", (double) e_minicard_label->width - 1,
			       "y2", (double) e_minicard_label->height - 1,
			       NULL );

	if (e_minicard_label->max_field_name_length != -1 && ((e_minicard_label->width / 2) - 4 > e_minicard_label->max_field_name_length))
		left_width = e_minicard_label->max_field_name_length;
	else
		left_width = e_minicard_label->width / 2 - 4;

	e_canvas_item_move_absolute(e_minicard_label->field, left_width + 6, 1);

	if (old_height != e_minicard_label->height)
		e_canvas_item_request_parent_reflow(item);
}

GnomeCanvasItem *
e_minicard_label_new(GnomeCanvasGroup *parent)
{
	GnomeCanvasItem *item = gnome_canvas_item_new(parent, e_minicard_label_get_type(), NULL);
	e_minicard_label_construct(item);
	return item;
}

