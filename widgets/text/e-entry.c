/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-entry.c - An EText-based entry widget
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey     <clahey@ximian.com>
 *   Jon Trowbridge  <trow@ximian.com>
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
 * 02111-1307, USA.
 */

#include <config.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <stdio.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtksignal.h>
#include <gnome-xml/parser.h>
#include <libgnomeui/gnome-canvas.h>
#include "gal/util/e-util.h"
#include "gal/widgets/e-canvas.h"
#include "gal/widgets/e-canvas-utils.h"
#include "e-completion-view.h"
#include "e-text.h"
#include "e-entry.h"

#define MOVE_RIGHT_AND_UP 0

#define EVIL_POINTER_WARPING_HACK

#ifdef EVIL_POINTER_WARPING_HACK
#include <gdk/gdkx.h>
#endif

#define MIN_ENTRY_WIDTH  150

#define d(x)

#define PARENT_TYPE gtk_table_get_type ()

static GtkObjectClass *parent_class;

enum {
	E_ENTRY_CHANGED,
	E_ENTRY_ACTIVATE,
	E_ENTRY_POPUP,
	E_ENTRY_COMPLETION_POPUP,
	E_ENTRY_LAST_SIGNAL
};

static guint e_entry_signals[E_ENTRY_LAST_SIGNAL] = { 0 };

/* Object argument IDs */
enum {
	ARG_0,
	ARG_MODEL,
	ARG_EVENT_PROCESSOR,
	ARG_TEXT,
	ARG_FONT,
        ARG_FONTSET,
	ARG_FONT_GDK,
	ARG_ANCHOR,
	ARG_JUSTIFICATION,
	ARG_X_OFFSET,
	ARG_Y_OFFSET,
	ARG_FILL_COLOR,
	ARG_FILL_COLOR_GDK,
	ARG_FILL_COLOR_RGBA,
	ARG_FILL_STIPPLE,
	ARG_EDITABLE,
	ARG_USE_ELLIPSIS,
	ARG_ELLIPSIS,
	ARG_LINE_WRAP,
	ARG_BREAK_CHARACTERS,
	ARG_MAX_LINES,
	ARG_ALLOW_NEWLINES,
	ARG_DRAW_BORDERS,
	ARG_DRAW_BACKGROUND,
	ARG_DRAW_BUTTON,
	ARG_EMULATE_LABEL_RESIZE,
	ARG_CURSOR_POS
};

typedef struct _EEntryPrivate EEntryPrivate;
struct _EEntryPrivate {
	GtkJustification justification;

	guint changed_proxy_tag;
	guint activate_proxy_tag;
	guint popup_proxy_tag;
	/* Data related to completions */
	ECompletion *completion;
	EEntryCompletionHandler handler;
	GtkWidget *completion_view;
	guint nonempty_signal_id;
	guint added_signal_id;
	guint full_signal_id;
	guint browse_signal_id;
	guint unbrowse_signal_id;
	guint activate_signal_id;
	GtkWidget *completion_view_popup;
	gboolean popup_is_visible;
	gchar *pre_browse_text;
	gint completion_delay;
	guint completion_delay_tag;
	gboolean ptr_grab;
	gboolean changed_since_keypress;
	guint changed_since_keypress_tag;
	gint last_completion_pos;

	guint draw_borders : 1;
	guint emulate_label_resize : 1;
	guint have_set_transient : 1;
	gint last_width;
};

static gboolean e_entry_is_empty              (EEntry *entry);
static void e_entry_show_popup                (EEntry *entry, gboolean x);
static void e_entry_start_completion          (EEntry *entry);
static void e_entry_start_delayed_completion  (EEntry *entry, gint delay);
static void e_entry_cancel_delayed_completion (EEntry *entry);

static void
canvas_size_allocate (GtkWidget *widget, GtkAllocation *alloc,
		      EEntry *entry)
{
	gnome_canvas_set_scroll_region (entry->canvas,
					0, 0, alloc->width, alloc->height);
	gtk_object_set (GTK_OBJECT (entry->item),
			"clip_width", (double) (alloc->width),
			"clip_height", (double) (alloc->height),
			NULL);

	switch (entry->priv->justification) {
	case GTK_JUSTIFY_RIGHT:
		e_canvas_item_move_absolute(GNOME_CANVAS_ITEM(entry->item),
					    alloc->width, 0);
		break;
	case GTK_JUSTIFY_CENTER:
		e_canvas_item_move_absolute(GNOME_CANVAS_ITEM(entry->item),
					    alloc->width / 2, 0);
		break;
	default:
		e_canvas_item_move_absolute(GNOME_CANVAS_ITEM(entry->item),
					    0, 0);
		break;
	}
}

static void
canvas_size_request (GtkWidget *widget, GtkRequisition *requisition,
		     EEntry *entry)
{
	int xthick, ythick;
	
	g_return_if_fail (widget != NULL);
	g_return_if_fail (GNOME_IS_CANVAS (widget));
	g_return_if_fail (requisition != NULL);

	if (entry->priv->draw_borders) {
		xthick = 2 * widget->style->klass->xthickness;
		ythick = 2 * widget->style->klass->ythickness;
	} else {
		xthick = ythick = 0;
	}

	if (entry->priv->emulate_label_resize) {
		gdouble width;
		gtk_object_get (GTK_OBJECT (entry->item),
				"text_width", &width,
				NULL);
		requisition->width = 2 + xthick + width;
	} else {
		requisition->width = 2 + MIN_ENTRY_WIDTH + xthick;
	}
	if (entry->priv->last_width != requisition->width)
		gtk_widget_queue_resize (widget);
	entry->priv->last_width = requisition->width;

	d(g_print("%s: width = %d\n", __FUNCTION__, requisition->width));

	requisition->height = (2 + widget->style->font->ascent +
			       widget->style->font->descent +
			       ythick);
}

