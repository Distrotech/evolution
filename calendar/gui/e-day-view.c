/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * Authors :
 *  Damon Chaplin <damon@ximian.com>
 *  Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright 1999, Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

/*
 * EDayView - displays the Day & Work-Week views of the calendar.
 */

#include <config.h>

#include "e-day-view.h"

#include <math.h>
#include <time.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gtk/gtkdnd.h>
#include <gtk/gtkinvisible.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkvscrollbar.h>
#include <gtk/gtkwindow.h>
#include <gal/e-text/e-text.h>
#include <gal/widgets/e-canvas-utils.h>
#include <gal/widgets/e-popup-menu.h>
#include <gal/widgets/e-gui-utils.h>
#include <gal/widgets/e-unicode.h>
#include <gal/util/e-util.h>
#include <libgnomecanvas/gnome-canvas-rect-ellipse.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-exec.h>
#include <libgnome/gnome-util.h>
#include <e-util/e-categories-config.h>
#include <e-util/e-dialog-utils.h>

#include "cal-util/timeutil.h"
#include "dialogs/delete-comp.h"
#include "dialogs/delete-error.h"
#include "dialogs/send-comp.h"
#include "dialogs/cancel-comp.h"
#include "dialogs/recur-comp.h"
#include "print.h"
#include "comp-util.h"
#include "itip-utils.h"
#include "calendar-commands.h"
#include "calendar-config.h"
#include "goto.h"
#include "e-day-view-time-item.h"
#include "e-day-view-top-item.h"
#include "e-day-view-layout.h"
#include "e-day-view-main-item.h"
#include "misc.h"

/* Images */
#include "art/bell.xpm"
#include "art/recur.xpm"
#include "art/timezone-16.xpm"
#include "art/schedule-meeting-16.xpm"

/* The minimum amount of space wanted on each side of the date string. */
#define E_DAY_VIEW_DATE_X_PAD	4

#define E_DAY_VIEW_LARGE_FONT_PTSIZE 18

/* The offset from the top/bottom of the canvas before auto-scrolling starts.*/
#define E_DAY_VIEW_AUTO_SCROLL_OFFSET	16

/* The time between each auto-scroll, in milliseconds. */
#define E_DAY_VIEW_AUTO_SCROLL_TIMEOUT	50

/* The number of timeouts we skip before we start scrolling. */
#define E_DAY_VIEW_AUTO_SCROLL_DELAY	5

/* The number of pixels the mouse has to be moved with the button down before
   we start a drag. */
#define E_DAY_VIEW_DRAG_START_OFFSET	4

/* The amount we scroll the main canvas when the Page Up/Down keys are pressed,
   as a fraction of the page size. */
#define E_DAY_VIEW_PAGE_STEP		0.5

/* The amount we scroll the main canvas when the mouse wheel buttons are
   pressed, as a fraction of the page size. */
#define E_DAY_VIEW_WHEEL_MOUSE_STEP_SIZE	0.25

/* The timeout before we do a layout, so we don't do a layout for each event
   we get from the server. */
#define E_DAY_VIEW_LAYOUT_TIMEOUT	100

/* Used for the status bar messages */
#define EVOLUTION_CALENDAR_PROGRESS_IMAGE "evolution-calendar-mini.png"
static GdkPixbuf *progress_icon[2] = { NULL, NULL };

/* Signal IDs */
enum {
	SELECTION_CHANGED,
	LAST_SIGNAL
};
static guint e_day_view_signals[LAST_SIGNAL] = { 0 };


/* Drag and Drop stuff. */
enum {
	TARGET_CALENDAR_EVENT,
	TARGET_VCALENDAR
};
static GtkTargetEntry target_table[] = {
	{ "application/x-e-calendar-event",     0, TARGET_CALENDAR_EVENT },
	{ "text/x-calendar",                    0, TARGET_VCALENDAR }
};
static guint n_targets = sizeof(target_table) / sizeof(target_table[0]);

static void e_day_view_class_init (EDayViewClass *class);
static void e_day_view_init (EDayView *day_view);
static void e_day_view_destroy (GtkObject *object);
static void e_day_view_realize (GtkWidget *widget);
static void e_day_view_unrealize (GtkWidget *widget);
static void e_day_view_style_set (GtkWidget *widget,
				  GtkStyle  *previous_style);
static void e_day_view_size_allocate (GtkWidget *widget,
				      GtkAllocation *allocation);
static gboolean e_day_view_update_scroll_regions (EDayView *day_view);
static gint e_day_view_focus_in (GtkWidget *widget,
				 GdkEventFocus *event);
static gint e_day_view_focus_out (GtkWidget *widget,
				  GdkEventFocus *event);
static gboolean e_day_view_key_press (GtkWidget *widget,
				      GdkEventKey *event);
static gboolean e_day_view_focus (GtkWidget *widget,
 				  GtkDirectionType direction);
static gboolean e_day_view_get_next_tab_event (EDayView *day_view,
					       GtkDirectionType direction,
					       gint *day, gint *event_num);
static gboolean e_day_view_get_extreme_long_event (EDayView *day_view,
						   gboolean first,
						   gint *day_out,
						   gint *event_num_out);
static gboolean e_day_view_get_extreme_event (EDayView *day_view,
					      gint start_day,
					      gint end_day,
					      gboolean first,
					      gint *day_out,
					      gint *event_num_out);
static gboolean e_day_view_do_key_press (GtkWidget *widget,
					 GdkEventKey *event);
static gboolean e_day_view_popup_menu (GtkWidget *widget);
static void e_day_view_cursor_key_up_shifted (EDayView *day_view,
					      GdkEventKey *event);
static void e_day_view_cursor_key_down_shifted (EDayView *day_view,
						GdkEventKey *event);
static void e_day_view_cursor_key_left_shifted (EDayView *day_view,
						GdkEventKey *event);
static void e_day_view_cursor_key_right_shifted (EDayView *day_view,
						 GdkEventKey *event);
static void e_day_view_cursor_key_up (EDayView *day_view,
				      GdkEventKey *event);
static void e_day_view_cursor_key_down (EDayView *day_view,
					GdkEventKey *event);
static void e_day_view_cursor_key_left (EDayView *day_view,
					GdkEventKey *event);
static void e_day_view_cursor_key_right (EDayView *day_view,
					 GdkEventKey *event);
static void e_day_view_ensure_rows_visible (EDayView *day_view,
					    gint start_row,
					    gint end_row);
static void e_day_view_scroll	(EDayView	*day_view,
				 gfloat		 pages_to_scroll);

static gboolean e_day_view_check_if_new_event_fits (EDayView *day_view);

static void e_day_view_on_canvas_realized (GtkWidget *widget,
					   EDayView *day_view);

static gboolean e_day_view_on_top_canvas_button_press (GtkWidget *widget,
						       GdkEventButton *event,
						       EDayView *day_view);
static gboolean e_day_view_on_top_canvas_button_release (GtkWidget *widget,
							 GdkEventButton *event,
							 EDayView *day_view);
static gboolean e_day_view_on_top_canvas_motion (GtkWidget *widget,
						 GdkEventMotion *event,
						 EDayView *day_view);

static gboolean e_day_view_on_main_canvas_button_press (GtkWidget *widget,
							GdkEventButton *event,
							EDayView *day_view);
static gboolean e_day_view_on_main_canvas_button_release (GtkWidget *widget,
							  GdkEventButton *event,
							  EDayView *day_view);
static gboolean e_day_view_on_main_canvas_scroll (GtkWidget *widget,
						  GdkEventScroll *scroll,
						  EDayView *day_view);

static gboolean e_day_view_on_time_canvas_scroll (GtkWidget *widget,
						  GdkEventScroll *scroll,
						  EDayView *day_view);

static void e_day_view_update_calendar_selection_time (EDayView *day_view);
static gboolean e_day_view_on_main_canvas_motion (GtkWidget *widget,
						  GdkEventMotion *event,
						  EDayView *day_view);
static gboolean e_day_view_convert_event_coords (EDayView *day_view,
						 GdkEvent *event,
						 GdkWindow *window,
						 gint *x_return,
						 gint *y_return);
static void e_day_view_update_long_event_resize (EDayView *day_view,
						 gint day);
static void e_day_view_update_resize (EDayView *day_view,
				      gint row);
static void e_day_view_finish_long_event_resize (EDayView *day_view);
static void e_day_view_finish_resize (EDayView *day_view);
static void e_day_view_abort_resize (EDayView *day_view,
				     guint32 time);


static gboolean e_day_view_on_long_event_button_press (EDayView		*day_view,
						       gint		 event_num,
						       GdkEventButton	*event,
						       EDayViewPosition  pos,
						       gint		 event_x,
						       gint		 event_y);
static gboolean e_day_view_on_event_button_press (EDayView	 *day_view,
						  gint		  day,
						  gint		  event_num,
						  GdkEventButton *event,
						  EDayViewPosition pos,
						  gint		  event_x,
						  gint		  event_y);
static void e_day_view_on_long_event_click (EDayView *day_view,
					    gint event_num,
					    GdkEventButton  *bevent,
					    EDayViewPosition pos,
					    gint	     event_x,
					    gint	     event_y);
static void e_day_view_on_event_click (EDayView *day_view,
				       gint day,
				       gint event_num,
				       GdkEventButton  *event,
				       EDayViewPosition pos,
				       gint		event_x,
				       gint		event_y);
static void e_day_view_on_event_double_click (EDayView *day_view,
					      gint day,
					      gint event_num);
static void e_day_view_on_event_right_click (EDayView *day_view,
					     GdkEventButton *bevent,
					     gint day,
					     gint event_num);
static void e_day_view_show_popup_menu (EDayView *day_view,
					GdkEvent *gdk_event,
					gint day,
					gint event_num);


static void e_day_view_recalc_day_starts (EDayView *day_view,
					  time_t start_time);
static void e_day_view_recalc_num_rows	(EDayView	*day_view);
static void e_day_view_recalc_cell_sizes	(EDayView	*day_view);

static EDayViewPosition e_day_view_convert_position_in_top_canvas (EDayView *day_view,
								   gint x,
								   gint y,
								   gint *day_return,
								   gint *event_num_return);
static EDayViewPosition e_day_view_convert_position_in_main_canvas (EDayView *day_view,
								    gint x,
								    gint y,
								    gint *day_return,
								    gint *row_return,
								    gint *event_num_return);
static gboolean e_day_view_find_event_from_item (EDayView *day_view,
						 GnomeCanvasItem *item,
						 gint *day_return,
						 gint *event_num_return);
static gboolean e_day_view_find_event_from_uid (EDayView *day_view,
						const gchar *uid,
						gint *day_return,
						gint *event_num_return);

typedef gboolean (* EDayViewForeachEventCallback) (EDayView *day_view,
						   gint day,
						   gint event_num,
						   gpointer data);

static void e_day_view_foreach_event		(EDayView	*day_view,
						 EDayViewForeachEventCallback callback,
						 gpointer	 data);
static void e_day_view_foreach_event_with_uid (EDayView *day_view,
					       const gchar *uid,
					       EDayViewForeachEventCallback callback,
					       gpointer data);

static void e_day_view_free_events (EDayView *day_view);
static void e_day_view_free_event_array (EDayView *day_view,
					 GArray *array);
static int e_day_view_add_event (CalComponent *comp,
				 time_t	  start,
				 time_t	  end,
				 gpointer data);
static void e_day_view_update_event_label (EDayView *day_view,
					   gint day,
					   gint event_num);
static void e_day_view_update_long_event_label (EDayView *day_view,
						gint event_num);

static void e_day_view_reshape_long_events (EDayView *day_view);
static void e_day_view_reshape_long_event (EDayView *day_view,
					   gint event_num);
static void e_day_view_reshape_day_events (EDayView *day_view,
					   gint day);
static void e_day_view_reshape_day_event (EDayView *day_view,
					  gint	day,
					  gint	event_num);
static void e_day_view_reshape_main_canvas_resize_bars (EDayView *day_view);
static void e_day_view_reshape_resize_long_event_rect_item (EDayView *day_view);
static void e_day_view_reshape_resize_rect_item (EDayView *day_view);

static void e_day_view_ensure_events_sorted (EDayView *day_view);

static void e_day_view_start_editing_event (EDayView *day_view,
					    gint day,
					    gint event_num,
					    gchar *initial_text);
static void e_day_view_stop_editing_event (EDayView *day_view);
static gboolean e_day_view_on_text_item_event (GnomeCanvasItem *item,
					       GdkEvent *event,
					       EDayView *day_view);
static void e_day_view_on_editing_started (EDayView *day_view,
					   GnomeCanvasItem *item);
static void e_day_view_on_editing_stopped (EDayView *day_view,
					   GnomeCanvasItem *item);

static time_t e_day_view_convert_grid_position_to_time (EDayView *day_view,
							gint col,
							gint row);
static gboolean e_day_view_convert_time_to_grid_position (EDayView *day_view,
							  time_t time,
							  gint *col,
							  gint *row);

static void e_day_view_start_auto_scroll (EDayView *day_view,
					  gboolean scroll_up);
static gboolean e_day_view_auto_scroll_handler (gpointer data);

static void e_day_view_on_new_appointment (GtkWidget *widget,
					   gpointer data);
static void e_day_view_on_new_event       (GtkWidget *widget,
					   gpointer data);
static void e_day_view_on_new_meeting (GtkWidget *widget,
				       gpointer data);
static void e_day_view_on_new_task (GtkWidget *widget,
				    gpointer data);
static void e_day_view_on_goto_today      (GtkWidget *widget,
					   gpointer data);
static void e_day_view_on_goto_date       (GtkWidget *widget,
					   gpointer data);
static void e_day_view_on_edit_appointment (GtkWidget *widget,
					    gpointer data);
static void e_day_view_on_save_as (GtkWidget *widget, 
				   gpointer data);
static void e_day_view_on_print (GtkWidget *widget,
				 gpointer data);
static void e_day_view_on_print_event (GtkWidget *widget, 
				       gpointer data);
static void e_day_view_on_meeting (GtkWidget *widget,
				   gpointer data);
static void e_day_view_on_forward (GtkWidget *widget,
				   gpointer data);
static void e_day_view_on_publish (GtkWidget *widget,
				   gpointer data);
static void e_day_view_on_settings (GtkWidget *widget,
				    gpointer data);
static void e_day_view_on_delete_occurrence (GtkWidget *widget,
					     gpointer data);
static void e_day_view_on_delete_appointment (GtkWidget *widget,
					      gpointer data);
static void e_day_view_on_cut (GtkWidget *widget, gpointer data);
static void e_day_view_on_copy (GtkWidget *widget, gpointer data);
static void e_day_view_on_paste (GtkWidget *widget, gpointer data);
static void e_day_view_on_unrecur_appointment (GtkWidget *widget,
					       gpointer data);
static EDayViewEvent* e_day_view_get_popup_menu_event (EDayView *day_view);

static gboolean e_day_view_on_top_canvas_drag_motion (GtkWidget      *widget,
						      GdkDragContext *context,
						      gint            x,
						      gint            y,
						      guint           time,
						      EDayView	  *day_view);
static void e_day_view_update_top_canvas_drag (EDayView *day_view,
					       gint day);
static void e_day_view_reshape_top_canvas_drag_item (EDayView *day_view);
static gboolean e_day_view_on_main_canvas_drag_motion (GtkWidget      *widget,
						       GdkDragContext *context,
						       gint            x,
						       gint            y,
						       guint           time,
						       EDayView	  *day_view);
static void e_day_view_reshape_main_canvas_drag_item (EDayView *day_view);
static void e_day_view_update_main_canvas_drag (EDayView *day_view,
						gint row,
						gint day);
static void e_day_view_on_top_canvas_drag_leave (GtkWidget      *widget,
						 GdkDragContext *context,
						 guint           time,
						 EDayView	     *day_view);
static void e_day_view_on_main_canvas_drag_leave (GtkWidget      *widget,
						  GdkDragContext *context,
						  guint           time,
						  EDayView	 *day_view);
static void e_day_view_on_drag_begin (GtkWidget      *widget,
				      GdkDragContext *context,
				      EDayView	   *day_view);
static void e_day_view_on_drag_end (GtkWidget      *widget,
				    GdkDragContext *context,
				    EDayView       *day_view);
static void e_day_view_on_drag_data_get (GtkWidget          *widget,
					 GdkDragContext     *context,
					 GtkSelectionData   *selection_data,
					 guint               info,
					 guint               time,
					 EDayView		*day_view);
static void e_day_view_on_top_canvas_drag_data_received (GtkWidget	*widget,
							 GdkDragContext *context,
							 gint            x,
							 gint            y,
							 GtkSelectionData *data,
							 guint           info,
							 guint           time,
							 EDayView	*day_view);
static void e_day_view_on_main_canvas_drag_data_received (GtkWidget      *widget,
							  GdkDragContext *context,
							  gint            x,
							  gint            y,
							  GtkSelectionData *data,
							  guint           info,
							  guint           time,
							  EDayView	 *day_view);

static gboolean e_day_view_update_event_cb (EDayView *day_view,
					    gint day,
					    gint event_num,
					    gpointer data);
static gboolean e_day_view_remove_event_cb (EDayView *day_view,
					    gint day,
					    gint event_num,
					    gpointer data);
static void e_day_view_normalize_selection (EDayView *day_view);
static gboolean e_day_view_set_show_times_cb	(EDayView	*day_view,
						 gint		 day,
						 gint		 event_num,
						 gpointer	 data);
static time_t e_day_view_find_work_week_start	(EDayView	*day_view,
						 time_t		 start_time);
static void e_day_view_recalc_work_week		(EDayView	*day_view);
static void e_day_view_recalc_work_week_days_shown	(EDayView	*day_view);

static void selection_clear_event (GtkWidget *invisible,
				   GdkEventSelection *event,
				   EDayView *day_view);
static void selection_received (GtkWidget *invisible,
				GtkSelectionData *selection_data,
				guint time,
				EDayView *day_view);
static void selection_get (GtkWidget *invisible,
			   GtkSelectionData *selection_data,
			   guint info,
			   guint time_stamp,
			   EDayView *day_view);

static void e_day_view_queue_layout (EDayView *day_view);
static void e_day_view_cancel_layout (EDayView *day_view);
static gboolean e_day_view_layout_timeout_cb (gpointer data);


static GtkTableClass *parent_class;
static GdkAtom clipboard_atom = GDK_NONE;

E_MAKE_TYPE (e_day_view, "EDayView", EDayView, e_day_view_class_init,
	     e_day_view_init, GTK_TYPE_TABLE);

static void
e_day_view_class_init (EDayViewClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	parent_class = g_type_class_peek_parent (class);
	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;

	e_day_view_signals[SELECTION_CHANGED] =
		gtk_signal_new ("selection_changed",
				GTK_RUN_LAST,
				G_TYPE_FROM_CLASS (object_class),
				GTK_SIGNAL_OFFSET (EDayViewClass, selection_changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);


	/* Method override */
	object_class->destroy		= e_day_view_destroy;

	widget_class->realize		= e_day_view_realize;
	widget_class->unrealize		= e_day_view_unrealize;
	widget_class->style_set		= e_day_view_style_set;
 	widget_class->size_allocate	= e_day_view_size_allocate;
	widget_class->focus_in_event	= e_day_view_focus_in;
	widget_class->focus_out_event	= e_day_view_focus_out;
	widget_class->key_press_event	= e_day_view_key_press;
 	widget_class->focus             = e_day_view_focus;
	widget_class->popup_menu        = e_day_view_popup_menu;

	class->selection_changed = NULL;

	/* clipboard atom */
	if (!clipboard_atom)
		clipboard_atom = gdk_atom_intern ("CLIPBOARD", FALSE);

}

static void
e_day_view_init (EDayView *day_view)
{
	gint day;
	GnomeCanvasGroup *canvas_group;

	GTK_WIDGET_SET_FLAGS (day_view, GTK_CAN_FOCUS);

	day_view->calendar = NULL;
	day_view->client = NULL;
	day_view->sexp = g_strdup ("#t"); /* match all by default */
	day_view->query = NULL;

	day_view->long_events = g_array_new (FALSE, FALSE,
					     sizeof (EDayViewEvent));
	day_view->long_events_sorted = TRUE;
	day_view->long_events_need_layout = FALSE;
	day_view->long_events_need_reshape = FALSE;

	day_view->layout_timeout_id = 0;

	for (day = 0; day < E_DAY_VIEW_MAX_DAYS; day++) {
		day_view->events[day] = g_array_new (FALSE, FALSE,
						     sizeof (EDayViewEvent));
		day_view->events_sorted[day] = TRUE;
		day_view->need_layout[day] = FALSE;
		day_view->need_reshape[day] = FALSE;
	}

	/* These indicate that the times haven't been set. */
	day_view->lower = 0;
	day_view->upper = 0;

	day_view->work_week_view = FALSE;
	day_view->days_shown = 1;

	day_view->zone = NULL;

	day_view->mins_per_row = 30;
	day_view->date_format = E_DAY_VIEW_DATE_FULL;
	day_view->rows_in_top_display = 0;

	/* Note that these don't work yet. It would need a few fixes to the
	   way event->start_minute and event->end_minute are used, and there
	   may be problems with events that go outside the visible times. */
	day_view->first_hour_shown = 0;
	day_view->first_minute_shown = 0;
	day_view->last_hour_shown = 24;
	day_view->last_minute_shown = 0;

	day_view->main_gc = NULL;
	e_day_view_recalc_num_rows (day_view);

	day_view->working_days = E_DAY_VIEW_MONDAY | E_DAY_VIEW_TUESDAY
		| E_DAY_VIEW_WEDNESDAY | E_DAY_VIEW_THURSDAY
		| E_DAY_VIEW_FRIDAY;

	day_view->work_day_start_hour = 9;
	day_view->work_day_start_minute = 0;
	day_view->work_day_end_hour = 17;
	day_view->work_day_end_minute = 0;
	day_view->show_event_end_times = TRUE;
	day_view->week_start_day = 0;
	day_view->scroll_to_work_day = TRUE;

	day_view->editing_event_day = -1;
	day_view->editing_event_num = -1;

	day_view->resize_bars_event_day = -1;
	day_view->resize_bars_event_num = -1;

	day_view->selection_start_row = -1;
	day_view->selection_start_day = -1;
	day_view->selection_end_row = -1;
	day_view->selection_end_day = -1;
	day_view->selection_is_being_dragged = FALSE;
	day_view->selection_drag_pos = E_DAY_VIEW_DRAG_END;
	day_view->selection_in_top_canvas = FALSE;

	day_view->resize_drag_pos = E_DAY_VIEW_POS_NONE;

	day_view->pressed_event_day = -1;

	day_view->drag_event_day = -1;
	day_view->drag_last_day = -1;

	day_view->auto_scroll_timeout_id = 0;

	day_view->default_category = NULL;

	day_view->large_font_desc = NULL;

	/* String to use in 12-hour time format for times in the morning. */
	day_view->am_string = _("am");

	/* String to use in 12-hour time format for times in the afternoon. */
	day_view->pm_string = _("pm");


	/*
	 * Top Canvas
	 */
	day_view->top_canvas = e_canvas_new ();
	gtk_table_attach (GTK_TABLE (day_view), day_view->top_canvas,
			  1, 2, 0, 1, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
	gtk_widget_show (day_view->top_canvas);
	g_signal_connect_after (day_view->top_canvas, "button_press_event",
				G_CALLBACK (e_day_view_on_top_canvas_button_press), day_view);
	g_signal_connect (day_view->top_canvas, "button_release_event",
			  G_CALLBACK (e_day_view_on_top_canvas_button_release), day_view);
	g_signal_connect (day_view->top_canvas, "motion_notify_event",
			  G_CALLBACK (e_day_view_on_top_canvas_motion), day_view);
	g_signal_connect (day_view->top_canvas, "drag_motion",
			  G_CALLBACK (e_day_view_on_top_canvas_drag_motion), day_view);
	g_signal_connect (day_view->top_canvas, "drag_leave",
			  G_CALLBACK (e_day_view_on_top_canvas_drag_leave), day_view);

	g_signal_connect (day_view->top_canvas, "drag_begin",
			  G_CALLBACK (e_day_view_on_drag_begin), day_view);
	g_signal_connect (day_view->top_canvas, "drag_end",
			  G_CALLBACK (e_day_view_on_drag_end), day_view);
	g_signal_connect (day_view->top_canvas, "drag_data_get",
			  G_CALLBACK (e_day_view_on_drag_data_get), day_view);
	g_signal_connect (day_view->top_canvas, "drag_data_received",
			  G_CALLBACK (e_day_view_on_top_canvas_drag_data_received), day_view);

	canvas_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (day_view->top_canvas)->root);

	day_view->top_canvas_item =
		gnome_canvas_item_new (canvas_group,
				       e_day_view_top_item_get_type (),
				       "EDayViewTopItem::day_view", day_view,
				       NULL);

	day_view->resize_long_event_rect_item =
		gnome_canvas_item_new (canvas_group,
				       gnome_canvas_rect_get_type(),
				       "width_pixels", 1,
				       NULL);
	gnome_canvas_item_hide (day_view->resize_long_event_rect_item);

	day_view->drag_long_event_rect_item =
		gnome_canvas_item_new (canvas_group,
				       gnome_canvas_rect_get_type (),
				       "width_pixels", 1,
				       NULL);
	gnome_canvas_item_hide (day_view->drag_long_event_rect_item);

	day_view->drag_long_event_item =
		gnome_canvas_item_new (canvas_group,
				       e_text_get_type (),
				       "anchor", GTK_ANCHOR_NW,
				       "line_wrap", TRUE,
				       "clip", TRUE,
				       "max_lines", 1,
				       "editable", TRUE,
				       "draw_background", FALSE,
				       "fill_color_rgba", GNOME_CANVAS_COLOR(0, 0, 0),
				       NULL);
	gnome_canvas_item_hide (day_view->drag_long_event_item);

	/*
	 * Main Canvas
	 */
	day_view->main_canvas = e_canvas_new ();
	gtk_table_attach (GTK_TABLE (day_view), day_view->main_canvas,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show (day_view->main_canvas);
	g_signal_connect (day_view->main_canvas, "realize",
			  G_CALLBACK (e_day_view_on_canvas_realized), day_view);

	g_signal_connect (day_view->main_canvas,
			  "button_press_event",
			  G_CALLBACK (e_day_view_on_main_canvas_button_press),
			  day_view);
	g_signal_connect (day_view->main_canvas,
			  "button_release_event",
			  G_CALLBACK (e_day_view_on_main_canvas_button_release),
			  day_view);
	g_signal_connect (day_view->main_canvas,
			  "scroll_event",
			  G_CALLBACK (e_day_view_on_main_canvas_scroll),
			  day_view);
	g_signal_connect (day_view->main_canvas,
			  "motion_notify_event",
			  G_CALLBACK (e_day_view_on_main_canvas_motion),
			  day_view);
	g_signal_connect (day_view->main_canvas,
			  "drag_motion",
			  G_CALLBACK (e_day_view_on_main_canvas_drag_motion),
			  day_view);
	g_signal_connect (day_view->main_canvas,
			  "drag_leave",
			  G_CALLBACK (e_day_view_on_main_canvas_drag_leave),
			  day_view);

	g_signal_connect (day_view->main_canvas, "drag_begin",
			  G_CALLBACK (e_day_view_on_drag_begin), day_view);
	g_signal_connect (day_view->main_canvas, "drag_end",
			  G_CALLBACK (e_day_view_on_drag_end), day_view);
	g_signal_connect (day_view->main_canvas, "drag_data_get",
			  G_CALLBACK (e_day_view_on_drag_data_get), day_view);
	g_signal_connect (day_view->main_canvas, "drag_data_received",
			  G_CALLBACK (e_day_view_on_main_canvas_drag_data_received), day_view);

	canvas_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (day_view->main_canvas)->root);

	day_view->main_canvas_item =
		gnome_canvas_item_new (canvas_group,
				       e_day_view_main_item_get_type (),
				       "EDayViewMainItem::day_view", day_view,
				       NULL);

	day_view->resize_rect_item =
		gnome_canvas_item_new (canvas_group,
				       gnome_canvas_rect_get_type(),
				       "width_pixels", 1,
				       NULL);
	gnome_canvas_item_hide (day_view->resize_rect_item);

	day_view->resize_bar_item =
		gnome_canvas_item_new (canvas_group,
				       gnome_canvas_rect_get_type(),
				       "width_pixels", 1,
				       NULL);
	gnome_canvas_item_hide (day_view->resize_bar_item);

	day_view->main_canvas_top_resize_bar_item =
		gnome_canvas_item_new (canvas_group,
				       gnome_canvas_rect_get_type (),
				       "width_pixels", 1,
				       NULL);
	gnome_canvas_item_hide (day_view->main_canvas_top_resize_bar_item);

	day_view->main_canvas_bottom_resize_bar_item =
		gnome_canvas_item_new (canvas_group,
				       gnome_canvas_rect_get_type (),
				       "width_pixels", 1,
				       NULL);
	gnome_canvas_item_hide (day_view->main_canvas_bottom_resize_bar_item);


	day_view->drag_rect_item =
		gnome_canvas_item_new (canvas_group,
				       gnome_canvas_rect_get_type (),
				       "width_pixels", 1,
				       NULL);
	gnome_canvas_item_hide (day_view->drag_rect_item);

	day_view->drag_bar_item =
		gnome_canvas_item_new (canvas_group,
				       gnome_canvas_rect_get_type (),
				       "width_pixels", 1,
				       NULL);
	gnome_canvas_item_hide (day_view->drag_bar_item);

	day_view->drag_item =
		gnome_canvas_item_new (canvas_group,
				       e_text_get_type (),
				       "anchor", GTK_ANCHOR_NW,
				       "line_wrap", TRUE,
				       "clip", TRUE,
				       "editable", TRUE,
				       "draw_background", FALSE,
				       "fill_color_rgba", GNOME_CANVAS_COLOR(0, 0, 0),
				       NULL);
	gnome_canvas_item_hide (day_view->drag_item);


	/*
	 * Times Canvas
	 */
	day_view->time_canvas = e_canvas_new ();
	gtk_layout_set_vadjustment (GTK_LAYOUT (day_view->time_canvas),
				    GTK_LAYOUT (day_view->main_canvas)->vadjustment);
	gtk_table_attach (GTK_TABLE (day_view), day_view->time_canvas,
			  0, 1, 1, 2,
			  GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show (day_view->time_canvas);
	g_signal_connect_after (day_view->time_canvas, "scroll_event",
				G_CALLBACK (e_day_view_on_time_canvas_scroll), day_view);

	canvas_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (day_view->time_canvas)->root);

	day_view->time_canvas_item =
		gnome_canvas_item_new (canvas_group,
				       e_day_view_time_item_get_type (),
				       "EDayViewTimeItem::day_view", day_view,
				       NULL);


	/*
	 * Scrollbar.
	 */
	day_view->vscrollbar = gtk_vscrollbar_new (GTK_LAYOUT (day_view->main_canvas)->vadjustment);
	gtk_table_attach (GTK_TABLE (day_view), day_view->vscrollbar,
			  2, 3, 1, 2, 0, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show (day_view->vscrollbar);


	/* Create the cursors. */
	day_view->normal_cursor = gdk_cursor_new (GDK_LEFT_PTR);
	day_view->move_cursor = gdk_cursor_new (GDK_FLEUR);
	day_view->resize_width_cursor = gdk_cursor_new (GDK_SB_H_DOUBLE_ARROW);
	day_view->resize_height_cursor = gdk_cursor_new (GDK_SB_V_DOUBLE_ARROW);
	day_view->last_cursor_set_in_top_canvas = NULL;
	day_view->last_cursor_set_in_main_canvas = NULL;

	/* Set up the drop sites. */
	gtk_drag_dest_set (day_view->top_canvas,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, n_targets,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK);
	gtk_drag_dest_set (day_view->main_canvas,
			   GTK_DEST_DEFAULT_ALL,
			   target_table, n_targets,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK);

	/* Set up the invisible widget for the clipboard selections */
	day_view->invisible = gtk_invisible_new ();
	gtk_selection_add_target (day_view->invisible,
				  clipboard_atom,
				  GDK_SELECTION_TYPE_STRING,
				  0);
	g_signal_connect (day_view->invisible, "selection_get",
			  G_CALLBACK (selection_get), (gpointer) day_view);
	g_signal_connect (day_view->invisible, "selection_clear_event",
			  G_CALLBACK (selection_clear_event), (gpointer) day_view);
	g_signal_connect (day_view->invisible, "selection_received",
			  G_CALLBACK (selection_received), (gpointer) day_view);

	day_view->clipboard_selection = NULL;

	day_view->activity = NULL;
}


/* Turn off the background of the canvas windows. This reduces flicker
   considerably when scrolling. (Why isn't it in GnomeCanvas?). */
static void
e_day_view_on_canvas_realized (GtkWidget *widget,
			       EDayView *day_view)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window,
				    NULL, FALSE);
}


