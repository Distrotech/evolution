/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - Timezone selector dialog
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Damon Chaplin <damon@ximian.com>
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklabel.h>
#include <gnome.h>
#include <glade/glade.h>
#include <misc/e-map.h>
#include <libecal/e-cal-time-util.h>

#include "e-util/e-util-private.h"

#include "e-timezone-dialog.h"

#ifdef G_OS_WIN32
/* Undef the similar macros from pthread.h, they don't check if
 * gmtime() and localtime() return NULL.
 */
#undef gmtime_r
#undef localtime_r

/* The gmtime() and localtime() in Microsoft's C library are MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#define localtime_r(tp,tmp) (localtime(tp)?(*(tmp)=*localtime(tp),(tmp)):0)
#endif

#define E_TIMEZONE_DIALOG_MAP_POINT_NORMAL_RGBA 0xc070a0ff
#define E_TIMEZONE_DIALOG_MAP_POINT_HOVER_RGBA 0xffff60ff
#define E_TIMEZONE_DIALOG_MAP_POINT_SELECTED_1_RGBA 0xff60e0ff
#define E_TIMEZONE_DIALOG_MAP_POINT_SELECTED_2_RGBA 0x000000ff

struct _ETimezoneDialogPrivate {
	/* The selected timezone. May be NULL for a 'local time' (i.e. when
	   the displayed name is ""). */
	icaltimezone *zone;

	/* Glade XML data */
	GladeXML *xml;

	EMapPoint *point_selected;
	EMapPoint *point_hover;

	EMap *map;

	/* The timeout used to flash the nearest point. */
	guint timeout_id;

	/* Widgets from the Glade file */
	GtkWidget *app;
	GtkWidget *table;
	GtkWidget *map_window;
	GtkWidget *timezone_combo;
	GtkWidget *preview_label;
};

#ifndef G_OS_WIN32 /* Declared properly in time.h already */
extern char *tzname[2];
extern long timezone;
extern int daylight;
#endif

static void e_timezone_dialog_class_init	(ETimezoneDialogClass *class);
static void e_timezone_dialog_init		(ETimezoneDialog      *etd);
static void e_timezone_dialog_dispose		(GObject	*object);
static void e_timezone_dialog_finalize		(GObject	*object);

static gboolean get_widgets			(ETimezoneDialog *etd);
static gboolean on_map_timeout			(gpointer	 data);
static gboolean on_map_motion			(GtkWidget	*widget,
						 GdkEventMotion *event,
						 gpointer	 data);
static gboolean on_map_leave			(GtkWidget	*widget,
						 GdkEventCrossing *event,
						 gpointer	 data);
static gboolean on_map_visibility_changed	(GtkWidget	*w,
						 GdkEventVisibility *event,
						 gpointer	 data);
static gboolean on_map_button_pressed		(GtkWidget	*w,
						 GdkEventButton *event,
						 gpointer	 data);

static icaltimezone* get_zone_from_point	(ETimezoneDialog *etd,
						 EMapPoint	*point);
static void	set_map_timezone		(ETimezoneDialog *etd,
						 icaltimezone    *zone);
static void	on_combo_changed		(GtkEditable	*entry,
						 ETimezoneDialog *etd);
static void	map_destroy_cb			(gpointer data, 
						 GObject *where_object_was);

G_DEFINE_TYPE (ETimezoneDialog, e_timezone_dialog, G_TYPE_OBJECT)

/* Class initialization function for the event editor */
static void
e_timezone_dialog_class_init (ETimezoneDialogClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose  = e_timezone_dialog_dispose;
	object_class->finalize = e_timezone_dialog_finalize;
}

/* Object initialization function for the event editor */
static void
e_timezone_dialog_init (ETimezoneDialog *etd)
{
	ETimezoneDialogPrivate *priv;

	priv = g_new0 (ETimezoneDialogPrivate, 1);
	etd->priv = priv;

	priv->point_selected = NULL;
	priv->point_hover = NULL;
	priv->timeout_id = 0;
}