static gint
canvas_focus_in_event (GtkWidget *widget, GdkEventFocus *focus, EEntry *entry)
{
	if (entry->canvas->focused_item != GNOME_CANVAS_ITEM(entry->item))
		gnome_canvas_item_grab_focus(GNOME_CANVAS_ITEM(entry->item));

	return FALSE;
}

static void
e_entry_text_keypress (EText *text, guint keyval, guint state, EEntry *entry)
{
	if (entry->priv->changed_since_keypress_tag) {
		gtk_timeout_remove (entry->priv->changed_since_keypress_tag);
		entry->priv->changed_since_keypress_tag = 0;
	}
	
	if (entry->priv->changed_since_keypress
	    || (entry->priv->popup_is_visible && e_entry_get_position (entry) != entry->priv->last_completion_pos)) {
		if (e_entry_is_empty (entry)) {
			e_entry_cancel_delayed_completion (entry);
			e_entry_show_popup (entry, FALSE);
		} else if (entry->priv->completion_delay >= 0) {
			int delay;
			delay = entry->priv->popup_is_visible 
				? 1 
				: entry->priv->completion_delay;
			e_entry_start_delayed_completion (entry, delay);
		}
	}
	entry->priv->changed_since_keypress = FALSE;
}

static gint
changed_since_keypress_timeout_fn (gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);
	entry->priv->changed_since_keypress = FALSE;
	entry->priv->changed_since_keypress_tag = 0;
	return FALSE;
}

static void
e_entry_proxy_changed (EText *text, EEntry *entry)
{
	if (entry->priv->changed_since_keypress_tag)
		gtk_timeout_remove (entry->priv->changed_since_keypress_tag);
	entry->priv->changed_since_keypress = TRUE;
	entry->priv->changed_since_keypress_tag = gtk_timeout_add (20, changed_since_keypress_timeout_fn, entry);
	
	gtk_signal_emit (GTK_OBJECT (entry), e_entry_signals [E_ENTRY_CHANGED]);
}

static void
e_entry_proxy_activate (EText *text, EEntry *entry)
{
	gtk_signal_emit (GTK_OBJECT (entry), e_entry_signals [E_ENTRY_ACTIVATE]);
}

static void
e_entry_proxy_popup (EText *text, GdkEventButton *ev, gint pos, EEntry *entry)
{
	gtk_signal_emit (GTK_OBJECT (entry), e_entry_signals [E_ENTRY_POPUP], ev, pos);
}

static void
e_entry_init (GtkObject *object)
{
	EEntry *entry = E_ENTRY (object);
	GtkTable *gtk_table = GTK_TABLE (object);

	entry->priv = g_new0 (EEntryPrivate, 1);

	entry->priv->emulate_label_resize = FALSE;
	
	entry->canvas = GNOME_CANVAS (e_canvas_new ());

	gtk_signal_connect (GTK_OBJECT (entry->canvas),
			    "size_allocate",
			    GTK_SIGNAL_FUNC (canvas_size_allocate),
			    entry);

	gtk_signal_connect (GTK_OBJECT (entry->canvas),
			    "size_request",
			    GTK_SIGNAL_FUNC (canvas_size_request),
			    entry);

	gtk_signal_connect(GTK_OBJECT (entry->canvas),
			   "focus_in_event",
			   GTK_SIGNAL_FUNC(canvas_focus_in_event),
			   entry);

	entry->priv->draw_borders = TRUE;
	entry->priv->last_width = -1;

	entry->item = E_TEXT(gnome_canvas_item_new(
		gnome_canvas_root (entry->canvas),
		e_text_get_type(),
		"clip", TRUE,
		"fill_clip_rectangle", TRUE,
		"anchor", GTK_ANCHOR_NW,
		"draw_borders", TRUE,
		"draw_background", TRUE,
		"draw_button", FALSE,
		"max_lines", 1,
		"editable", TRUE,
		"allow_newlines", FALSE,
		NULL));

	gtk_signal_connect (GTK_OBJECT (entry->item),
			    "keypress",
			    GTK_SIGNAL_FUNC (e_entry_text_keypress),
			    entry);

	entry->priv->justification = GTK_JUSTIFY_LEFT;
	gtk_table_attach (gtk_table, GTK_WIDGET (entry->canvas),
			  0, 1, 0, 1,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (entry->canvas));

	/*
	 * Proxy functions: we proxy the changed and activate signals
	 * from the item to ourselves
	 */
	entry->priv->changed_proxy_tag = gtk_signal_connect (
		GTK_OBJECT (entry->item),
		"changed",
		GTK_SIGNAL_FUNC (e_entry_proxy_changed),
		entry);
	entry->priv->activate_proxy_tag = gtk_signal_connect (
		GTK_OBJECT (entry->item),
		"activate",
		GTK_SIGNAL_FUNC (e_entry_proxy_activate),
		entry);
	entry->priv->popup_proxy_tag = gtk_signal_connect (
		GTK_OBJECT (entry->item),
		"popup",
		GTK_SIGNAL_FUNC (e_entry_proxy_popup),
		entry);

	entry->priv->completion_delay = 1;
}

/**
 * e_entry_construct
 * 
 * Constructs the given EEntry.
 * 
 **/