/**
 * e_day_view_new:
 * @Returns: a new #EDayView.
 *
 * Creates a new #EDayView.
 **/
GtkWidget *
e_day_view_new (void)
{
	GtkWidget *day_view;

	day_view = GTK_WIDGET (g_object_new (e_day_view_get_type (), NULL));

	return day_view;
}


static void
e_day_view_destroy (GtkObject *object)
{
	EDayView *day_view;
	gint day;

	day_view = E_DAY_VIEW (object);

	e_day_view_cancel_layout (day_view);

	e_day_view_stop_auto_scroll (day_view);

	if (day_view->client) {
		g_signal_handlers_disconnect_matched (day_view->client, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, day_view);
		g_object_unref (day_view->client);
		day_view->client = NULL;
	}

	if (day_view->sexp) {
		g_free (day_view->sexp);
		day_view->sexp = NULL;
	}

	if (day_view->query) {
		g_signal_handlers_disconnect_matched (day_view->query, G_SIGNAL_MATCH_DATA,
						      0, 0, NULL, NULL, day_view);
		g_object_unref (day_view->query);
		day_view->query = NULL;
	}

	if (day_view->large_font_desc) {
		pango_font_description_free (day_view->large_font_desc);
		day_view->large_font_desc = NULL;
	}

	if (day_view->default_category) {
		g_free (day_view->default_category);
		day_view->default_category = NULL;
	}

	
	if (day_view->normal_cursor) {
		gdk_cursor_unref (day_view->normal_cursor);
		day_view->normal_cursor = NULL;
	}
	if (day_view->move_cursor) {
		gdk_cursor_unref (day_view->move_cursor);
		day_view->move_cursor = NULL;
	}
	if (day_view->resize_width_cursor) {
		gdk_cursor_unref (day_view->resize_width_cursor);
		day_view->resize_width_cursor = NULL;
	}
	if (day_view->resize_height_cursor) {
		gdk_cursor_unref (day_view->resize_height_cursor);
		day_view->resize_height_cursor = NULL;
	}

	if (day_view->long_events) {
		e_day_view_free_events (day_view);
		g_array_free (day_view->long_events, TRUE);
		day_view->long_events = NULL;
	}
	
	for (day = 0; day < E_DAY_VIEW_MAX_DAYS; day++) {
		if (day_view->events[day]) {
			g_array_free (day_view->events[day], TRUE);
			day_view->events[day] = NULL;
		}
	}

	if (day_view->invisible) {
		gtk_widget_destroy (day_view->invisible);
		day_view->invisible = NULL;
	}
	if (day_view->clipboard_selection) {
		g_free (day_view->clipboard_selection);
		day_view->clipboard_selection = NULL;
	}

	if (day_view->activity) {
		g_object_unref (day_view->activity);
		day_view->activity = NULL;
	}

	GTK_OBJECT_CLASS (parent_class)->destroy (object);
}


static void
e_day_view_realize (GtkWidget *widget)
{
	EDayView *day_view;
	GdkColormap *colormap;
	gboolean success[E_DAY_VIEW_COLOR_LAST];
	gint nfailed;

	if (GTK_WIDGET_CLASS (parent_class)->realize)
		(*GTK_WIDGET_CLASS (parent_class)->realize)(widget);

	day_view = E_DAY_VIEW (widget);
	day_view->main_gc = gdk_gc_new (widget->window);

	colormap = gtk_widget_get_colormap (widget);

	/* Allocate the colors. */
	day_view->colors[E_DAY_VIEW_COLOR_BG_WORKING].red   = 247 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_BG_WORKING].green = 247 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_BG_WORKING].blue  = 244 * 257;

	day_view->colors[E_DAY_VIEW_COLOR_BG_NOT_WORKING].red   = 216 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_BG_NOT_WORKING].green = 216 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_BG_NOT_WORKING].blue  = 214 * 257;

	day_view->colors[E_DAY_VIEW_COLOR_BG_SELECTED].red   = 0 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_BG_SELECTED].green = 0 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_BG_SELECTED].blue  = 156 * 257;

	day_view->colors[E_DAY_VIEW_COLOR_BG_SELECTED_UNFOCUSSED].red   = 16 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_BG_SELECTED_UNFOCUSSED].green = 78 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_BG_SELECTED_UNFOCUSSED].blue  = 139 * 257;

	day_view->colors[E_DAY_VIEW_COLOR_BG_GRID].red   = 0x8000;
	day_view->colors[E_DAY_VIEW_COLOR_BG_GRID].green = 0x8000;
	day_view->colors[E_DAY_VIEW_COLOR_BG_GRID].blue  = 0x8000;

	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS].red   = 0x8000;
	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS].green = 0x8000;
	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS].blue  = 0x8000;

	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS_SELECTED].red   = 65535;
	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS_SELECTED].green = 65535;
	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS_SELECTED].blue  = 65535;

	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS_GRID].red   = 0;
	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS_GRID].green = 0;
	day_view->colors[E_DAY_VIEW_COLOR_BG_TOP_CANVAS_GRID].blue  = 0;

	day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR].red   = 0;
	day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR].green = 0;
	day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR].blue  = 65535;

	day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND].red   = 65535;
	day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND].green = 65535;
	day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND].blue  = 65535;

	day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER].red   = 0;
	day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER].green = 0;
	day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER].blue  = 0;

	day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].red   = 213 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].green = 213 * 257;
	day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BACKGROUND].blue  = 213 * 257;

	day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BORDER].red   = 0;
	day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BORDER].green = 0;
	day_view->colors[E_DAY_VIEW_COLOR_LONG_EVENT_BORDER].blue  = 0;

	nfailed = gdk_colormap_alloc_colors (colormap, day_view->colors,
					     E_DAY_VIEW_COLOR_LAST, FALSE,
					     TRUE, success);
	if (nfailed)
		g_warning ("Failed to allocate all colors");


	/* Create the pixmaps. */
	day_view->reminder_icon = gdk_pixmap_colormap_create_from_xpm_d (NULL, colormap, &day_view->reminder_mask, NULL, bell_xpm);
	day_view->recurrence_icon = gdk_pixmap_colormap_create_from_xpm_d (NULL, colormap, &day_view->recurrence_mask, NULL, recur_xpm);
	day_view->timezone_icon = gdk_pixmap_colormap_create_from_xpm_d (NULL, colormap, &day_view->timezone_mask, NULL, timezone_16_xpm);
	day_view->meeting_icon = gdk_pixmap_colormap_create_from_xpm_d (NULL, colormap, &day_view->meeting_mask, NULL, schedule_meeting_16_xpm);



	/* Set the canvas item colors. */
	gnome_canvas_item_set (day_view->resize_long_event_rect_item,
			       "fill_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND],
			       "outline_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER],
			       NULL);

	gnome_canvas_item_set (day_view->drag_long_event_rect_item,
			       "fill_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND],
			       "outline_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER],
			       NULL);


	gnome_canvas_item_set (day_view->resize_rect_item,
			       "fill_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND],
			       "outline_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER],
			       NULL);

	gnome_canvas_item_set (day_view->resize_bar_item,
			       "fill_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR],
			       "outline_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER],
			       NULL);

	gnome_canvas_item_set (day_view->main_canvas_top_resize_bar_item,
			       "fill_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR],
			       "outline_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER],
			       NULL);

	gnome_canvas_item_set (day_view->main_canvas_bottom_resize_bar_item,
			       "fill_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR],
			       "outline_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER],
			       NULL);


	gnome_canvas_item_set (day_view->drag_rect_item,
			       "fill_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BACKGROUND],
			       "outline_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER],
			       NULL);

	gnome_canvas_item_set (day_view->drag_bar_item,
			       "fill_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_VBAR],
			       "outline_color_gdk", &day_view->colors[E_DAY_VIEW_COLOR_EVENT_BORDER],
			       NULL);
}


static void
e_day_view_unrealize (GtkWidget *widget)
{
	EDayView *day_view;
	GdkColormap *colormap;

	day_view = E_DAY_VIEW (widget);

	gdk_gc_unref (day_view->main_gc);
	day_view->main_gc = NULL;

	colormap = gtk_widget_get_colormap (widget);
	gdk_colormap_free_colors (colormap, day_view->colors, E_DAY_VIEW_COLOR_LAST);

	gdk_pixmap_unref (day_view->reminder_icon);
	day_view->reminder_icon = NULL;
	gdk_pixmap_unref (day_view->recurrence_icon);
	day_view->recurrence_icon = NULL;

	if (GTK_WIDGET_CLASS (parent_class)->unrealize)
		(*GTK_WIDGET_CLASS (parent_class)->unrealize)(widget);
}


static void
e_day_view_style_set (GtkWidget *widget,
		      GtkStyle  *previous_style)
{
	EDayView *day_view;
	gint top_rows, top_canvas_height;
	gint hour, max_large_hour_width;
	gint minute, max_minute_width, i;
	gint month, day, width;
	gint longest_month_width, longest_abbreviated_month_width;
	gint longest_weekday_width, longest_abbreviated_weekday_width;
	struct tm date_tm;
	gchar buffer[128];
	gint times_width;
	PangoFontDescription *font_desc;
	PangoContext *pango_context;
	PangoFontMetrics *font_metrics;
	PangoLayout *layout;

	if (GTK_WIDGET_CLASS (parent_class)->style_set)
		(*GTK_WIDGET_CLASS (parent_class)->style_set)(widget, previous_style);

	day_view = E_DAY_VIEW (widget);

	/* Set up Pango prerequisites */
	font_desc = gtk_widget_get_style (widget)->font_desc;
	pango_context = gtk_widget_get_pango_context (widget);
	font_metrics = pango_context_get_metrics (pango_context, font_desc,
						  pango_context_get_language (pango_context));
	layout = pango_layout_new (pango_context);

	/* Create the large font. */
	if (day_view->large_font_desc != NULL) 
		pango_font_description_free (day_view->large_font_desc);

	day_view->large_font_desc = pango_font_description_copy (font_desc);
	pango_font_description_set_size (day_view->large_font_desc,
					 E_DAY_VIEW_LARGE_FONT_PTSIZE * PANGO_SCALE);

	/* Recalculate the height of each row based on the font size. */
	day_view->row_height =
		PANGO_PIXELS (pango_font_metrics_get_ascent (font_metrics)) +
		PANGO_PIXELS (pango_font_metrics_get_descent (font_metrics)) +
		E_DAY_VIEW_EVENT_BORDER_HEIGHT + E_DAY_VIEW_EVENT_Y_PAD * 2 + 2 /* FIXME */;
	day_view->row_height = MAX (day_view->row_height, E_DAY_VIEW_ICON_HEIGHT + E_DAY_VIEW_ICON_Y_PAD + 2);
	GTK_LAYOUT (day_view->main_canvas)->vadjustment->step_increment = day_view->row_height;

	day_view->top_row_height =
		PANGO_PIXELS (pango_font_metrics_get_ascent (font_metrics)) +
		PANGO_PIXELS (pango_font_metrics_get_descent (font_metrics)) +
		E_DAY_VIEW_LONG_EVENT_BORDER_HEIGHT * 2 + E_DAY_VIEW_LONG_EVENT_Y_PAD * 2 +
		E_DAY_VIEW_TOP_CANVAS_Y_GAP;
	day_view->top_row_height = MAX (day_view->top_row_height, E_DAY_VIEW_ICON_HEIGHT + E_DAY_VIEW_ICON_Y_PAD + 2 + E_DAY_VIEW_TOP_CANVAS_Y_GAP);

	/* Set the height of the top canvas based on the row height and the
	   number of rows needed (min 1 + 1 for the dates + 1 space for DnD).*/
	top_rows = MAX (1, day_view->rows_in_top_display);
	top_canvas_height = (top_rows + 2) * day_view->top_row_height;
	gtk_widget_set_usize (day_view->top_canvas, -1, top_canvas_height);

	/* Find the longest full & abbreviated month names. */
	memset (&date_tm, 0, sizeof (date_tm));
	date_tm.tm_year = 100;
	date_tm.tm_mday = 1;
	date_tm.tm_isdst = -1;

	longest_month_width = 0;
	longest_abbreviated_month_width = 0;
	for (month = 0; month < 12; month++) {
		date_tm.tm_mon = month;

		e_utf8_strftime (buffer, sizeof (buffer), "%B", &date_tm);
		pango_layout_set_text (layout, buffer, -1);
		pango_layout_get_pixel_size (layout, &width, NULL);

		if (width > longest_month_width) {
			longest_month_width = width;
			day_view->longest_month_name = month;
		}

		e_utf8_strftime (buffer, sizeof (buffer), "%b", &date_tm);
		pango_layout_set_text (layout, buffer, -1);
		pango_layout_get_pixel_size (layout, &width, NULL);

		if (width > longest_abbreviated_month_width) {
			longest_abbreviated_month_width = width;
			day_view->longest_abbreviated_month_name = month;
		}
	}

	/* Find the longest full & abbreviated weekday names. */
	memset (&date_tm, 0, sizeof (date_tm));
	date_tm.tm_year = 100;
	date_tm.tm_mon = 0;
	date_tm.tm_isdst = -1;

	longest_weekday_width = 0;
	longest_abbreviated_weekday_width = 0;
	for (day = 0; day < 7; day++) {
		date_tm.tm_mday = 2 + day;
		date_tm.tm_wday = day;

		e_utf8_strftime (buffer, sizeof (buffer), "%A", &date_tm);
		pango_layout_set_text (layout, buffer, -1);
		pango_layout_get_pixel_size (layout, &width, NULL);

		if (width > longest_weekday_width) {
			longest_weekday_width = width;
			day_view->longest_weekday_name = day;
		}

		e_utf8_strftime (buffer, sizeof (buffer), "%a", &date_tm);
		pango_layout_set_text (layout, buffer, -1);
		pango_layout_get_pixel_size (layout, &width, NULL);

		if (width > longest_abbreviated_weekday_width) {
			longest_abbreviated_weekday_width = width;
			day_view->longest_abbreviated_weekday_name = day;
		}
	}


	/* Calculate the widths of all the time strings necessary. */
	day_view->max_small_hour_width = 0;
	max_large_hour_width = 0;
	for (hour = 0; hour < 24; hour++) {
		g_snprintf (buffer, sizeof (buffer), "%02i", hour);
		pango_layout_set_text (layout, buffer, -1);
		pango_layout_get_pixel_size (layout, &day_view->small_hour_widths [hour], NULL);

		day_view->max_small_hour_width = MAX (day_view->max_small_hour_width, day_view->small_hour_widths[hour]);
	}

	max_minute_width = 0;
	for (minute = 0, i = 0; minute < 60; minute += 5, i++) {
		gint minute_width;

		g_snprintf (buffer, sizeof (buffer), "%02i", minute);
		pango_layout_set_text (layout, buffer, -1);
		pango_layout_get_pixel_size (layout, &minute_width, NULL);

		max_minute_width = MAX (max_minute_width, minute_width);
	}
	day_view->max_minute_width = max_minute_width;

	pango_layout_set_text (layout, ":", 1);
	pango_layout_get_pixel_size (layout, &day_view->colon_width, NULL);
	pango_layout_set_text (layout, "0", 1);
	pango_layout_get_pixel_size (layout, &day_view->digit_width, NULL);

	pango_layout_set_text (layout, day_view->am_string, -1);
	pango_layout_get_pixel_size (layout, &day_view->am_string_width, NULL);
	pango_layout_set_text (layout, day_view->pm_string, -1);
	pango_layout_get_pixel_size (layout, &day_view->pm_string_width, NULL);

	/* Calculate the width of the time column. */
	times_width = e_day_view_time_item_get_column_width (E_DAY_VIEW_TIME_ITEM (day_view->time_canvas_item));
	gtk_widget_set_usize (day_view->time_canvas, times_width, -1);

	g_object_unref (layout);
	pango_font_metrics_unref (font_metrics);
}


/* This recalculates the sizes of each column. */
static void
e_day_view_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	EDayView *day_view;
	gint day, scroll_y;
	gboolean need_reshape;
	gdouble old_x2, old_y2, new_x2, new_y2;

#if 0
	g_print ("In e_day_view_size_allocate\n");
#endif
	day_view = E_DAY_VIEW (widget);

	(*GTK_WIDGET_CLASS (parent_class)->size_allocate) (widget, allocation);

	e_day_view_recalc_cell_sizes (day_view);

	/* Set the scroll region of the top canvas to its allocated size. */
	gnome_canvas_get_scroll_region (GNOME_CANVAS (day_view->top_canvas),
					NULL, NULL, &old_x2, &old_y2);
	new_x2 = day_view->top_canvas->allocation.width - 1;
	new_y2 = day_view->top_canvas->allocation.height - 1;
	if (old_x2 != new_x2 || old_y2 != new_y2)
		gnome_canvas_set_scroll_region (GNOME_CANVAS (day_view->top_canvas),
						0, 0, new_x2, new_y2);

	need_reshape = e_day_view_update_scroll_regions (day_view);

	/* Scroll to the start of the working day, if this is the initial
	   allocation. */
	if (day_view->scroll_to_work_day) {
		scroll_y = e_day_view_convert_time_to_position (day_view, day_view->work_day_start_hour, day_view->work_day_start_minute);
		gnome_canvas_scroll_to (GNOME_CANVAS (day_view->main_canvas),
					0, scroll_y);
		day_view->scroll_to_work_day = FALSE;
	}

	/* Flag that we need to reshape the events. Note that changes in height
	   don't matter, since the rows are always the same height. */
	if (need_reshape) {
		day_view->long_events_need_reshape = TRUE;
		for (day = 0; day < E_DAY_VIEW_MAX_DAYS; day++)
			day_view->need_reshape[day] = TRUE;

		e_day_view_check_layout (day_view);
	}
}


static void
e_day_view_recalc_cell_sizes	(EDayView	*day_view)
{
	/* An array of dates, one for each month in the year 2000. They must
	   all be Sundays. */
	static const int days[12] = { 23, 20, 19, 23, 21, 18,
				      23, 20, 17, 22, 19, 24 };
	gfloat width, offset;
	gint day, max_width;
	struct tm date_tm;
	char buffer[128];
	PangoFontDescription *font_desc;
	PangoContext *pango_context;
	PangoLayout *layout;
	gint pango_width;

	g_return_if_fail (((GtkWidget*)day_view)->style != NULL);

	/* Set up Pango prerequisites */
	font_desc = gtk_widget_get_style (GTK_WIDGET (day_view))->font_desc;
	pango_context = gtk_widget_get_pango_context (GTK_WIDGET (day_view));
	layout = pango_layout_new (pango_context);

	/* Calculate the column sizes, using floating point so that pixels
	   get divided evenly. Note that we use one more element than the
	   number of columns, to make it easy to get the column widths. */
	width = day_view->main_canvas->allocation.width;
	width /= day_view->days_shown;
	offset = 0;
	for (day = 0; day <= day_view->days_shown; day++) {
		day_view->day_offsets[day] = floor (offset + 0.5);
		offset += width;
	}

	/* Calculate the days widths based on the offsets. */
	for (day = 0; day < day_view->days_shown; day++) {
		day_view->day_widths[day] = day_view->day_offsets[day + 1] - day_view->day_offsets[day];
	}

	/* Determine which date format to use, based on the column widths.
	   We want to check the widths using the longest full or abbreviated
	   month name and the longest full or abbreviated weekday name, as
	   appropriate. */
	max_width = day_view->day_widths[0];

	memset (&date_tm, 0, sizeof (date_tm));
	date_tm.tm_year = 100;

	/* Try "Thursday 21 January". */
	date_tm.tm_mon = day_view->longest_month_name;
	date_tm.tm_mday = days[date_tm.tm_mon]
		+ day_view->longest_weekday_name;
	date_tm.tm_wday = day_view->longest_weekday_name;
	date_tm.tm_isdst = -1;
	/* strftime format %A = full weekday name, %d = day of month,
	   %B = full month name. Don't use any other specifiers. */
	e_utf8_strftime (buffer, sizeof (buffer), _("%A %d %B"), &date_tm);
	pango_layout_set_text (layout, buffer, -1);
	pango_layout_get_pixel_size (layout, &pango_width, NULL);

	if (pango_width < max_width) {
		day_view->date_format = E_DAY_VIEW_DATE_FULL;
		return;
	}

	/* Try "Thu 21 Jan". */
	date_tm.tm_mon = day_view->longest_abbreviated_month_name;
	date_tm.tm_mday = days[date_tm.tm_mon]
		+ day_view->longest_abbreviated_weekday_name;
	date_tm.tm_wday = day_view->longest_abbreviated_weekday_name;
	date_tm.tm_isdst = -1;
	/* strftime format %a = abbreviated weekday name, %d = day of month,
	   %b = abbreviated month name. Don't use any other specifiers. */
	e_utf8_strftime (buffer, sizeof (buffer), _("%a %d %b"), &date_tm);
	pango_layout_set_text (layout, buffer, -1);
	pango_layout_get_pixel_size (layout, &pango_width, NULL);

	if (pango_width < max_width) {
		day_view->date_format = E_DAY_VIEW_DATE_ABBREVIATED;
		return;
	}

	/* Try "23 Jan". */
	date_tm.tm_mon = day_view->longest_abbreviated_month_name;
	date_tm.tm_mday = 23;
	date_tm.tm_wday = 0;
	date_tm.tm_isdst = -1;
	/* strftime format %d = day of month, %b = abbreviated month name.
	   Don't use any other specifiers. */
	e_utf8_strftime (buffer, sizeof (buffer), _("%d %b"), &date_tm);
	pango_layout_set_text (layout, buffer, -1);
	pango_layout_get_pixel_size (layout, &pango_width, NULL);

	if (pango_width < max_width)
		day_view->date_format = E_DAY_VIEW_DATE_NO_WEEKDAY;
	else
		day_view->date_format = E_DAY_VIEW_DATE_SHORT;

	g_object_unref (layout);
}


static gint
e_day_view_focus_in (GtkWidget *widget, GdkEventFocus *event)
{
	EDayView *day_view;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (E_IS_DAY_VIEW (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	day_view = E_DAY_VIEW (widget);

	GTK_WIDGET_SET_FLAGS (widget, GTK_HAS_FOCUS);

	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);

	return FALSE;
}


static gint
e_day_view_focus_out (GtkWidget *widget, GdkEventFocus *event)
{
	EDayView *day_view;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (E_IS_DAY_VIEW (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	day_view = E_DAY_VIEW (widget);

	GTK_WIDGET_UNSET_FLAGS (widget, GTK_HAS_FOCUS);

	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);

	return FALSE;
}


void
e_day_view_set_calendar		(EDayView	*day_view,
				 GnomeCalendar	*calendar)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	day_view->calendar = calendar;
}


/* Callback used when a component is updated in the live query */
static void
query_obj_updated_cb (CalQuery *query, const char *uid,
		      gboolean query_in_progress, int n_scanned, int total,
		      gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;
	CalComponent *comp;
	CalClientGetStatus status;
	gint day, event_num;

	day_view = E_DAY_VIEW (data);

	/* If our time hasn't been set yet, just return. */
	if (day_view->lower == 0 && day_view->upper == 0)
		return;

	/* Get the event from the server. */
	status = cal_client_get_object (day_view->client, uid, &comp);

	switch (status) {
	case CAL_CLIENT_GET_SUCCESS:
		/* Everything is fine */
		break;

	case CAL_CLIENT_GET_SYNTAX_ERROR:
		g_message ("query_obj_updated_cb(): Syntax error when getting object `%s'", uid);
		return;

	case CAL_CLIENT_GET_NOT_FOUND:
		/* The object is no longer in the server, so do nothing */
		return;

	default:
		g_assert_not_reached ();
		return;
	}

	/* If the event already exists and the dates didn't change, we can
	   update the event fairly easily without changing the events arrays
	   or computing a new layout. */
	if (e_day_view_find_event_from_uid (day_view, uid, &day, &event_num)) {
		if (day == E_DAY_VIEW_LONG_EVENT)
			event = &g_array_index (day_view->long_events,
						EDayViewEvent, event_num);
		else
			event = &g_array_index (day_view->events[day],
						EDayViewEvent, event_num);

		if (!cal_component_has_recurrences (comp)
		    && !cal_component_has_recurrences (event->comp)
		    && cal_component_event_dates_match (comp, event->comp)) {
#if 0
			g_print ("updated object's dates unchanged\n");
#endif
			e_day_view_foreach_event_with_uid (day_view, uid, e_day_view_update_event_cb, comp);
			g_object_unref (comp);
			gtk_widget_queue_draw (day_view->top_canvas);
			gtk_widget_queue_draw (day_view->main_canvas);
			return;
		}

		/* The dates have changed, so we need to remove the
		   old occurrrences before adding the new ones. */
#if 0
		g_print ("dates changed - removing occurrences\n");
#endif
		e_day_view_foreach_event_with_uid (day_view, uid,
						   e_day_view_remove_event_cb,
						   NULL);
	}

	/* Add the occurrences of the event. */
	cal_recur_generate_instances (comp, day_view->lower,
				      day_view->upper,
				      e_day_view_add_event, day_view,
				      cal_client_resolve_tzid_cb, day_view->client,
				      day_view->zone);
	g_object_unref (comp);

	e_day_view_queue_layout (day_view);
}

/* Callback used when a component is removed from the live query */
static void
query_obj_removed_cb (CalQuery *query, const char *uid, gpointer data)
{
	EDayView *day_view;

	day_view = E_DAY_VIEW (data);

	e_day_view_foreach_event_with_uid (day_view, uid,
					   e_day_view_remove_event_cb, NULL);

	e_day_view_check_layout (day_view);
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
}

/* Callback used when a query ends */
static void
query_query_done_cb (CalQuery *query, CalQueryDoneStatus status, const char *error_str, gpointer data)
{
	EDayView *day_view;

	day_view = E_DAY_VIEW (data);

	/* FIXME */

	e_day_view_set_status_message (day_view, NULL);

	if (status != CAL_QUERY_DONE_SUCCESS)
		fprintf (stderr, "query done: %s\n", error_str);
}

/* Callback used when an evaluation error occurs when running a query */
static void
query_eval_error_cb (CalQuery *query, const char *error_str, gpointer data)
{
	EDayView *day_view;

	day_view = E_DAY_VIEW (data);

	/* FIXME */

	e_day_view_set_status_message (day_view, NULL);

	fprintf (stderr, "eval error: %s\n", error_str);
}


/* Builds a complete query sexp for the day view by adding the predicates to
 * filter only for VEVENTS that fit in the day view's time range.
 */
static char *
adjust_query_sexp (EDayView *day_view, const char *sexp)
{
	char *start, *end;
	char *new_sexp;

	/* If the dates have not been set yet, we just want an empty query. */
	if (day_view->lower == 0 || day_view->upper == 0)
		return NULL;

	start = isodate_from_time_t (day_view->lower);
	end = isodate_from_time_t (day_view->upper);

	new_sexp = g_strdup_printf ("(and (= (get-vtype) \"VEVENT\")"
				    "     (occur-in-time-range? (make-time \"%s\")"
				    "                           (make-time \"%s\"))"
				    "     %s)",
				    start, end,
				    sexp);

	g_free (start);
	g_free (end);

	return new_sexp;
}


/* Restarts a query for the day view */
static void
update_query (EDayView *day_view)
{
	CalQuery *old_query;
	char *real_sexp;

	e_day_view_stop_editing_event (day_view);

	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
	e_day_view_free_events (day_view);
	e_day_view_queue_layout (day_view);

	if (!(day_view->client
	      && cal_client_get_load_state (day_view->client) == CAL_CLIENT_LOAD_LOADED))
		return;

	old_query = day_view->query;
	day_view->query = NULL;

	if (old_query) {
		g_signal_handlers_disconnect_matched (old_query, G_SIGNAL_MATCH_DATA,
						      0, 0, NULL, NULL, day_view);
		g_object_unref (old_query);
	}

	g_assert (day_view->sexp != NULL);

	real_sexp = adjust_query_sexp (day_view, day_view->sexp);
	if (!real_sexp)
		return; /* No time range is set, so don't start a query */

	e_day_view_set_status_message (day_view, _("Searching"));
	day_view->query = cal_client_get_query (day_view->client, real_sexp);
	g_free (real_sexp);

	if (!day_view->query) {
		g_message ("update_query(): Could not create the query");
		return;
	}

	g_signal_connect (day_view->query, "obj_updated",
			  G_CALLBACK (query_obj_updated_cb), day_view);
	g_signal_connect (day_view->query, "obj_removed",
			  G_CALLBACK (query_obj_removed_cb), day_view);
	g_signal_connect (day_view->query, "query_done",
			  G_CALLBACK (query_query_done_cb), day_view);
	g_signal_connect (day_view->query, "eval_error",
			  G_CALLBACK (query_eval_error_cb), day_view);
}

/* Callback used when the calendar client finishes opening */
static void
cal_opened_cb (CalClient *client, CalClientOpenStatus status, gpointer data)
{
	EDayView *day_view;

	day_view = E_DAY_VIEW (data);

	if (status != CAL_CLIENT_OPEN_SUCCESS)
		return;

	update_query (day_view);
}

/**
 * e_day_view_set_cal_client:
 * @day_view: A day view.
 * @client: A calendar client interface object.
 *
 * Sets the calendar client interface object that a day view will monitor.
 **/
void
e_day_view_set_cal_client	(EDayView	*day_view,
				 CalClient	*client)
{
	g_return_if_fail (day_view != NULL);
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (client == day_view->client)
		return;

	if (client)
		g_return_if_fail (IS_CAL_CLIENT (client));

	if (client)
		g_object_ref (client);

	if (day_view->client) {
		g_signal_handlers_disconnect_matched (day_view->client, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, day_view);
		g_object_unref (day_view->client);
	}

	day_view->client = client;

	if (day_view->client) {
		if (cal_client_get_load_state (day_view->client) == CAL_CLIENT_LOAD_LOADED)
			update_query (day_view);
		else
			g_signal_connect (day_view->client, "cal_opened",
					  G_CALLBACK (cal_opened_cb), day_view);
	}
}

/**
 * e_day_view_set_query:
 * @day_view: A day view.
 * @sexp: S-expression that defines the query.
 * 
 * Sets the query sexp that the day view will use for filtering the displayed
 * events.
 **/
void
e_day_view_set_query (EDayView *day_view, const char *sexp)
{
	g_return_if_fail (day_view != NULL);
	g_return_if_fail (E_IS_DAY_VIEW (day_view));
	g_return_if_fail (sexp != NULL);

	if (day_view->sexp)
		g_free (day_view->sexp);

	day_view->sexp = g_strdup (sexp);

	update_query (day_view);
}


/**
 * e_day_view_set_default_category:
 * @day_view: A day view.
 * @category: Default category name or NULL for no category.
 * 
 * Sets the default category that will be used when creating new calendar
 * components from the day view.
 **/
void
e_day_view_set_default_category (EDayView *day_view, const char *category)
{
	g_return_if_fail (day_view != NULL);
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (day_view->default_category)
		g_free (day_view->default_category);

	day_view->default_category = g_strdup (category);
}

static gboolean
e_day_view_update_event_cb (EDayView *day_view,
			    gint day,
			    gint event_num,
			    gpointer data)
{
	EDayViewEvent *event;
	CalComponent *comp;

	comp = data;
#if 0
	g_print ("In e_day_view_update_event_cb day:%i event_num:%i\n",
		 day, event_num);
#endif
	if (day == E_DAY_VIEW_LONG_EVENT) {
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);
	} else {
		event = &g_array_index (day_view->events[day], EDayViewEvent,
					event_num);
	}

	g_object_unref (event->comp);
	event->comp = comp;
	g_object_ref (comp);

	if (day == E_DAY_VIEW_LONG_EVENT) {
		e_day_view_update_long_event_label (day_view, event_num);
		e_day_view_reshape_long_event (day_view, event_num);
	} else {
		e_day_view_update_event_label (day_view, day, event_num);
		e_day_view_reshape_day_event (day_view, day, event_num);
	}
	return TRUE;
}