/* Dispose handler for the event editor */
static void
e_timezone_dialog_dispose (GObject *object)
{
	ETimezoneDialog *etd;
	ETimezoneDialogPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_TIMEZONE_DIALOG (object));

	etd = E_TIMEZONE_DIALOG (object);
	priv = etd->priv;

	/* Destroy the actual dialog. */
	if (priv->app != NULL) {
		gtk_widget_destroy (priv->app);
		priv->app = NULL;
	}

	if (priv->timeout_id) {
		g_source_remove (priv->timeout_id);
		priv->timeout_id = 0;
	}

	if (priv->xml) {
		g_object_unref (priv->xml);
		priv->xml = NULL;
	}

	(* G_OBJECT_CLASS (e_timezone_dialog_parent_class)->dispose) (object);
}

/* Finalize handler for the event editor */
static void
e_timezone_dialog_finalize (GObject *object)
{
	ETimezoneDialog *etd;
	ETimezoneDialogPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_TIMEZONE_DIALOG (object));

	etd = E_TIMEZONE_DIALOG (object);
	priv = etd->priv;

	g_free (priv);

	(* G_OBJECT_CLASS (e_timezone_dialog_parent_class)->finalize) (object);
}


static void
e_timezone_dialog_add_timezones (ETimezoneDialog *etd)
{
	ETimezoneDialogPrivate *priv;
	icalarray *zones;
	GtkWidget *listitem;
	GtkCombo *combo;
	int i;

	priv = etd->priv;

	combo = GTK_COMBO (priv->timezone_combo);

	/* Clear any existing items in the combo. */
	gtk_list_clear_items (GTK_LIST (combo->list), 0, -1);

	/* Put the ""UTC" entry at the top of the combo's list. */
	listitem = gtk_list_item_new_with_label (_("UTC"));
	gtk_widget_show (listitem);
	gtk_container_add (GTK_CONTAINER (combo->list), listitem);

	/* Get the array of builtin timezones. */
	zones = icaltimezone_get_builtin_timezones ();

	for (i = 0; i < zones->num_elements; i++) {
		icaltimezone *zone;
		char *location;

		zone = icalarray_element_at (zones, i);

		location = _(icaltimezone_get_location (zone));

		e_map_add_point (priv->map, location,
				 icaltimezone_get_longitude (zone),
				 icaltimezone_get_latitude (zone),
				 E_TIMEZONE_DIALOG_MAP_POINT_NORMAL_RGBA);

		listitem = gtk_list_item_new_with_label (location);
		gtk_widget_show (listitem);
		gtk_container_add (GTK_CONTAINER (combo->list), listitem);
	}
}


ETimezoneDialog *
e_timezone_dialog_construct (ETimezoneDialog *etd)
{
	ETimezoneDialogPrivate *priv;
	GtkWidget *map;
	gchar *filename;

	g_return_val_if_fail (etd != NULL, NULL);
	g_return_val_if_fail (E_IS_TIMEZONE_DIALOG (etd), NULL);

	priv = etd->priv;

	/* Load the content widgets */

	filename = g_build_filename (EVOLUTION_GLADEDIR,
				     "e-timezone-dialog.glade",
				     NULL);
	priv->xml = glade_xml_new (filename, NULL, NULL);
	g_free (filename);

	if (!priv->xml) {
		g_message ("e_timezone_dialog_construct(): Could not load the Glade XML file!");
		goto error;
	}

	if (!get_widgets (etd)) {
		g_message ("e_timezone_dialog_construct(): Could not find all widgets in the XML file!");
		goto error;
	}

	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (priv->app)->vbox), 0);
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (priv->app)->action_area), 12);

	priv->map = e_map_new ();
	map = GTK_WIDGET (priv->map);

	g_object_weak_ref(G_OBJECT(map), map_destroy_cb, priv);

	gtk_widget_set_events (map, gtk_widget_get_events (map)
			       | GDK_LEAVE_NOTIFY_MASK
			       | GDK_VISIBILITY_NOTIFY_MASK);

	gtk_editable_set_editable (GTK_EDITABLE (GTK_COMBO (priv->timezone_combo)->entry), FALSE);

	e_timezone_dialog_add_timezones (etd);

	gtk_container_add (GTK_CONTAINER (priv->map_window), map);
	gtk_widget_show (map);

	/* Ensure a reasonable minimum amount of map is visible */
	gtk_widget_set_size_request (priv->map_window, 200, 200);

        g_signal_connect (map, "motion-notify-event", G_CALLBACK (on_map_motion), etd);
        g_signal_connect (map, "leave-notify-event", G_CALLBACK (on_map_leave), etd);
        g_signal_connect (map, "visibility-notify-event", G_CALLBACK (on_map_visibility_changed), etd);
	g_signal_connect (map, "button-press-event", G_CALLBACK (on_map_button_pressed), etd);

	g_signal_connect (GTK_COMBO (priv->timezone_combo)->entry, "changed", G_CALLBACK (on_combo_changed), etd);

	return etd;

 error:

	g_object_unref (etd);
	return NULL;
}