void
e_entry_construct (EEntry *entry)
{
	/* Do nothing */
}


/**
 * e_entry_new
 * 
 * Creates a new EEntry.
 * 
 * Returns: The new EEntry
 **/
GtkWidget *
e_entry_new (void)
{
	EEntry *entry;
	entry = gtk_type_new (e_entry_get_type ());
	e_entry_construct (entry);

	return GTK_WIDGET (entry);
}

const gchar *
e_entry_get_text (EEntry *entry)
{
	g_return_val_if_fail (entry != NULL && E_IS_ENTRY (entry), NULL);

	return e_text_model_get_text (entry->item->model);
}

void
e_entry_set_text (EEntry *entry, const gchar *txt)
{
	g_return_if_fail (entry != NULL && E_IS_ENTRY (entry));

	e_text_model_set_text (entry->item->model, txt);
}

static void
e_entry_set_text_quiet (EEntry *entry, const gchar *txt)
{
	g_return_if_fail (entry != NULL && E_IS_ENTRY (entry));

	gtk_signal_handler_block (GTK_OBJECT (entry->item), entry->priv->changed_proxy_tag);
	e_entry_set_text (entry, txt);
	gtk_signal_handler_unblock (GTK_OBJECT (entry->item), entry->priv->changed_proxy_tag);
}


void
e_entry_set_editable (EEntry *entry, gboolean am_i_editable)
{
	g_return_if_fail (entry != NULL && E_IS_ENTRY (entry));

	gtk_object_set (GTK_OBJECT (entry->item), "editable", am_i_editable, NULL);
}

gint
e_entry_get_position (EEntry *entry)
{
	g_return_val_if_fail (entry != NULL && E_IS_ENTRY (entry), -1);

	return entry->item->selection_start;
}

void
e_entry_set_position (EEntry *entry, gint pos)
{
	g_return_if_fail (entry != NULL && E_IS_ENTRY (entry));
	if (pos < 0)
		pos = 0;
	else if (pos > e_text_model_get_text_length (entry->item->model))
		pos = e_text_model_get_text_length (entry->item->model);

	entry->item->selection_start = entry->item->selection_end = pos;
}

void
e_entry_select_region (EEntry *entry, gint pos1, gint pos2)
{
	gint len;

	g_return_if_fail (entry != NULL && E_IS_ENTRY (entry));
	
	len = e_text_model_get_text_length (entry->item->model);
	pos1 = CLAMP (pos1, 0, len);
	pos2 = CLAMP (pos2, 0, len);

	entry->item->selection_start = MIN (pos1, pos2);
	entry->item->selection_end   = MAX (pos1, pos2);
}

/** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** **/

/*** Completion-related code ***/

static gboolean
e_entry_is_empty (EEntry *entry)
{
	const gchar *txt = e_entry_get_text (entry);

	if (txt == NULL)
		return TRUE;

	while (*txt) {
		if (!isspace ((gint) *txt))
			return FALSE;
		++txt;
	}
	
	return TRUE;
}

static void
e_entry_show_popup (EEntry *entry, gboolean visible)
{
	GtkWidget *pop = entry->priv->completion_view_popup;

	if (pop == NULL)
		return;

	/* The async query can give us a result after the focus was lost by the
	   widget.  In that case, we don't want to show the pop-up.   */
	if (! GTK_WIDGET_HAS_FOCUS (entry->canvas))
		return;

	if (visible) {
		GtkAllocation *dim = &(GTK_WIDGET (entry)->allocation);
		gint x, y, xo, yo, fudge;
		const GdkEventMask grab_mask = (GdkEventMask)GDK_BUTTON_PRESS_MASK | GDK_BUTTON_MOTION_MASK | GDK_BUTTON_RELEASE_MASK;

		/* Figure out where to put our popup. */
		gdk_window_get_origin (GTK_WIDGET (entry)->window, &xo, &yo);
		x = xo + dim->x;
		y = yo + dim->height + dim->y;

#if MOVE_RIGHT_AND_UP
		/* Put our popup slightly to the right and up, to try to give a visual cue that this popup
		 is tied to this entry.  Otherwise one-row popups can sort of "blend" with an entry
		 directly below. */
		fudge = MAX (dim->height/10, 3); /* just in case we are using a really big font, etc. */
		x += 2*fudge;
		y -= fudge;
#else
		fudge = 1;
		y -= fudge;
#endif
		gtk_widget_set_uposition (pop, x, y);
		e_completion_view_set_width (E_COMPLETION_VIEW (entry->priv->completion_view), dim->width);

#ifdef EVIL_POINTER_WARPING_HACK
		/*
		  I should have learned by now to listen to Havoc... 
		  http://developer.gnome.org/doc/GGAD/faqs.html
		*/
		   
		if (! entry->priv->popup_is_visible) {
			GdkWindow *gwin = GTK_WIDGET (entry)->window;
			gint xx, yy;
			gdk_window_get_pointer (gwin, &xx, &yy, NULL);
			xx += xo;
			yy += yo;
			
			/* If we are inside the "zone of death" where the popup will appear, warp the pointer to safety.
			   This is a horrible thing to do. */
			if (y <= yy && yy < yy + dim->height && x <= xx && xx < xx + dim->width) {
				XWarpPointer (GDK_WINDOW_XDISPLAY (gwin), None, GDK_WINDOW_XWINDOW (gwin),
					      0, 0, 0, 0, 
					      xx - xo, (y-1) - yo);
			}
		}
#endif
	
		gtk_widget_show (pop);


		if (getenv ("GAL_E_ENTRY_NO_GRABS_HACK") == NULL && !entry->priv->ptr_grab) {
			entry->priv->ptr_grab = (0 == gdk_pointer_grab (GTK_WIDGET (entry->priv->completion_view)->window, TRUE,
									grab_mask, NULL, NULL, GDK_CURRENT_TIME));
			if (entry->priv->ptr_grab) {
 				gtk_grab_add (GTK_WIDGET (entry->priv->completion_view));
			}
		}
		
		
	} else {

		gtk_widget_hide (pop);

		if (entry->priv->ptr_grab) {
			gdk_pointer_ungrab (GDK_CURRENT_TIME);
			gtk_grab_remove (GTK_WIDGET (entry->priv->completion_view));
		}

		entry->priv->ptr_grab = FALSE;

		entry->priv->last_completion_pos = -1;
	}

	e_completion_view_set_editable (E_COMPLETION_VIEW (entry->priv->completion_view), visible);

	if (entry->priv->popup_is_visible != visible) {
		entry->priv->popup_is_visible = visible;
		gtk_signal_emit (GTK_OBJECT (entry), e_entry_signals[E_ENTRY_COMPLETION_POPUP], (gint) visible);
	}
}