/* This calls a given function for each event instance (in both views).
   If the callback returns FALSE the iteration is stopped.
   Note that it is safe for the callback to remove the event (since we
   step backwards through the arrays). */
static void
e_day_view_foreach_event		(EDayView	*day_view,
					 EDayViewForeachEventCallback callback,
					 gpointer	 data)
{
	EDayViewEvent *event;
	gint day, event_num;

	for (day = 0; day < day_view->days_shown; day++) {
		for (event_num = day_view->events[day]->len - 1;
		     event_num >= 0;
		     event_num--) {
			event = &g_array_index (day_view->events[day],
						EDayViewEvent, event_num);

			if (!(*callback) (day_view, day, event_num, data))
				return;
		}
	}

	for (event_num = day_view->long_events->len - 1;
	     event_num >= 0;
	     event_num--) {
		event = &g_array_index (day_view->long_events,
					EDayViewEvent, event_num);

		if (!(*callback) (day_view, E_DAY_VIEW_LONG_EVENT, event_num,
				  data))
			return;
	}
}


/* This calls a given function for each event instance that matches the given
   uid. If the callback returns FALSE the iteration is stopped.
   Note that it is safe for the callback to remove the event (since we
   step backwards through the arrays). */
static void
e_day_view_foreach_event_with_uid (EDayView *day_view,
				   const gchar *uid,
				   EDayViewForeachEventCallback callback,
				   gpointer data)
{
	EDayViewEvent *event;
	gint day, event_num;
	const char *u;

	for (day = 0; day < day_view->days_shown; day++) {
		for (event_num = day_view->events[day]->len - 1;
		     event_num >= 0;
		     event_num--) {
			event = &g_array_index (day_view->events[day],
						EDayViewEvent, event_num);

			cal_component_get_uid (event->comp, &u);
			if (uid && !strcmp (uid, u)) {
				if (!(*callback) (day_view, day, event_num,
						  data))
					return;
			}
		}
	}

	for (event_num = day_view->long_events->len - 1;
	     event_num >= 0;
	     event_num--) {
		event = &g_array_index (day_view->long_events,
					EDayViewEvent, event_num);

		cal_component_get_uid (event->comp, &u);
		if (u && !strcmp (uid, u)) {
			if (!(*callback) (day_view, E_DAY_VIEW_LONG_EVENT,
					  event_num, data))
				return;
		}
	}
}


static gboolean
e_day_view_remove_event_cb (EDayView *day_view,
			    gint day,
			    gint event_num,
			    gpointer data)
{
	EDayViewEvent *event;

#if 0
	g_print ("In e_day_view_remove_event_cb day:%i event_num:%i\n",
		 day, event_num);
#endif

	if (day == E_DAY_VIEW_LONG_EVENT)
		event = &g_array_index (day_view->long_events,
					EDayViewEvent, event_num);
	else
		event = &g_array_index (day_view->events[day],
					EDayViewEvent, event_num);

	/* If we were editing this event, set editing_event_day to -1 so
	   on_editing_stopped doesn't try to update the event. */
	if (day_view->editing_event_day == day
	    && day_view->editing_event_num == event_num)
		day_view->editing_event_day = -1;

	if (event->canvas_item)
		gtk_object_destroy (GTK_OBJECT (event->canvas_item));
	g_object_unref (event->comp);

	if (day == E_DAY_VIEW_LONG_EVENT) {
		g_array_remove_index (day_view->long_events, event_num);
		day_view->long_events_need_layout = TRUE;
	} else {
		g_array_remove_index (day_view->events[day], event_num);
		day_view->need_layout[day] = TRUE;
	}
	return TRUE;
}


/* This updates the text shown for an event. If the event start or end do not
   lie on a row boundary, the time is displayed before the summary. */
static void
e_day_view_update_event_label (EDayView *day_view,
			       gint day,
			       gint event_num)
{
	EDayViewEvent *event;
	char *text, *start_suffix, *end_suffix;
	gboolean free_text = FALSE, editing_event = FALSE;
	gint offset;
	gint start_hour, start_display_hour, start_minute, start_suffix_width;
	gint end_hour, end_display_hour, end_minute, end_suffix_width;
	CalComponentText summary;

	event = &g_array_index (day_view->events[day], EDayViewEvent,
				event_num);

	/* If the event isn't visible just return. */
	if (!event->canvas_item)
		return;

	cal_component_get_summary (event->comp, &summary);
	text = summary.value ? (char*) summary.value : "";

	if (day_view->editing_event_day == day
	    && day_view->editing_event_num == event_num)
		editing_event = TRUE;

	if (!editing_event
	    && (event->start_minute % day_view->mins_per_row != 0
		|| (day_view->show_event_end_times
		    && event->end_minute % day_view->mins_per_row != 0))) {
		offset = day_view->first_hour_shown * 60
			+ day_view->first_minute_shown;
		start_minute = offset + event->start_minute;
		end_minute = offset + event->end_minute;

		start_hour = start_minute / 60;
		start_minute = start_minute % 60;

		end_hour = end_minute / 60;
		end_minute = end_minute % 60;

		e_day_view_convert_time_to_display (day_view, start_hour,
						    &start_display_hour,
						    &start_suffix,
						    &start_suffix_width);
		e_day_view_convert_time_to_display (day_view, end_hour,
						    &end_display_hour,
						    &end_suffix,
						    &end_suffix_width);

		if (day_view->use_24_hour_format) {
			if (day_view->show_event_end_times) {
				/* 24 hour format with end time. */
				text = g_strdup_printf
					("%2i:%02i-%2i:%02i %s",
					 start_display_hour, start_minute,
					 end_display_hour, end_minute,
					 text);
			} else {
				/* 24 hour format without end time. */
				text = g_strdup_printf
					("%2i:%02i %s",
					 start_display_hour, start_minute,
					 text);
			}
		} else {
			if (day_view->show_event_end_times) {
				/* 12 hour format with end time. */
				text = g_strdup_printf
					("%2i:%02i%s-%2i:%02i%s %s",
					 start_display_hour, start_minute,
					 start_suffix,
					 end_display_hour, end_minute,
					 end_suffix,
					 text);
			} else {
				/* 12 hour format without end time. */
				text = g_strdup_printf
					("%2i:%02i%s %s",
					 start_display_hour, start_minute,
					 start_suffix,
					 text);
			}
		}

		free_text = TRUE;
	}

	gnome_canvas_item_set (event->canvas_item,
			       "text", text,
			       NULL);

	if (free_text)
		g_free (text);
}


static void
e_day_view_update_long_event_label (EDayView *day_view,
				    gint      event_num)
{
	EDayViewEvent *event;
	CalComponentText summary;

	event = &g_array_index (day_view->long_events, EDayViewEvent,
				event_num);

	/* If the event isn't visible just return. */
	if (!event->canvas_item)
		return;

	cal_component_get_summary (event->comp, &summary);
	gnome_canvas_item_set (event->canvas_item,
			       "text", summary.value ? summary.value : "",
			       NULL);
}


/* Finds the day and index of the event with the given canvas item.
   If is is a long event, -1 is returned as the day.
   Returns TRUE if the event was found. */
static gboolean
e_day_view_find_event_from_item (EDayView *day_view,
				 GnomeCanvasItem *item,
				 gint *day_return,
				 gint *event_num_return)
{
	EDayViewEvent *event;
	gint day, event_num;

	for (day = 0; day < day_view->days_shown; day++) {
		for (event_num = 0; event_num < day_view->events[day]->len;
		     event_num++) {
			event = &g_array_index (day_view->events[day],
						EDayViewEvent, event_num);
			if (event->canvas_item == item) {
				*day_return = day;
				*event_num_return = event_num;
				return TRUE;
			}
		}
	}

	for (event_num = 0; event_num < day_view->long_events->len;
	     event_num++) {
		event = &g_array_index (day_view->long_events,
					EDayViewEvent, event_num);
		if (event->canvas_item == item) {
			*day_return = E_DAY_VIEW_LONG_EVENT;
			*event_num_return = event_num;
			return TRUE;
		}
	}

	return FALSE;
}


/* Finds the day and index of the event with the given uid.
   If is is a long event, E_DAY_VIEW_LONG_EVENT is returned as the day.
   Returns TRUE if an event with the uid was found.
   Note that for recurring events there may be several EDayViewEvents, one
   for each instance, all with the same iCalObject and uid. So only use this
   function if you know the event doesn't recur or you are just checking to
   see if any events with the uid exist. */
static gboolean
e_day_view_find_event_from_uid (EDayView *day_view,
				const gchar *uid,
				gint *day_return,
				gint *event_num_return)
{
	EDayViewEvent *event;
	gint day, event_num;
	const char *u;

	for (day = 0; day < day_view->days_shown; day++) {
		for (event_num = 0; event_num < day_view->events[day]->len;
		     event_num++) {
			event = &g_array_index (day_view->events[day],
						EDayViewEvent, event_num);

			cal_component_get_uid (event->comp, &u);
			if (u && !strcmp (uid, u)) {
				*day_return = day;
				*event_num_return = event_num;
				return TRUE;
			}
		}
	}

	for (event_num = 0; event_num < day_view->long_events->len;
	     event_num++) {
		event = &g_array_index (day_view->long_events,
					EDayViewEvent, event_num);

		cal_component_get_uid (event->comp, &u);
		if (u && !strcmp (uid, u)) {
			*day_return = E_DAY_VIEW_LONG_EVENT;
			*event_num_return = event_num;
			return TRUE;
		}
	}

	return FALSE;
}


/* This sets the selected time range. The EDayView will show the day or week
   corresponding to the start time. If the start_time & end_time are not equal
   and are both visible in the view, then the selection is set to those times,
   otherwise it is set to 1 hour from the start of the working day. */
void
e_day_view_set_selected_time_range	(EDayView	*day_view,
					 time_t		 start_time,
					 time_t		 end_time)
{
	time_t lower;
	gint start_row, start_col, end_row, end_col;
	gboolean need_redraw = FALSE, start_in_grid, end_in_grid;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	/* Calculate the first day that should be shown, based on start_time
	   and the days_shown setting. If we are showing 1 day it is just the
	   start of the day given by start_time, otherwise it is the previous
	   work-week start day. */
	if (!day_view->work_week_view) {
		lower = time_day_begin_with_zone (start_time, day_view->zone);
	} else {
		lower = e_day_view_find_work_week_start (day_view, start_time);
	}
		
	/* See if we need to change the days shown. */
	if (lower != day_view->lower) {
		e_day_view_recalc_day_starts (day_view, lower);
		update_query (day_view);
	}

	/* Set the selection. */
	start_in_grid = e_day_view_convert_time_to_grid_position (day_view,
								  start_time,
								  &start_col,
								  &start_row);
	end_in_grid = e_day_view_convert_time_to_grid_position (day_view,
								end_time - 60,
								&end_col,
								&end_row);

	/* If either of the times isn't in the grid, or the selection covers
	   an entire day, we set the selection to 1 row from the start of the
	   working day, in the day corresponding to the start time. */
	if (!start_in_grid || !end_in_grid
	    || (start_row == 0 && end_row == day_view->rows - 1)) {
		end_col = start_col;

		start_row = e_day_view_convert_time_to_row (day_view, day_view->work_day_start_hour, day_view->work_day_start_minute);
		start_row = CLAMP (start_row, 0, day_view->rows - 1);
		end_row = start_row;
	}

	if (start_row != day_view->selection_start_row
	    || start_col != day_view->selection_start_day) {
		need_redraw = TRUE;
		day_view->selection_in_top_canvas = FALSE;
		day_view->selection_start_row = start_row;
		day_view->selection_start_day = start_col;
	}

	if (end_row != day_view->selection_end_row
	    || end_col != day_view->selection_end_day) {
		need_redraw = TRUE;
		day_view->selection_in_top_canvas = FALSE;
		day_view->selection_end_row = end_row;
		day_view->selection_end_day = end_col;
	}

	if (need_redraw) {
		gtk_widget_queue_draw (day_view->top_canvas);
		gtk_widget_queue_draw (day_view->main_canvas);
	}
}

static void
e_day_view_set_selected_time_range_in_top_visible	(EDayView	*day_view,
							 time_t		 start_time,
							 time_t		 end_time)
{
	gint start_row, start_col, end_row, end_col;
	gboolean need_redraw = FALSE, start_in_grid, end_in_grid;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	/* Set the selection. */
	start_in_grid = e_day_view_convert_time_to_grid_position (day_view,
								  start_time,
								  &start_col,
								  &start_row);
	end_in_grid = e_day_view_convert_time_to_grid_position (day_view,
								end_time - 60,
								&end_col,
								&end_row);

	if (!start_in_grid)
		start_col = 0;
	if (!end_in_grid)
		end_col = day_view->days_shown - 1;

	if (start_row != day_view->selection_start_row
	    || start_col != day_view->selection_start_day) {
		need_redraw = TRUE;
		day_view->selection_in_top_canvas = TRUE;
		day_view->selection_start_row = -1;
		day_view->selection_start_day = start_col;
	}

	if (end_row != day_view->selection_end_row
	    || end_col != day_view->selection_end_day) {
		need_redraw = TRUE;
		day_view->selection_in_top_canvas = TRUE;
		day_view->selection_end_row = -1;
		day_view->selection_end_day = end_col;
	}

	if (need_redraw) {
		gtk_widget_queue_draw (day_view->top_canvas);
		gtk_widget_queue_draw (day_view->main_canvas);
	}
}

static void
e_day_view_set_selected_time_range_visible	(EDayView	*day_view,
						 time_t		 start_time,
						 time_t		 end_time)
{
	gint start_row, start_col, end_row, end_col;
	gboolean need_redraw = FALSE, start_in_grid, end_in_grid;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	/* Set the selection. */
	start_in_grid = e_day_view_convert_time_to_grid_position (day_view,
								  start_time,
								  &start_col,
								  &start_row);
	end_in_grid = e_day_view_convert_time_to_grid_position (day_view,
								end_time - 60,
								&end_col,
								&end_row);

	/* If either of the times isn't in the grid, or the selection covers
	   an entire day, we set the selection to 1 row from the start of the
	   working day, in the day corresponding to the start time. */
	if (!start_in_grid || !end_in_grid
	    || (start_row == 0 && end_row == day_view->rows - 1)) {
		end_col = start_col;

		start_row = e_day_view_convert_time_to_row (day_view, day_view->work_day_start_hour, day_view->work_day_start_minute);
		start_row = CLAMP (start_row, 0, day_view->rows - 1);
		end_row = start_row;
	}

	if (start_row != day_view->selection_start_row
	    || start_col != day_view->selection_start_day) {
		need_redraw = TRUE;
		day_view->selection_in_top_canvas = FALSE;
		day_view->selection_start_row = start_row;
		day_view->selection_start_day = start_col;
	}

	if (end_row != day_view->selection_end_row
	    || end_col != day_view->selection_end_day) {
		need_redraw = TRUE;
		day_view->selection_in_top_canvas = FALSE;
		day_view->selection_end_row = end_row;
		day_view->selection_end_day = end_col;
	}

	if (need_redraw) {
		gtk_widget_queue_draw (day_view->top_canvas);
		gtk_widget_queue_draw (day_view->main_canvas);
	}
}


/* Finds the start of the working week which includes the given time. */
static time_t
e_day_view_find_work_week_start		(EDayView	*day_view,
					 time_t		 start_time)
{
	GDate date;
	gint weekday, day, i;
	guint offset;
	struct icaltimetype tt = icaltime_null_time ();

	time_to_gdate_with_zone (&date, start_time, day_view->zone);

	/* The start of the work-week is the first working day after the
	   week start day. */

	/* Get the weekday corresponding to start_time, 0 (Sun) to 6 (Sat). */
	weekday = g_date_weekday (&date) % 7;

	/* Calculate the first working day of the week, 0 (Sun) to 6 (Sat).
	   It will automatically default to the week start day if no days
	   are set as working days. */
	day = (day_view->week_start_day + 1) % 7;
	for (i = 0; i < 7; i++) {
		if (day_view->working_days & (1 << day))
			break;
		day = (day + 1) % 7;
	}

	/* Calculate how many days we need to go back to the first workday. */
	if (weekday < day) {
		offset = (day - weekday) % 7;
		g_date_add_days (&date, offset);
	} else {
		offset = (weekday - day) % 7;
		g_date_subtract_days (&date, offset);
	}

	tt.year = g_date_year (&date);
	tt.month = g_date_month (&date);
	tt.day = g_date_day (&date);

	return icaltime_as_timet_with_zone (tt, day_view->zone);
}


/* Returns the selected time range. */
void
e_day_view_get_selected_time_range	(EDayView	*day_view,
					 time_t		*start_time,
					 time_t		*end_time)
{
	gint start_col, start_row, end_col, end_row;
	time_t start, end;

	start_col = day_view->selection_start_day;
	start_row = day_view->selection_start_row;
	end_col = day_view->selection_end_day;
	end_row = day_view->selection_end_row;

	if (start_col == -1) {
		start_col = 0;
		start_row = 0;
		end_col = 0;
		end_row = 0;
	}

	/* Check if the selection is only in the top canvas, in which case
	   we can simply use the day_starts array. */
	if (day_view->selection_in_top_canvas) {
		start = day_view->day_starts[start_col];
		end = day_view->day_starts[end_col + 1];
	} else {
		/* Convert the start col + row into a time. */
		start = e_day_view_convert_grid_position_to_time (day_view, start_col, start_row);
		end = e_day_view_convert_grid_position_to_time (day_view, end_col, end_row + 1);
	}

	if (start_time)
		*start_time = start;

	if (end_time)
		*end_time = end;
}


static void
e_day_view_recalc_day_starts (EDayView *day_view,
			      time_t start_time)
{
	gint day;

	day_view->day_starts[0] = start_time;
	for (day = 1; day <= day_view->days_shown; day++) {
		day_view->day_starts[day] = time_add_day_with_zone (day_view->day_starts[day - 1], 1, day_view->zone);
	}

#if 0
	for (day = 0; day <= day_view->days_shown; day++)
		g_print ("Day Starts %i: %s", day, ctime (&day_view->day_starts[day]));
#endif

	day_view->lower = start_time;
	day_view->upper = day_view->day_starts[day_view->days_shown];
}


/* Whether we are displaying a work-week, in which case the display always
   starts on the first day of the working week. */
gboolean
e_day_view_get_work_week_view	(EDayView	*day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), FALSE);

	return day_view->work_week_view;
}


void
e_day_view_set_work_week_view	(EDayView	*day_view,
				 gboolean	 work_week_view)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (day_view->work_week_view == work_week_view)
		return;

	day_view->work_week_view = work_week_view;

	if (day_view->work_week_view)
		e_day_view_recalc_work_week (day_view);
}


gint
e_day_view_get_days_shown	(EDayView	*day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), -1);

	return day_view->days_shown;
}


void
e_day_view_set_days_shown	(EDayView	*day_view,
				 gint		 days_shown)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));
	g_return_if_fail (days_shown >= 1);
	g_return_if_fail (days_shown <= E_DAY_VIEW_MAX_DAYS);

	if (day_view->days_shown == days_shown)
		return;

	day_view->days_shown = days_shown;

	/* If the date isn't set, just return. */
	if (day_view->lower == 0 && day_view->upper == 0)
		return;

	e_day_view_recalc_day_starts (day_view, day_view->lower);
	e_day_view_recalc_cell_sizes (day_view);

	update_query (day_view);
}


gint
e_day_view_get_mins_per_row	(EDayView	*day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), -1);

	return day_view->mins_per_row;
}


void
e_day_view_set_mins_per_row	(EDayView	*day_view,
				 gint		 mins_per_row)
{
	gint day;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (mins_per_row != 5 && mins_per_row != 10 && mins_per_row != 15
	    && mins_per_row != 30 && mins_per_row != 60) {
		g_warning ("Invalid minutes per row setting");
		return;
	}

	if (day_view->mins_per_row == mins_per_row)
		return;

	day_view->mins_per_row = mins_per_row;
	e_day_view_recalc_num_rows (day_view);

	/* If we aren't visible, we'll sort it out later. */
	if (!GTK_WIDGET_VISIBLE (day_view))
	    return;

	for (day = 0; day < E_DAY_VIEW_MAX_DAYS; day++)
		day_view->need_layout[day] = TRUE;

	/* We need to update all the day event labels since the start & end
	   times may or may not be on row boundaries any more. */
	e_day_view_foreach_event (day_view,
				  e_day_view_set_show_times_cb, NULL);

	/* We must layout the events before updating the scroll region, since
	   that will result in a redraw which would crash otherwise. */
	e_day_view_check_layout (day_view);
	gtk_widget_queue_draw (day_view->time_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);

	e_day_view_update_scroll_regions (day_view);
}


/* This specifies the working days in the week. The value is a bitwise
   combination of day flags. Defaults to Mon-Fri. */
EDayViewDays
e_day_view_get_working_days	(EDayView	*day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), 0);

	return day_view->working_days;
}


void
e_day_view_set_working_days	(EDayView	*day_view,
				 EDayViewDays	 days)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (day_view->working_days == days)
		return;

	day_view->working_days = days;

	if (day_view->work_week_view)
		e_day_view_recalc_work_week (day_view);

	/* We have to do this, as the new working days may have no effect on
	   the days shown, but we still want the background color to change. */
	gtk_widget_queue_draw (day_view->main_canvas);
}


static void
e_day_view_recalc_work_week_days_shown	(EDayView	*day_view)
{
	gint first_day, last_day, i, days_shown;
	gboolean has_working_days = FALSE;

	/* Find the first working day in the week, 0 (Sun) to 6 (Sat). */
	first_day = (day_view->week_start_day + 1) % 7;
	for (i = 0; i < 7; i++) {
		if (day_view->working_days & (1 << first_day)) {
			has_working_days = TRUE;
			break;
		}
		first_day = (first_day + 1) % 7;
	}

	if (has_working_days) {
		/* Now find the last working day of the week, backwards. */
		last_day = day_view->week_start_day % 7;
		for (i = 0; i < 7; i++) {
			if (day_view->working_days & (1 << last_day))
				break;
			last_day = (last_day + 6) % 7;
		}
		/* Now calculate the days we need to show to include all the
		   working days in the week. Add 1 to make it inclusive. */
		days_shown = (last_day + 7 - first_day) % 7 + 1;
	} else {
		/* If no working days are set, just use 7. */
		days_shown = 7;
	}

	e_day_view_set_days_shown (day_view, days_shown);
}


/* The start and end time of the working day. This only affects the background
   colors. */
void
e_day_view_get_working_day		(EDayView	*day_view,
					 gint		*start_hour,
					 gint		*start_minute,
					 gint		*end_hour,
					 gint		*end_minute)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	*start_hour = day_view->work_day_start_hour;
	*start_minute = day_view->work_day_start_minute;
	*end_hour = day_view->work_day_end_hour;
	*end_minute = day_view->work_day_end_minute;
}


void
e_day_view_set_working_day		(EDayView	*day_view,
					 gint		 start_hour,
					 gint		 start_minute,
					 gint		 end_hour,
					 gint		 end_minute)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	day_view->work_day_start_hour = start_hour;
	day_view->work_day_start_minute = start_minute;
	day_view->work_day_end_hour = end_hour;
	day_view->work_day_end_minute = end_minute;

	gtk_widget_queue_draw (day_view->main_canvas);
}


/* Whether we use 12-hour of 24-hour format. */
gboolean
e_day_view_get_24_hour_format	(EDayView	*day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), FALSE);

	return day_view->use_24_hour_format;
}


void
e_day_view_set_24_hour_format	(EDayView	*day_view,
				 gboolean	 use_24_hour)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (day_view->use_24_hour_format == use_24_hour)
		return;

	day_view->use_24_hour_format = use_24_hour;

	/* We need to update all the text in the events since they may contain
	   the time in the old format. */
	e_day_view_foreach_event (day_view, e_day_view_set_show_times_cb,
				  NULL);

	/* FIXME: We need to re-layout the top canvas since the time
	   format affects the sizes. */
	gtk_widget_queue_draw (day_view->time_canvas);
	gtk_widget_queue_draw (day_view->top_canvas);
}


/* Whether we display event end times in the main canvas. */
gboolean
e_day_view_get_show_event_end_times	(EDayView	*day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), TRUE);

	return day_view->show_event_end_times;
}


void
e_day_view_set_show_event_end_times	(EDayView	*day_view,
					 gboolean	 show)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (day_view->show_event_end_times != show) {
		day_view->show_event_end_times = show;
		e_day_view_foreach_event (day_view,
					  e_day_view_set_show_times_cb, NULL);
	}
}


/* The current timezone. */
icaltimezone*
e_day_view_get_timezone			(EDayView	*day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), NULL);

	return day_view->zone;
}


void
e_day_view_set_timezone			(EDayView	*day_view,
					 icaltimezone	*zone)
{
	icaltimezone *old_zone;
	struct icaltimetype tt;
	time_t lower;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	old_zone = day_view->zone;
	if (old_zone == zone)
		return;

	day_view->zone = zone;

	/* If our time hasn't been set yet, just return. */
	if (day_view->lower == 0 && day_view->upper == 0)
		return;

	/* Recalculate the new start of the first day. We just use exactly
	   the same time, but with the new timezone. */
	tt = icaltime_from_timet_with_zone (day_view->lower, FALSE,
					    old_zone);

	lower = icaltime_as_timet_with_zone (tt, zone);

	e_day_view_recalc_day_starts (day_view, lower);
	update_query (day_view);
}


/* This is a callback used to update all day event labels. */
static gboolean
e_day_view_set_show_times_cb		(EDayView	*day_view,
					 gint		 day,
					 gint		 event_num,
					 gpointer	 data)
{
	if (day != E_DAY_VIEW_LONG_EVENT) {
		e_day_view_update_event_label (day_view, day, event_num);
	}

	return TRUE;
}


/* The first day of the week, 0 (Monday) to 6 (Sunday). */
gint
e_day_view_get_week_start_day	(EDayView	*day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), 0);

	return day_view->week_start_day;
}


void
e_day_view_set_week_start_day	(EDayView	*day_view,
				 gint		 week_start_day)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));
	g_return_if_fail (week_start_day >= 0);
	g_return_if_fail (week_start_day < 7);

	if (day_view->week_start_day == week_start_day)
		return;

	day_view->week_start_day = week_start_day;

	if (day_view->work_week_view)
		e_day_view_recalc_work_week (day_view);
}

static EDayViewEvent *
get_current_event (EDayView *day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), NULL);

	if (day_view->editing_event_day == -1)
		return NULL;

	if (day_view->editing_event_day == E_DAY_VIEW_LONG_EVENT)
		return &g_array_index (day_view->long_events,
				       EDayViewEvent,
				       day_view->editing_event_num);
	else
		return &g_array_index (day_view->events[day_view->editing_event_day],
				       EDayViewEvent,
				       day_view->editing_event_num);
}

void
e_day_view_cut_clipboard (EDayView *day_view)
{
	EDayViewEvent *event;
	const char *uid;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	event = get_current_event (day_view);
	if (event == NULL)
		return;

	e_day_view_set_status_message (day_view, _("Deleting selected objects"));

	e_day_view_copy_clipboard (day_view);
	cal_component_get_uid (event->comp, &uid);
	delete_error_dialog (cal_client_remove_object (day_view->client, uid), CAL_COMPONENT_EVENT);

	e_day_view_set_status_message (day_view, NULL);
}

void
e_day_view_copy_clipboard (EDayView *day_view)
{
	EDayViewEvent *event;
	char *comp_str;
	icalcomponent *vcal_comp;
	icalcomponent *new_icalcomp;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	event = get_current_event (day_view);
	if (event == NULL)
		return;

	/* create top-level VCALENDAR component and add VTIMEZONE's */
	vcal_comp = cal_util_new_top_level ();
	cal_util_add_timezones_from_component (vcal_comp, event->comp);

	new_icalcomp = icalcomponent_new_clone (cal_component_get_icalcomponent (event->comp));
	icalcomponent_add_component (vcal_comp, new_icalcomp);

	comp_str = icalcomponent_as_ical_string (vcal_comp);
	if (day_view->clipboard_selection != NULL)
		g_free (day_view->clipboard_selection);
	day_view->clipboard_selection = g_strdup (comp_str);
	gtk_selection_owner_set (day_view->invisible, clipboard_atom, GDK_CURRENT_TIME);

	/* free memory */
	icalcomponent_free (vcal_comp);
}

void
e_day_view_paste_clipboard (EDayView *day_view)
{
	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	gtk_selection_convert (day_view->invisible,
			       clipboard_atom,
			       GDK_SELECTION_TYPE_STRING,
			       GDK_CURRENT_TIME);
}

static void
e_day_view_recalc_work_week	(EDayView	*day_view)
{		
	time_t lower;

	/* If we aren't showing the work week, just return. */
	if (!day_view->work_week_view)
		return;

	e_day_view_recalc_work_week_days_shown	(day_view);
	
	/* If the date isn't set, just return. */
	if (day_view->lower == 0 && day_view->upper == 0)
		return;

	lower = e_day_view_find_work_week_start (day_view, day_view->lower);
	if (lower != day_view->lower) {
		/* Reset the selection, as it may disappear. */
		day_view->selection_start_day = -1;

		e_day_view_recalc_day_starts (day_view, lower);
		update_query (day_view);

		/* This updates the date navigator. */
		e_day_view_update_calendar_selection_time (day_view);
	}
}


static gboolean
e_day_view_update_scroll_regions (EDayView *day_view)
{
	gdouble old_x2, old_y2, new_x2, new_y2;
	gboolean need_reshape = FALSE;

	/* Set the scroll region of the time canvas to its allocated width,
	   but with the height the same as the main canvas. */
	gnome_canvas_get_scroll_region (GNOME_CANVAS (day_view->time_canvas),
					NULL, NULL, &old_x2, &old_y2);
	new_x2 = day_view->time_canvas->allocation.width - 1;
	new_y2 = MAX (day_view->rows * day_view->row_height,
		      day_view->main_canvas->allocation.height) - 1;
	if (old_x2 != new_x2 || old_y2 != new_y2)
		gnome_canvas_set_scroll_region (GNOME_CANVAS (day_view->time_canvas),
						0, 0, new_x2, new_y2);

	/* Set the scroll region of the main canvas to its allocated width,
	   but with the height depending on the number of rows needed. */
	gnome_canvas_get_scroll_region (GNOME_CANVAS (day_view->main_canvas),
					NULL, NULL, &old_x2, &old_y2);
	new_x2 = day_view->main_canvas->allocation.width - 1;
	if (old_x2 != new_x2 || old_y2 != new_y2) {
		need_reshape = TRUE;
		gnome_canvas_set_scroll_region (GNOME_CANVAS (day_view->main_canvas),
						0, 0, new_x2, new_y2);
	}

	return need_reshape;
}


/* This recalculates the number of rows to display, based on the time range
   shown and the minutes per row. */
static void
e_day_view_recalc_num_rows	(EDayView	*day_view)
{
	gint hours, minutes, total_minutes;

	hours = day_view->last_hour_shown - day_view->first_hour_shown;
	/* This could be negative but it works out OK. */
	minutes = day_view->last_minute_shown - day_view->first_minute_shown;
	total_minutes = hours * 60 + minutes;
	day_view->rows = total_minutes / day_view->mins_per_row;
}


/* Converts an hour and minute to a row in the canvas. Note that if we aren't
   showing all 24 hours of the day, the returned row may be negative or
   greater than day_view->rows. */