#if 0
static int
get_local_offset ()
{
	time_t now = time(NULL), t_gmt, t_local;
	struct tm gmt, local;
	int diff;
		
	gmtime_r (&now, &gmt);
	localtime_r (&now, &local);
	t_gmt = mktime (&gmt);
	t_local = mktime (&local);
	diff = t_local - t_gmt;

	return diff;
}
#endif

static const icaltimezone*
get_local_timezone()
{
	icaltimezone *zone;
	
	tzset();
	zone =  icaltimezone_get_builtin_timezone_from_offset (-timezone, tzname[0]);

	return zone;
}

/* Gets the widgets from the XML file and returns if they are all available.
 * For the widgets whose values can be simply set with e-dialog-utils, it does
 * that as well.
 */
static gboolean
get_widgets (ETimezoneDialog *etd)
{
	ETimezoneDialogPrivate *priv;

	priv = etd->priv;

#define GW(name) glade_xml_get_widget (priv->xml, name)

	priv->app		= GW ("timezone-dialog");
	priv->map_window	= GW ("map-window");
	priv->timezone_combo	= GW ("timezone-combo");
	priv->table             = GW ("timezone-table");
	priv->preview_label     = GW ("preview-label");

	return (priv->app
		&& priv->map_window
		&& priv->timezone_combo
		&& priv->table
		&& priv->preview_label);
}


/**
 * e_timezone_dialog_new:
 *
 * Creates a new event editor dialog.
 *
 * Return value: A newly-created event editor dialog, or NULL if the event
 * editor could not be created.
 **/
ETimezoneDialog *
e_timezone_dialog_new (void)
{
	ETimezoneDialog *etd;

	etd = E_TIMEZONE_DIALOG (g_object_new (E_TYPE_TIMEZONE_DIALOG, NULL));
	return e_timezone_dialog_construct (E_TIMEZONE_DIALOG (etd));
}

static void
format_utc_offset			(int		 utc_offset,
					 char		*buffer)
{
  char *sign = "+";
  int hours, minutes, seconds;

  if (utc_offset < 0) {
    utc_offset = -utc_offset;
    sign = "-";
  }

  hours = utc_offset / 3600;
  minutes = (utc_offset % 3600) / 60;
  seconds = utc_offset % 60;

  /* Sanity check. Standard timezone offsets shouldn't be much more than 12
     hours, and daylight saving shouldn't change it by more than a few hours.
     (The maximum offset is 15 hours 56 minutes at present.) */
  if (hours < 0 || hours >= 24 || minutes < 0 || minutes >= 60
      || seconds < 0 || seconds >= 60) {
    fprintf (stderr, "Warning: Strange timezone offset: H:%i M:%i S:%i\n",
	     hours, minutes, seconds);
  }

  if (hours == 0 && minutes == 0 && seconds == 0)
	  strcpy (buffer, _("UTC"));
  else if (seconds == 0)
	  sprintf (buffer, "%s %s%02i:%02i", _("UTC"), sign, hours, minutes);
  else
	  sprintf (buffer, "%s %s%02i:%02i:%02i", _("UTC"), sign, hours, minutes, seconds);
}