static void
e_entry_refresh_popup (EEntry *entry)
{
	if (entry->priv->popup_is_visible)
		e_entry_show_popup (entry, TRUE);
}

static void
e_entry_start_completion (EEntry *entry)
{
	if (entry->priv->completion == NULL)
		return;

	e_entry_cancel_delayed_completion (entry);

	if (e_entry_is_empty (entry))
		return;

	e_completion_begin_search (entry->priv->completion,
				   e_entry_get_text (entry),
				   entry->priv->last_completion_pos = e_entry_get_position (entry),
				   0); /* No limit.  Probably a bad idea. */
}

static gboolean
start_delayed_cb (gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);
	entry->priv->completion_delay_tag = 0;
	e_entry_start_completion (entry);
	return FALSE;
}

static void
e_entry_start_delayed_completion (EEntry *entry, gint delay)
{
	if (delay < 0)
		return;

	e_entry_cancel_delayed_completion (entry);
	entry->priv->completion_delay_tag = gtk_timeout_add (MAX (delay, 1), start_delayed_cb, entry);
}

static void
e_entry_cancel_delayed_completion (EEntry *entry)
{
	if (entry->priv->completion == NULL)
		return;

	if (entry->priv->completion_delay_tag) {
		gtk_timeout_remove (entry->priv->completion_delay_tag);
		entry->priv->completion_delay_tag = 0;
	}
}

static void
nonempty_cb (ECompletionView *view, gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);

	e_entry_show_popup (entry, TRUE);
}

static void
added_cb (ECompletionView *view, gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);
	e_entry_refresh_popup (entry);
}

static void
full_cb (ECompletionView *view, gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);
	gboolean show;

	show = GTK_WIDGET_HAS_FOCUS (GTK_WIDGET (entry->canvas)) && view->choices->len > 0;
	e_entry_show_popup (entry, show);
}

static void
browse_cb (ECompletionView *view, ECompletionMatch *match, gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);
	
	if (match == NULL) {
		/* Requesting a completion. */
		e_entry_start_completion (entry);
		return;
	}

	if (entry->priv->pre_browse_text == NULL)
		entry->priv->pre_browse_text = g_strdup (e_entry_get_text (entry));

	/* If there is no other handler in place, echo the selected completion in
	   the entry. */
	if (entry->priv->handler == NULL)
		e_entry_set_text_quiet (entry, e_completion_match_get_match_text (match));
}

static void
unbrowse_cb (ECompletionView *view, gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);

	if (entry->priv->pre_browse_text) {

		if (entry->priv->handler == NULL)
			e_entry_set_text_quiet (entry, entry->priv->pre_browse_text);

		g_free (entry->priv->pre_browse_text);
		entry->priv->pre_browse_text = NULL;
	}

	e_entry_show_popup (entry, FALSE);
}

static void
activate_cb (ECompletionView *view, ECompletionMatch *match, gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);

	e_entry_cancel_delayed_completion (entry);
	
	g_free (entry->priv->pre_browse_text);
	entry->priv->pre_browse_text = NULL;
	e_entry_show_popup (entry, FALSE);

	if (entry->priv->handler)
		entry->priv->handler (entry, match);
	else
		e_entry_set_text (entry, match->match_text);

	e_entry_cancel_delayed_completion (entry);
}

void
e_entry_enable_completion (EEntry *entry, ECompletion *completion)
{
	g_return_if_fail (entry != NULL && E_IS_ENTRY (entry));
	g_return_if_fail (completion != NULL && E_IS_COMPLETION (completion));

	e_entry_enable_completion_full (entry, completion, -1, NULL);
}

static void
button_press_cb (GtkWidget *w, GdkEvent *ev, gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);
	GtkWidget *child;

	/* Bail out if our click happened inside of our widget. */
	child = gtk_get_event_widget (ev);
	if (child != w) {
		while (child) {
			if (child == w)
				return;
			child = child->parent;
		}
	}

	/* Treat this as an unbrowse */
	unbrowse_cb (E_COMPLETION_VIEW (w), entry);
}