gint
e_day_view_convert_time_to_row	(EDayView	*day_view,
				 gint		 hour,
				 gint		 minute)
{
	gint total_minutes, start_minute, offset;

	total_minutes = hour * 60 + minute;
	start_minute = day_view->first_hour_shown * 60
		+ day_view->first_minute_shown;
	offset = total_minutes - start_minute;
	if (offset < 0)
		return -1;
	else
		return offset / day_view->mins_per_row;
}


/* Converts an hour and minute to a y coordinate in the canvas. */
gint
e_day_view_convert_time_to_position (EDayView	*day_view,
				     gint	 hour,
				     gint	 minute)
{
	gint total_minutes, start_minute, offset;

	total_minutes = hour * 60 + minute;
	start_minute = day_view->first_hour_shown * 60
		+ day_view->first_minute_shown;
	offset = total_minutes - start_minute;

	return offset * day_view->row_height / day_view->mins_per_row;
}


static gboolean
e_day_view_on_top_canvas_button_press (GtkWidget *widget,
				       GdkEventButton *event,
				       EDayView *day_view)
{
	gint event_x, event_y, day, event_num;
	EDayViewPosition pos;

	/* Convert the coords to the main canvas window, or return if the
	   window is not found. */
	if (!e_day_view_convert_event_coords (day_view, (GdkEvent*) event,
					      GTK_LAYOUT (widget)->bin_window,
					      &event_x, &event_y))
		return FALSE;

	pos = e_day_view_convert_position_in_top_canvas (day_view,
							 event_x, event_y,
							 &day, &event_num);

	if (pos == E_DAY_VIEW_POS_OUTSIDE)
		return FALSE;

	if (pos != E_DAY_VIEW_POS_NONE)
		return e_day_view_on_long_event_button_press (day_view,
							      event_num,
							      event, pos,
							      event_x,
							      event_y);

	e_day_view_stop_editing_event (day_view);

	if (event->button == 1) {
		if (event->type == GDK_2BUTTON_PRESS) {
			time_t dtstart, dtend;

			e_day_view_get_selected_time_range (day_view, &dtstart,
							    &dtend);
			gnome_calendar_new_appointment_for (day_view->calendar,
							    dtstart, dtend,
							    TRUE, FALSE);
			return TRUE;
		}

		if (!GTK_WIDGET_HAS_FOCUS (day_view))
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

		if (gdk_pointer_grab (GTK_LAYOUT (widget)->bin_window, FALSE,
				      GDK_POINTER_MOTION_MASK
				      | GDK_BUTTON_RELEASE_MASK,
				      FALSE, NULL, event->time) == 0) {
			e_day_view_start_selection (day_view, day, -1);
		}
	} else if (event->button == 3) {
		if (!GTK_WIDGET_HAS_FOCUS (day_view))
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

		if (day < day_view->selection_start_day || day > day_view->selection_end_day) {
			e_day_view_start_selection (day_view, day, -1);
			e_day_view_finish_selection (day_view);
		}
		
		e_day_view_on_event_right_click (day_view, event, -1, -1);
	}

	return TRUE;
}


static gboolean
e_day_view_convert_event_coords (EDayView *day_view,
				 GdkEvent *event,
				 GdkWindow *window,
				 gint *x_return,
				 gint *y_return)
{
	gint event_x, event_y, win_x, win_y;
	GdkWindow *event_window;;

	/* Get the event window, x & y from the appropriate event struct. */
	switch (event->type) {
	case GDK_BUTTON_PRESS:
	case GDK_2BUTTON_PRESS:
	case GDK_3BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
		event_x = event->button.x;
		event_y = event->button.y;
		event_window = event->button.window;
		break;
	case GDK_MOTION_NOTIFY:
		event_x = event->motion.x;
		event_y = event->motion.y;
		event_window = event->motion.window;
		break;
	default:
		/* Shouldn't get here. */
		g_assert_not_reached ();
		return FALSE;
	}

	while (event_window && event_window != window
	       && event_window != GDK_ROOT_PARENT()) {
		gdk_window_get_position (event_window, &win_x, &win_y);
		event_x += win_x;
		event_y += win_y;
		event_window = gdk_window_get_parent (event_window);
	}

	*x_return = event_x;
	*y_return = event_y;

	if (event_window != window)
		g_warning ("Couldn't find event window\n");

	return (event_window == window) ? TRUE : FALSE;
}


static gboolean
e_day_view_on_main_canvas_button_press (GtkWidget *widget,
					GdkEventButton *event,
					EDayView *day_view)
{
	gint event_x, event_y, row, day, event_num;
	EDayViewPosition pos;

#if 0
	g_print ("In e_day_view_on_main_canvas_button_press\n");
#endif

	/* Convert the coords to the main canvas window, or return if the
	   window is not found. */
	if (!e_day_view_convert_event_coords (day_view, (GdkEvent*) event,
					      GTK_LAYOUT (widget)->bin_window,
					      &event_x, &event_y))
		return FALSE;

	/* Find out where the mouse is. */
	pos = e_day_view_convert_position_in_main_canvas (day_view,
							  event_x, event_y,
							  &day, &row,
							  &event_num);

	if (pos == E_DAY_VIEW_POS_OUTSIDE)
		return FALSE;

	if (pos != E_DAY_VIEW_POS_NONE)
		return e_day_view_on_event_button_press (day_view, day,
							 event_num, event, pos,
							 event_x, event_y);

	e_day_view_stop_editing_event (day_view);

	/* Start the selection drag. */
	if (event->button == 1) {
		if (event->type == GDK_2BUTTON_PRESS) {
			time_t dtstart, dtend;

			e_day_view_get_selected_time_range (day_view, &dtstart,
							    &dtend);
			gnome_calendar_new_appointment_for (day_view->calendar,
							    dtstart, dtend,
							    FALSE, FALSE);
			return TRUE;
		}

		if (!GTK_WIDGET_HAS_FOCUS (day_view))
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

		if (gdk_pointer_grab (GTK_LAYOUT (widget)->bin_window, FALSE,
				      GDK_POINTER_MOTION_MASK
				      | GDK_BUTTON_RELEASE_MASK,
				      FALSE, NULL, event->time) == 0) {
			e_day_view_start_selection (day_view, day, row);
		}
	} else if (event->button == 3) {
		if (!GTK_WIDGET_HAS_FOCUS (day_view))
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

		
		if ((day < day_view->selection_start_day || day > day_view->selection_end_day)
		    || (day == day_view->selection_start_day && row < day_view->selection_start_row)
		    || (day == day_view->selection_end_day && row > day_view->selection_end_row)) {
			e_day_view_start_selection (day_view, day, row);
			e_day_view_finish_selection (day_view);
		}
		
		e_day_view_on_event_right_click (day_view, event, -1, -1);
	}

	return TRUE;
}

static gboolean
e_day_view_on_main_canvas_scroll (GtkWidget *widget,
				  GdkEventScroll *scroll,
				  EDayView *day_view)
{
	switch (scroll->direction) {
	case GDK_SCROLL_UP:
		e_day_view_scroll (day_view, E_DAY_VIEW_WHEEL_MOUSE_STEP_SIZE);
		return TRUE;
	case GDK_SCROLL_DOWN:
		e_day_view_scroll (day_view, -E_DAY_VIEW_WHEEL_MOUSE_STEP_SIZE);
		return TRUE;
	default:
	}

	return FALSE;
}


static gboolean
e_day_view_on_time_canvas_scroll (GtkWidget      *widget,
				  GdkEventScroll *scroll,
				  EDayView       *day_view)
{
	
	switch (scroll->direction) {
	case GDK_SCROLL_UP:
		e_day_view_scroll (day_view, E_DAY_VIEW_WHEEL_MOUSE_STEP_SIZE);
		return TRUE;
	case GDK_SCROLL_DOWN:
		e_day_view_scroll (day_view, -E_DAY_VIEW_WHEEL_MOUSE_STEP_SIZE);
		return TRUE;
	default:
	}

	return FALSE;
}


/* Callback used when a component is destroyed.  Expects the closure data to be
 * a pointer to a boolean; will set it to TRUE.
 */
static void
comp_destroy_cb (gpointer data, GObject *deadbeef)
{
	gboolean *destroyed;

	destroyed = data;
	*destroyed = TRUE;
}


static gboolean
e_day_view_on_long_event_button_press (EDayView		*day_view,
				       gint		 event_num,
				       GdkEventButton	*event,
				       EDayViewPosition  pos,
				       gint		 event_x,
				       gint		 event_y)
{
	if (event->button == 1) {
		if (event->type == GDK_BUTTON_PRESS) {
			e_day_view_on_long_event_click (day_view, event_num,
							event, pos,
							event_x, event_y);
			return TRUE;
		} else if (event->type == GDK_2BUTTON_PRESS) {
			e_day_view_on_event_double_click (day_view, -1,
							  event_num);
			gtk_signal_emit_stop_by_name (GTK_OBJECT (day_view->top_canvas),
						      "button_press_event");
			return TRUE;
		}
	} else if (event->button == 3) {
		EDayViewEvent *e;
		gboolean destroyed;

		e = &g_array_index (day_view->long_events, EDayViewEvent, event_num);
		destroyed = FALSE;
		g_object_weak_ref ((GObject *) e->comp, comp_destroy_cb, &destroyed);

		if (!GTK_WIDGET_HAS_FOCUS (day_view))
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

		if (!destroyed) {
			g_object_weak_unref ((GObject *) e->comp, comp_destroy_cb, &destroyed);

			e_day_view_set_selected_time_range_in_top_visible (day_view, e->start, e->end);
			
			e_day_view_on_event_right_click (day_view, event,
							 E_DAY_VIEW_LONG_EVENT,
							 event_num);
		}

		return TRUE;
	}
	return FALSE;
}


static gboolean
e_day_view_on_event_button_press (EDayView	  *day_view,
				  gint		   day,
				  gint		   event_num,
				  GdkEventButton  *event,
				  EDayViewPosition pos,
				  gint		   event_x,
				  gint		   event_y)
{
	if (event->button == 1) {
		if (event->type == GDK_BUTTON_PRESS) {
			e_day_view_on_event_click (day_view, day, event_num,
						   event, pos,
						   event_x, event_y);
			return TRUE;
		} else if (event->type == GDK_2BUTTON_PRESS) {
			e_day_view_on_event_double_click (day_view, day,
							  event_num);
			gtk_signal_emit_stop_by_name (GTK_OBJECT (day_view->main_canvas),
						      "button_press_event");
			return TRUE;
		}
	} else if (event->button == 3) {
		EDayViewEvent *e;
		gboolean destroyed;

		e = &g_array_index (day_view->events[day], EDayViewEvent, event_num);

		destroyed = FALSE;
		g_object_weak_ref ((GObject *) e->comp, comp_destroy_cb, &destroyed);

		if (!GTK_WIDGET_HAS_FOCUS (day_view))
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

		if (!destroyed) {
			g_object_weak_unref ((GObject *) e->comp, comp_destroy_cb, &destroyed);

			e_day_view_set_selected_time_range_visible (day_view, e->start, e->end);
	
			e_day_view_on_event_right_click (day_view, event,
							 day, event_num);
		}

		return TRUE;
	}
	return FALSE;
}


static void
e_day_view_on_long_event_click (EDayView *day_view,
				gint event_num,
				GdkEventButton  *bevent,
				EDayViewPosition pos,
				gint	     event_x,
				gint	     event_y)
{
	EDayViewEvent *event;
	gint start_day, end_day, day;
	gint item_x, item_y, item_w, item_h;

	event = &g_array_index (day_view->long_events, EDayViewEvent,
				event_num);

	/* Ignore clicks on the EText while editing. */
	if (pos == E_DAY_VIEW_POS_EVENT
	    && E_TEXT (event->canvas_item)->editing)
		return;

	if ((cal_component_is_instance (event->comp) || !cal_component_has_recurrences (event->comp))
	    && (pos == E_DAY_VIEW_POS_LEFT_EDGE
		|| pos == E_DAY_VIEW_POS_RIGHT_EDGE)) {
		gboolean destroyed;

		if (!e_day_view_find_long_event_days (event,
						      day_view->days_shown,
						      day_view->day_starts,
						      &start_day, &end_day))
			return;

		destroyed = FALSE;
		g_object_weak_ref ((GObject *) event->comp, comp_destroy_cb, &destroyed);

		/* Grab the keyboard focus, so the event being edited is saved
		   and we can use the Escape key to abort the resize. */
		if (!GTK_WIDGET_HAS_FOCUS (day_view))
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

		if (destroyed)
			return;

		g_object_weak_unref ((GObject *) event->comp, comp_destroy_cb, &destroyed);

		if (gdk_pointer_grab (GTK_LAYOUT (day_view->top_canvas)->bin_window, FALSE,
				      GDK_POINTER_MOTION_MASK
				      | GDK_BUTTON_RELEASE_MASK,
				      FALSE, NULL, bevent->time) == 0) {

			day_view->resize_event_day = E_DAY_VIEW_LONG_EVENT;
			day_view->resize_event_num = event_num;
			day_view->resize_drag_pos = pos;
			day_view->resize_start_row = start_day;
			day_view->resize_end_row = end_day;

			/* Create the edit rect if necessary. */
			e_day_view_reshape_resize_long_event_rect_item (day_view);

			/* Make sure the text item is on top. */
			gnome_canvas_item_raise_to_top (day_view->resize_long_event_rect_item);

			/* Raise the event's item, above the rect as well. */
			gnome_canvas_item_raise_to_top (event->canvas_item);
		}
	} else if (e_day_view_get_long_event_position (day_view, event_num,
						       &start_day, &end_day,
						       &item_x, &item_y,
						       &item_w, &item_h)) {
		/* Remember the item clicked and the mouse position,
		   so we can start a drag if the mouse moves. */
		day_view->pressed_event_day = E_DAY_VIEW_LONG_EVENT;
		day_view->pressed_event_num = event_num;

		day_view->drag_event_x = event_x;
		day_view->drag_event_y = event_y;

		e_day_view_convert_position_in_top_canvas (day_view,
							   event_x, event_y,
							   &day, NULL);
		day_view->drag_event_offset = day - start_day;
	}
}


static void
e_day_view_on_event_click (EDayView *day_view,
			   gint day,
			   gint event_num,
			   GdkEventButton  *bevent,
			   EDayViewPosition pos,
			   gint		  event_x,
			   gint		  event_y)
{
	EDayViewEvent *event;
	gint tmp_day, row, start_row;

	event = &g_array_index (day_view->events[day], EDayViewEvent,
				event_num);

	/* Ignore clicks on the EText while editing. */
	if (pos == E_DAY_VIEW_POS_EVENT
	    && E_TEXT (event->canvas_item)->editing)
		return;

	if ((cal_component_is_instance (event->comp) || !cal_component_has_recurrences (event->comp))
	    && (pos == E_DAY_VIEW_POS_TOP_EDGE
		|| pos == E_DAY_VIEW_POS_BOTTOM_EDGE)) {
		gboolean destroyed;

		destroyed = FALSE;
		g_object_weak_ref ((GObject *) event->comp, comp_destroy_cb, &destroyed);

		/* Grab the keyboard focus, so the event being edited is saved
		   and we can use the Escape key to abort the resize. */
		if (!GTK_WIDGET_HAS_FOCUS (day_view))
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

		if (destroyed)
			return;

		g_object_weak_unref ((GObject *) event->comp, comp_destroy_cb, &destroyed);

		if (gdk_pointer_grab (GTK_LAYOUT (day_view->main_canvas)->bin_window, FALSE,
				      GDK_POINTER_MOTION_MASK
				      | GDK_BUTTON_RELEASE_MASK,
				      FALSE, NULL, bevent->time) == 0) {

			day_view->resize_event_day = day;
			day_view->resize_event_num = event_num;
			day_view->resize_drag_pos = pos;
			day_view->resize_start_row = event->start_minute / day_view->mins_per_row;
			day_view->resize_end_row = (event->end_minute - 1) / day_view->mins_per_row;
			if (day_view->resize_end_row < day_view->resize_start_row)
				day_view->resize_end_row = day_view->resize_start_row;

			day_view->resize_bars_event_day = day;
			day_view->resize_bars_event_num = event_num;

			/* Create the edit rect if necessary. */
			e_day_view_reshape_resize_rect_item (day_view);

			e_day_view_reshape_main_canvas_resize_bars (day_view);

			/* Make sure the text item is on top. */
			gnome_canvas_item_raise_to_top (day_view->resize_rect_item);
			gnome_canvas_item_raise_to_top (day_view->resize_bar_item);

			/* Raise the event's item, above the rect as well. */
			gnome_canvas_item_raise_to_top (event->canvas_item);
		}

	} else {
		/* Remember the item clicked and the mouse position,
		   so we can start a drag if the mouse moves. */
		day_view->pressed_event_day = day;
		day_view->pressed_event_num = event_num;

		day_view->drag_event_x = event_x;
		day_view->drag_event_y = event_y;

		e_day_view_convert_position_in_main_canvas (day_view,
							    event_x, event_y,
							    &tmp_day, &row,
							    NULL);
		start_row = event->start_minute / day_view->mins_per_row;
		day_view->drag_event_offset = row - start_row;
	}
}


static void
e_day_view_reshape_resize_long_event_rect_item (EDayView *day_view)
{
	gint day, event_num, start_day, end_day;
	gint item_x, item_y, item_w, item_h;
	gdouble x1, y1, x2, y2;

	day = day_view->resize_event_day;
	event_num = day_view->resize_event_num;

	/* If we're not resizing an event, or the event is not shown,
	   hide the resize bars. */
	if (day_view->resize_drag_pos == E_DAY_VIEW_POS_NONE
	    || !e_day_view_get_long_event_position (day_view, event_num,
						    &start_day, &end_day,
						    &item_x, &item_y,
						    &item_w, &item_h)) {
		gnome_canvas_item_hide (day_view->resize_long_event_rect_item);
		return;
	}

	x1 = item_x;
	y1 = item_y;
	x2 = item_x + item_w - 1;
	y2 = item_y + item_h - 1;

	gnome_canvas_item_set (day_view->resize_long_event_rect_item,
			       "x1", x1,
			       "y1", y1,
			       "x2", x2,
			       "y2", y2,
			       NULL);
	gnome_canvas_item_show (day_view->resize_long_event_rect_item);
}


static void
e_day_view_reshape_resize_rect_item (EDayView *day_view)
{
	gint day, event_num;
	gint item_x, item_y, item_w, item_h;
	gdouble x1, y1, x2, y2;

	day = day_view->resize_event_day;
	event_num = day_view->resize_event_num;

	/* If we're not resizing an event, or the event is not shown,
	   hide the resize bars. */
	if (day_view->resize_drag_pos == E_DAY_VIEW_POS_NONE
	    || !e_day_view_get_event_position (day_view, day, event_num,
					       &item_x, &item_y,
					       &item_w, &item_h)) {
		gnome_canvas_item_hide (day_view->resize_rect_item);
		return;
	}

	x1 = item_x;
	y1 = item_y;
	x2 = item_x + item_w - 1;
	y2 = item_y + item_h - 1;

	gnome_canvas_item_set (day_view->resize_rect_item,
			       "x1", x1 + E_DAY_VIEW_BAR_WIDTH - 1,
			       "y1", y1,
			       "x2", x2,
			       "y2", y2,
			       NULL);
	gnome_canvas_item_show (day_view->resize_rect_item);

	gnome_canvas_item_set (day_view->resize_bar_item,
			       "x1", x1,
			       "y1", y1,
			       "x2", x1 + E_DAY_VIEW_BAR_WIDTH - 1,
			       "y2", y2,
			       NULL);
	gnome_canvas_item_show (day_view->resize_bar_item);
}


static void
e_day_view_on_event_double_click (EDayView *day_view,
				  gint day,
				  gint event_num)
{
	EDayViewEvent *event;
	gboolean destroyed;

	if (day == -1)
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);
	else 
		event = &g_array_index (day_view->events[day], EDayViewEvent,
					event_num);

	destroyed = FALSE;
	g_object_weak_ref ((GObject *) event->comp, comp_destroy_cb, &destroyed);

	e_day_view_stop_editing_event (day_view);

	if (!destroyed) {
		g_object_weak_unref ((GObject *) event->comp, comp_destroy_cb, &destroyed);

		if (day_view->calendar)
			gnome_calendar_edit_object (day_view->calendar, event->comp, FALSE);
		else
			g_warning ("Calendar not set");
	}
}

enum {
	/*
	 * This is used to "flag" events that can not be edited
	 */
	MASK_EDITABLE = 1,

	/*
	 * To disable recurring actions to be displayed
	 */
	MASK_RECURRING = 2,

	/*
	 * To disable actions for non-recurring items to be displayed
	 */
	MASK_SINGLE   = 4,

	/*
	 * This is used to when an event is currently being edited
	 * in another window and we want to disable the event
	 * from being edited twice
	 */
	MASK_EDITING  = 8,

	/*
	 * This is used to when an event is already a meeting and
	 * we want to disable the schedule meeting command
	 */
	MASK_MEETING  = 16,

	/*
	 * To disable cut and copy for meetings the user is not the
	 * organizer of
	 */
	MASK_MEETING_ORGANIZER = 32,

	/*
	 * To disable things not valid for instances
	 */
	MASK_INSTANCE = 64
};

static EPopupMenu main_items [] = {
	E_POPUP_ITEM (N_("New _Appointment"),
	  GTK_SIGNAL_FUNC (e_day_view_on_new_appointment), MASK_EDITABLE),
	E_POPUP_ITEM (N_("New All Day _Event"),
	  GTK_SIGNAL_FUNC (e_day_view_on_new_event), MASK_EDITABLE),
	E_POPUP_ITEM (N_("New Meeting"),
	  GTK_SIGNAL_FUNC (e_day_view_on_new_meeting), MASK_EDITABLE),
	E_POPUP_ITEM (N_("New Task"),
	  GTK_SIGNAL_FUNC (e_day_view_on_new_task), MASK_EDITABLE),

	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("Print..."),
	  GTK_SIGNAL_FUNC (e_day_view_on_print), 0),
	
	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("_Paste"),
	  GTK_SIGNAL_FUNC (e_day_view_on_paste), MASK_EDITABLE),

	E_POPUP_SEPARATOR,

	E_POPUP_SUBMENU (N_("Current View"), NULL, 0),

	E_POPUP_ITEM (N_("Go to _Today"),
	  GTK_SIGNAL_FUNC (e_day_view_on_goto_today), 0),
	E_POPUP_ITEM (N_("_Go to Date..."),
	  GTK_SIGNAL_FUNC (e_day_view_on_goto_date), 0),

	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("_Publish Free/Busy Information"),
	  GTK_SIGNAL_FUNC (e_day_view_on_publish), 0),

	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("_Settings..."),
	  GTK_SIGNAL_FUNC (e_day_view_on_settings), 0),

	E_POPUP_TERMINATOR
};

static EPopupMenu child_items [] = {

	E_POPUP_ITEM (N_("_Open"), GTK_SIGNAL_FUNC (e_day_view_on_edit_appointment), MASK_EDITING),
	E_POPUP_ITEM (N_("_Save As..."), GTK_SIGNAL_FUNC (e_day_view_on_save_as), MASK_EDITING),
	E_POPUP_ITEM (N_("_Print..."), GTK_SIGNAL_FUNC (e_day_view_on_print_event), MASK_EDITING),

	/* Only show this separator if one of the above is shown. */
	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("C_ut"), GTK_SIGNAL_FUNC (e_day_view_on_cut), MASK_EDITABLE | MASK_EDITING | MASK_MEETING_ORGANIZER),
	E_POPUP_ITEM (N_("_Copy"), GTK_SIGNAL_FUNC (e_day_view_on_copy), MASK_EDITING | MASK_MEETING_ORGANIZER),
	E_POPUP_ITEM (N_("_Paste"), GTK_SIGNAL_FUNC (e_day_view_on_paste), MASK_EDITABLE),

	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("_Schedule Meeting..."), GTK_SIGNAL_FUNC (e_day_view_on_meeting), MASK_EDITABLE | MASK_EDITING | MASK_MEETING),
	E_POPUP_ITEM (N_("_Forward as iCalendar..."), GTK_SIGNAL_FUNC (e_day_view_on_forward), MASK_EDITING),
	
	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("_Delete"), GTK_SIGNAL_FUNC (e_day_view_on_delete_appointment), MASK_EDITABLE | MASK_SINGLE | MASK_EDITING),
	E_POPUP_ITEM (N_("Make this Occurrence _Movable"), GTK_SIGNAL_FUNC (e_day_view_on_unrecur_appointment), MASK_EDITABLE | MASK_RECURRING | MASK_EDITING | MASK_INSTANCE),
	E_POPUP_ITEM (N_("Delete this _Occurrence"), GTK_SIGNAL_FUNC (e_day_view_on_delete_occurrence), MASK_EDITABLE | MASK_RECURRING | MASK_EDITING),
	E_POPUP_ITEM (N_("Delete _All Occurrences"), GTK_SIGNAL_FUNC (e_day_view_on_delete_appointment), MASK_EDITABLE | MASK_RECURRING | MASK_EDITING),

	E_POPUP_TERMINATOR
};

static void
free_view_popup (GtkWidget *widget, gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);

	if (day_view->view_menu == NULL)
		return;
	
	gnome_calendar_discard_view_popup (day_view->calendar, day_view->view_menu);
	day_view->view_menu = NULL;
}

static void
e_day_view_show_popup_menu (EDayView *day_view,
			    GdkEvent *gdk_event,
			    gint day,
			    gint event_num)
{
	EDayViewEvent *event;
	int have_selection;
	gboolean being_edited;
	EPopupMenu *context_menu;
	GtkMenu *popup;
	int hide_mask = 0;
	int disable_mask = 0;

	/*
	 * FIXME:
	 * This used to be set only if the event wasn't being edited
	 * in the event editor, but we can't check that at present.
	 * We could possibly set up another method of checking it.
	 */
	
	being_edited = FALSE;
	
	have_selection = GTK_WIDGET_HAS_FOCUS (day_view)
		&& day_view->selection_start_day != -1;

	if (event_num == -1) {
		day_view->view_menu = gnome_calendar_setup_view_popup (day_view->calendar);
		main_items[9].submenu = day_view->view_menu;
		context_menu = main_items;
	} else {
		context_menu = child_items;
		
		if (day == E_DAY_VIEW_LONG_EVENT)
			event = &g_array_index (day_view->long_events,
						EDayViewEvent, event_num);
		else
			event = &g_array_index (day_view->events[day],
						EDayViewEvent, event_num);

		if (cal_component_has_recurrences (event->comp)) 
			hide_mask |= MASK_SINGLE;
		else
			hide_mask |= MASK_RECURRING;

		if (cal_component_is_instance (event->comp))
			hide_mask |= MASK_INSTANCE;
		
		if (cal_component_has_organizer (event->comp)) {
			disable_mask |= MASK_MEETING;

			if (!itip_organizer_is_user (event->comp, day_view->client))
				disable_mask |= MASK_MEETING_ORGANIZER;
		}
	}

	if (cal_client_is_read_only (day_view->client))
		disable_mask |= MASK_EDITABLE;

	if (being_edited)
		disable_mask |= MASK_EDITING;
	
	day_view->popup_event_day = day;
	day_view->popup_event_num = event_num;
	
	popup = e_popup_menu_create (context_menu, disable_mask, hide_mask, day_view);
	g_signal_connect (popup, "selection-done", G_CALLBACK (free_view_popup), day_view);
	e_popup_menu (popup, gdk_event);
}

static gboolean
e_day_view_popup_menu (GtkWidget *widget)
{
	EDayView *day_view = E_DAY_VIEW (widget);
	e_day_view_show_popup_menu (day_view, NULL,
				    day_view->editing_event_day,
				    day_view->editing_event_num);
	return TRUE;
}

static void
e_day_view_on_event_right_click (EDayView *day_view,
				 GdkEventButton *bevent,
				 gint day,
				 gint event_num)
{
	e_day_view_show_popup_menu (day_view, (GdkEvent*)bevent,
				    day, event_num);
}

static void
e_day_view_on_new_appointment (GtkWidget *widget, gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);
	time_t dtstart, dtend;
	struct icaltimetype itt;
	
	/* Edit a new event. If only one day is selected in the top canvas,
	   we set the time to the first 1/2-hour of the working day. */
	if (day_view->selection_in_top_canvas
	    && day_view->selection_start_day != -1
	    && day_view->selection_start_day == day_view->selection_end_day) {
		dtstart = day_view->day_starts[day_view->selection_start_day];
		itt = icaltime_from_timet_with_zone (dtstart, FALSE,
						     day_view->zone);
		itt.hour = calendar_config_get_day_start_hour ();
		itt.minute = calendar_config_get_day_start_minute ();
		dtstart = icaltime_as_timet_with_zone (itt, day_view->zone);

		icaltime_adjust (&itt, 0, 0, 30, 0);
		dtend = icaltime_as_timet_with_zone (itt, day_view->zone);
	} else {
		e_day_view_get_selected_time_range (day_view, &dtstart,
						    &dtend);
	}

	gnome_calendar_new_appointment_for (
		day_view->calendar, dtstart, dtend, FALSE, FALSE);
}

static void
e_day_view_on_new_event (GtkWidget *widget, gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);
	time_t dtstart, dtend;
	
	e_day_view_get_selected_time_range (day_view, &dtstart, &dtend);
	gnome_calendar_new_appointment_for (
		day_view->calendar, dtstart, dtend, TRUE, FALSE);
}

static void
e_day_view_on_new_meeting (GtkWidget *widget, gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);
	time_t dtstart, dtend;
	struct icaltimetype itt;
	
	/* Edit a new event. If only one day is selected in the top canvas,
	   we set the time to the first 1/2-hour of the working day. */
	if (day_view->selection_in_top_canvas
	    && day_view->selection_start_day != -1
	    && day_view->selection_start_day == day_view->selection_end_day) {
		dtstart = day_view->day_starts[day_view->selection_start_day];
		itt = icaltime_from_timet_with_zone (dtstart, FALSE,
						     day_view->zone);
		itt.hour = calendar_config_get_day_start_hour ();
		itt.minute = calendar_config_get_day_start_minute ();
		dtstart = icaltime_as_timet_with_zone (itt, day_view->zone);

		icaltime_adjust (&itt, 0, 0, 30, 0);
		dtend = icaltime_as_timet_with_zone (itt, day_view->zone);
	} else {
		e_day_view_get_selected_time_range (day_view, &dtstart,
						    &dtend);
	}

	gnome_calendar_new_appointment_for (
		day_view->calendar, dtstart, dtend, FALSE, TRUE);
}

static void
e_day_view_on_new_task (GtkWidget *widget, gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);

	gnome_calendar_new_task (day_view->calendar);
}

static void
e_day_view_on_goto_date (GtkWidget *widget, gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);

	goto_dialog (day_view->calendar);
}

static void
e_day_view_on_goto_today (GtkWidget *widget, gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);

	calendar_goto_today (day_view->calendar);
}

static void
e_day_view_on_edit_appointment (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	if (day_view->calendar)
		gnome_calendar_edit_object (day_view->calendar, event->comp, FALSE);
	else
		g_warning ("Calendar not set");
}

static void
e_day_view_on_save_as (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;
	char *filename;
	char *ical_string;
	FILE *file;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;
	
	filename = e_file_dialog_save (_("Save as..."));
	if (filename == NULL)
		return;
	
	ical_string = cal_client_get_component_as_string (day_view->client, event->comp);
	if (ical_string == NULL) {
		g_warning ("Couldn't convert item to a string");
		return;
	}
	
	file = fopen (filename, "w");
	if (file == NULL) {
		g_warning ("Couldn't save item");
		return;
	}
	
	fprintf (file, ical_string);
	g_free (ical_string);
	fclose (file);
}

