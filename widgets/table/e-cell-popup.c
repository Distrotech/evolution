/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-cell-popup.c: Popup cell renderer
 * Copyright 2001, Ximian, Inc.
 *
 * Authors:
 *   Damon Chaplin <damon@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  */

/*
 * ECellPopup - an abstract ECell class used to support popup selections like
 * a GtkCombo widget. It contains a child ECell, e.g. an ECellText, but when
 * selected it displays an arrow on the right edge which the user can click to
 * show a popup. Subclasses implement the popup class function to show the
 * popup.
 */

#include <config.h>
#include <gdk/gdkkeysyms.h>
#include "gal/util/e-util.h"
#include "e-table-item.h"
#include "e-cell-popup.h"


#define E_CELL_POPUP_ARROW_WIDTH	16
#define E_CELL_POPUP_ARROW_XPAD		3
#define E_CELL_POPUP_ARROW_YPAD		3


static void	e_cell_popup_class_init	(GtkObjectClass	*object_class);
static void	e_cell_popup_init	(ECellPopup	*ecp);
static void	e_cell_popup_destroy	(GtkObject	*object);


static ECellView* ecp_new_view		(ECell		*ecell,
					 ETableModel	*table_model,
					 void		*e_table_item_view);
static void	ecp_kill_view		(ECellView	*ecv);
static void	ecp_realize		(ECellView	*ecv);
static void	ecp_unrealize		(ECellView	*ecv);
static void	ecp_draw		(ECellView	*ecv,
					 GdkDrawable	*drawable,
					 int		 model_col,
					 int		 view_col,
					 int		 row,
					 ECellFlags	 flags,
					 int		 x1,
					 int		 y1,
					 int		 x2,
					 int		 y2);
static gint	ecp_event		(ECellView	*ecv,
					 GdkEvent	*event,
					 int		 model_col,
					 int		 view_col,
					 int		 row,
					 ECellFlags	 flags,
					 ECellActions	*actions);
static int	ecp_height		(ECellView	*ecv,
					 int		 model_col,
					 int		 view_col,
					 int		 row);
static void*	ecp_enter_edit		(ECellView	*ecv,
					 int		 model_col,
					 int		 view_col,
					 int		 row);
static void	ecp_leave_edit		(ECellView	*ecv,
					 int		 model_col,
					 int		 view_col,
					 int		 row,
					 void		*edit_context);
static void	ecp_print		(ECellView	*ecv,
					 GnomePrintContext *context, 
					 int		 model_col,
					 int		 view_col,
					 int		 row,
					 double		 width,
					 double		 height);
static gdouble	ecp_print_height	(ECellView	*ecv,
					 GnomePrintContext *context, 
					 int		 model_col,
					 int		 view_col,
					 int		 row,
					 double		 width);
static int	ecp_max_width		(ECellView	*ecv,
					 int		 model_col,
					 int		 view_col);
static void	ecp_show_tooltip	(ECellView	*ecv, 
					 int		 model_col,
					 int		 view_col,
					 int		 row,
					 int		 col_width,
					 ETableTooltip	*tooltip);
static char *ecp_get_bg_color (ECellView *ecell_view, int row);

static gint e_cell_popup_do_popup	(ECellPopupView	*ecp_view,
					 GdkEvent	*event,
					 int             row,
					 int             model_col);

static ECellClass *parent_class;


E_MAKE_TYPE (e_cell_popup, "ECellPopup", ECellPopup, e_cell_popup_class_init,
	     e_cell_popup_init, e_cell_get_type())