static void
cancel_completion_cb (ETextModel *model, gpointer user_data)
{
	EEntry *entry = E_ENTRY (user_data);

	/* If we get the signal from the underlying text model, unbrowse.
	   This usually means that the text model itself has done some
	   sort of completion, or has otherwise transformed its contents
	   in some way that would render any previous completion invalid. */
	unbrowse_cb (E_COMPLETION_VIEW (entry->priv->completion_view), entry);
}

static gint
key_press_cb (GtkWidget *w, GdkEventKey *ev, gpointer user_data)
{
	gint rv = 0;
	/* Forward signal */
	gtk_signal_emit_by_name (GTK_OBJECT (user_data), "key_press_event", ev, &rv);
	return rv;
}

static gint
key_release_cb (GtkWidget *w, GdkEventKey *ev, gpointer user_data)
{
	gint rv = 0;
	/* Forward signal */
	gtk_signal_emit_by_name (GTK_OBJECT (user_data), "key_release_event", ev, &rv);
	return rv;
}

static void
e_entry_make_completion_window_transient (EEntry *entry)
{
	GtkWidget *w;

	if (entry->priv->have_set_transient || entry->priv->completion_view_popup == NULL)
		return;
	
	w = GTK_WIDGET (entry)->parent;
	while (w && ! GTK_IS_WINDOW (w))
		w = w->parent;

	if (w) {
		gtk_window_set_transient_for (GTK_WINDOW (entry->priv->completion_view_popup),
					      GTK_WINDOW (w));
		entry->priv->have_set_transient = 1;
	}
}

void
e_entry_enable_completion_full (EEntry *entry, ECompletion *completion, gint delay, EEntryCompletionHandler handler)
{
	g_return_if_fail (entry != NULL && E_IS_ENTRY (entry));
	g_return_if_fail (completion != NULL && E_IS_COMPLETION (completion));

	/* For now, completion can't be changed mid-stream. */
	g_return_if_fail (entry->priv->completion == NULL);

	entry->priv->completion = completion;
	gtk_object_ref (GTK_OBJECT (completion));
	gtk_object_sink (GTK_OBJECT (completion));
	
	entry->priv->completion_delay = delay;
	entry->priv->handler = handler;

	entry->priv->completion_view = e_completion_view_new (completion);
	/* Make the up and down keys enable and disable completions. */
	e_completion_view_set_complete_key (E_COMPLETION_VIEW (entry->priv->completion_view), GDK_Down);
	e_completion_view_set_uncomplete_key (E_COMPLETION_VIEW (entry->priv->completion_view), GDK_Up);

	gtk_signal_connect_after (GTK_OBJECT (entry->priv->completion_view),
				  "button_press_event",
				  GTK_SIGNAL_FUNC (button_press_cb),
				  entry);

	entry->priv->nonempty_signal_id = gtk_signal_connect (GTK_OBJECT (entry->priv->completion_view),
							      "nonempty",
							      GTK_SIGNAL_FUNC (nonempty_cb),
							      entry);

	entry->priv->added_signal_id = gtk_signal_connect (GTK_OBJECT (entry->priv->completion_view),
							   "added",
							   GTK_SIGNAL_FUNC (added_cb),
							   entry);

	entry->priv->full_signal_id = gtk_signal_connect (GTK_OBJECT (entry->priv->completion_view),
							  "full",
							  GTK_SIGNAL_FUNC (full_cb),
							  entry);

	entry->priv->browse_signal_id = gtk_signal_connect (GTK_OBJECT (entry->priv->completion_view),
							    "browse",
							    GTK_SIGNAL_FUNC (browse_cb),
							    entry);

	entry->priv->unbrowse_signal_id = gtk_signal_connect (GTK_OBJECT (entry->priv->completion_view),
							      "unbrowse",
							      GTK_SIGNAL_FUNC (unbrowse_cb),
							      entry);

	entry->priv->activate_signal_id = gtk_signal_connect (GTK_OBJECT (entry->priv->completion_view),
							      "activate",
							      GTK_SIGNAL_FUNC (activate_cb),
							      entry);

	entry->priv->completion_view_popup = gtk_window_new (GTK_WINDOW_POPUP);

	e_entry_make_completion_window_transient (entry);

	gtk_signal_connect (GTK_OBJECT (entry->item->model),
			    "cancel_completion",
			    GTK_SIGNAL_FUNC (cancel_completion_cb),
			    entry);

	gtk_signal_connect (GTK_OBJECT (entry->priv->completion_view_popup),
			    "key_press_event",
			    GTK_SIGNAL_FUNC (key_press_cb),
			    entry->canvas);
	gtk_signal_connect (GTK_OBJECT (entry->priv->completion_view_popup),
			    "key_release_event",
			    GTK_SIGNAL_FUNC (key_release_cb),
			    entry->canvas);

	e_completion_view_connect_keys (E_COMPLETION_VIEW (entry->priv->completion_view),
					GTK_WIDGET (entry->canvas));

	gtk_object_ref (GTK_OBJECT (entry->priv->completion_view_popup));
	gtk_object_sink (GTK_OBJECT (entry->priv->completion_view_popup));
	gtk_window_set_policy (GTK_WINDOW (entry->priv->completion_view_popup), TRUE, TRUE, TRUE);
	gtk_container_add (GTK_CONTAINER (entry->priv->completion_view_popup), entry->priv->completion_view);
	gtk_widget_show (entry->priv->completion_view);
}

gboolean
e_entry_completion_popup_is_visible (EEntry *entry)
{
	g_return_val_if_fail (E_IS_ENTRY (entry), FALSE);

	return entry->priv->popup_is_visible;
}

/** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** **/