static void
e_day_view_on_print (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	time_t start;
	GnomeCalendarViewType view_type;
	PrintView print_view;

	day_view = E_DAY_VIEW (data);

	gnome_calendar_get_current_time_range (day_view->calendar, &start, NULL);
	view_type = gnome_calendar_get_view (day_view->calendar);

	switch (view_type) {
	case GNOME_CAL_DAY_VIEW:
		print_view = PRINT_VIEW_DAY;
		break;

	case GNOME_CAL_WORK_WEEK_VIEW:
		print_view = PRINT_VIEW_WEEK;
		break;

	default:
		g_assert_not_reached ();
		return;
	}

	print_calendar (day_view->calendar, FALSE, start, print_view);
}

static void
e_day_view_on_print_event (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	print_comp (event->comp, day_view->client, FALSE);
}

static void
e_day_view_on_meeting (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	if (day_view->calendar)
		gnome_calendar_edit_object (day_view->calendar, event->comp, TRUE);
	else
		g_warning ("Calendar not set");
}

static void
e_day_view_on_forward (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	itip_send_comp (CAL_COMPONENT_METHOD_PUBLISH, event->comp, 
			day_view->client, NULL);
}

static void
e_day_view_on_publish (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	icaltimezone *utc;
	time_t start = time (NULL), end;
	GList *comp_list;

	day_view = E_DAY_VIEW (data);

	utc = icaltimezone_get_utc_timezone ();
	start = time_day_begin_with_zone (start, utc);
	end = time_add_week_with_zone (start, 6, utc);

	comp_list = cal_client_get_free_busy (day_view->client, NULL, start, end);
	if (comp_list) {
		GList *l;

		for (l = comp_list; l; l = l->next) {
			CalComponent *comp = CAL_COMPONENT (l->data);
			itip_send_comp (CAL_COMPONENT_METHOD_PUBLISH, comp, 
					day_view->client, NULL);

			g_object_unref (comp);
		}

 		g_list_free (comp_list);
	}
}

static void
e_day_view_on_settings (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;

	day_view = E_DAY_VIEW (data);

	control_util_show_settings (day_view->calendar);
}

static void
e_day_view_delete_event_internal (EDayView *day_view, EDayViewEvent *event)
{
	CalComponentVType vtype;

	vtype = cal_component_get_vtype (event->comp);

	if (delete_component_dialog (event->comp, FALSE, 1, vtype,
				     GTK_WIDGET (day_view))) {
		const char *uid;

		if (itip_organizer_is_user (event->comp, day_view->client) 
		    && cancel_component_dialog ((GtkWindow *) gtk_widget_get_toplevel (day_view),
						day_view->client, event->comp, TRUE))
			itip_send_comp (CAL_COMPONENT_METHOD_CANCEL, event->comp, day_view->client, NULL);

		cal_component_get_uid (event->comp, &uid);

		delete_error_dialog (cal_client_remove_object (day_view->client, uid), CAL_COMPONENT_EVENT);
	}
}

static void
e_day_view_on_delete_appointment (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;
	gboolean destroyed;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	destroyed = FALSE;
	g_object_weak_ref ((GObject *) event->comp, comp_destroy_cb, &destroyed);

	if (day_view->editing_event_day >= 0)
		e_day_view_stop_editing_event (day_view);

	if (!destroyed) {
		g_object_weak_unref ((GObject *) event->comp, comp_destroy_cb, &destroyed);

		e_day_view_delete_event_internal (day_view, event);
	}
}

void
e_day_view_delete_event		(EDayView       *day_view)
{
	EDayViewEvent *event;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (day_view->editing_event_day == -1)
		return;

	if (day_view->editing_event_day == E_DAY_VIEW_LONG_EVENT)
		event = &g_array_index (day_view->long_events,
					EDayViewEvent,
					day_view->editing_event_num);
	else
		event = &g_array_index (day_view->events[day_view->editing_event_day],
					EDayViewEvent,
					day_view->editing_event_num);

	e_day_view_delete_event_internal (day_view, event);
}

static void
e_day_view_delete_occurrence_internal (EDayView *day_view, EDayViewEvent *event)
{
	CalComponent *comp;

	if (cal_component_is_instance (event->comp)) {
		const char *uid;

		cal_component_get_uid (event->comp, &uid);
		
		delete_error_dialog (cal_client_remove_object_with_mod (day_view->client, uid, CALOBJ_MOD_THIS),
				     CAL_COMPONENT_EVENT);
		return;
	}
	
	/* We must duplicate the CalComponent, or we won't know it has changed
	   when we get the "update_event" callback. */
	comp = cal_component_clone (event->comp);
	cal_comp_util_add_exdate (comp, event->start, day_view->zone);

	if (cal_client_update_object (day_view->client, comp) != CAL_CLIENT_RESULT_SUCCESS)
		g_message ("e_day_view_on_delete_occurrence(): Could not update the object!");

	g_object_unref (comp);
}

static void
e_day_view_on_delete_occurrence (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	e_day_view_delete_occurrence_internal (day_view, event);
}

void
e_day_view_delete_occurrence (EDayView *day_view)
{
	EDayViewEvent *event;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (day_view->editing_event_day == -1)
		return;

	if (day_view->editing_event_day == E_DAY_VIEW_LONG_EVENT)
		event = &g_array_index (day_view->long_events,
					EDayViewEvent,
					day_view->editing_event_num);
	else
		event = &g_array_index (day_view->events[day_view->editing_event_day],
					EDayViewEvent,
					day_view->editing_event_num);

	e_day_view_delete_occurrence_internal (day_view, event);
}

static void
e_day_view_on_cut (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;
	const char *uid;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	e_day_view_on_copy (widget, data);

	if (itip_organizer_is_user (event->comp, day_view->client) 
	    && cancel_component_dialog ((GtkWindow *) gtk_widget_get_toplevel (day_view),
					day_view->client, event->comp, TRUE))
		itip_send_comp (CAL_COMPONENT_METHOD_CANCEL, event->comp, day_view->client, NULL);

	cal_component_get_uid (event->comp, &uid);
	delete_error_dialog (cal_client_remove_object (day_view->client, uid), CAL_COMPONENT_EVENT);
}

static void
e_day_view_on_copy (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;
	char *comp_str;
	icalcomponent *vcal_comp;
	icalcomponent *new_icalcomp;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	/* create top-level VCALENDAR component and add VTIMEZONE's */
	vcal_comp = cal_util_new_top_level ();
	cal_util_add_timezones_from_component (vcal_comp, event->comp);

	new_icalcomp = icalcomponent_new_clone (cal_component_get_icalcomponent (event->comp));
	icalcomponent_add_component (vcal_comp, new_icalcomp);

	comp_str = icalcomponent_as_ical_string (vcal_comp);
	if (day_view->clipboard_selection)
		g_free (day_view->clipboard_selection);
	day_view->clipboard_selection = g_strdup (comp_str);

	gtk_selection_owner_set (day_view->invisible, clipboard_atom, GDK_CURRENT_TIME);

	/* free memory */
	icalcomponent_free (vcal_comp);
}

static void
e_day_view_on_paste (GtkWidget *widget, gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);

	gtk_selection_convert (day_view->invisible,
			       clipboard_atom,
			       GDK_SELECTION_TYPE_STRING,
			       GDK_CURRENT_TIME);
}

static void
e_day_view_on_unrecur_appointment (GtkWidget *widget, gpointer data)
{
	EDayView *day_view;
	EDayViewEvent *event;
	CalComponent *comp, *new_comp;
	CalComponentDateTime date;
	struct icaltimetype itt;

	day_view = E_DAY_VIEW (data);

	event = e_day_view_get_popup_menu_event (day_view);
	if (event == NULL)
		return;

	date.value = &itt;
	date.tzid = NULL;

	/* For the recurring object, we add an exception to get rid of the
	   instance. */

	comp = cal_component_clone (event->comp);
	cal_comp_util_add_exdate (comp, event->start, day_view->zone);

	/* For the unrecurred instance we duplicate the original object,
	   create a new uid for it, get rid of the recurrence rules, and set
	   the start & end times to the instances times. */
	new_comp = cal_component_clone (event->comp);
	cal_component_set_uid (new_comp, cal_component_gen_uid ());
	cal_component_set_rdate_list (new_comp, NULL);
	cal_component_set_rrule_list (new_comp, NULL);
	cal_component_set_exdate_list (new_comp, NULL);
	cal_component_set_exrule_list (new_comp, NULL);

	date.value = &itt;
	date.tzid = icaltimezone_get_tzid (day_view->zone);

	*date.value = icaltime_from_timet_with_zone (event->start, FALSE,
						     day_view->zone);
	cal_component_set_dtstart (new_comp, &date);
	*date.value = icaltime_from_timet_with_zone (event->end, FALSE,
						     day_view->zone);
	cal_component_set_dtend (new_comp, &date);


	/* Now update both CalComponents. Note that we do this last since at
	 * present the updates happen synchronously so our event may disappear.
	 */
	if (cal_client_update_object (day_view->client, comp) != CAL_CLIENT_RESULT_SUCCESS)
		g_message ("e_day_view_on_unrecur_appointment(): Could not update the object!");

	g_object_unref (comp);

	if (cal_client_update_object (day_view->client, new_comp) != CAL_CLIENT_RESULT_SUCCESS)
		g_message ("e_day_view_on_unrecur_appointment(): Could not update the object!");

	g_object_unref (new_comp);
}


static EDayViewEvent*
e_day_view_get_popup_menu_event (EDayView *day_view)
{
	if (day_view->popup_event_num == -1)
		return NULL;

	if (day_view->popup_event_day == E_DAY_VIEW_LONG_EVENT)
		return &g_array_index (day_view->long_events,
				       EDayViewEvent,
				       day_view->popup_event_num);
	else
		return &g_array_index (day_view->events[day_view->popup_event_day],
				       EDayViewEvent,
				       day_view->popup_event_num);
}


static gboolean
e_day_view_on_top_canvas_button_release (GtkWidget *widget,
					 GdkEventButton *event,
					 EDayView *day_view)
{
	if (day_view->selection_is_being_dragged) {
		gdk_pointer_ungrab (event->time);
		e_day_view_finish_selection (day_view);
	} else if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE) {
		gdk_pointer_ungrab (event->time);
		e_day_view_finish_long_event_resize (day_view);
	} else if (day_view->pressed_event_day != -1) {
		e_day_view_start_editing_event (day_view,
						day_view->pressed_event_day,
						day_view->pressed_event_num,
						NULL);
	}

	day_view->pressed_event_day = -1;

	return FALSE;
}


static gboolean
e_day_view_on_main_canvas_button_release (GtkWidget *widget,
					  GdkEventButton *event,
					  EDayView *day_view)
{
#if 0
	g_print ("In e_day_view_on_main_canvas_button_release\n");
#endif

	if (day_view->selection_is_being_dragged) {
		gdk_pointer_ungrab (event->time);
		e_day_view_finish_selection (day_view);
		e_day_view_stop_auto_scroll (day_view);
	} else if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE) {
		gdk_pointer_ungrab (event->time);
		e_day_view_finish_resize (day_view);
		e_day_view_stop_auto_scroll (day_view);
	} else if (day_view->pressed_event_day != -1) {
		e_day_view_start_editing_event (day_view,
						day_view->pressed_event_day,
						day_view->pressed_event_num,
						NULL);
	}

	day_view->pressed_event_day = -1;

	return FALSE;
}


static void
e_day_view_update_calendar_selection_time (EDayView *day_view)
{
	time_t start, end;

	e_day_view_get_selected_time_range (day_view, &start, &end);

#if 0
	g_print ("Start: %s", ctime (&start));
	g_print ("End  : %s", ctime (&end));
#endif

	if (day_view->calendar)
		gnome_calendar_set_selected_time_range (day_view->calendar,
							start, end);
}


static gboolean
e_day_view_on_top_canvas_motion (GtkWidget *widget,
				 GdkEventMotion *mevent,
				 EDayView *day_view)
{
	EDayViewEvent *event = NULL;
	EDayViewPosition pos;
	gint event_x, event_y, canvas_x, canvas_y;
	gint day, event_num;
	GdkCursor *cursor;

#if 0
	g_print ("In e_day_view_on_top_canvas_motion\n");
#endif

	/* Convert the coords to the main canvas window, or return if the
	   window is not found. */
	if (!e_day_view_convert_event_coords (day_view, (GdkEvent*) mevent,
					      GTK_LAYOUT (widget)->bin_window,
					      &event_x, &event_y))
		return FALSE;

	canvas_x = event_x;
	canvas_y = event_y;

	pos = e_day_view_convert_position_in_top_canvas (day_view,
							 canvas_x, canvas_y,
							 &day, &event_num);
	if (event_num != -1)
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);

	if (day_view->selection_is_being_dragged) {
		e_day_view_update_selection (day_view, day, -1);
		return TRUE;
	} else if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE) {
		if (pos != E_DAY_VIEW_POS_OUTSIDE) {
			e_day_view_update_long_event_resize (day_view, day);
			return TRUE;
		}
	} else if (day_view->pressed_event_day == E_DAY_VIEW_LONG_EVENT) {
		GtkTargetList *target_list;

		event = &g_array_index (day_view->long_events, EDayViewEvent,
					day_view->pressed_event_num);

		if ((cal_component_is_instance (event->comp) || !cal_component_has_recurrences (event->comp))
		    && (abs (canvas_x - day_view->drag_event_x)
			> E_DAY_VIEW_DRAG_START_OFFSET
			|| abs (canvas_y - day_view->drag_event_y)
			> E_DAY_VIEW_DRAG_START_OFFSET)) {
			day_view->drag_event_day = day_view->pressed_event_day;
			day_view->drag_event_num = day_view->pressed_event_num;
			day_view->pressed_event_day = -1;

			/* Hide the horizontal bars. */
			if (day_view->resize_bars_event_day != -1) {
				day_view->resize_bars_event_day = -1;
				day_view->resize_bars_event_num = -1;
				gnome_canvas_item_hide (day_view->main_canvas_top_resize_bar_item);
				gnome_canvas_item_hide (day_view->main_canvas_bottom_resize_bar_item);
			}

			target_list = gtk_target_list_new (target_table,
							   n_targets);
			gtk_drag_begin (widget, target_list,
					GDK_ACTION_COPY | GDK_ACTION_MOVE,
					1, (GdkEvent*)mevent);
			gtk_target_list_unref (target_list);
		}
	} else {
		cursor = day_view->normal_cursor;

		/* Recurring events can't be resized. */
		if (event && (cal_component_is_instance (event->comp) || !cal_component_has_recurrences (event->comp))) {
			switch (pos) {
			case E_DAY_VIEW_POS_LEFT_EDGE:
			case E_DAY_VIEW_POS_RIGHT_EDGE:
				cursor = day_view->resize_width_cursor;
				break;
			default:
				break;
			}
		}

		/* Only set the cursor if it is different to last one set. */
		if (day_view->last_cursor_set_in_top_canvas != cursor) {
			day_view->last_cursor_set_in_top_canvas = cursor;
			gdk_window_set_cursor (widget->window, cursor);
		}

	}

	return FALSE;
}


static gboolean
e_day_view_on_main_canvas_motion (GtkWidget *widget,
				  GdkEventMotion *mevent,
				  EDayView *day_view)
{
	EDayViewEvent *event = NULL;
	EDayViewPosition pos;
	gint event_x, event_y, canvas_x, canvas_y;
	gint row, day, event_num;
	GdkCursor *cursor;

#if 0
	g_print ("In e_day_view_on_main_canvas_motion\n");
#endif

	/* Convert the coords to the main canvas window, or return if the
	   window is not found. */
	if (!e_day_view_convert_event_coords (day_view, (GdkEvent*) mevent,
					      GTK_LAYOUT (widget)->bin_window,
					      &event_x, &event_y))
		return FALSE;

	canvas_x = event_x;
	canvas_y = event_y;

	pos = e_day_view_convert_position_in_main_canvas (day_view,
							  canvas_x, canvas_y,
							  &day, &row,
							  &event_num);
	if (event_num != -1)
		event = &g_array_index (day_view->events[day], EDayViewEvent,
					event_num);

	if (day_view->selection_is_being_dragged) {
		if (pos != E_DAY_VIEW_POS_OUTSIDE) {
			e_day_view_update_selection (day_view, day, row);
			e_day_view_check_auto_scroll (day_view,
						      event_x, event_y);
			return TRUE;
		}
	} else if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE) {
		if (pos != E_DAY_VIEW_POS_OUTSIDE) {
			e_day_view_update_resize (day_view, row);
			e_day_view_check_auto_scroll (day_view,
						      event_x, event_y);
			return TRUE;
		}
	} else if (day_view->pressed_event_day != -1
		   && day_view->pressed_event_day != E_DAY_VIEW_LONG_EVENT) {
		GtkTargetList *target_list;

		event = &g_array_index (day_view->events[day_view->pressed_event_day], EDayViewEvent, day_view->pressed_event_num);

		if ((cal_component_is_instance (event->comp) || !cal_component_has_recurrences (event->comp))
		    && (abs (canvas_x - day_view->drag_event_x)
			> E_DAY_VIEW_DRAG_START_OFFSET
			|| abs (canvas_y - day_view->drag_event_y)
			> E_DAY_VIEW_DRAG_START_OFFSET)) {
			day_view->drag_event_day = day_view->pressed_event_day;
			day_view->drag_event_num = day_view->pressed_event_num;
			day_view->pressed_event_day = -1;

			/* Hide the horizontal bars. */
			if (day_view->resize_bars_event_day != -1) {
				day_view->resize_bars_event_day = -1;
				day_view->resize_bars_event_num = -1;
				gnome_canvas_item_hide (day_view->main_canvas_top_resize_bar_item);
				gnome_canvas_item_hide (day_view->main_canvas_bottom_resize_bar_item);
			}

			target_list = gtk_target_list_new (target_table,
							   n_targets);
			gtk_drag_begin (widget, target_list,
					GDK_ACTION_COPY | GDK_ACTION_MOVE,
					1, (GdkEvent*)mevent);
			gtk_target_list_unref (target_list);
		}
	} else {
		cursor = day_view->normal_cursor;

		/* Recurring events can't be resized. */
		if (event && (cal_component_is_instance (event->comp) || !cal_component_has_recurrences (event->comp))) {
			switch (pos) {
			case E_DAY_VIEW_POS_LEFT_EDGE:
				cursor = day_view->move_cursor;
				break;
			case E_DAY_VIEW_POS_TOP_EDGE:
			case E_DAY_VIEW_POS_BOTTOM_EDGE:
				cursor = day_view->resize_height_cursor;
				break;
			default:
				break;
			}
		}

		/* Only set the cursor if it is different to last one set. */
		if (day_view->last_cursor_set_in_main_canvas != cursor) {
			day_view->last_cursor_set_in_main_canvas = cursor;
			gdk_window_set_cursor (widget->window, cursor);
		}
	}

	return FALSE;
}


/* This sets the selection to a single cell. If day is -1 then the current
   start day is reused. If row is -1 then the selection is in the top canvas.
*/
void
e_day_view_start_selection (EDayView *day_view,
			    gint day,
			    gint row)
{
	if (day == -1) {
		day = day_view->selection_start_day;
		if (day == -1)
			day = 0;
	}

	day_view->selection_start_day = day;
	day_view->selection_end_day = day;

	day_view->selection_start_row = row;
	day_view->selection_end_row = row;

	day_view->selection_is_being_dragged = TRUE;
	day_view->selection_drag_pos = E_DAY_VIEW_DRAG_END;
	day_view->selection_in_top_canvas = (row == -1) ? TRUE : FALSE;

	/* FIXME: Optimise? */
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
}


/* Updates the selection during a drag. If day is -1 the selection day is
   unchanged. */
void
e_day_view_update_selection (EDayView *day_view,
			     gint day,
			     gint row)
{
	gboolean need_redraw = FALSE;

#if 0
	g_print ("Updating selection %i,%i\n", day, row);
#endif

	day_view->selection_in_top_canvas = (row == -1) ? TRUE : FALSE;

	if (day == -1)
		day = (day_view->selection_drag_pos == E_DAY_VIEW_DRAG_START)
			? day_view->selection_start_day
			: day_view->selection_end_day;

	if (day_view->selection_drag_pos == E_DAY_VIEW_DRAG_START) {
		if (row != day_view->selection_start_row
		    || day != day_view->selection_start_day) {
			need_redraw = TRUE;
			day_view->selection_start_row = row;
			day_view->selection_start_day = day;
		}
	} else {
		if (row != day_view->selection_end_row
		    || day != day_view->selection_end_day) {
			need_redraw = TRUE;
			day_view->selection_end_row = row;
			day_view->selection_end_day = day;
		}
	}

	e_day_view_normalize_selection (day_view);

	/* FIXME: Optimise? */
	if (need_redraw) {
		gtk_widget_queue_draw (day_view->top_canvas);
		gtk_widget_queue_draw (day_view->main_canvas);
	}
}


static void
e_day_view_normalize_selection (EDayView *day_view)
{
	gint tmp_row, tmp_day;

	/* Switch the drag position if necessary. */
	if (day_view->selection_start_day > day_view->selection_end_day
	    || (day_view->selection_start_day == day_view->selection_end_day
		&& day_view->selection_start_row > day_view->selection_end_row)) {
		tmp_row = day_view->selection_start_row;
		tmp_day = day_view->selection_start_day;
		day_view->selection_start_day = day_view->selection_end_day;
		day_view->selection_start_row = day_view->selection_end_row;
		day_view->selection_end_day = tmp_day;
		day_view->selection_end_row = tmp_row;
		if (day_view->selection_drag_pos == E_DAY_VIEW_DRAG_START)
			day_view->selection_drag_pos = E_DAY_VIEW_DRAG_END;
		else
			day_view->selection_drag_pos = E_DAY_VIEW_DRAG_START;
	}
}


void
e_day_view_finish_selection (EDayView *day_view)
{
	day_view->selection_is_being_dragged = FALSE;
	e_day_view_update_calendar_selection_time (day_view);
}

static void
e_day_view_update_long_event_resize (EDayView *day_view,
				     gint day)
{
	EDayViewEvent *event;
	gint event_num;
	gboolean need_reshape = FALSE;

#if 0
	g_print ("Updating resize Day:%i\n", day);
#endif

	event_num = day_view->resize_event_num;
	event = &g_array_index (day_view->long_events, EDayViewEvent,
				event_num);

	if (day_view->resize_drag_pos == E_DAY_VIEW_POS_LEFT_EDGE) {
		day = MIN (day, day_view->resize_end_row);
		if (day != day_view->resize_start_row) {
			need_reshape = TRUE;
			day_view->resize_start_row = day;

		}
	} else {
		day = MAX (day, day_view->resize_start_row);
		if (day != day_view->resize_end_row) {
			need_reshape = TRUE;
			day_view->resize_end_row = day;
		}
	}

	/* FIXME: Optimise? */
	if (need_reshape) {
		e_day_view_reshape_long_event (day_view, event_num);
		e_day_view_reshape_resize_long_event_rect_item (day_view);
		gtk_widget_queue_draw (day_view->top_canvas);
	}
}


static void
e_day_view_update_resize (EDayView *day_view,
			  gint row)
{
	EDayViewEvent *event;
	gint day, event_num;
	gboolean need_reshape = FALSE;

#if 0
	g_print ("Updating resize Row:%i\n", row);
#endif

	day = day_view->resize_event_day;
	event_num = day_view->resize_event_num;
	event = &g_array_index (day_view->events[day], EDayViewEvent,
				event_num);

	if (day_view->resize_drag_pos == E_DAY_VIEW_POS_TOP_EDGE) {
		row = MIN (row, day_view->resize_end_row);
		if (row != day_view->resize_start_row) {
			need_reshape = TRUE;
			day_view->resize_start_row = row;

		}
	} else {
		row = MAX (row, day_view->resize_start_row);
		if (row != day_view->resize_end_row) {
			need_reshape = TRUE;
			day_view->resize_end_row = row;
		}
	}

	/* FIXME: Optimise? */
	if (need_reshape) {
		e_day_view_reshape_day_event (day_view, day, event_num);
		e_day_view_reshape_resize_rect_item (day_view);
		e_day_view_reshape_main_canvas_resize_bars (day_view);
		gtk_widget_queue_draw (day_view->main_canvas);
	}
}


/* This converts the resize start or end row back to a time and updates the
   event. */
static void
e_day_view_finish_long_event_resize (EDayView *day_view)
{
	EDayViewEvent *event;
	gint event_num;
	CalComponent *comp;
	CalComponentDateTime date;
	struct icaltimetype itt;
	time_t dt;

	event_num = day_view->resize_event_num;
	event = &g_array_index (day_view->long_events, EDayViewEvent,
				event_num);

	/* We use a temporary copy of the comp since we don't want to
	   change the original comp here. Otherwise we would not detect that
	   the event's time had changed in the "update_event" callback. */
	comp = cal_component_clone (event->comp);

	date.value = &itt;
	/* FIXME: Should probably keep the timezone of the original start
	   and end times. */
	date.tzid = icaltimezone_get_tzid (day_view->zone);

	if (day_view->resize_drag_pos == E_DAY_VIEW_POS_LEFT_EDGE) {
		dt = day_view->day_starts[day_view->resize_start_row];
		*date.value = icaltime_from_timet_with_zone (dt, FALSE,
							     day_view->zone);
		cal_component_set_dtstart (comp, &date);
	} else {
		dt = day_view->day_starts[day_view->resize_end_row + 1];
		*date.value = icaltime_from_timet_with_zone (dt, FALSE,
							     day_view->zone);
		cal_component_set_dtend (comp, &date);
	}

 	if (cal_component_is_instance (comp)) {
 		CalObjModType mod;
 
 		if (recur_component_dialog (comp, &mod, NULL)) {
 			if (cal_client_update_object_with_mod (day_view->client, comp, mod) == CAL_CLIENT_RESULT_SUCCESS) {
 				if (itip_organizer_is_user (comp, day_view->client) &&
				    send_component_dialog (gtk_widget_get_toplevel (day_view),
							   day_view->client, comp, FALSE))
 					itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp, day_view->client, NULL);
 			} else {
 				g_message ("e_day_view_finish_resize(): Could not update the object!");
 			}
 		} else {
 			gtk_widget_queue_draw (day_view->top_canvas);
 		}		
 	} else if (cal_client_update_object (day_view->client, comp) == CAL_CLIENT_RESULT_SUCCESS) {
 		if (itip_organizer_is_user (comp, day_view->client) &&
		    send_component_dialog (gtk_widget_get_toplevel (day_view),
					   day_view->client, comp, TRUE))
  			itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp, day_view->client, NULL);
  	} else {
  		g_message ("e_day_view_finish_long_event_resize(): Could not update the object!");
  	}
  
 	gnome_canvas_item_hide (day_view->resize_long_event_rect_item);
 
 	day_view->resize_drag_pos = E_DAY_VIEW_POS_NONE;

	g_object_unref (comp);
}


/* This converts the resize start or end row back to a time and updates the
   event. */
static void
e_day_view_finish_resize (EDayView *day_view)
{
	EDayViewEvent *event;
	gint day, event_num;
	CalComponent *comp;
	CalComponentDateTime date;
	struct icaltimetype itt;
	time_t dt;

	day = day_view->resize_event_day;
	event_num = day_view->resize_event_num;
	event = &g_array_index (day_view->events[day], EDayViewEvent,
				event_num);

	/* We use a temporary shallow copy of the ico since we don't want to
	   change the original ico here. Otherwise we would not detect that
	   the event's time had changed in the "update_event" callback. */
	comp = cal_component_clone (event->comp);

	date.value = &itt;
	/* FIXME: Should probably keep the timezone of the original start
	   and end times. */
	date.tzid = icaltimezone_get_tzid (day_view->zone);

	if (day_view->resize_drag_pos == E_DAY_VIEW_POS_TOP_EDGE) {
		dt = e_day_view_convert_grid_position_to_time (day_view, day, day_view->resize_start_row);
		*date.value = icaltime_from_timet_with_zone (dt, FALSE,
							     day_view->zone);
		cal_component_set_dtstart (comp, &date);
	} else {
		dt = e_day_view_convert_grid_position_to_time (day_view, day, day_view->resize_end_row + 1);
		*date.value = icaltime_from_timet_with_zone (dt, FALSE,
							     day_view->zone);
		cal_component_set_dtend (comp, &date);
	}

	gnome_canvas_item_hide (day_view->resize_rect_item);
	gnome_canvas_item_hide (day_view->resize_bar_item);

	/* Hide the horizontal bars. */
	day_view->resize_bars_event_day = -1;
	day_view->resize_bars_event_num = -1;
	gnome_canvas_item_hide (day_view->main_canvas_top_resize_bar_item);
	gnome_canvas_item_hide (day_view->main_canvas_bottom_resize_bar_item);

	day_view->resize_drag_pos = E_DAY_VIEW_POS_NONE;

	if (cal_component_is_instance (comp)) {
		CalObjModType mod;

		if (recur_component_dialog (comp, &mod, NULL)) {
			if (cal_client_update_object_with_mod (day_view->client, comp, mod) == CAL_CLIENT_RESULT_SUCCESS) {
				if (itip_organizer_is_user (comp, day_view->client) &&
				    send_component_dialog (gtk_widget_get_toplevel (day_view),
							   day_view->client, comp, FALSE))
					itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp, day_view->client, NULL);
			} else {
				g_message ("e_day_view_finish_resize(): Could not update the object!");
			}
		} else {
			gtk_widget_queue_draw (day_view->main_canvas);
		}		
	} else if (cal_client_update_object (day_view->client, comp) == CAL_CLIENT_RESULT_SUCCESS) {
		if (itip_organizer_is_user (comp, day_view->client) &&
		    send_component_dialog (gtk_widget_get_toplevel (day_view), day_view->client, comp, FALSE))
			itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp, day_view->client, NULL);
	} else {
		g_message ("e_day_view_finish_resize(): Could not update the object!");
	}
	
	g_object_unref (comp);
}


static void
e_day_view_abort_resize (EDayView *day_view,
			 guint32 time)
{
	gint day, event_num;

	if (day_view->resize_drag_pos == E_DAY_VIEW_POS_NONE)
		return;

	day_view->resize_drag_pos = E_DAY_VIEW_POS_NONE;
	gdk_pointer_ungrab (time);

	day = day_view->resize_event_day;
	event_num = day_view->resize_event_num;

	if (day == E_DAY_VIEW_LONG_EVENT) {
		e_day_view_reshape_long_event (day_view, event_num);
		gtk_widget_queue_draw (day_view->top_canvas);

		day_view->last_cursor_set_in_top_canvas = day_view->normal_cursor;
		gdk_window_set_cursor (day_view->top_canvas->window,
				       day_view->normal_cursor);
		gnome_canvas_item_hide (day_view->resize_long_event_rect_item);
	} else {
		e_day_view_reshape_day_event (day_view, day, event_num);
		e_day_view_reshape_main_canvas_resize_bars (day_view);
		gtk_widget_queue_draw (day_view->main_canvas);

		day_view->last_cursor_set_in_main_canvas = day_view->normal_cursor;
		gdk_window_set_cursor (day_view->main_canvas->window,
				       day_view->normal_cursor);
		gnome_canvas_item_hide (day_view->resize_rect_item);
		gnome_canvas_item_hide (day_view->resize_bar_item);
	}
}


static void
e_day_view_free_events (EDayView *day_view)
{
	gint day;

	/* Reset all our indices. */
	day_view->editing_event_day = -1;
	day_view->popup_event_day = -1;
	day_view->resize_bars_event_day = -1;
	day_view->resize_event_day = -1;
	day_view->pressed_event_day = -1;
	day_view->drag_event_day = -1;

	e_day_view_free_event_array (day_view, day_view->long_events);

	for (day = 0; day < E_DAY_VIEW_MAX_DAYS; day++)
		e_day_view_free_event_array (day_view, day_view->events[day]);
}


static void
e_day_view_free_event_array (EDayView *day_view,
			     GArray *array)
{
	EDayViewEvent *event;
	gint event_num;

	for (event_num = 0; event_num < array->len; event_num++) {
		event = &g_array_index (array, EDayViewEvent, event_num);
		if (event->canvas_item)
			gtk_object_destroy (GTK_OBJECT (event->canvas_item));
		g_object_unref (event->comp);
	}

	g_array_set_size (array, 0);
}