static char *
zone_display_name_with_offset (icaltimezone *zone)
{
	const char *display_name;
	struct tm local;
	struct icaltimetype tt;
	int offset;
	char buffer [100];
	time_t now = time(NULL);
	
	gmtime_r ((const time_t *) &now, &local);
	tt = tm_to_icaltimetype (&local, TRUE);
	offset = icaltimezone_get_utc_offset(zone, &tt, NULL);

	format_utc_offset (offset, buffer);
	
	display_name = icaltimezone_get_display_name (zone);
	if (icaltimezone_get_builtin_timezone (display_name))
		display_name = _(display_name);

	return g_strdup_printf("%s (%s)\n", display_name, buffer);;
}

static const char *
zone_display_name (icaltimezone *zone)
{
	const char *display_name;
	
	display_name = icaltimezone_get_display_name (zone);
	if (icaltimezone_get_builtin_timezone (display_name))
		display_name = _(display_name);

	return display_name;
}

/* This flashes the currently selected timezone in the map. */
static gboolean
on_map_timeout (gpointer data)
{
	ETimezoneDialog *etd;
	ETimezoneDialogPrivate *priv;

	etd = E_TIMEZONE_DIALOG (data);
	priv = etd->priv;

	if (!priv->point_selected)
		return TRUE;

        if (e_map_point_get_color_rgba (priv->point_selected)
	    == E_TIMEZONE_DIALOG_MAP_POINT_SELECTED_1_RGBA)
	        e_map_point_set_color_rgba (priv->map, priv->point_selected,
					    E_TIMEZONE_DIALOG_MAP_POINT_SELECTED_2_RGBA);
	else
	        e_map_point_set_color_rgba (priv->map, priv->point_selected,
					    E_TIMEZONE_DIALOG_MAP_POINT_SELECTED_1_RGBA);

	return TRUE;
}


static gboolean
on_map_motion (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
	ETimezoneDialog *etd;
	ETimezoneDialogPrivate *priv;
	double longitude, latitude;
	icaltimezone *new_zone;
	char *display=NULL;
	
	etd = E_TIMEZONE_DIALOG (data);
	priv = etd->priv;

	e_map_window_to_world (priv->map, (double) event->x, (double) event->y,
			       &longitude, &latitude);

	if (priv->point_hover && priv->point_hover != priv->point_selected)
	        e_map_point_set_color_rgba (priv->map, priv->point_hover,
					    E_TIMEZONE_DIALOG_MAP_POINT_NORMAL_RGBA);

	priv->point_hover = e_map_get_closest_point (priv->map, longitude,
						     latitude, TRUE);

	if (priv->point_hover != priv->point_selected)
	        e_map_point_set_color_rgba (priv->map, priv->point_hover,
					    E_TIMEZONE_DIALOG_MAP_POINT_HOVER_RGBA);

	new_zone = get_zone_from_point (etd, priv->point_hover);

	display = zone_display_name_with_offset(new_zone);
	gtk_label_set_text (GTK_LABEL (priv->preview_label), display);

	g_free (display);
	
	return TRUE;
}


static gboolean
on_map_leave (GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
	ETimezoneDialog *etd;
	ETimezoneDialogPrivate *priv;

	etd = E_TIMEZONE_DIALOG (data);
	priv = etd->priv;

	/* We only want to reset the hover point if this is a normal leave
	   event. For some reason we are getting leave events when the
	   button is pressed in the map, which causes problems. */
	if (event->mode != GDK_CROSSING_NORMAL)
		return FALSE;

	if (priv->point_hover && priv->point_hover != priv->point_selected)
	        e_map_point_set_color_rgba (priv->map, priv->point_hover,
					    E_TIMEZONE_DIALOG_MAP_POINT_NORMAL_RGBA);

	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (priv->timezone_combo)->entry),
			    zone_display_name (priv->zone));
	gtk_label_set_text (GTK_LABEL (priv->preview_label), "");

	priv->point_hover = NULL;

	return FALSE;
}