static void
et_get_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	EEntry *entry = E_ENTRY (o);
	GtkObject *item = GTK_OBJECT (entry->item);
	
	switch (arg_id){
	case ARG_MODEL:
		gtk_object_get(item,
			       "model", &GTK_VALUE_OBJECT (*arg),
			       NULL);
		break;

	case ARG_EVENT_PROCESSOR:
		gtk_object_get(item,
			       "event_processor", &GTK_VALUE_OBJECT (*arg),
			       NULL);
		break;

	case ARG_TEXT:
		gtk_object_get(item,
			       "text", &GTK_VALUE_STRING (*arg),
			       NULL);
		break;

	case ARG_FONT_GDK:
		gtk_object_get(item,
			       "font_gdk", &GTK_VALUE_BOXED (*arg),
			       NULL);
		break;

	case ARG_JUSTIFICATION:
		gtk_object_get(item,
			       "justification", &GTK_VALUE_ENUM (*arg),
			       NULL);
		break;

	case ARG_FILL_COLOR_GDK:
		gtk_object_get(item,
			       "fill_color_gdk", &GTK_VALUE_BOXED (*arg),
			       NULL);
		break;

	case ARG_FILL_COLOR_RGBA:
		gtk_object_get(item,
			       "fill_color_rgba", &GTK_VALUE_UINT (*arg),
			       NULL);
		break;

	case ARG_FILL_STIPPLE:
		gtk_object_get(item,
			       "fill_stiple", &GTK_VALUE_BOXED (*arg),
			       NULL);
		break;

	case ARG_EDITABLE:
		gtk_object_get(item,
			       "editable", &GTK_VALUE_BOOL (*arg),
			       NULL);
		break;

	case ARG_USE_ELLIPSIS:
		gtk_object_get(item,
			       "use_ellipsis", &GTK_VALUE_BOOL (*arg),
			       NULL);
		break;

	case ARG_ELLIPSIS:
		gtk_object_get(item,
			       "ellipsis", &GTK_VALUE_STRING (*arg),
			       NULL);
		break;

	case ARG_LINE_WRAP:
		gtk_object_get(item,
			       "line_wrap", &GTK_VALUE_BOOL (*arg),
			       NULL);
		break;
		
	case ARG_BREAK_CHARACTERS:
		gtk_object_get(item,
			       "break_characters", &GTK_VALUE_STRING (*arg),
			       NULL);
		break;

	case ARG_MAX_LINES:
		gtk_object_get(item,
			       "max_lines", &GTK_VALUE_INT (*arg),
			       NULL);
		break;
	case ARG_ALLOW_NEWLINES:
		gtk_object_get(item,
			       "allow_newlines", &GTK_VALUE_BOOL (*arg),
			       NULL);
		break;

	case ARG_DRAW_BORDERS:
		GTK_VALUE_BOOL (*arg) = entry->priv->draw_borders;
		break;

	case ARG_DRAW_BACKGROUND:
		gtk_object_get (item,
				"draw_background", &GTK_VALUE_BOOL (*arg),
				NULL);
		break;

	case ARG_DRAW_BUTTON:
		gtk_object_get (item,
				"draw_button", &GTK_VALUE_BOOL (*arg),
				NULL);
		break;

	case ARG_EMULATE_LABEL_RESIZE:
		GTK_VALUE_BOOL (*arg) = entry->priv->emulate_label_resize;
		break;

	case ARG_CURSOR_POS:
		gtk_object_get (item,
				"cursor_pos", &GTK_VALUE_INT (*arg),
				NULL);
		
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

static void
et_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	EEntry *entry = E_ENTRY (o);
	GtkObject *item = GTK_OBJECT (entry->item);
	GtkAnchorType anchor;
	double width, height;
	gint xthick;
	gint ythick;
	GtkWidget *widget = GTK_WIDGET(entry->canvas);
	
	d(g_print("%s: arg_id: %d\n", __FUNCTION__, arg_id));

	switch (arg_id){
	case ARG_MODEL:
		gtk_object_set(item,
			       "model", GTK_VALUE_OBJECT (*arg),
			       NULL);
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_EVENT_PROCESSOR:
		gtk_object_set(item,
			       "event_processor", GTK_VALUE_OBJECT (*arg),
			       NULL);
		break;

	case ARG_TEXT:
		gtk_object_set(item,
			       "text", GTK_VALUE_STRING (*arg),
			       NULL);
		d(g_print("%s: text: %s\n", __FUNCTION__, GTK_VALUE_STRING (*arg)));
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_FONT:
		gtk_object_set(item,
			       "font", GTK_VALUE_STRING (*arg),
			       NULL);
		d(g_print("%s: font: %s\n", __FUNCTION__, GTK_VALUE_STRING (*arg)));
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_FONTSET:
		gtk_object_set(item,
			       "fontset", GTK_VALUE_STRING (*arg),
			       NULL);
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_FONT_GDK:
		gtk_object_set(item,
			       "font_gdk", GTK_VALUE_BOXED (*arg),
			       NULL);
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_JUSTIFICATION:
		entry->priv->justification = GTK_VALUE_ENUM (*arg);
		gtk_object_get(item,
			       "clip_width", &width,
			       "clip_height", &height,
			       NULL);

		if (entry->priv->draw_borders) {
			xthick = 0;
			ythick = 0;
		} else {
			xthick = widget->style->klass->xthickness;
			ythick = widget->style->klass->ythickness;
		}

		switch (entry->priv->justification) {
		case GTK_JUSTIFY_CENTER:
			anchor = GTK_ANCHOR_N;
			e_canvas_item_move_absolute(GNOME_CANVAS_ITEM(entry->item), width / 2, ythick);
			break;
		case GTK_JUSTIFY_RIGHT:
			anchor = GTK_ANCHOR_NE;
			e_canvas_item_move_absolute(GNOME_CANVAS_ITEM(entry->item), width - xthick, ythick);
			break;
		default:
			anchor = GTK_ANCHOR_NW;
			e_canvas_item_move_absolute(GNOME_CANVAS_ITEM(entry->item), xthick, ythick);
			break;
		}
		gtk_object_set(item,
			       "justification", entry->priv->justification,
			       "anchor", anchor,
			       NULL);
		break;

	case ARG_FILL_COLOR:
		gtk_object_set(item,
			       "fill_color", GTK_VALUE_STRING (*arg),
			       NULL);
		break;

	case ARG_FILL_COLOR_GDK:
		gtk_object_set(item,
			       "fill_color_gdk", GTK_VALUE_BOXED (*arg),
			       NULL);
		break;

	case ARG_FILL_COLOR_RGBA:
		gtk_object_set(item,
			       "fill_color_rgba", GTK_VALUE_UINT (*arg),
			       NULL);
		break;

	case ARG_FILL_STIPPLE:
		gtk_object_set(item,
			       "fill_stiple", GTK_VALUE_BOXED (*arg),
			       NULL);
		break;

	case ARG_EDITABLE:
		gtk_object_set(item,
			       "editable", GTK_VALUE_BOOL (*arg),
			       NULL);
		break;

	case ARG_USE_ELLIPSIS:
		gtk_object_set(item,
			       "use_ellipsis", GTK_VALUE_BOOL (*arg),
			       NULL);
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_ELLIPSIS:
		gtk_object_set(item,
			       "ellipsis", GTK_VALUE_STRING (*arg),
			       NULL);
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_LINE_WRAP:
		gtk_object_set(item,
			       "line_wrap", GTK_VALUE_BOOL (*arg),
			       NULL);
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;
		
	case ARG_BREAK_CHARACTERS:
		gtk_object_set(item,
			       "break_characters", GTK_VALUE_STRING (*arg),
			       NULL);
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_MAX_LINES:
		gtk_object_set(item,
			       "max_lines", GTK_VALUE_INT (*arg),
			       NULL);
		if (entry->priv->emulate_label_resize)
			gtk_widget_queue_resize (widget);
		break;

	case ARG_ALLOW_NEWLINES:
		gtk_object_set(item,
			       "allow_newlines", GTK_VALUE_BOOL (*arg),
			       NULL);
		break;

	case ARG_DRAW_BORDERS:
		if (entry->priv->draw_borders != GTK_VALUE_BOOL (*arg)) {
			entry->priv->draw_borders = GTK_VALUE_BOOL (*arg);
			gtk_object_set (item,
					"draw_borders", entry->priv->draw_borders,
					NULL);
			gtk_widget_queue_resize (GTK_WIDGET (entry));
		}
		break;

	case ARG_CURSOR_POS:
		gtk_object_set (item,
				"cursor_pos", GTK_VALUE_INT (*arg), NULL);
		break;
		
	case ARG_DRAW_BACKGROUND:
		gtk_object_set (item, "draw_background",
				GTK_VALUE_BOOL (*arg), NULL);
		break;

	case ARG_DRAW_BUTTON:
		gtk_object_set (item, "draw_button",
				GTK_VALUE_BOOL (*arg), NULL);
		break;

	case ARG_EMULATE_LABEL_RESIZE:
		if (entry->priv->emulate_label_resize != GTK_VALUE_BOOL (*arg)) {
			entry->priv->emulate_label_resize = GTK_VALUE_BOOL (*arg);
			gtk_widget_queue_resize (widget);
		}		
		break;
	}
}