/* This adds one event to the view, adding it to the appropriate array. */
static gboolean
e_day_view_add_event (CalComponent *comp,
		      time_t	  start,
		      time_t	  end,
		      gpointer	  data)

{
	EDayView *day_view;
	EDayViewEvent event;
	gint day, offset;
	struct icaltimetype start_tt, end_tt;

	day_view = E_DAY_VIEW (data);

#if 0
	g_print ("Day view lower: %s", ctime (&day_view->lower));
	g_print ("Day view upper: %s", ctime (&day_view->upper));
	g_print ("Event start: %s", ctime (&start));
	g_print ("Event end  : %s\n", ctime (&end));
#endif

	/* Check that the event times are valid. */
	g_return_val_if_fail (start <= end, TRUE);
	g_return_val_if_fail (start < day_view->upper, TRUE);
	g_return_val_if_fail (end > day_view->lower, TRUE);

	start_tt = icaltime_from_timet_with_zone (start, FALSE,
						  day_view->zone);
	end_tt = icaltime_from_timet_with_zone (end, FALSE,
						day_view->zone);

	event.comp = comp;
	g_object_ref (comp);
	event.start = start;
	event.end = end;
	event.canvas_item = NULL;

	/* Calculate the start & end minute, relative to the top of the
	   display. */
	offset = day_view->first_hour_shown * 60
		+ day_view->first_minute_shown;
	event.start_minute = start_tt.hour * 60 + start_tt.minute - offset;
	event.end_minute = end_tt.hour * 60 + end_tt.minute - offset;

	event.start_row_or_col = 0;
	event.num_columns = 0;

	event.different_timezone = FALSE;
	if (!cal_comp_util_compare_event_timezones (comp, day_view->client,
						    day_view->zone))
		event.different_timezone = TRUE;

	/* Find out which array to add the event to. */
	for (day = 0; day < day_view->days_shown; day++) {
		if (start >= day_view->day_starts[day]
		    && end <= day_view->day_starts[day + 1]) {

			/* Special case for when the appointment ends at
			   midnight, i.e. the start of the next day. */
			if (end == day_view->day_starts[day + 1]) {

				/* If the event last the entire day, then we
				   skip it here so it gets added to the top
				   canvas. */
				if (start == day_view->day_starts[day])
				    break;

				event.end_minute = 24 * 60;
			}

			g_array_append_val (day_view->events[day], event);
			day_view->events_sorted[day] = FALSE;
			day_view->need_layout[day] = TRUE;
			return TRUE;
		}
	}

	/* The event wasn't within one day so it must be a long event,
	   i.e. shown in the top canvas. */
	g_array_append_val (day_view->long_events, event);
	day_view->long_events_sorted = FALSE;
	day_view->long_events_need_layout = TRUE;
	return TRUE;
}


/* This lays out the short (less than 1 day) events in the columns.
   Any long events are simply skipped. */
void
e_day_view_check_layout (EDayView *day_view)
{
	gint day, rows_in_top_display, top_canvas_height, top_rows;

	/* Don't bother if we aren't visible. */
	if (!GTK_WIDGET_VISIBLE (day_view))
	    return;

	/* Make sure the events are sorted (by start and size). */
	e_day_view_ensure_events_sorted (day_view);

	for (day = 0; day < day_view->days_shown; day++) {
		if (day_view->need_layout[day])
			e_day_view_layout_day_events (day_view->events[day],
						      day_view->rows,
						      day_view->mins_per_row,
						      day_view->cols_per_row[day]);

		if (day_view->need_layout[day]
		    || day_view->need_reshape[day]) {
			e_day_view_reshape_day_events (day_view, day);

			if (day_view->resize_bars_event_day == day)
				e_day_view_reshape_main_canvas_resize_bars (day_view);
		}

		day_view->need_layout[day] = FALSE;
		day_view->need_reshape[day] = FALSE;
	}

	if (day_view->long_events_need_layout) {
		e_day_view_layout_long_events (day_view->long_events,
					       day_view->days_shown,
					       day_view->day_starts,
					       &rows_in_top_display);

		/* Set the height of the top canvas based on the row height
		   and the number of rows needed (min 1 + 1 for the dates + 1
		   space for DnD).*/
		if (day_view->rows_in_top_display != rows_in_top_display) {
			day_view->rows_in_top_display = rows_in_top_display;
			top_rows = MAX (1, rows_in_top_display);
			top_canvas_height = (top_rows + 2)
				* day_view->top_row_height;
			gtk_widget_set_usize (day_view->top_canvas, -1,
					      top_canvas_height);
		}
	}


	if (day_view->long_events_need_layout
	    || day_view->long_events_need_reshape)
		e_day_view_reshape_long_events (day_view);

	day_view->long_events_need_layout = FALSE;
	day_view->long_events_need_reshape = FALSE;
}


static void
e_day_view_reshape_long_events (EDayView *day_view)
{
	EDayViewEvent *event;
	gint event_num;

	for (event_num = 0; event_num < day_view->long_events->len;
	     event_num++) {
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);

		if (event->num_columns == 0) {
			if (event->canvas_item) {
				gtk_object_destroy (GTK_OBJECT (event->canvas_item));
				event->canvas_item = NULL;
			}
		} else {
			e_day_view_reshape_long_event (day_view, event_num);
		}
	}
}


static void
e_day_view_reshape_long_event (EDayView *day_view,
			       gint	 event_num)
{
	EDayViewEvent *event;
	gint start_day, end_day, item_x, item_y, item_w, item_h;
	gint text_x, text_w, num_icons, icons_width, width, time_width;
	CalComponent *comp;
	gint min_text_x, max_text_w, text_width, line_len;
	gchar *text, *end_of_line;
	gboolean show_icons = TRUE, use_max_width = FALSE;
	PangoFontDescription *font_desc;
	PangoContext *pango_context;
	PangoLayout *layout;

	event = &g_array_index (day_view->long_events, EDayViewEvent,
				event_num);

	if (!e_day_view_get_long_event_position (day_view, event_num,
						 &start_day, &end_day,
						 &item_x, &item_y,
						 &item_w, &item_h)) {
		if (event->canvas_item) {
			gtk_object_destroy (GTK_OBJECT (event->canvas_item));
			event->canvas_item = NULL;
		}
		return;
	}

	/* Take off the border and padding. */
	item_x += E_DAY_VIEW_LONG_EVENT_BORDER_WIDTH + E_DAY_VIEW_LONG_EVENT_X_PAD;
	item_w -= (E_DAY_VIEW_LONG_EVENT_BORDER_WIDTH + E_DAY_VIEW_LONG_EVENT_X_PAD) * 2;
	item_y += E_DAY_VIEW_LONG_EVENT_BORDER_HEIGHT + E_DAY_VIEW_LONG_EVENT_Y_PAD;
	item_h -= (E_DAY_VIEW_LONG_EVENT_BORDER_HEIGHT + E_DAY_VIEW_LONG_EVENT_Y_PAD) * 2;

	/* We don't show the icons while resizing, since we'd have to
	   draw them on top of the resize rect. Nor when editing. */
	num_icons = 0;
	comp = event->comp;

	/* Set up Pango prerequisites */
	font_desc = gtk_widget_get_style (GTK_WIDGET (day_view))->font_desc;
	pango_context = gtk_widget_get_pango_context (GTK_WIDGET (day_view));
	layout = pango_layout_new (pango_context);

	if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE
	    && day_view->resize_event_day == E_DAY_VIEW_LONG_EVENT
	    && day_view->resize_event_num == event_num)
		show_icons = FALSE;

	if (day_view->editing_event_day == E_DAY_VIEW_LONG_EVENT
	    && day_view->editing_event_num == event_num) {
		show_icons = FALSE;
		use_max_width = TRUE;
	}

	if (show_icons) {
		GSList *categories_list, *elem;

		if (cal_component_has_alarms (comp))
			num_icons++;
		if (cal_component_has_recurrences (comp))
			num_icons++;
		if (event->different_timezone)
			num_icons++;
		if (cal_component_has_organizer (comp))
			num_icons++;

		cal_component_get_categories_list (comp, &categories_list);
		for (elem = categories_list; elem; elem = elem->next) {
			char *category;
			GdkPixmap *pixmap = NULL;
			GdkBitmap *mask = NULL;

			category = (char *) elem->data;
			if (e_categories_config_get_icon_for (category, &pixmap, &mask))
				num_icons++;
		}
		cal_component_free_categories_list (categories_list);
	}

	if (!event->canvas_item) {
		event->canvas_item =
			gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (day_view->top_canvas)->root),
					       e_text_get_type (),
					       "anchor", GTK_ANCHOR_NW,
					       "clip", TRUE,
					       "max_lines", 1,
					       "editable", TRUE,
					       "use_ellipsis", TRUE,
					       "draw_background", FALSE,
					       "fill_color_rgba", GNOME_CANVAS_COLOR(0, 0, 0),
					       "im_context", E_CANVAS (day_view->top_canvas)->im_context,
					       NULL);
		g_signal_connect (event->canvas_item, "event",
				  G_CALLBACK (e_day_view_on_text_item_event), day_view);
		e_day_view_update_long_event_label (day_view, event_num);
	}

	/* Calculate its position. We first calculate the ideal position which
	   is centered with the icons. We then make sure we haven't gone off
	   the left edge of the available space. Finally we make sure we don't
	   go off the right edge. */
	icons_width = (E_DAY_VIEW_ICON_WIDTH + E_DAY_VIEW_ICON_X_PAD)
		* num_icons + E_DAY_VIEW_LONG_EVENT_ICON_R_PAD;
	time_width = e_day_view_get_time_string_width (day_view);

	if (use_max_width) {
		text_x = item_x;
		text_w = item_w;
	} else {
		/* Get the requested size of the label. */
		g_object_get (G_OBJECT (event->canvas_item), "text", &text, NULL);
		text_width = 0;
		if (text) {
			end_of_line = strchr (text, '\n');
			if (end_of_line)
				line_len = end_of_line - text;
			else
				line_len = strlen (text);
			pango_layout_set_text (layout, text, line_len);
			pango_layout_get_pixel_size (layout, &text_width, NULL);
			g_free (text);
		}

		width = text_width + icons_width;
		text_x = item_x + (item_w - width) / 2;

		min_text_x = item_x;
		if (event->start > day_view->day_starts[start_day])
			min_text_x += time_width + E_DAY_VIEW_LONG_EVENT_TIME_X_PAD;

		text_x = MAX (text_x, min_text_x);

		max_text_w = item_x + item_w - text_x;
		if (event->end < day_view->day_starts[end_day + 1])
			max_text_w -= time_width + E_DAY_VIEW_LONG_EVENT_TIME_X_PAD;

		text_w = MIN (width, max_text_w);

		/* Now take out the space for the icons. */
		text_x += icons_width;
		text_w -= icons_width;
	}

	text_w = MAX (text_w, 0);
	gnome_canvas_item_set (event->canvas_item,
			       "clip_width", (gdouble) text_w,
			       "clip_height", (gdouble) item_h,
			       NULL);
	e_canvas_item_move_absolute(event->canvas_item,
				    text_x, item_y);

	g_object_unref (layout);
}


/* This creates or updates the sizes of the canvas items for one day of the
   main canvas. */
static void
e_day_view_reshape_day_events (EDayView *day_view,
			       gint	 day)
{
	gint event_num;

	for (event_num = 0; event_num < day_view->events[day]->len;
	     event_num++) {
		e_day_view_reshape_day_event (day_view, day, event_num);
	}
}


static void
e_day_view_reshape_day_event (EDayView *day_view,
			      gint	day,
			      gint	event_num)
{
	EDayViewEvent *event;
	gint item_x, item_y, item_w, item_h;
	gint num_icons, icons_offset;
	CalComponent *comp;

	event = &g_array_index (day_view->events[day], EDayViewEvent,
				event_num);
	comp = event->comp;

	if (!e_day_view_get_event_position (day_view, day, event_num,
					    &item_x, &item_y,
					    &item_w, &item_h)) {
		if (event->canvas_item) {
			gtk_object_destroy (GTK_OBJECT (event->canvas_item));
			event->canvas_item = NULL;
		}
	} else {
		/* Skip the border and padding. */
		item_x += E_DAY_VIEW_BAR_WIDTH + E_DAY_VIEW_EVENT_X_PAD;
		item_w -= E_DAY_VIEW_BAR_WIDTH + E_DAY_VIEW_EVENT_X_PAD * 2;
		item_y += E_DAY_VIEW_EVENT_BORDER_HEIGHT + E_DAY_VIEW_EVENT_Y_PAD;
		item_h -= (E_DAY_VIEW_EVENT_BORDER_HEIGHT + E_DAY_VIEW_EVENT_Y_PAD) * 2;

		/* We don't show the icons while resizing, since we'd have to
		   draw them on top of the resize rect. */
		num_icons = 0;
		if (day_view->resize_drag_pos == E_DAY_VIEW_POS_NONE
		    || day_view->resize_event_day != day
		    || day_view->resize_event_num != event_num) {
			GSList *categories_list, *elem;

			if (cal_component_has_alarms (comp))
				num_icons++;
			if (cal_component_has_recurrences (comp))
				num_icons++;
			if (event->different_timezone)
				num_icons++;
			if (cal_component_has_organizer (comp))
				num_icons++;

			cal_component_get_categories_list (comp, &categories_list);
			for (elem = categories_list; elem; elem = elem->next) {
				char *category;
				GdkPixmap *pixmap = NULL;
				GdkBitmap *mask = NULL;

				category = (char *) elem->data;
				if (e_categories_config_get_icon_for (category, &pixmap, &mask))
					num_icons++;
			}
			cal_component_free_categories_list (categories_list);
		}

		if (num_icons > 0) {
			if (item_h >= (E_DAY_VIEW_ICON_HEIGHT + E_DAY_VIEW_ICON_Y_PAD) * num_icons)
				icons_offset = E_DAY_VIEW_ICON_WIDTH + E_DAY_VIEW_ICON_X_PAD * 2;
			else
				icons_offset = (E_DAY_VIEW_ICON_WIDTH + E_DAY_VIEW_ICON_X_PAD) * num_icons + E_DAY_VIEW_ICON_X_PAD;
			item_x += icons_offset;
			item_w -= icons_offset;
		}

		if (!event->canvas_item) {
			event->canvas_item =
				gnome_canvas_item_new (GNOME_CANVAS_GROUP (GNOME_CANVAS (day_view->main_canvas)->root),
						       e_text_get_type (),
						       "anchor", GTK_ANCHOR_NW,
						       "line_wrap", TRUE,
						       "editable", TRUE,
						       "clip", TRUE,
						       "use_ellipsis", TRUE,
						       "draw_background", FALSE,
						       "fill_color_rgba", GNOME_CANVAS_COLOR(0, 0, 0),
						       "im_context", E_CANVAS (day_view->main_canvas)->im_context,
						       NULL);
			g_signal_connect (event->canvas_item, "event",
					  G_CALLBACK (e_day_view_on_text_item_event), day_view);
			e_day_view_update_event_label (day_view, day, event_num);
		}

		item_w = MAX (item_w, 0);
		gnome_canvas_item_set (event->canvas_item,
				       "clip_width", (gdouble) item_w,
				       "clip_height", (gdouble) item_h,
				       NULL);
		e_canvas_item_move_absolute(event->canvas_item,
					    item_x, item_y);
	}
}


/* This creates or resizes the horizontal bars used to resize events in the
   main canvas. */
static void
e_day_view_reshape_main_canvas_resize_bars (EDayView *day_view)
{
	gint day, event_num;
	gint item_x, item_y, item_w, item_h;
	gdouble x, y, w, h;

	day = day_view->resize_bars_event_day;
	event_num = day_view->resize_bars_event_num;

	/* If we're not editing an event, or the event is not shown,
	   hide the resize bars. */
	if (day != -1 && day == day_view->drag_event_day
	    && event_num == day_view->drag_event_num) {
		g_object_get (G_OBJECT (day_view->drag_rect_item),
			      "x1", &x,
			      "y1", &y,
			      "x2", &w,
			      "y2", &h,
			      NULL);
		w -= x;
		x++;
		h -= y;
	} else if (day != -1
		   && e_day_view_get_event_position (day_view, day, event_num,
						     &item_x, &item_y,
						     &item_w, &item_h)) {
		x = item_x + E_DAY_VIEW_BAR_WIDTH;
		y = item_y;
		w = item_w - E_DAY_VIEW_BAR_WIDTH;
		h = item_h;
	} else {
		gnome_canvas_item_hide (day_view->main_canvas_top_resize_bar_item);
		gnome_canvas_item_hide (day_view->main_canvas_bottom_resize_bar_item);
		return;
	}

	gnome_canvas_item_set (day_view->main_canvas_top_resize_bar_item,
			       "x1", x - E_DAY_VIEW_BAR_WIDTH,
			       "y1", y - E_DAY_VIEW_BAR_HEIGHT,
			       "x2", x + w - 1,
			       "y2", y - 1,
			       NULL);
	gnome_canvas_item_show (day_view->main_canvas_top_resize_bar_item);

	gnome_canvas_item_set (day_view->main_canvas_bottom_resize_bar_item,
			       "x1", x - E_DAY_VIEW_BAR_WIDTH,
			       "y1", y + h,
			       "x2", x + w - 1,
			       "y2", y + h + E_DAY_VIEW_BAR_HEIGHT - 1,
			       NULL);
	gnome_canvas_item_show (day_view->main_canvas_bottom_resize_bar_item);
}


static void
e_day_view_ensure_events_sorted (EDayView *day_view)
{
	gint day;

	/* Sort the long events. */
	if (!day_view->long_events_sorted) {
		qsort (day_view->long_events->data,
		       day_view->long_events->len,
		       sizeof (EDayViewEvent),
		       e_day_view_event_sort_func);
		day_view->long_events_sorted = TRUE;
	}

	/* Sort the events for each day. */
	for (day = 0; day < day_view->days_shown; day++) {
		if (!day_view->events_sorted[day]) {
			qsort (day_view->events[day]->data,
			       day_view->events[day]->len,
			       sizeof (EDayViewEvent),
			       e_day_view_event_sort_func);
			day_view->events_sorted[day] = TRUE;
		}
	}
}


gint
e_day_view_event_sort_func (const void *arg1,
			    const void *arg2)
{
	EDayViewEvent *event1, *event2;

	event1 = (EDayViewEvent*) arg1;
	event2 = (EDayViewEvent*) arg2;

	if (event1->start < event2->start)
		return -1;
	if (event1->start > event2->start)
		return 1;

	if (event1->end > event2->end)
		return -1;
	if (event1->end < event2->end)
		return 1;

	return 0;
}

static gboolean
e_day_view_do_key_press (GtkWidget *widget, GdkEventKey *event)
{
	EDayView *day_view;
	CalComponent *comp;
	gint day, event_num;
	gchar *initial_text;
	guint keyval;
	gboolean stop_emission;
	time_t dtstart, dtend;
	CalComponentDateTime start_dt, end_dt;
	struct icaltimetype start_tt, end_tt;
	const char *uid;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (E_IS_DAY_VIEW (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	day_view = E_DAY_VIEW (widget);
	keyval = event->keyval;

	if (!(day_view->client && cal_client_get_load_state (day_view->client) == CAL_CLIENT_LOAD_LOADED))
		return TRUE;
	
	/* The Escape key aborts a resize operation. */
	if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE) {
		if (keyval == GDK_Escape) {
			e_day_view_abort_resize (day_view, event->time);
		}
		return FALSE;
	}

	/* Handle the cursor keys for moving & extending the selection. */
	stop_emission = TRUE;
	if (event->state & GDK_SHIFT_MASK) {
		switch (keyval) {
		case GDK_Up:
			e_day_view_cursor_key_up_shifted (day_view, event);
			break;
		case GDK_Down:
			e_day_view_cursor_key_down_shifted (day_view, event);
			break;
		case GDK_Left:
			e_day_view_cursor_key_left_shifted (day_view, event);
			break;
		case GDK_Right:
			e_day_view_cursor_key_right_shifted (day_view, event);
			break;
		default:
			stop_emission = FALSE;
			break;
		}
	} else {
		switch (keyval) {
		case GDK_Up:
			e_day_view_cursor_key_up (day_view, event);
			break;
		case GDK_Down:
			e_day_view_cursor_key_down (day_view, event);
			break;
		case GDK_Left:
			e_day_view_cursor_key_left (day_view, event);
			break;
		case GDK_Right:
			e_day_view_cursor_key_right (day_view, event);
			break;
		case GDK_Page_Up:
			e_day_view_scroll (day_view, E_DAY_VIEW_PAGE_STEP);
			break;
		case GDK_Page_Down:
			e_day_view_scroll (day_view, -E_DAY_VIEW_PAGE_STEP);
			break;
		default:
			stop_emission = FALSE;
			break;
		}
	}
	if (stop_emission)
		return TRUE;

	if (day_view->selection_start_day == -1)
		return FALSE;

	/* Check if there is room for a new event to be typed in. If there
	   isn't we don't want to add an event as we will then add a new
	   event for every key press. */
	if (!e_day_view_check_if_new_event_fits (day_view)) {
		return FALSE;
	}

	/* We only want to start an edit with a return key or a simple
	   character. */
	if (keyval == GDK_Return) {
		initial_text = NULL;
	} else if (((keyval >= 0x20) && (keyval <= 0xFF)
		    && (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
		   || (event->length == 0)
		   || (keyval == GDK_Tab)) {
		return FALSE;
	} else
		initial_text = e_utf8_from_gtk_event_key (widget, event->keyval, event->string);

	/* Add a new event covering the selected range */

	comp = cal_comp_event_new_with_defaults (day_view->client);

	e_day_view_get_selected_time_range (day_view, &dtstart, &dtend);

	start_tt = icaltime_from_timet_with_zone (dtstart, FALSE,
						  day_view->zone);

	end_tt = icaltime_from_timet_with_zone (dtend, FALSE,
						day_view->zone);

	if (day_view->selection_in_top_canvas) {
		start_dt.tzid = NULL;
		start_tt.is_date = 1;
		end_tt.is_date = 1;
	} else {
		start_dt.tzid = icaltimezone_get_tzid (day_view->zone);
	}

	start_dt.value = &start_tt;
	end_dt.value = &end_tt;
	end_dt.tzid = start_dt.tzid;
	cal_component_set_dtstart (comp, &start_dt);
	cal_component_set_dtend (comp, &end_dt);

	cal_component_set_categories (comp, day_view->default_category);

	/* We add the event locally and start editing it. We don't send it
	   to the server until the user finishes editing it. */
	e_day_view_add_event (comp, dtstart, dtend, day_view);
	e_day_view_check_layout (day_view);
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);

	cal_component_get_uid (comp, &uid);
	if (e_day_view_find_event_from_uid (day_view, uid, &day, &event_num)) {
		e_day_view_start_editing_event (day_view, day, event_num,
						initial_text);
	} else {
		g_warning ("Couldn't find event to start editing.\n");
	}

	if (initial_text)
		g_free (initial_text);

	g_object_unref (comp);

	return TRUE;
}

static gboolean
e_day_view_key_press (GtkWidget *widget, GdkEventKey *event)
{
	gboolean handled = FALSE;
	handled = e_day_view_do_key_press (widget, event);

	/* if not handled, try key bindings */
	if (!handled)
		handled = GTK_WIDGET_CLASS (parent_class)->key_press_event (widget, event);
	return handled;
}

static void
e_day_view_cursor_key_up_shifted (EDayView *day_view, GdkEventKey *event)
{
	gint *row;

	if (day_view->selection_in_top_canvas)
		return;

	if (day_view->selection_drag_pos == E_DAY_VIEW_DRAG_START)
		row = &day_view->selection_start_row;
	else
		row = &day_view->selection_end_row;

	if (*row == 0)
		return;

	*row = *row - 1;

	e_day_view_ensure_rows_visible (day_view, *row, *row);

	e_day_view_normalize_selection (day_view);

	e_day_view_update_calendar_selection_time (day_view);

	/* FIXME: Optimise? */
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
}

static gboolean
e_day_view_focus (GtkWidget *widget, GtkDirectionType direction)
{
	EDayView *day_view;
	gint new_day;
	gint new_event_num;
	gint start_row, end_row;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (E_IS_DAY_VIEW (widget), FALSE);
	day_view = E_DAY_VIEW (widget);

	if (!e_day_view_get_next_tab_event (day_view, direction,
					    &new_day, &new_event_num))
		return FALSE;

	if (new_day != E_DAY_VIEW_LONG_EVENT && new_day != -1) {
		if (e_day_view_get_event_rows (day_view, new_day, new_event_num,
					       &start_row, &end_row))
			/* ajust the scrollbar to ensure the event to be seen */
			e_day_view_ensure_rows_visible (day_view,
							start_row, end_row);
	}
	e_day_view_start_editing_event (day_view, new_day,
					new_event_num, NULL);

	return TRUE;
}

static gboolean
e_day_view_get_extreme_event (EDayView *day_view, gint start_day,
			      gint end_day, gboolean first,
			      gint *day_out, gint *event_num_out)
{
	gint loop_day;

	g_return_val_if_fail (day_view != NULL, FALSE);
	g_return_val_if_fail (start_day >= 0, FALSE);
	g_return_val_if_fail (end_day <= E_DAY_VIEW_LONG_EVENT, FALSE);
	g_return_val_if_fail (day_out && event_num_out, FALSE);

	if (start_day > end_day)
		return FALSE;
	if (first) {
		for (loop_day = start_day; loop_day <= end_day; ++loop_day)
			if (day_view->events[loop_day]->len > 0) {
				*day_out = loop_day;
				*event_num_out = 0;
				return TRUE;
			}
	}
	else {
		for (loop_day = end_day; loop_day >= start_day; --loop_day)
			if (day_view->events[loop_day]->len > 0) {
				*day_out = loop_day;
				*event_num_out =
					day_view->events[loop_day]->len - 1;
				return TRUE;
			}
	}
	*day_out = -1;
	*event_num_out = -1;
	return FALSE;
}

static gboolean
e_day_view_get_extreme_long_event (EDayView *day_view, gboolean first,
				   gint *day_out, gint *event_num_out)
{
	g_return_val_if_fail (day_view != NULL, FALSE);
	g_return_val_if_fail (day_out && event_num_out, FALSE);

	if (first && (day_view->long_events->len > 0)) {
		*day_out = E_DAY_VIEW_LONG_EVENT;
		*event_num_out = 0;
		return TRUE;
	}
	if ((!first) && (day_view->long_events->len > 0)) {
		*day_out = E_DAY_VIEW_LONG_EVENT;
		*event_num_out = day_view->long_events->len - 1;
		return TRUE;
	}
	*day_out = -1;
	*event_num_out = -1;
	return FALSE;
}

static gboolean
e_day_view_get_next_tab_event (EDayView *day_view, GtkDirectionType direction,
			       gint *day_out, gint *event_num_out)
{
	gint new_day;
	gint new_event_num;
	gint days_shown;

	g_return_val_if_fail (day_view != NULL, FALSE);
	g_return_val_if_fail (day_out != NULL, FALSE);
	g_return_val_if_fail (event_num_out != NULL, FALSE);

	days_shown = e_day_view_get_days_shown(day_view);
	*day_out = -1;
	*event_num_out = -1;

	g_return_val_if_fail (days_shown > 0, FALSE);

	switch (direction) {
	case GTK_DIR_TAB_BACKWARD:
		new_event_num = day_view->editing_event_num - 1;
		break;
	case GTK_DIR_TAB_FORWARD:
		new_event_num = day_view->editing_event_num + 1;
		break;
	default:
		return FALSE;
	}

	new_day = day_view->editing_event_day;

	/* not current editing event, set to first long event if there is one
	 */
	if (new_day == -1) {
		if (e_day_view_get_extreme_long_event (day_view, TRUE,
						       day_out, event_num_out))
			return TRUE;

		/* no long event, set to first normal event if there is one
		 */
		return e_day_view_get_extreme_event (day_view, 0,
						     days_shown - 1, TRUE,
						     day_out, event_num_out);
	}
	/* go backward from the first long event */
	else if ((new_day == E_DAY_VIEW_LONG_EVENT) && (new_event_num < 0)) {
		if (e_day_view_get_extreme_event (day_view, 0,
						  days_shown - 1, FALSE,
						  day_out, event_num_out))
			return TRUE;
		return e_day_view_get_extreme_long_event (day_view, FALSE,
							  day_out,
							  event_num_out);
	}
	/* go forward from the last long event */
	else if ((new_day == E_DAY_VIEW_LONG_EVENT) &&
		 (new_event_num >= day_view->long_events->len)) {
		if (e_day_view_get_extreme_event (day_view, 0,
						  days_shown - 1, TRUE,
						  day_out, event_num_out))
			return TRUE;
		return e_day_view_get_extreme_long_event (day_view, TRUE,
							  day_out,
							  event_num_out);
	}

	/* go backward from the first event in current editting day */
	else if ((new_day < E_DAY_VIEW_LONG_EVENT) && (new_event_num < 0)) {
		/* try to find a event from the previous day in days shown
		 */
		if (e_day_view_get_extreme_event (day_view, 0,
						  new_day - 1, FALSE,
						  day_out, event_num_out))
			return TRUE;
		else if (e_day_view_get_extreme_long_event (day_view, FALSE,
							    day_out,
							    event_num_out))
			return TRUE;
		return e_day_view_get_extreme_event (day_view, new_day,
						     days_shown - 1, FALSE,
						     day_out, event_num_out);
	}
	/* go forward from the last event in current editting day */
	else if ((new_day < E_DAY_VIEW_LONG_EVENT) &&
		 (new_event_num >= day_view->events[new_day]->len)) {
		/* try to find a event from the next day in days shown
		 */
		if (e_day_view_get_extreme_event (day_view, (new_day + 1),
						  days_shown - 1, TRUE,
						  day_out, event_num_out))
			return TRUE;
		else if (e_day_view_get_extreme_long_event (day_view, TRUE,
							    day_out,
							    event_num_out))
			return TRUE;
		return e_day_view_get_extreme_event (day_view, 0,
						     new_day, TRUE,
						     day_out, event_num_out);
	}
	*day_out = new_day;
	*event_num_out = new_event_num;
	return TRUE;
}

static void
e_day_view_cursor_key_down_shifted (EDayView *day_view, GdkEventKey *event)
{
	gint *row;

	if (day_view->selection_in_top_canvas)
		return;

	if (day_view->selection_drag_pos == E_DAY_VIEW_DRAG_START)
		row = &day_view->selection_start_row;
	else
		row = &day_view->selection_end_row;

	if (*row >= day_view->rows - 1)
		return;

	*row = *row + 1;

	e_day_view_ensure_rows_visible (day_view, *row, *row);

	e_day_view_normalize_selection (day_view);

	e_day_view_update_calendar_selection_time (day_view);

	/* FIXME: Optimise? */
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
}


static void
e_day_view_cursor_key_left_shifted (EDayView *day_view, GdkEventKey *event)
{
	gint *day;

	if (day_view->selection_drag_pos == E_DAY_VIEW_DRAG_START)
		day = &day_view->selection_start_day;
	else
		day = &day_view->selection_end_day;

	if (*day == 0)
		return;

	*day = *day - 1;

	e_day_view_normalize_selection (day_view);

	e_day_view_update_calendar_selection_time (day_view);

	/* FIXME: Optimise? */
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
}


static void
e_day_view_cursor_key_right_shifted (EDayView *day_view, GdkEventKey *event)
{
	gint *day;

	if (day_view->selection_drag_pos == E_DAY_VIEW_DRAG_START)
		day = &day_view->selection_start_day;
	else
		day = &day_view->selection_end_day;

	if (*day >= day_view->days_shown - 1)
		return;

	*day = *day + 1;

	e_day_view_normalize_selection (day_view);

	e_day_view_update_calendar_selection_time (day_view);

	/* FIXME: Optimise? */
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
}


static void
e_day_view_cursor_key_up (EDayView *day_view, GdkEventKey *event)
{
	if (day_view->selection_start_day == -1) {
		day_view->selection_start_day = 0;
		day_view->selection_start_row = 0;
	}
	day_view->selection_end_day = day_view->selection_start_day;

	if (day_view->selection_in_top_canvas) {
		return;
	} else if (day_view->selection_start_row == 0) {
		day_view->selection_in_top_canvas = TRUE;
		day_view->selection_start_row = -1;
	} else {
		day_view->selection_start_row--;
	}
	day_view->selection_end_row = day_view->selection_start_row;

	if (!day_view->selection_in_top_canvas)
		e_day_view_ensure_rows_visible (day_view,
						day_view->selection_start_row,
						day_view->selection_end_row);

	e_day_view_update_calendar_selection_time (day_view);

	/* FIXME: Optimise? */
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
}


static void
e_day_view_cursor_key_down (EDayView *day_view, GdkEventKey *event)
{
	if (day_view->selection_start_day == -1) {
		day_view->selection_start_day = 0;
		day_view->selection_start_row = 0;
	}
	day_view->selection_end_day = day_view->selection_start_day;

	if (day_view->selection_in_top_canvas) {
		day_view->selection_in_top_canvas = FALSE;
		day_view->selection_start_row = 0;
	} else if (day_view->selection_start_row >= day_view->rows - 1) {
		return;
	} else {
		day_view->selection_start_row++;
	}
	day_view->selection_end_row = day_view->selection_start_row;

	if (!day_view->selection_in_top_canvas)
		e_day_view_ensure_rows_visible (day_view,
						day_view->selection_start_row,
						day_view->selection_end_row);

	e_day_view_update_calendar_selection_time (day_view);

	/* FIXME: Optimise? */
	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
}


static void
e_day_view_cursor_key_left (EDayView *day_view, GdkEventKey *event)
{
	if (day_view->selection_start_day == 0) {
		if (day_view->calendar)
			gnome_calendar_previous (day_view->calendar);
	} else {
		day_view->selection_start_day--;
		day_view->selection_end_day--;

		e_day_view_update_calendar_selection_time (day_view);

		/* FIXME: Optimise? */
		gtk_widget_queue_draw (day_view->top_canvas);
		gtk_widget_queue_draw (day_view->main_canvas);
	}
}


static void
e_day_view_cursor_key_right (EDayView *day_view, GdkEventKey *event)
{
	if (day_view->selection_end_day == day_view->days_shown - 1) {
		if (day_view->calendar)
			gnome_calendar_next (day_view->calendar);
	} else {
		day_view->selection_start_day++;
		day_view->selection_end_day++;

		e_day_view_update_calendar_selection_time (day_view);

		/* FIXME: Optimise? */
		gtk_widget_queue_draw (day_view->top_canvas);
		gtk_widget_queue_draw (day_view->main_canvas);
	}
}


/* Scrolls the main canvas up or down. The pages_to_scroll argument
   is multiplied with the adjustment's page size and added to the adjustment's
   value, while ensuring we stay within the bounds. A positive value will
   scroll the canvas down and a negative value will scroll it up. */
static void
e_day_view_scroll	(EDayView	*day_view,
			 gfloat		 pages_to_scroll)
{
	GtkAdjustment *adj = GTK_LAYOUT (day_view->main_canvas)->vadjustment;
	gfloat new_value;

	new_value = adj->value - adj->page_size * pages_to_scroll;
	new_value = CLAMP (new_value, adj->lower, adj->upper - adj->page_size);
	gtk_adjustment_set_value (adj, new_value);
}


static gboolean
e_day_view_check_if_new_event_fits (EDayView *day_view)
{
	gint day, start_row, end_row, row;

	day = day_view->selection_start_day;
	start_row = day_view->selection_start_row;
	end_row = day_view->selection_end_row;

	/* Long events always fit, since we keep adding rows to the top
	   canvas. */
	if (day != day_view->selection_end_day)
		return TRUE;
	if (start_row == 0 && end_row == day_view->rows)
		return TRUE;

	/* If any of the rows already have E_DAY_VIEW_MAX_COLUMNS columns,
	   return FALSE. */
	for (row = start_row; row <= end_row; row++) {
		if (day_view->cols_per_row[day][row] >= E_DAY_VIEW_MAX_COLUMNS)
			return FALSE;
	}

	return TRUE;
}


static void
e_day_view_ensure_rows_visible (EDayView *day_view,
				gint start_row,
				gint end_row)
{
	GtkAdjustment *adj;
	gfloat value, min_value, max_value;

	adj = GTK_LAYOUT (day_view->main_canvas)->vadjustment;

	value = adj->value;

	min_value = (end_row + 1) * day_view->row_height - adj->page_size;
	if (value < min_value)
		value = min_value;

	max_value = start_row * day_view->row_height;
	if (value > max_value)
		value = max_value;

	if (value != adj->value) {
		adj->value = value;
		gtk_adjustment_value_changed (adj);
	}
}


static void
e_day_view_start_editing_event (EDayView *day_view,
				gint	  day,
				gint	  event_num,
				gchar    *initial_text)
{
	EDayViewEvent *event;
	ETextEventProcessor *event_processor = NULL;
	ETextEventProcessorCommand command;

#if 0
	g_print ("In e_day_view_start_editing_event\n");
#endif

	/* If we are already editing the event, just return. */
	if (day == day_view->editing_event_day
	    && event_num == day_view->editing_event_num)
		return;

	if (day == E_DAY_VIEW_LONG_EVENT) {
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);
	} else {
		event = &g_array_index (day_view->events[day], EDayViewEvent,
					event_num);
	}

	/* If the event is not shown, don't try to edit it. */
	if (!event->canvas_item)
		return;

	/* We must grab the focus before setting the initial text, since
	   grabbing the focus will result in a call to
	   e_day_view_on_editing_started(), which will reset the text to get
	   rid of the start and end times. */
	e_canvas_item_grab_focus (event->canvas_item, TRUE);

	if (initial_text) {
		gnome_canvas_item_set (event->canvas_item,
				       "text", initial_text,
				       NULL);
	}

	/* Try to move the cursor to the end of the text. */
	g_object_get (G_OBJECT (event->canvas_item),
		      "event_processor", &event_processor,
		      NULL);
	if (event_processor) {
		command.action = E_TEP_MOVE;
		command.position = E_TEP_END_OF_BUFFER;
		g_signal_emit_by_name (event_processor,
				       "command", &command);
	}
}


/* This stops the current edit. If accept is TRUE the event summary is updated,
   else the edit is cancelled. */
static void
e_day_view_stop_editing_event (EDayView *day_view)
{
	GtkWidget *toplevel;

	/* Check we are editing an event. */
	if (day_view->editing_event_day == -1)
		return;

	/* Set focus to the toplevel so the item loses focus. */
	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (day_view));
	if (toplevel && GTK_IS_WINDOW (toplevel))
		gtk_window_set_focus (GTK_WINDOW (toplevel), NULL);
}