static void
e_cell_popup_class_init		(GtkObjectClass	*object_class)
{
	ECellClass *ecc = (ECellClass *) object_class;

	object_class->destroy = e_cell_popup_destroy;

	ecc->new_view     = ecp_new_view;
	ecc->kill_view    = ecp_kill_view;
	ecc->realize      = ecp_realize;
	ecc->unrealize    = ecp_unrealize;
	ecc->draw         = ecp_draw;
	ecc->event        = ecp_event;
	ecc->height       = ecp_height;
	ecc->enter_edit   = ecp_enter_edit;
	ecc->leave_edit   = ecp_leave_edit;
	ecc->print        = ecp_print;
	ecc->print_height = ecp_print_height;
	ecc->max_width	  = ecp_max_width;
	ecc->show_tooltip = ecp_show_tooltip;
	ecc->get_bg_color = ecp_get_bg_color;

	parent_class = gtk_type_class (e_cell_get_type ());
}


static void
e_cell_popup_init		(ECellPopup	*ecp)
{
	ecp->popup_shown = FALSE;
	ecp->popup_model = NULL;
}


/**
 * e_cell_popup_new:
 *
 * Creates a new ECellPopup renderer.
 *
 * Returns: an ECellPopup object.
 */
ECell *
e_cell_popup_new		(void)
{
	ECellPopup *ecp = gtk_type_new (e_cell_popup_get_type ());

	return (ECell*) ecp;
}


/*
 * GtkObject::destroy method
 */
static void
e_cell_popup_destroy		(GtkObject *object)
{
	ECellPopup *ecp = E_CELL_POPUP (object);

	gtk_object_unref (GTK_OBJECT (ecp->child));

	GTK_OBJECT_CLASS (parent_class)->destroy (object);
}



/*
 * ECell::new_view method
 */
static ECellView *
ecp_new_view (ECell *ecell, ETableModel *table_model, void *e_table_item_view)
{
	ECellPopup *ecp = E_CELL_POPUP (ecell);
	ECellPopupView *ecp_view;
	
	/* We must have a child ECell before we create any views. */
	g_return_val_if_fail (ecp->child != NULL, NULL);

	ecp_view = g_new0 (ECellPopupView, 1);

	ecp_view->cell_view.ecell = ecell;
	ecp_view->cell_view.e_table_model = table_model;
	ecp_view->cell_view.e_table_item_view = e_table_item_view;

	ecp_view->child_view = e_cell_new_view (ecp->child, table_model,
						e_table_item_view);

	return (ECellView*) ecp_view;
}


/*
 * ECell::kill_view method
 */
static void
ecp_kill_view (ECellView *ecv)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	if (ecp_view->child_view)
		e_cell_kill_view (ecp_view->child_view);
	g_free (ecp_view);
}


/*
 * ECell::realize method
 */
static void
ecp_realize (ECellView *ecv)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	e_cell_realize (ecp_view->child_view);

	if (parent_class->realize)
		(* parent_class->realize) (ecv);
}


/*
 * ECell::unrealize method
 */
static void
ecp_unrealize (ECellView *ecv)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	e_cell_unrealize (ecp_view->child_view);

	if (parent_class->unrealize)
		(* parent_class->unrealize) (ecv);
}


/*
 * ECell::draw method
 */