static gboolean
on_map_visibility_changed (GtkWidget *w, GdkEventVisibility *event,
			   gpointer data)
{
	ETimezoneDialog *etd;
	ETimezoneDialogPrivate *priv;
	
	etd = E_TIMEZONE_DIALOG (data);
	priv = etd->priv;

	if (event->state != GDK_VISIBILITY_FULLY_OBSCURED) {
		/* Map is visible, at least partly, so make sure we flash the
		   selected point. */
		if (!priv->timeout_id)
			priv->timeout_id = g_timeout_add (100, on_map_timeout, etd);
	} else {
		/* Map is invisible, so don't waste resources on the timeout.*/
		if (priv->timeout_id) {
			g_source_remove (priv->timeout_id);
			priv->timeout_id = 0;
		}
	}

	return FALSE;
}


static gboolean
on_map_button_pressed (GtkWidget *w, GdkEventButton *event, gpointer data)
{
	ETimezoneDialog *etd;
	ETimezoneDialogPrivate *priv;
	double longitude, latitude;
	
	etd = E_TIMEZONE_DIALOG (data);
	priv = etd->priv;

	e_map_window_to_world (priv->map, (double) event->x, (double) event->y,
			       &longitude, &latitude);
	
	if (event->button != 1) {
	        e_map_zoom_out (priv->map);
	} else {
		if (e_map_get_magnification (priv->map) <= 1.0)
		        e_map_zoom_to_location (priv->map, longitude,
						latitude);
	
		if (priv->point_selected)
		        e_map_point_set_color_rgba (priv->map,
						    priv->point_selected,
						    E_TIMEZONE_DIALOG_MAP_POINT_NORMAL_RGBA);
		priv->point_selected = priv->point_hover;
		
		priv->zone = get_zone_from_point (etd, priv->point_selected);
		gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (priv->timezone_combo)->entry),
				    zone_display_name (priv->zone));
	}
	
	return TRUE;
}


/* Returns the translated timezone location of the given EMapPoint,
   e.g. "Europe/London". */
static icaltimezone *
get_zone_from_point (ETimezoneDialog *etd,
		     EMapPoint *point)
{
	icalarray *zones;
	double longitude, latitude;
	int i;

	if (point == NULL)
		return NULL;

	e_map_point_get_location (point, &longitude, &latitude);

	/* Get the array of builtin timezones. */
	zones = icaltimezone_get_builtin_timezones ();

	for (i = 0; i < zones->num_elements; i++) {
		icaltimezone *zone;
		double zone_longitude, zone_latitude;

		zone = icalarray_element_at (zones, i);
		zone_longitude = icaltimezone_get_longitude (zone);
		zone_latitude = icaltimezone_get_latitude (zone);

		if (zone_longitude - 0.005 <= longitude &&
		    zone_longitude + 0.005 >= longitude &&
		    zone_latitude - 0.005 <= latitude &&
		    zone_latitude + 0.005 >= latitude)
		{
			return zone;
		}
	}

	g_assert_not_reached ();

	return NULL;
}


/**
 * e_timezone_dialog_get_timezone:
 * @etd: the timezone dialog
 *
 * Return value: the currently-selected timezone, or %NULL if no timezone
 * is selected.
 **/
icaltimezone *
e_timezone_dialog_get_timezone (ETimezoneDialog *etd)
{
	ETimezoneDialogPrivate *priv;

	g_return_val_if_fail (E_IS_TIMEZONE_DIALOG (etd), NULL);

	priv = etd->priv;

	return priv->zone;
}