/* Cancels the current edition by resetting the appointment's text to its original value */
static void
cancel_editing (EDayView *day_view)
{
	int day, event_num;
	EDayViewEvent *event;
	CalComponentText summary;

	day = day_view->editing_event_day;
	event_num = day_view->editing_event_num;

	g_assert (day != -1);

	if (day == E_DAY_VIEW_LONG_EVENT)
		event = &g_array_index (day_view->long_events, EDayViewEvent, event_num);
	else
		event = &g_array_index (day_view->events[day], EDayViewEvent, event_num);

	/* Reset the text to what was in the component */

	cal_component_get_summary (event->comp, &summary);
	g_object_set (G_OBJECT (event->canvas_item),
		      "text", summary.value ? summary.value : "",
		      NULL);

	/* Stop editing */
	e_day_view_stop_editing_event (day_view);
}


static gboolean
e_day_view_on_text_item_event (GnomeCanvasItem *item,
			       GdkEvent *event,
			       EDayView *day_view)
{
	switch (event->type) {
	case GDK_KEY_PRESS:
		if (event && event->key.keyval == GDK_Return) {
			/* We set the keyboard focus to the EDayView, so the
			   EText item loses it and stops the edit. */
			gtk_widget_grab_focus (GTK_WIDGET (day_view));

			/* Stop the signal last or we will also stop any
			   other events getting to the EText item. */
			gtk_signal_emit_stop_by_name (GTK_OBJECT (item),
						      "event");
			return TRUE;
		} else if (event->key.keyval == GDK_Escape) {
			cancel_editing (day_view);
			gtk_signal_emit_stop_by_name (GTK_OBJECT (item), "event");
			/* focus should go to day view when stop editing */
			gtk_widget_grab_focus (GTK_WIDGET (day_view));
			return TRUE;
		}
		break;
	case GDK_2BUTTON_PRESS:
#if 0
		g_print ("Item got double-click\n");
#endif
		break;

	case GDK_BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
		/* Only let the EText handle the event while editing. */
		if (!E_TEXT (item)->editing)
			gtk_signal_emit_stop_by_name (GTK_OBJECT (item),
						      "event");
		break;
	case GDK_FOCUS_CHANGE:
		if (event->focus_change.in)
			e_day_view_on_editing_started (day_view, item);
		else
			e_day_view_on_editing_stopped (day_view, item);

		return FALSE;
	default:
		break;
	}

	return FALSE;
}


static void
e_day_view_on_editing_started (EDayView *day_view,
			       GnomeCanvasItem *item)
{
	gint day, event_num;

	if (!e_day_view_find_event_from_item (day_view, item,
					      &day, &event_num))
		return;

#if 0
	g_print ("In e_day_view_on_editing_started Day:%i Event:%i\n",
		 day, event_num);
#endif

	/* FIXME: This is a temporary workaround for a bug which seems to stop
	   us getting focus_out signals. It is not a complete fix since if we
	   don't get focus_out signals we don't save the appointment text so
	   this may be lost. */
	if (day_view->editing_event_day == day
	    && day_view->editing_event_num == event_num)
		return;

	day_view->editing_event_day = day;
	day_view->editing_event_num = event_num;

	if (day == E_DAY_VIEW_LONG_EVENT) {
		e_day_view_reshape_long_event (day_view, event_num);
	} else {
		day_view->resize_bars_event_day = day;
		day_view->resize_bars_event_num = event_num;
		e_day_view_update_event_label (day_view, day, event_num);
		e_day_view_reshape_main_canvas_resize_bars (day_view);
	}

	g_object_set (item, "handle_popup", TRUE, NULL);

	gtk_signal_emit (GTK_OBJECT (day_view),
			 e_day_view_signals[SELECTION_CHANGED]);
}

static void
e_day_view_on_editing_stopped (EDayView *day_view,
			       GnomeCanvasItem *item)
{
	gint day, event_num;
	gboolean editing_long_event = FALSE;
	EDayViewEvent *event;
	gchar *text = NULL;
	CalComponentText summary;

	/* Note: the item we are passed here isn't reliable, so we just stop
	   the edit of whatever item was being edited. We also receive this
	   event twice for some reason. */
	day = day_view->editing_event_day;
	event_num = day_view->editing_event_num;

#if 0
	g_print ("In e_day_view_on_editing_stopped Day:%i Event:%i\n",
		 day, event_num);
#endif

	/* If no item is being edited, just return. */
	if (day == -1)
		return;

	if (day == E_DAY_VIEW_LONG_EVENT) {
		editing_long_event = TRUE;
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);
	} else {
		event = &g_array_index (day_view->events[day], EDayViewEvent,
					event_num);

		/* Hide the horizontal bars. */
		gnome_canvas_item_hide (day_view->main_canvas_top_resize_bar_item);
		gnome_canvas_item_hide (day_view->main_canvas_bottom_resize_bar_item);
	}

	/* Reset the edit fields. */
	day_view->editing_event_day = -1;
	day_view->editing_event_num = -1;

	day_view->resize_bars_event_day = -1;
	day_view->resize_bars_event_num = -1;

	g_object_set (event->canvas_item, "handle_popup", FALSE, NULL);
	g_object_get (G_OBJECT (event->canvas_item),
		      "text", &text,
		      NULL);
	g_assert (text != NULL);

	if (string_is_empty (text) && !cal_comp_is_on_server (event->comp, day_view->client)) {
		const char *uid;
		
		cal_component_get_uid (event->comp, &uid);
		
		e_day_view_foreach_event_with_uid (day_view, uid,
						   e_day_view_remove_event_cb, NULL);
		e_day_view_check_layout (day_view);
		gtk_widget_queue_draw (day_view->top_canvas);
		gtk_widget_queue_draw (day_view->main_canvas);
		goto out;
	}

	/* Only update the summary if necessary. */
	cal_component_get_summary (event->comp, &summary);
	if (summary.value && !strcmp (text, summary.value)) {
		if (day == E_DAY_VIEW_LONG_EVENT)
			e_day_view_reshape_long_event (day_view, event_num);
		else
			e_day_view_update_event_label (day_view, day,
						       event_num);
	} else if (summary.value || !string_is_empty (text)) {
		summary.value = text;
		summary.altrep = NULL;
		cal_component_set_summary (event->comp, &summary);

		if (cal_component_is_instance (event->comp)) {
			CalObjModType mod;
			
			if (recur_component_dialog (event->comp, &mod, NULL)) {
				if (cal_client_update_object_with_mod (day_view->client, event->comp, mod) == CAL_CLIENT_RESULT_SUCCESS) {
					if (itip_organizer_is_user (event->comp, day_view->client) 
					    && send_component_dialog (gtk_widget_get_toplevel (day_view),
								      day_view->client, event->comp, FALSE))
						itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, event->comp, 
								day_view->client, NULL);
				} else {
					g_message ("e_day_view_on_editing_stopped(): Could not update the object!");
				}
			}
		} else if (cal_client_update_object (day_view->client, event->comp) == CAL_CLIENT_RESULT_SUCCESS) {
			if (itip_organizer_is_user (event->comp, day_view->client) &&
			    send_component_dialog (gtk_widget_get_toplevel (day_view),
						   day_view->client, event->comp, FALSE))
				itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, event->comp,
						day_view->client, NULL);
		} else {
			g_message ("e_day_view_on_editing_stopped(): Could not update the object!");
		}
	}

 out:

	g_free (text);

	gtk_signal_emit (GTK_OBJECT (day_view),
			 e_day_view_signals[SELECTION_CHANGED]);
}


/* FIXME: It is possible that we may produce an invalid time due to daylight
   saving times (i.e. when clocks go forward there is a range of time which
   is not valid). I don't know the best way to handle daylight saving time. */
static time_t
e_day_view_convert_grid_position_to_time (EDayView *day_view,
					  gint col,
					  gint row)
{
	struct icaltimetype tt;
	time_t val;
	gint minutes;

	/* Calulate the number of minutes since the start of the day. */
	minutes = day_view->first_hour_shown * 60
		+ day_view->first_minute_shown
		+ row * day_view->mins_per_row;

	/* A special case for midnight, where we can use the start of the
	   next day. */
	if (minutes == 60 * 24)
		return day_view->day_starts[col + 1];

	/* Create an icaltimetype and convert to a time_t. */
	tt = icaltime_from_timet_with_zone (day_view->day_starts[col],
					    FALSE, day_view->zone);
	tt.hour = minutes / 60;
	tt.minute = minutes % 60;
	tt.second = 0;

	val = icaltime_as_timet_with_zone (tt, day_view->zone);
	return val;
}


static gboolean
e_day_view_convert_time_to_grid_position (EDayView *day_view,
					  time_t time,
					  gint *col,
					  gint *row)
{
	struct icaltimetype tt;
	gint day, minutes;

	*col = *row = 0;

	if (time < day_view->lower || time >= day_view->upper)
		return FALSE;

	/* We can find the column easily using the day_starts array. */
	for (day = 1; day <= day_view->days_shown; day++) {
		if (time < day_view->day_starts[day]) {
			*col = day - 1;
			break;
		}
	}

	/* To find the row we need to convert the time to an icaltimetype,
	   calculate the offset in minutes from the top of the display and
	   divide it by the mins per row setting. */
	tt = icaltime_from_timet_with_zone (time, FALSE, day_view->zone);

	minutes = tt.hour * 60 + tt.minute;
	minutes -= day_view->first_hour_shown * 60 + day_view->first_minute_shown;

	*row = minutes / day_view->mins_per_row;

	if (*row < 0 || *row >= day_view->rows)
		return FALSE;

	return TRUE;
}


/* This starts or stops auto-scrolling when dragging a selection or resizing
   an event. */
void
e_day_view_check_auto_scroll (EDayView *day_view,
			      gint event_x,
			      gint event_y)
{
	gint scroll_x, scroll_y;

	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (day_view->main_canvas),
					 &scroll_x, &scroll_y);

	event_x -= scroll_x;
	event_y -= scroll_y;

	day_view->last_mouse_x = event_x;
	day_view->last_mouse_y = event_y;

	if (event_y < E_DAY_VIEW_AUTO_SCROLL_OFFSET)
		e_day_view_start_auto_scroll (day_view, TRUE);
	else if (event_y >= day_view->main_canvas->allocation.height
		 - E_DAY_VIEW_AUTO_SCROLL_OFFSET)
		e_day_view_start_auto_scroll (day_view, FALSE);
	else
		e_day_view_stop_auto_scroll (day_view);
}


static void
e_day_view_start_auto_scroll (EDayView *day_view,
			      gboolean scroll_up)
{
	if (day_view->auto_scroll_timeout_id == 0) {
		day_view->auto_scroll_timeout_id = g_timeout_add (E_DAY_VIEW_AUTO_SCROLL_TIMEOUT, e_day_view_auto_scroll_handler, day_view);
		day_view->auto_scroll_delay = E_DAY_VIEW_AUTO_SCROLL_DELAY;
	}
	day_view->auto_scroll_up = scroll_up;
}


void
e_day_view_stop_auto_scroll (EDayView *day_view)
{
	if (day_view->auto_scroll_timeout_id != 0) {
		gtk_timeout_remove (day_view->auto_scroll_timeout_id);
		day_view->auto_scroll_timeout_id = 0;
	}
}


static gboolean
e_day_view_auto_scroll_handler (gpointer data)
{
	EDayView *day_view;
	EDayViewPosition pos;
	gint scroll_x, scroll_y, new_scroll_y, canvas_x, canvas_y, row, day;
	GtkAdjustment *adj;

	g_return_val_if_fail (E_IS_DAY_VIEW (data), FALSE);

	day_view = E_DAY_VIEW (data);

	GDK_THREADS_ENTER ();

	if (day_view->auto_scroll_delay > 0) {
		day_view->auto_scroll_delay--;
		GDK_THREADS_LEAVE ();
		return TRUE;
	}

	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (day_view->main_canvas),
					 &scroll_x, &scroll_y);

	adj = GTK_LAYOUT (day_view->main_canvas)->vadjustment;

	if (day_view->auto_scroll_up)
		new_scroll_y = MAX (scroll_y - adj->step_increment, 0);
	else
		new_scroll_y = MIN (scroll_y + adj->step_increment,
				    adj->upper - adj->page_size);

	if (new_scroll_y != scroll_y) {
		/* NOTE: This reduces flicker, but only works if we don't use
		   canvas items which have X windows. */

		/* FIXME: Since GNOME 2.0 we can't do this, since the canvas
		 * won't update when its's thawed. Is this a bug or should we
		 * really be doing something else? Investigate. */
#if 0
		gtk_layout_freeze (GTK_LAYOUT (day_view->main_canvas));
#endif

		gnome_canvas_scroll_to (GNOME_CANVAS (day_view->main_canvas),
					scroll_x, new_scroll_y);
#if 0
		gtk_layout_thaw (GTK_LAYOUT (day_view->main_canvas));
#endif
	}

	canvas_x = day_view->last_mouse_x + scroll_x;
	canvas_y = day_view->last_mouse_y + new_scroll_y;

	/* The last_mouse_x position is set to -1 when we are selecting using
	   the time column. In this case we set canvas_x to 0 and we ignore
	   the resulting day. */
	if (day_view->last_mouse_x == -1)
		canvas_x = 0;

	/* Update the selection/resize/drag if necessary. */
	pos = e_day_view_convert_position_in_main_canvas (day_view,
							  canvas_x, canvas_y,
							  &day, &row, NULL);

	if (day_view->last_mouse_x == -1)
		day = -1;

	if (pos != E_DAY_VIEW_POS_OUTSIDE) {
		if (day_view->selection_is_being_dragged) {
			e_day_view_update_selection (day_view, day, row);
		} else if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE) {
			e_day_view_update_resize (day_view, row);
		} else if (day_view->drag_item->object.flags
			   & GNOME_CANVAS_ITEM_VISIBLE) {
			e_day_view_update_main_canvas_drag (day_view, row,
							    day);
		}
	}

	GDK_THREADS_LEAVE ();
	return TRUE;
}

gboolean
e_day_view_get_event_rows (EDayView *day_view,
			   gint day,
			   gint event_num,
			   gint *start_row_out,
			   gint *end_row_out)
{
	gint start_row, end_row;
	EDayViewEvent *event;

	g_return_val_if_fail (day >= 0, FALSE);
	g_return_val_if_fail (day < E_DAY_VIEW_LONG_EVENT, FALSE);
	g_return_val_if_fail (event_num >= 0, FALSE);

	event = &g_array_index (day_view->events[day], EDayViewEvent,
				event_num);
	start_row = event->start_minute / day_view->mins_per_row;
	end_row = (event->end_minute - 1) / day_view->mins_per_row;
	if (end_row < start_row)
		end_row = start_row;

	*start_row_out = start_row;
	*end_row_out = end_row;
	return TRUE;
}

gboolean
e_day_view_get_event_position (EDayView *day_view,
			       gint day,
			       gint event_num,
			       gint *item_x,
			       gint *item_y,
			       gint *item_w,
			       gint *item_h)
{
	EDayViewEvent *event;
	gint start_row, end_row, cols_in_row, start_col, num_columns;

	event = &g_array_index (day_view->events[day], EDayViewEvent,
				event_num);

	/* If the event is flagged as not displayed, return FALSE. */
	if (event->num_columns == 0)
		return FALSE;

	e_day_view_get_event_rows(day_view, day, event_num, &start_row, &end_row);

	cols_in_row = day_view->cols_per_row[day][start_row];
	start_col = event->start_row_or_col;
	num_columns = event->num_columns;

	if (cols_in_row == 0)
		return FALSE;

	/* If the event is being resize, use the resize position. */
	if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE
	    && day_view->resize_event_day == day
	    && day_view->resize_event_num == event_num) {
		if (day_view->resize_drag_pos == E_DAY_VIEW_POS_TOP_EDGE)
			start_row = day_view->resize_start_row;
		else if (day_view->resize_drag_pos == E_DAY_VIEW_POS_BOTTOM_EDGE)
			end_row = day_view->resize_end_row;
	}


	*item_x = day_view->day_offsets[day]
		+ day_view->day_widths[day] * start_col / cols_in_row;
	*item_w = day_view->day_widths[day] * num_columns / cols_in_row
		- E_DAY_VIEW_GAP_WIDTH;
	*item_w = MAX (*item_w, 0);
	*item_y = start_row * day_view->row_height;
#if 0
	*item_h = (end_row - start_row + 1) * day_view->row_height;
#else
	/* This makes the event end on the grid line of the next row,
	   which maybe looks nicer if you have 2 events on consecutive rows. */
	*item_h = (end_row - start_row + 1) * day_view->row_height + 1;
#endif
	return TRUE;
}


gboolean
e_day_view_get_long_event_position	(EDayView	*day_view,
					 gint		 event_num,
					 gint		*start_day,
					 gint		*end_day,
					 gint		*item_x,
					 gint		*item_y,
					 gint		*item_w,
					 gint		*item_h)
{
	EDayViewEvent *event;

	event = &g_array_index (day_view->long_events, EDayViewEvent,
				event_num);

	/* If the event is flagged as not displayed, return FALSE. */
	if (event->num_columns == 0)
		return FALSE;

	if (!e_day_view_find_long_event_days (event,
					      day_view->days_shown,
					      day_view->day_starts,
					      start_day, end_day))
		return FALSE;

	/* If the event is being resize, use the resize position. */
	if (day_view->resize_drag_pos != E_DAY_VIEW_POS_NONE
	    && day_view->resize_event_day == E_DAY_VIEW_LONG_EVENT
	    && day_view->resize_event_num == event_num) {
		if (day_view->resize_drag_pos == E_DAY_VIEW_POS_LEFT_EDGE)
			*start_day = day_view->resize_start_row;
		else if (day_view->resize_drag_pos == E_DAY_VIEW_POS_RIGHT_EDGE)
			*end_day = day_view->resize_end_row;
	}

	*item_x = day_view->day_offsets[*start_day] + E_DAY_VIEW_BAR_WIDTH;
	*item_w = day_view->day_offsets[*end_day + 1] - *item_x
		- E_DAY_VIEW_GAP_WIDTH;
	*item_w = MAX (*item_w, 0);
	*item_y = (event->start_row_or_col + 1) * day_view->top_row_height;
	*item_h = day_view->top_row_height - E_DAY_VIEW_TOP_CANVAS_Y_GAP;
	return TRUE;
}


/* Converts a position within the entire top canvas to a day & event and
   a place within the event if appropriate. If event_num_return is NULL, it
   simply returns the grid position without trying to find the event. */
static EDayViewPosition
e_day_view_convert_position_in_top_canvas (EDayView *day_view,
					   gint x,
					   gint y,
					   gint *day_return,
					   gint *event_num_return)
{
	EDayViewEvent *event;
	gint day, row, col;
	gint event_num, start_day, end_day, item_x, item_y, item_w, item_h;

	*day_return = -1;
	if (event_num_return)
		*event_num_return = -1;

	if (x < 0 || y < 0)
		return E_DAY_VIEW_POS_OUTSIDE;

	row = y / day_view->top_row_height - 1;

	day = -1;
	for (col = 1; col <= day_view->days_shown; col++) {
		if (x < day_view->day_offsets[col]) {
			day = col - 1;
			break;
		}
	}
	if (day == -1)
		return E_DAY_VIEW_POS_OUTSIDE;

	*day_return = day;

	/* If only the grid position is wanted, return. */
	if (event_num_return == NULL)
		return E_DAY_VIEW_POS_NONE;

	for (event_num = 0; event_num < day_view->long_events->len;
	     event_num++) {
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);

		if (event->start_row_or_col != row)
			continue;

		if (!e_day_view_get_long_event_position (day_view, event_num,
							 &start_day, &end_day,
							 &item_x, &item_y,
							 &item_w, &item_h))
			continue;

		if (x < item_x)
			continue;

		if (x >= item_x + item_w)
			continue;

		*event_num_return = event_num;

		if (x < item_x + E_DAY_VIEW_LONG_EVENT_BORDER_WIDTH
		    + E_DAY_VIEW_LONG_EVENT_X_PAD)
			return E_DAY_VIEW_POS_LEFT_EDGE;

		if (x >= item_x + item_w - E_DAY_VIEW_LONG_EVENT_BORDER_WIDTH
		    - E_DAY_VIEW_LONG_EVENT_X_PAD)
			return E_DAY_VIEW_POS_RIGHT_EDGE;

		return E_DAY_VIEW_POS_EVENT;
	}

	return E_DAY_VIEW_POS_NONE;
}


/* Converts a position within the entire main canvas to a day, row, event and
   a place within the event if appropriate. If event_num_return is NULL, it
   simply returns the grid position without trying to find the event. */
static EDayViewPosition
e_day_view_convert_position_in_main_canvas (EDayView *day_view,
					    gint x,
					    gint y,
					    gint *day_return,
					    gint *row_return,
					    gint *event_num_return)
{
	gint day, row, col, event_num;
	gint item_x, item_y, item_w, item_h;

#if 0
	g_print ("e_day_view_convert_position_in_main_canvas: (%d, %d)\n", x, y);
#endif

	*day_return = -1;
	*row_return = -1;
	if (event_num_return)
		*event_num_return = -1;

	/* Check the position is inside the canvas, and determine the day
	   and row. */
	if (x < 0 || y < 0)
		return E_DAY_VIEW_POS_OUTSIDE;

	row = y / day_view->row_height;
	if (row >= day_view->rows)
		return E_DAY_VIEW_POS_OUTSIDE;

	day = -1;
	for (col = 1; col <= day_view->days_shown; col++) {
		if (x < day_view->day_offsets[col]) {
			day = col - 1;
			break;
		}
	}
	if (day == -1)
		return E_DAY_VIEW_POS_OUTSIDE;

	*day_return = day;
	*row_return = row;

	/* If only the grid position is wanted, return. */
	if (event_num_return == NULL)
		return E_DAY_VIEW_POS_NONE;

	/* Check the selected item first, since the horizontal resizing bars
	   may be above other events. */
	if (day_view->resize_bars_event_day == day) {
		if (e_day_view_get_event_position (day_view, day,
						   day_view->resize_bars_event_num,
						   &item_x, &item_y,
						   &item_w, &item_h)) {
			if (x >= item_x && x < item_x + item_w) {
				*event_num_return = day_view->resize_bars_event_num;
				if (y >= item_y - E_DAY_VIEW_BAR_HEIGHT
				    && y < item_y + E_DAY_VIEW_EVENT_BORDER_HEIGHT)
					return E_DAY_VIEW_POS_TOP_EDGE;
				if (y >= item_y + item_h - E_DAY_VIEW_EVENT_BORDER_HEIGHT
				    && y < item_y + item_h + E_DAY_VIEW_BAR_HEIGHT)
					return E_DAY_VIEW_POS_BOTTOM_EDGE;
			}
		}
	}

	/* Try to find the event at the found position. */
	*event_num_return = -1;
	for (event_num = 0; event_num < day_view->events[day]->len;
	     event_num++) {
		if (!e_day_view_get_event_position (day_view, day, event_num,
						    &item_x, &item_y,
						    &item_w, &item_h))
			continue;

		if (x < item_x || x >= item_x + item_w
		    || y < item_y || y >= item_y + item_h)
			continue;

		*event_num_return = event_num;

		if (x < item_x + E_DAY_VIEW_BAR_WIDTH)
			return E_DAY_VIEW_POS_LEFT_EDGE;

		if (y < item_y + E_DAY_VIEW_EVENT_BORDER_HEIGHT
		    + E_DAY_VIEW_EVENT_Y_PAD)
			return E_DAY_VIEW_POS_TOP_EDGE;

		if (y >= item_y + item_h - E_DAY_VIEW_EVENT_BORDER_HEIGHT
		    - E_DAY_VIEW_EVENT_Y_PAD)
			return E_DAY_VIEW_POS_BOTTOM_EDGE;

		return E_DAY_VIEW_POS_EVENT;
	}

	return E_DAY_VIEW_POS_NONE;
}


static gboolean
e_day_view_on_top_canvas_drag_motion (GtkWidget      *widget,
				      GdkDragContext *context,
				      gint            x,
				      gint            y,
				      guint           time,
				      EDayView	     *day_view)
{
	gint scroll_x, scroll_y;

	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (widget),
					 &scroll_x, &scroll_y);
	day_view->drag_event_x = x + scroll_x;
	day_view->drag_event_y = y + scroll_y;

	e_day_view_reshape_top_canvas_drag_item (day_view);

	return TRUE;
}


static void
e_day_view_reshape_top_canvas_drag_item (EDayView *day_view)
{
	EDayViewPosition pos;
	gint x, y, day;

	/* Calculate the day & start row of the event being dragged, using
	   the current mouse position. */
	x = day_view->drag_event_x;
	y = day_view->drag_event_y;
	pos = e_day_view_convert_position_in_top_canvas (day_view, x, y,
							 &day, NULL);
	/* This shouldn't really happen in a drag. */
	if (pos == E_DAY_VIEW_POS_OUTSIDE)
		return;

	if (day_view->drag_event_day == E_DAY_VIEW_LONG_EVENT)
		day -= day_view->drag_event_offset;
	day = MAX (day, 0);

	e_day_view_update_top_canvas_drag (day_view, day);
}