static void
ecp_draw (ECellView *ecv, GdkDrawable *drawable,
	  int model_col, int view_col, int row, ECellFlags flags,
	  int x1, int y1, int x2, int y2)
{
	ECellPopup *ecp = E_CELL_POPUP (ecv->ecell);
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;
	GtkWidget *canvas = GTK_WIDGET (GNOME_CANVAS_ITEM (ecv->e_table_item_view)->canvas);
	GtkShadowType shadow;
	GdkRectangle rect;
	gboolean show_popup_arrow;

	/* Display the popup arrow if we are the cursor cell, or the popup
	   is shown for this cell. */
	show_popup_arrow = e_table_model_is_cell_editable (ecv->e_table_model, model_col, row) &&
		(flags & E_CELL_CURSOR ||
		 (ecp->popup_shown && ecp->popup_view_col == view_col
		  && ecp->popup_row == row
		  && ecp->popup_model == ((ECellView *) ecp_view)->e_table_model));

	if (flags & E_CELL_CURSOR)
		ecp->popup_arrow_shown = show_popup_arrow;

#if 0
	g_print ("In ecp_draw row:%i col: %i %i,%i %i,%i Show Arrow:%i\n",
		 row, view_col, x1, y1, x2, y2, show_popup_arrow);
#endif

	if (show_popup_arrow) {
		e_cell_draw (ecp_view->child_view, drawable, model_col,
			     view_col, row, flags,
			     x1, y1, x2 - E_CELL_POPUP_ARROW_WIDTH, y2);

		rect.x = x2 - E_CELL_POPUP_ARROW_WIDTH;
		rect.y = y1 + 1;
		rect.width = E_CELL_POPUP_ARROW_WIDTH;
		rect.height = y2 - y1 - 2;

		if (ecp->popup_shown)
			shadow = GTK_SHADOW_IN;
		else
			shadow = GTK_SHADOW_OUT;

		gtk_paint_box (canvas->style, drawable,
			       GTK_STATE_NORMAL, shadow,
			       &rect, canvas, "ecellpopup",
			       rect.x, rect.y, rect.width, rect.height);
		gtk_paint_arrow (canvas->style, drawable,
				 GTK_STATE_NORMAL, shadow,
				 &rect, canvas, NULL,
				 GTK_ARROW_DOWN, TRUE,
				 rect.x + E_CELL_POPUP_ARROW_XPAD,
				 rect.y + E_CELL_POPUP_ARROW_YPAD,
				 rect.width - E_CELL_POPUP_ARROW_XPAD * 2,
				 rect.height - E_CELL_POPUP_ARROW_YPAD * 2);
	} else {
		e_cell_draw (ecp_view->child_view, drawable, model_col,
			     view_col, row, flags, x1, y1, x2, y2);
	}
}


/*
 * ECell::event method
 */
static gint
ecp_event (ECellView *ecv, GdkEvent *event, int model_col, int view_col,
	   int row, ECellFlags flags, ECellActions *actions)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;
	ECellPopup *ecp = E_CELL_POPUP (ecp_view->cell_view.ecell);
	ETableItem *eti = E_TABLE_ITEM (ecv->e_table_item_view);
	int width;

	switch (event->type) {
	case GDK_BUTTON_PRESS:
		if (e_table_model_is_cell_editable (ecv->e_table_model, model_col, row) &&
		    flags & E_CELL_CURSOR
		    && ecp->popup_arrow_shown) {
			width = e_table_header_col_diff (eti->header, view_col,
							 view_col + 1);

			g_print ("Event in item popup width: %i button: %g,%g\n",
				 width, event->button.x, event->button.y);

			/* FIXME: The event coords seem to be relative to the
			   text within the cell, so we have to add 4. */
			if (event->button.x + 4 >= width - E_CELL_POPUP_ARROW_WIDTH) {
				return e_cell_popup_do_popup (ecp_view, event, row, view_col);
			}
		}
		break;
	case GDK_KEY_PRESS:
		if (e_table_model_is_cell_editable (ecv->e_table_model, model_col, row) &&
		    event->key.state & GDK_MOD1_MASK
		    && event->key.keyval == GDK_Down) {
			g_print ("## Alt-Down pressed\n");
			return e_cell_popup_do_popup (ecp_view, event, row, view_col);
		}
		g_print ("Key Press Event ECellPopup\n");
		break;
	default:
		break;
	}

	return e_cell_event (ecp_view->child_view, event, model_col, view_col,
			     row, flags, actions);
}


/*
 * ECell::height method
 */
static int
ecp_height (ECellView *ecv, int model_col, int view_col, int row) 
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	return e_cell_height (ecp_view->child_view, model_col, view_col, row);
}


/*
 * ECellView::enter_edit method
 */