static void
e_entry_destroy (GtkObject *object)
{
	EEntry *entry = E_ENTRY (object);

	if (entry->priv->completion_delay_tag)
		gtk_timeout_remove (entry->priv->completion_delay_tag);

	if (entry->priv->completion)
		gtk_object_unref (GTK_OBJECT (entry->priv->completion));
	if (entry->priv->completion_view_popup) {
		gtk_widget_destroy (GTK_WIDGET (entry->priv->completion_view_popup));
		gtk_object_unref (GTK_OBJECT (entry->priv->completion_view_popup));
	}
	g_free (entry->priv->pre_browse_text);

	if (entry->priv->changed_since_keypress_tag)
		gtk_timeout_remove (entry->priv->changed_since_keypress_tag);

	if (entry->priv->ptr_grab)
		gdk_pointer_ungrab (GDK_CURRENT_TIME);

	g_free (entry->priv);
	entry->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void
e_entry_realize (GtkWidget *widget)
{
	EEntry *entry;

	if (GTK_WIDGET_CLASS (parent_class)->realize)
		(* GTK_WIDGET_CLASS (parent_class)->realize) (widget);

	entry = E_ENTRY (widget);

	e_entry_make_completion_window_transient (entry);

	if (entry->priv->emulate_label_resize) {
		d(g_print("%s: queue_resize\n", __FUNCTION__));
		gtk_widget_queue_resize (GTK_WIDGET (entry->canvas));
	}
}

static void
e_entry_class_init (GtkObjectClass *object_class)
{
	EEntryClass *klass = E_ENTRY_CLASS(object_class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(object_class);

	parent_class = gtk_type_class (PARENT_TYPE);

	object_class->set_arg = et_set_arg;
	object_class->get_arg = et_get_arg;
	object_class->destroy = e_entry_destroy;

	widget_class->realize = e_entry_realize;

	klass->changed = NULL;
	klass->activate = NULL;

	e_entry_signals[E_ENTRY_CHANGED] =
		gtk_signal_new ("changed",
				GTK_RUN_LAST,
				E_OBJECT_CLASS_TYPE (object_class),
				GTK_SIGNAL_OFFSET (EEntryClass, changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	e_entry_signals[E_ENTRY_ACTIVATE] =
		gtk_signal_new ("activate",
				GTK_RUN_LAST,
				E_OBJECT_CLASS_TYPE (object_class),
				GTK_SIGNAL_OFFSET (EEntryClass, activate),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	e_entry_signals[E_ENTRY_POPUP] =
		gtk_signal_new ("popup",
				GTK_RUN_LAST,
				E_OBJECT_CLASS_TYPE (object_class),
				GTK_SIGNAL_OFFSET (EEntryClass, popup),
				gtk_marshal_NONE__POINTER_INT,
				GTK_TYPE_NONE, 2, GTK_TYPE_POINTER, GTK_TYPE_INT);

	e_entry_signals[E_ENTRY_COMPLETION_POPUP] =
		gtk_signal_new ("completion_popup",
				GTK_RUN_LAST,
				E_OBJECT_CLASS_TYPE (object_class),
				GTK_SIGNAL_OFFSET (EEntryClass, completion_popup),
				gtk_marshal_NONE__INT,
				GTK_TYPE_NONE, 1, GTK_TYPE_INT);


	E_OBJECT_CLASS_ADD_SIGNALS (object_class, e_entry_signals, E_ENTRY_LAST_SIGNAL);

	gtk_object_add_arg_type ("EEntry::model",
				 GTK_TYPE_OBJECT, GTK_ARG_READWRITE, ARG_MODEL);  
	gtk_object_add_arg_type ("EEntry::event_processor",
				 GTK_TYPE_OBJECT, GTK_ARG_READWRITE, ARG_EVENT_PROCESSOR);
	gtk_object_add_arg_type ("EEntry::text",
				 GTK_TYPE_STRING, GTK_ARG_READWRITE, ARG_TEXT);
	gtk_object_add_arg_type ("EEntry::font",
				 GTK_TYPE_STRING, GTK_ARG_WRITABLE, ARG_FONT);
	gtk_object_add_arg_type ("EEntry::fontset",
				 GTK_TYPE_STRING, GTK_ARG_WRITABLE, ARG_FONTSET);
	gtk_object_add_arg_type ("EEntry::font_gdk",
				 GTK_TYPE_GDK_FONT, GTK_ARG_READWRITE, ARG_FONT_GDK);
	gtk_object_add_arg_type ("EEntry::justification",
				 GTK_TYPE_JUSTIFICATION, GTK_ARG_READWRITE, ARG_JUSTIFICATION);
	gtk_object_add_arg_type ("EEntry::fill_color",
				 GTK_TYPE_STRING, GTK_ARG_WRITABLE, ARG_FILL_COLOR);
	gtk_object_add_arg_type ("EEntry::fill_color_gdk",
				 GTK_TYPE_GDK_COLOR, GTK_ARG_READWRITE, ARG_FILL_COLOR_GDK);
	gtk_object_add_arg_type ("EEntry::fill_color_rgba",
				 GTK_TYPE_UINT, GTK_ARG_READWRITE, ARG_FILL_COLOR_RGBA);
	gtk_object_add_arg_type ("EEntry::fill_stipple",
				 GTK_TYPE_GDK_WINDOW, GTK_ARG_READWRITE, ARG_FILL_STIPPLE);
	gtk_object_add_arg_type ("EEntry::editable",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE, ARG_EDITABLE);
	gtk_object_add_arg_type ("EEntry::use_ellipsis",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE, ARG_USE_ELLIPSIS);
	gtk_object_add_arg_type ("EEntry::ellipsis",
				 GTK_TYPE_STRING, GTK_ARG_READWRITE, ARG_ELLIPSIS);
	gtk_object_add_arg_type ("EEntry::line_wrap",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE, ARG_LINE_WRAP);
	gtk_object_add_arg_type ("EEntry::break_characters",
				 GTK_TYPE_STRING, GTK_ARG_READWRITE, ARG_BREAK_CHARACTERS);
	gtk_object_add_arg_type ("EEntry::max_lines",
				 GTK_TYPE_INT, GTK_ARG_READWRITE, ARG_MAX_LINES);
	gtk_object_add_arg_type ("EEntry::allow_newlines",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE, ARG_ALLOW_NEWLINES);
	gtk_object_add_arg_type ("EEntry::draw_borders",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE, ARG_DRAW_BORDERS);
	gtk_object_add_arg_type ("EEntry::draw_background",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE, ARG_DRAW_BACKGROUND);
	gtk_object_add_arg_type ("EEntry::draw_button",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE, ARG_DRAW_BUTTON);
	gtk_object_add_arg_type ("EEntry::emulate_label_resize",
				 GTK_TYPE_BOOL, GTK_ARG_READWRITE, ARG_EMULATE_LABEL_RESIZE);
	gtk_object_add_arg_type ("EEntry::cursor_pos",
				 GTK_TYPE_INT, GTK_ARG_READWRITE, ARG_CURSOR_POS);
}

E_MAKE_TYPE(e_entry, "EEntry", EEntry, e_entry_class_init, e_entry_init, PARENT_TYPE)