static void
e_day_view_update_top_canvas_drag (EDayView *day_view,
				   gint day)
{
	EDayViewEvent *event = NULL;
	gint row, num_days, start_day, end_day;
	gdouble item_x, item_y, item_w, item_h;
	gchar *text;

	/* Calculate the event's position. If the event is in the same
	   position we started in, we use the same columns. */
	row = day_view->rows_in_top_display + 1;
	num_days = 1;

	if (day_view->drag_event_day == E_DAY_VIEW_LONG_EVENT) {
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					day_view->drag_event_num);
		row = event->start_row_or_col + 1;

		if (!e_day_view_find_long_event_days (event,
						      day_view->days_shown,
						      day_view->day_starts,
						      &start_day, &end_day))
			return;

		num_days = end_day - start_day + 1;

		/* Make sure we don't go off the screen. */
		day = MIN (day, day_view->days_shown - num_days);

	} else if (day_view->drag_event_day != -1) {
		event = &g_array_index (day_view->events[day_view->drag_event_day],
					EDayViewEvent,
					day_view->drag_event_num);
	}

	/* If the position hasn't changed, just return. */
	if (day_view->drag_last_day == day
	    && (day_view->drag_long_event_item->object.flags
		& GNOME_CANVAS_ITEM_VISIBLE))
		return;

	day_view->drag_last_day = day;


	item_x = day_view->day_offsets[day] + E_DAY_VIEW_BAR_WIDTH;
	item_w = day_view->day_offsets[day + num_days] - item_x
		- E_DAY_VIEW_GAP_WIDTH;
	item_y = row * day_view->top_row_height;
	item_h = day_view->top_row_height - E_DAY_VIEW_TOP_CANVAS_Y_GAP;


	/* Set the positions of the event & associated items. */
	gnome_canvas_item_set (day_view->drag_long_event_rect_item,
			       "x1", item_x,
			       "y1", item_y,
			       "x2", item_x + item_w - 1,
			       "y2", item_y + item_h - 1,
			       NULL);

	gnome_canvas_item_set (day_view->drag_long_event_item,
			       "clip_width", item_w - (E_DAY_VIEW_LONG_EVENT_BORDER_WIDTH + E_DAY_VIEW_LONG_EVENT_X_PAD) * 2,
			       "clip_height", item_h - (E_DAY_VIEW_LONG_EVENT_BORDER_HEIGHT + E_DAY_VIEW_LONG_EVENT_Y_PAD) * 2,
			       NULL);
	e_canvas_item_move_absolute (day_view->drag_long_event_item,
				     item_x + E_DAY_VIEW_LONG_EVENT_BORDER_WIDTH + E_DAY_VIEW_LONG_EVENT_X_PAD,
				     item_y + E_DAY_VIEW_LONG_EVENT_BORDER_HEIGHT + E_DAY_VIEW_LONG_EVENT_Y_PAD);

	if (!(day_view->drag_long_event_rect_item->object.flags & GNOME_CANVAS_ITEM_VISIBLE)) {
		gnome_canvas_item_raise_to_top (day_view->drag_long_event_rect_item);
		gnome_canvas_item_show (day_view->drag_long_event_rect_item);
	}

	/* Set the text, if necessary. We don't want to set the text every
	   time it moves, so we check if it is currently invisible and only
	   set the text then. */
	if (!(day_view->drag_long_event_item->object.flags
	      & GNOME_CANVAS_ITEM_VISIBLE)) {
		CalComponentText summary;

		if (event) {
			cal_component_get_summary (event->comp, &summary);
			text = g_strdup (summary.value);
		} else {
			text = NULL;
		}

		gnome_canvas_item_set (day_view->drag_long_event_item,
				       "text", text ? text : "",
				       NULL);
		gnome_canvas_item_raise_to_top (day_view->drag_long_event_item);
		gnome_canvas_item_show (day_view->drag_long_event_item);

		g_free (text);
	}
}


static gboolean
e_day_view_on_main_canvas_drag_motion (GtkWidget      *widget,
				       GdkDragContext *context,
				       gint            x,
				       gint            y,
				       guint           time,
				       EDayView	      *day_view)
{
	gint scroll_x, scroll_y;

	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (widget),
					 &scroll_x, &scroll_y);

	day_view->drag_event_x = x + scroll_x;
	day_view->drag_event_y = y + scroll_y;

	e_day_view_reshape_main_canvas_drag_item (day_view);
	e_day_view_reshape_main_canvas_resize_bars (day_view);

	e_day_view_check_auto_scroll (day_view, day_view->drag_event_x, day_view->drag_event_y);

	return TRUE;
}


static void
e_day_view_reshape_main_canvas_drag_item (EDayView *day_view)
{
	EDayViewPosition pos;
	gint x, y, day, row;

	/* Calculate the day & start row of the event being dragged, using
	   the current mouse position. */
	x = day_view->drag_event_x;
	y = day_view->drag_event_y;
	pos = e_day_view_convert_position_in_main_canvas (day_view, x, y,
							  &day, &row, NULL);
	/* This shouldn't really happen in a drag. */
	if (pos == E_DAY_VIEW_POS_OUTSIDE)
		return;

	if (day_view->drag_event_day != -1
	    && day_view->drag_event_day != E_DAY_VIEW_LONG_EVENT)
		row -= day_view->drag_event_offset;
	row = MAX (row, 0);

	e_day_view_update_main_canvas_drag (day_view, row, day);
}


static void
e_day_view_update_main_canvas_drag (EDayView *day_view,
				    gint row,
				    gint day)
{
	EDayViewEvent *event = NULL;
	gint cols_in_row, start_col, num_columns, num_rows, start_row, end_row;
	gdouble item_x, item_y, item_w, item_h;
	gchar *text;

	/* If the position hasn't changed, just return. */
	if (day_view->drag_last_day == day
	    && day_view->drag_last_row == row
	    && (day_view->drag_item->object.flags & GNOME_CANVAS_ITEM_VISIBLE))
		return;

	day_view->drag_last_day = day;
	day_view->drag_last_row = row;

	/* Calculate the event's position. If the event is in the same
	   position we started in, we use the same columns. */
	cols_in_row = 1;
	start_row = 0;
	start_col = 0;
	num_columns = 1;
	num_rows = 1;

	if (day_view->drag_event_day == E_DAY_VIEW_LONG_EVENT) {
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					day_view->drag_event_num);
	} else if (day_view->drag_event_day != -1) {
		event = &g_array_index (day_view->events[day_view->drag_event_day],
					EDayViewEvent,
					day_view->drag_event_num);
		start_row = event->start_minute / day_view->mins_per_row;
		end_row = (event->end_minute - 1) / day_view->mins_per_row;
		if (end_row < start_row)
			end_row = start_row;

		num_rows = end_row - start_row + 1;
	}

	if (day_view->drag_event_day == day && start_row == row) {
		cols_in_row = day_view->cols_per_row[day][row];
		start_col = event->start_row_or_col;
		num_columns = event->num_columns;
	}

	item_x = day_view->day_offsets[day]
		+ day_view->day_widths[day] * start_col / cols_in_row;
	item_w = day_view->day_widths[day] * num_columns / cols_in_row
		- E_DAY_VIEW_GAP_WIDTH;
	item_y = row * day_view->row_height;
	item_h = num_rows * day_view->row_height;

	/* Set the positions of the event & associated items. */
	gnome_canvas_item_set (day_view->drag_rect_item,
			       "x1", item_x + E_DAY_VIEW_BAR_WIDTH - 1,
			       "y1", item_y,
			       "x2", item_x + item_w - 1,
			       "y2", item_y + item_h - 1,
			       NULL);

	gnome_canvas_item_set (day_view->drag_bar_item,
			       "x1", item_x,
			       "y1", item_y,
			       "x2", item_x + E_DAY_VIEW_BAR_WIDTH - 1,
			       "y2", item_y + item_h - 1,
			       NULL);

	gnome_canvas_item_set (day_view->drag_item,
			       "clip_width", item_w - E_DAY_VIEW_BAR_WIDTH - E_DAY_VIEW_EVENT_X_PAD * 2,
			       "clip_height", item_h - (E_DAY_VIEW_EVENT_BORDER_HEIGHT + E_DAY_VIEW_EVENT_Y_PAD) * 2,
			       NULL);
	e_canvas_item_move_absolute (day_view->drag_item,
				     item_x + E_DAY_VIEW_BAR_WIDTH + E_DAY_VIEW_EVENT_X_PAD,
				     item_y + E_DAY_VIEW_EVENT_BORDER_HEIGHT + E_DAY_VIEW_EVENT_Y_PAD);

	if (!(day_view->drag_bar_item->object.flags & GNOME_CANVAS_ITEM_VISIBLE)) {
		gnome_canvas_item_raise_to_top (day_view->drag_bar_item);
		gnome_canvas_item_show (day_view->drag_bar_item);
	}

	if (!(day_view->drag_rect_item->object.flags & GNOME_CANVAS_ITEM_VISIBLE)) {
		gnome_canvas_item_raise_to_top (day_view->drag_rect_item);
		gnome_canvas_item_show (day_view->drag_rect_item);
	}

	/* Set the text, if necessary. We don't want to set the text every
	   time it moves, so we check if it is currently invisible and only
	   set the text then. */
	if (!(day_view->drag_item->object.flags & GNOME_CANVAS_ITEM_VISIBLE)) {
		CalComponentText summary;

		if (event) {
			cal_component_get_summary (event->comp, &summary);
			text = g_strdup (summary.value);
		} else {
			text = NULL;
		}

		gnome_canvas_item_set (day_view->drag_item,
				       "text", text ? text : "",
				       NULL);
		gnome_canvas_item_raise_to_top (day_view->drag_item);
		gnome_canvas_item_show (day_view->drag_item);

		g_free (text);
	}
}


static void
e_day_view_on_top_canvas_drag_leave (GtkWidget      *widget,
				     GdkDragContext *context,
				     guint           time,
				     EDayView	     *day_view)
{
	day_view->drag_last_day = -1;

	gnome_canvas_item_hide (day_view->drag_long_event_rect_item);
	gnome_canvas_item_hide (day_view->drag_long_event_item);
}


static void
e_day_view_on_main_canvas_drag_leave (GtkWidget      *widget,
				      GdkDragContext *context,
				      guint           time,
				      EDayView	     *day_view)
{
	day_view->drag_last_day = -1;

	e_day_view_stop_auto_scroll (day_view);

	gnome_canvas_item_hide (day_view->drag_rect_item);
	gnome_canvas_item_hide (day_view->drag_bar_item);
	gnome_canvas_item_hide (day_view->drag_item);

	/* Hide the resize bars if they are being used in the drag. */
	if (day_view->drag_event_day == day_view->resize_bars_event_day
	    && day_view->drag_event_num == day_view->resize_bars_event_num) {
		gnome_canvas_item_hide (day_view->main_canvas_top_resize_bar_item);
		gnome_canvas_item_hide (day_view->main_canvas_bottom_resize_bar_item);
	}
}


static void
e_day_view_on_drag_begin (GtkWidget      *widget,
			  GdkDragContext *context,
			  EDayView	 *day_view)
{
	EDayViewEvent *event;
	gint day, event_num;

	day = day_view->drag_event_day;
	event_num = day_view->drag_event_num;

	/* These should both be set. */
	g_return_if_fail (day != -1);
	g_return_if_fail (event_num != -1);

	if (day == E_DAY_VIEW_LONG_EVENT)
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);
	else
		event = &g_array_index (day_view->events[day], EDayViewEvent,
					event_num);

	/* Hide the text item, since it will be shown in the special drag
	   items. */
	gnome_canvas_item_hide (event->canvas_item);
}


static void
e_day_view_on_drag_end (GtkWidget      *widget,
			GdkDragContext *context,
			EDayView       *day_view)
{
	EDayViewEvent *event;
	gint day, event_num;

	day = day_view->drag_event_day;
	event_num = day_view->drag_event_num;

	/* If the calendar has already been updated in drag_data_received()
	   we just return. */
	if (day == -1 || event_num == -1)
		return;

	if (day == E_DAY_VIEW_LONG_EVENT) {
		event = &g_array_index (day_view->long_events, EDayViewEvent,
					event_num);
		gtk_widget_queue_draw (day_view->top_canvas);
	} else {
		event = &g_array_index (day_view->events[day], EDayViewEvent,
					event_num);
		gtk_widget_queue_draw (day_view->main_canvas);
	}

	/* Show the text item again. */
	gnome_canvas_item_show (event->canvas_item);

	day_view->drag_event_day = -1;
	day_view->drag_event_num = -1;
}


static void
e_day_view_on_drag_data_get (GtkWidget          *widget,
			     GdkDragContext     *context,
			     GtkSelectionData   *selection_data,
			     guint               info,
			     guint               time,
			     EDayView		*day_view)
{
	EDayViewEvent *event;
	gint day, event_num;

	day = day_view->drag_event_day;
	event_num = day_view->drag_event_num;

	/* These should both be set. */
	g_return_if_fail (day != -1);
	g_return_if_fail (event_num != -1);

	if (day == E_DAY_VIEW_LONG_EVENT)
		event = &g_array_index (day_view->long_events,
					EDayViewEvent, event_num);
	else
		event = &g_array_index (day_view->events[day],
					EDayViewEvent, event_num);

	if (info == TARGET_CALENDAR_EVENT) {
		const char *event_uid;

		cal_component_get_uid (event->comp, &event_uid);
		g_return_if_fail (event_uid != NULL);

		gtk_selection_data_set (selection_data,	selection_data->target,
 					8, event_uid, strlen (event_uid));
	} else if (info == TARGET_VCALENDAR) {
		char *comp_str;
		icalcomponent *vcal;

		vcal = cal_util_new_top_level ();
		cal_util_add_timezones_from_component (vcal, event->comp);
		icalcomponent_add_component (
			vcal,
			icalcomponent_new_clone (cal_component_get_icalcomponent (event->comp)));

		comp_str = icalcomponent_as_ical_string (vcal);
		if (comp_str) {
			gtk_selection_data_set (selection_data, selection_data->target,
						8, comp_str, strlen (comp_str));
		}

		icalcomponent_free (vcal);
	}
}


static void
e_day_view_on_top_canvas_drag_data_received  (GtkWidget          *widget,
					      GdkDragContext     *context,
					      gint                x,
					      gint                y,
					      GtkSelectionData   *data,
					      guint               info,
					      guint               time,
					      EDayView	         *day_view)
{
	EDayViewEvent *event=NULL;
	EDayViewPosition pos;
	gint day, start_day, end_day, num_days;
	gint start_offset, end_offset;
	gchar *event_uid;
	CalComponent *comp;
	CalComponentDateTime date;
	struct icaltimetype itt;
	time_t dt;
	gboolean all_day_event;

	/* Note that we only support DnD within the EDayView at present. */
	if ((data->length >= 0) && (data->format == 8)
	    && (day_view->drag_event_day != -1)) {
		pos = e_day_view_convert_position_in_top_canvas (day_view,
								 x, y, &day,
								 NULL);
		if (pos != E_DAY_VIEW_POS_OUTSIDE) {
			const char *uid;
			num_days = 1;
			start_offset = 0;
			end_offset = 0;

			if (day_view->drag_event_day == E_DAY_VIEW_LONG_EVENT) {
				event = &g_array_index (day_view->long_events, EDayViewEvent,
							day_view->drag_event_num);
				day -= day_view->drag_event_offset;
				day = MAX (day, 0);

				e_day_view_find_long_event_days (event,
								 day_view->days_shown,
								 day_view->day_starts,
								 &start_day,
								 &end_day);
				num_days = end_day - start_day + 1;
				/* Make sure we don't go off the screen. */
				day = MIN (day, day_view->days_shown - num_days);

				start_offset = event->start_minute;
				end_offset = event->end_minute;
			} else {
				event = &g_array_index (day_view->events[day_view->drag_event_day],
							EDayViewEvent,
							day_view->drag_event_num);
			}

			event_uid = data->data;

			cal_component_get_uid (event->comp, &uid);

			if (!event_uid || !uid || strcmp (event_uid, uid))
				g_warning ("Unexpected event UID");

			/* We clone the event since we don't want to change
			   the original comp here.
			   Otherwise we would not detect that the event's time
			   had changed in the "update_event" callback. */

			comp = cal_component_clone (event->comp);

			if (start_offset == 0 && end_offset == 0)
				all_day_event = TRUE;
			else
				all_day_event = FALSE;

			date.value = &itt;

			dt = day_view->day_starts[day] + start_offset * 60;
			itt = icaltime_from_timet_with_zone (dt, FALSE,
							     day_view->zone);
			if (all_day_event) {
				itt.is_date = TRUE;
				date.tzid = NULL;
			} else {
				/* FIXME: Should probably keep the timezone of
				   the original start and end times. */
				date.tzid = icaltimezone_get_tzid (day_view->zone);
			}
			cal_component_set_dtstart (comp, &date);

			if (end_offset == 0)
				dt = day_view->day_starts[day + num_days];
			else
				dt = day_view->day_starts[day + num_days - 1] + end_offset * 60;
			itt = icaltime_from_timet_with_zone (dt, FALSE,
							     day_view->zone);
			if (all_day_event) {
				itt.is_date = TRUE;
				date.tzid = NULL;
			} else {
				/* FIXME: Should probably keep the timezone of
				   the original start and end times. */
				date.tzid = icaltimezone_get_tzid (day_view->zone);
			}
			cal_component_set_dtend (comp, &date);

			gtk_drag_finish (context, TRUE, TRUE, time);

			/* Reset this since it will be invalid. */
			day_view->drag_event_day = -1;

			/* Show the text item again, just in case it hasn't
			   moved. If we don't do this it may not appear. */
			if (event->canvas_item)
				gnome_canvas_item_show (event->canvas_item);

			if (cal_component_is_instance (comp)) {
				CalObjModType mod;
				
				if (recur_component_dialog (comp, &mod, NULL)) {
					if (cal_client_update_object_with_mod (day_view->client, comp, mod) == CAL_CLIENT_RESULT_SUCCESS) {
						if (itip_organizer_is_user (comp, day_view->client) 
						    && send_component_dialog (gtk_widget_get_toplevel (day_view),
									      day_view->client, comp, FALSE))
							itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp, 
									day_view->client, NULL);
					} else {
						g_message ("e_day_view_on_top_canvas_drag_data_received(): Could "
							   "not update the object!");
					}
				}
			} else if (cal_client_update_object (day_view->client, comp)
			    == CAL_CLIENT_RESULT_SUCCESS) {
				if (itip_organizer_is_user (comp, day_view->client) &&
				    send_component_dialog (gtk_widget_get_toplevel (day_view),
							   day_view->client, comp, FALSE))
					itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp,
							day_view->client, NULL);
			} else {
				g_message ("e_day_view_on_top_canvas_drag_data_received(): Could "
					   "not update the object!");
			}

			g_object_unref (comp);

			return;
		}
	}

	gtk_drag_finish (context, FALSE, FALSE, time);
}


static void
e_day_view_on_main_canvas_drag_data_received  (GtkWidget          *widget,
					       GdkDragContext     *context,
					       gint                x,
					       gint                y,
					       GtkSelectionData   *data,
					       guint               info,
					       guint               time,
					       EDayView		  *day_view)
{
	EDayViewEvent *event = NULL;
	EDayViewPosition pos;
	gint day, row, start_row, end_row, num_rows, scroll_x, scroll_y;
	gint start_offset, end_offset;
	gchar *event_uid;
	CalComponent *comp;
	CalComponentDateTime date;
	struct icaltimetype itt;
	time_t dt;

	gnome_canvas_get_scroll_offsets (GNOME_CANVAS (widget),
					 &scroll_x, &scroll_y);
	x += scroll_x;
	y += scroll_y;

	/* Note that we only support DnD within the EDayView at present. */
	if ((data->length >= 0) && (data->format == 8)
	    && (day_view->drag_event_day != -1)) {
		pos = e_day_view_convert_position_in_main_canvas (day_view,
								  x, y, &day,
								  &row, NULL);
		if (pos != E_DAY_VIEW_POS_OUTSIDE) {
			const char *uid;
			num_rows = 1;
			start_offset = 0;
			end_offset = 0;

			if (day_view->drag_event_day == E_DAY_VIEW_LONG_EVENT) {
				event = &g_array_index (day_view->long_events, EDayViewEvent,
							day_view->drag_event_num);
			} else {
				event = &g_array_index (day_view->events[day_view->drag_event_day],
							EDayViewEvent,
							day_view->drag_event_num);
				row -= day_view->drag_event_offset;

				/* Calculate time offset from start row. */
				start_row = event->start_minute / day_view->mins_per_row;
				end_row = (event->end_minute - 1) / day_view->mins_per_row;
				if (end_row < start_row)
					end_row = start_row;

				num_rows = end_row - start_row + 1;

				start_offset = event->start_minute % day_view->mins_per_row;
				end_offset = event->end_minute % day_view->mins_per_row;
				if (end_offset != 0)
					end_offset = day_view->mins_per_row - end_offset;
			}

			event_uid = data->data;

			cal_component_get_uid (event->comp, &uid);
			if (!event_uid || !uid || strcmp (event_uid, uid))
				g_warning ("Unexpected event UID");

			/* We use a temporary shallow copy of comp since we
			   don't want to change the original comp here.
			   Otherwise we would not detect that the event's time
			   had changed in the "update_event" callback. */
			comp = cal_component_clone (event->comp);

			date.value = &itt;
			date.tzid = icaltimezone_get_tzid (day_view->zone);

			dt = e_day_view_convert_grid_position_to_time (day_view, day, row) + start_offset * 60;
			*date.value = icaltime_from_timet_with_zone (dt, FALSE,
								     day_view->zone);
			cal_component_set_dtstart (comp, &date);
			dt = e_day_view_convert_grid_position_to_time (day_view, day, row + num_rows) - end_offset * 60;
			*date.value = icaltime_from_timet_with_zone (dt, FALSE,
								     day_view->zone);
			cal_component_set_dtend (comp, &date);

			gtk_drag_finish (context, TRUE, TRUE, time);

			/* Reset this since it will be invalid. */
			day_view->drag_event_day = -1;

			/* Show the text item again, just in case it hasn't
			   moved. If we don't do this it may not appear. */
			if (event->canvas_item)
				gnome_canvas_item_show (event->canvas_item);

			if (cal_component_is_instance (comp)) {
				CalObjModType mod;
				
				if (recur_component_dialog (comp, &mod, NULL)) {
					if (cal_client_update_object_with_mod (day_view->client, comp, mod) == CAL_CLIENT_RESULT_SUCCESS) {
						if (itip_organizer_is_user (comp, day_view->client) 
						    && send_component_dialog (gtk_widget_get_toplevel (day_view),
									      day_view->client, comp, FALSE))
							itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp, 
									day_view->client, NULL);
					} else {
						g_message ("e_day_view_on_top_canvas_drag_data_received(): Could "
							   "not update the object!");
					}
				}
			} else if (cal_client_update_object (day_view->client, comp)
			    == CAL_CLIENT_RESULT_SUCCESS) {
				if (itip_organizer_is_user (comp, day_view->client) &&
				    send_component_dialog (gtk_widget_get_toplevel (day_view),
							   day_view->client, comp, FALSE))
					itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp,
							day_view->client, NULL);
			} else {
				g_message ("e_day_view_on_main_canvas_drag_data_received(): "
					   "Could not update the object!");
			}

			g_object_unref (comp);

			return;
		}
	}

	gtk_drag_finish (context, FALSE, FALSE, time);
}


/* Converts an hour from 0-23 to the preferred time format, and returns the
   suffix to add and the width of it in the normal font. */
void
e_day_view_convert_time_to_display	(EDayView	*day_view,
					 gint		 hour,
					 gint		*display_hour,
					 gchar	       **suffix,
					 gint		*suffix_width)
{
	/* Calculate the actual hour number to display. For 12-hour
	   format we convert 0-23 to 12-11am/12-11pm. */
	*display_hour = hour;
	if (day_view->use_24_hour_format) {
		*suffix = "";
		*suffix_width = 0;
	} else {
		if (hour < 12) {
			*suffix = day_view->am_string;
			*suffix_width = day_view->am_string_width;
		} else {
			*display_hour -= 12;
			*suffix = day_view->pm_string;
			*suffix_width = day_view->pm_string_width;
		}

		/* 12-hour uses 12:00 rather than 0:00. */
		if (*display_hour == 0)
			*display_hour = 12;
	}
}


gint
e_day_view_get_time_string_width	(EDayView	*day_view)
{
	gint time_width;

	time_width = day_view->digit_width * 4 + day_view->colon_width;

	if (!day_view->use_24_hour_format)
		time_width += MAX (day_view->am_string_width,
				   day_view->pm_string_width);

	return time_width;
}

static void
selection_get (GtkWidget *invisible,
	       GtkSelectionData *selection_data,
	       guint info,
	       guint time_stamp,
	       EDayView *day_view)
{
	if (day_view->clipboard_selection != NULL) {
		gtk_selection_data_set (selection_data,
					GDK_SELECTION_TYPE_STRING,
					8,
					day_view->clipboard_selection,
					strlen (day_view->clipboard_selection));
	}
}

static void
selection_clear_event (GtkWidget *invisible,
		       GdkEventSelection *event,
		       EDayView *day_view)
{
	if (day_view->clipboard_selection != NULL) {
		g_free (day_view->clipboard_selection);
		day_view->clipboard_selection = NULL;
	}
}

static void
selection_received (GtkWidget *invisible,
		    GtkSelectionData *selection_data,
		    guint time,
		    EDayView *day_view)
{
	char *comp_str;
	icalcomponent *icalcomp;
	time_t dtstart, dtend;
	struct icaltimetype itime;
	icalcomponent_kind kind;
	CalComponent *comp;
	char *uid;
	time_t tt_start, tt_end;
	struct icaldurationtype ic_dur;

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (selection_data->length < 0 ||
	    selection_data->type != GDK_SELECTION_TYPE_STRING) {
		return;
	}

	comp_str = (char *) selection_data->data;
	icalcomp = icalparser_parse_string ((const char *) comp_str);
	if (!icalcomp)
		return;

	/* check the type of the component */
	kind = icalcomponent_isa (icalcomp);
	if (kind != ICAL_VCALENDAR_COMPONENT &&
	    kind != ICAL_VEVENT_COMPONENT &&
	    kind != ICAL_VTODO_COMPONENT &&
	    kind != ICAL_VJOURNAL_COMPONENT) {
		return;
	}

	e_day_view_set_status_message (day_view, _("Updating objects"));
	e_day_view_get_selected_time_range (day_view, &dtstart, &dtend);

	if (kind == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_kind child_kind;
		icalcomponent *subcomp;

		subcomp = icalcomponent_get_first_component (
			icalcomp, ICAL_ANY_COMPONENT);
		while (subcomp) {
			child_kind = icalcomponent_isa (subcomp);
			if (child_kind == ICAL_VEVENT_COMPONENT ||
			    child_kind == ICAL_VTODO_COMPONENT ||
			    child_kind == ICAL_VJOURNAL_COMPONENT) {
				tt_start = icaltime_as_timet (icalcomponent_get_dtstart (subcomp));
				tt_end = icaltime_as_timet (icalcomponent_get_dtend (subcomp));
				ic_dur = icaldurationtype_from_int (tt_end - tt_start);
				itime = icaltime_from_timet_with_zone (dtstart, FALSE, day_view->zone);

				icalcomponent_set_dtstart (subcomp, itime);
				itime = icaltime_add (itime, ic_dur);
				icalcomponent_set_dtend (subcomp, itime);

				uid = cal_component_gen_uid ();
				comp = cal_component_new ();
				cal_component_set_icalcomponent (
					comp, icalcomponent_new_clone (subcomp));
				cal_component_set_uid (comp, uid);

				cal_client_update_object (day_view->client, comp);

				free (uid);
				g_object_unref (comp);

			}
			subcomp = icalcomponent_get_next_component (
				icalcomp, ICAL_ANY_COMPONENT);
		}
	}
	else {
		tt_start = icaltime_as_timet (icalcomponent_get_dtstart (icalcomp));
		tt_end = icaltime_as_timet (icalcomponent_get_dtend (icalcomp));
		ic_dur = icaldurationtype_from_int (tt_end - tt_start);
		itime = icaltime_from_timet_with_zone (dtstart, FALSE, day_view->zone);

		icalcomponent_set_dtstart (icalcomp, itime);
		itime = icaltime_add (itime, ic_dur);
		icalcomponent_set_dtend (icalcomp, itime);

		comp = cal_component_new ();
		cal_component_set_icalcomponent (comp, icalcomp);

		uid = cal_component_gen_uid ();
		cal_component_set_uid (comp, (const char *) uid);
		free (uid);

		cal_client_update_object (day_view->client, comp);

		if (itip_organizer_is_user (comp, day_view->client) && 
		    send_component_dialog (gtk_widget_get_toplevel (day_view), day_view->client, comp, TRUE))
			itip_send_comp (CAL_COMPONENT_METHOD_REQUEST, comp, day_view->client, NULL);

		g_object_unref (comp);
	}

	e_day_view_set_status_message (day_view, NULL);
}


/* Gets the visible time range. Returns FALSE if no time range has been set. */
gboolean
e_day_view_get_visible_time_range	(EDayView	*day_view,
					 time_t		*start_time,
					 time_t		*end_time)
{
	/* If the date isn't set, return FALSE. */
	if (day_view->lower == 0 && day_view->upper == 0)
		return FALSE;

	*start_time = day_view->day_starts[0];
	*end_time = day_view->day_starts[day_view->days_shown];

	return TRUE;
}


/* Queues a layout, unless one is already queued. */
static void
e_day_view_queue_layout (EDayView *day_view)
{
	if (day_view->layout_timeout_id == 0) {
		day_view->layout_timeout_id = g_timeout_add (E_DAY_VIEW_LAYOUT_TIMEOUT, e_day_view_layout_timeout_cb, day_view);
	}
}


/* Removes any queued layout. */
static void
e_day_view_cancel_layout (EDayView *day_view)
{
	if (day_view->layout_timeout_id != 0) {
		gtk_timeout_remove (day_view->layout_timeout_id);
		day_view->layout_timeout_id = 0;
	}
}


static gboolean
e_day_view_layout_timeout_cb (gpointer data)
{
	EDayView *day_view = E_DAY_VIEW (data);

	gtk_widget_queue_draw (day_view->top_canvas);
	gtk_widget_queue_draw (day_view->main_canvas);
	e_day_view_check_layout (day_view);

	day_view->layout_timeout_id = 0;
	return FALSE;
}


/* Returns the number of selected events (0 or 1 at present). */
gint
e_day_view_get_num_events_selected (EDayView *day_view)
{
	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), 0);

	return (day_view->editing_event_day != -1) ? 1 : 0;
}

/* Returns the currently-selected event, or NULL if none */
CalComponent *
e_day_view_get_selected_event (EDayView *day_view)
{
	EDayViewEvent *event;

	g_return_val_if_fail (E_IS_DAY_VIEW (day_view), NULL);
	g_return_val_if_fail (day_view->editing_event_day != -1, NULL);

	if (day_view->editing_event_day == E_DAY_VIEW_LONG_EVENT)
		event = &g_array_index (day_view->long_events,
				       EDayViewEvent,
				       day_view->editing_event_num);
	else
		event = &g_array_index (day_view->events[day_view->editing_event_day],
				       EDayViewEvent,
				       day_view->editing_event_num);

	return event ? event->comp : NULL;
}

/* Displays messages on the status bar. */
void
e_day_view_set_status_message (EDayView *day_view, const char *message)
{
	extern EvolutionShellClient *global_shell_client; /* ugly */

	g_return_if_fail (E_IS_DAY_VIEW (day_view));

	if (!message || !*message) {
		if (day_view->activity) {
			g_object_unref (day_view->activity);
			day_view->activity = NULL;
		}
	}
	else if (!day_view->activity) {
		int display;
		char *client_id = g_strdup_printf ("%p", day_view);

		if (progress_icon[0] == NULL)
			progress_icon[0] = gdk_pixbuf_new_from_file (EVOLUTION_IMAGESDIR "/" EVOLUTION_CALENDAR_PROGRESS_IMAGE, NULL);
		day_view->activity = evolution_activity_client_new (
			global_shell_client, client_id,
			progress_icon, message, TRUE, &display);

		g_free (client_id);
	}
	else
		evolution_activity_client_update (day_view->activity, message, -1.0);
}