static void *
ecp_enter_edit (ECellView *ecv, int model_col, int view_col, int row)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	g_print ("In ecp_enter_edit model_col: %i view_col: %i row: %i\n",
		 model_col, view_col, row);

	return e_cell_enter_edit (ecp_view->child_view, model_col, view_col, row);
}


/*
 * ECellView::leave_edit method
 */
static void
ecp_leave_edit (ECellView *ecv, int model_col, int view_col, int row,
		void *edit_context)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	g_print ("In ecp_leave_edit model_col: %i view_col: %i row: %i\n",
		 model_col, view_col, row);

	e_cell_leave_edit (ecp_view->child_view, model_col, view_col, row,
			   edit_context);
}


static void
ecp_print (ECellView *ecv, GnomePrintContext *context, 
	   int model_col, int view_col, int row, double width, double height)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	e_cell_print (ecp_view->child_view, context, model_col, view_col, row,
		      width, height);
}


static gdouble
ecp_print_height (ECellView *ecv, GnomePrintContext *context, 
		  int model_col, int view_col, int row,
		  double width)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	return e_cell_print_height (ecp_view->child_view, context, model_col,
				    view_col, row, width);
}


static int
ecp_max_width (ECellView *ecv,
	       int model_col,
	       int view_col)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	return e_cell_max_width (ecp_view->child_view, model_col, view_col);
}


static void
ecp_show_tooltip (ECellView *ecv, 
		  int model_col,
		  int view_col,
		  int row,
		  int col_width,
		  ETableTooltip *tooltip)
{
	ECellPopupView *ecp_view = (ECellPopupView *) ecv;

	e_cell_show_tooltip (ecp_view->child_view, model_col, view_col, row,
			     col_width, tooltip);
}

static char *
ecp_get_bg_color (ECellView *ecell_view, int row)
{		
	ECellPopupView *ecp_view = (ECellPopupView *) ecell_view;

	return e_cell_get_bg_color (ecp_view->child_view, row);
}



ECell*
e_cell_popup_get_child			(ECellPopup	*ecp)
{
	g_return_val_if_fail (E_IS_CELL_POPUP (ecp), NULL);

	return ecp->child;
}


void
e_cell_popup_set_child			(ECellPopup	*ecp,
					 ECell		*child)
{
	g_return_if_fail (E_IS_CELL_POPUP (ecp));

	if (ecp->child)
		gtk_object_unref (GTK_OBJECT (ecp->child));

	ecp->child = child;
	gtk_object_ref (GTK_OBJECT (child));
}


static gint
e_cell_popup_do_popup			(ECellPopupView	*ecp_view,
					 GdkEvent	*event,
					 int             row,
					 int             view_col)
{
	ECellPopup *ecp = E_CELL_POPUP (ecp_view->cell_view.ecell);
	gint (*popup_func) (ECellPopup *ecp, GdkEvent *event, int row, int view_col);

	ecp->popup_cell_view = ecp_view;

	popup_func = E_CELL_POPUP_CLASS (GTK_OBJECT (ecp)->klass)->popup;

	ecp->popup_view_col = view_col;
	ecp->popup_row = row;
	ecp->popup_model = ((ECellView *) ecp_view)->e_table_model;

	return popup_func ? popup_func (ecp, event, row, view_col) : FALSE;
}

/* This redraws the popup cell. Only use this if you know popup_view_col and
   popup_row are valid. */
void
e_cell_popup_queue_cell_redraw (ECellPopup *ecp)
{
       ETableItem *eti = E_TABLE_ITEM (ecp->popup_cell_view->cell_view.e_table_item_view);

       e_table_item_redraw_range (eti, ecp->popup_view_col, ecp->popup_row,
                                  ecp->popup_view_col, ecp->popup_row);
}

void
e_cell_popup_set_shown  (ECellPopup *ecp,
			 gboolean    shown)
{
	ecp->popup_shown = shown;
	e_cell_popup_queue_cell_redraw (ecp);
}