/**
 * e_timezone_dialog_set_timezone:
 * @etd: the timezone dialog
 * @zone: the timezone
 *
 * Sets the timezone of @etd to @zone. Updates the display name and
 * selected location. The caller must ensure that @zone is not freed
 * before @etd is destroyed.
 **/

void
e_timezone_dialog_set_timezone (ETimezoneDialog *etd,
				icaltimezone    *zone)
{
	ETimezoneDialogPrivate *priv;
	char *display=NULL;
	
	g_return_if_fail (E_IS_TIMEZONE_DIALOG (etd));

 	if (!zone) {
		zone = (icaltimezone *)get_local_timezone();
		if (!zone)
			zone = icaltimezone_get_utc_timezone();
	}

	if (zone)
		display=zone_display_name_with_offset(zone);
	
	priv = etd->priv;

	priv->zone = zone;

	gtk_label_set_text (GTK_LABEL (priv->preview_label),
			    zone ? display : "");
	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (priv->timezone_combo)->entry),
			    zone ? zone_display_name(zone) : "");

	set_map_timezone (etd, zone);
	g_free (display);
}


GtkWidget *
e_timezone_dialog_get_toplevel	(ETimezoneDialog  *etd)
{
	ETimezoneDialogPrivate *priv;

	g_return_val_if_fail (etd != NULL, NULL);
	g_return_val_if_fail (E_IS_TIMEZONE_DIALOG (etd), NULL);

	priv = etd->priv;

	return priv->app;
}


static void
set_map_timezone (ETimezoneDialog *etd, icaltimezone *zone)
{
	ETimezoneDialogPrivate *priv;
	EMapPoint *point;
	double zone_longitude, zone_latitude;

	priv = etd->priv;

	if (zone) {
		zone_longitude = icaltimezone_get_longitude (zone);
		zone_latitude = icaltimezone_get_latitude (zone);
		point = e_map_get_closest_point (priv->map,
						 zone_longitude,
						 zone_latitude,
						 FALSE);
	} else
		point = NULL;

	if (priv->point_selected)
		e_map_point_set_color_rgba (priv->map, priv->point_selected,
					    E_TIMEZONE_DIALOG_MAP_POINT_NORMAL_RGBA);

	priv->point_selected = point;
}


static void
on_combo_changed (GtkEditable *entry, ETimezoneDialog *etd)
{
	ETimezoneDialogPrivate *priv;
	const char *new_zone_name;
	icalarray *zones;
	icaltimezone *map_zone = NULL;
	char *location;
	int i;

	priv = etd->priv;

	new_zone_name = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (priv->timezone_combo)->entry));

	if (!*new_zone_name)
		priv->zone = NULL;
	else if (!strcmp (new_zone_name, _("UTC")))
		priv->zone = icaltimezone_get_utc_timezone ();
	else {
		priv->zone = NULL;

		zones = icaltimezone_get_builtin_timezones ();
		for (i = 0; i < zones->num_elements; i++) {
			map_zone = icalarray_element_at (zones, i);
			location = _(icaltimezone_get_location (map_zone));
			if (!strcmp (new_zone_name, location)) {
				priv->zone = map_zone;
				break;
			}
		}
	}

	set_map_timezone (etd, map_zone);
}

/**
 * e_timezone_dialog_reparent:
 * @etd: #ETimezoneDialog.
 * @new_parent: The new parent widget.
 *
 * Takes the internal widgets out of the dialog and put them into @new_parent
 */
void
e_timezone_dialog_reparent (ETimezoneDialog *etd,
			    GtkWidget *new_parent)
{
	ETimezoneDialogPrivate *priv;

	priv = etd->priv;

	gtk_widget_reparent (priv->table, new_parent);
}

static void
map_destroy_cb(gpointer data, GObject *where_object_was)
{
	
	ETimezoneDialogPrivate *priv = data;
	if (priv->timeout_id) {
		g_source_remove(priv->timeout_id);
		priv->timeout_id = 0;
	}
	return;
}
