/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * Author :
 *  Damon Chaplin <damon@ximian.com>
 *
 * Copyright 2001, Ximian, Inc.
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
 * ETimezoneEntry - a field for setting a timezone. It shows the timezone in
 * a GtkEntry with a '...' button beside it which shows a dialog for changing
 * the timezone. The dialog contains a map of the world with a point for each
 * timezone, and an option menu as an alternative way of selecting the
 * timezone.
 */

#include <config.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkentry.h>
#include <gtk/gtksignal.h>
#include <gnome.h>
#include <gal/util/e-util.h>
#include <gal/widgets/e-unicode.h>
#include <widgets/e-timezone-dialog/e-timezone-dialog.h>
#include "e-timezone-entry.h"

/* The timezone icon for the button. */
#include "art/timezone-16.xpm"

struct _ETimezoneEntryPrivate {
	/* The current timezone, set in e_timezone_entry_set_timezone()
	   or from the timezone dialog. Note that we don't copy it or
	   use a ref count - we assume it is never destroyed for the
	   lifetime of this widget. */
	icaltimezone *zone;

	/* This can be set to the default timezone. If the current timezone
	   setting in the ETimezoneEntry matches this, then the entry field
	   is hidden. This makes the user interface simpler. */
	icaltimezone *default_zone;

	GtkWidget *entry;
	GtkWidget *button;
};


enum {
  CHANGED,
  LAST_SIGNAL
};


static void e_timezone_entry_class_init	(ETimezoneEntryClass	*class);
static void e_timezone_entry_init	(ETimezoneEntry	*tentry);
static void e_timezone_entry_destroy	(GtkObject	*object);

static void on_entry_changed		(GtkEntry	*entry,
					 ETimezoneEntry *tentry);
static void on_button_clicked		(GtkWidget	*widget,
					 ETimezoneEntry	*tentry);

static void e_timezone_entry_set_entry  (ETimezoneEntry *tentry);


static GtkHBoxClass *parent_class;
static guint timezone_entry_signals[LAST_SIGNAL] = { 0 };

E_MAKE_TYPE (e_timezone_entry, "ETimezoneEntry", ETimezoneEntry,
	     e_timezone_entry_class_init, e_timezone_entry_init, GTK_TYPE_HBOX);

static void
e_timezone_entry_class_init		(ETimezoneEntryClass	*class)
{
	GtkObjectClass *object_class = (GtkObjectClass *) class;

	object_class = (GtkObjectClass*) class;

	parent_class = g_type_class_peek_parent (class);

	timezone_entry_signals[CHANGED] =
		gtk_signal_new ("changed",
				GTK_RUN_LAST,
				G_TYPE_FROM_CLASS (object_class),
				GTK_SIGNAL_OFFSET (ETimezoneEntryClass,
						   changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);


	object_class->destroy		= e_timezone_entry_destroy;

	class->changed = NULL;
}


static void
e_timezone_entry_init		(ETimezoneEntry	*tentry)
{
	ETimezoneEntryPrivate *priv;
	GdkColormap *colormap;
	GdkPixmap *timezone_icon;
	GdkBitmap *timezone_mask;
	GtkWidget *pixmap;

	tentry->priv = priv = g_new0 (ETimezoneEntryPrivate, 1);

	priv->zone = NULL;
	priv->default_zone = NULL;

	priv->entry  = gtk_entry_new ();
	gtk_entry_set_editable (GTK_ENTRY (priv->entry), FALSE);
	/*gtk_widget_set_usize (priv->date_entry, 90, 0);*/
	gtk_box_pack_start (GTK_BOX (tentry), priv->entry, TRUE, TRUE, 6);
	gtk_widget_show (priv->entry);
	g_signal_connect (priv->entry, "changed", G_CALLBACK (on_entry_changed), tentry);
	
	priv->button = gtk_button_new ();
	g_signal_connect (priv->button, "clicked", G_CALLBACK (on_button_clicked), tentry);
	gtk_box_pack_start (GTK_BOX (tentry), priv->button, FALSE, FALSE, 6);
	gtk_widget_show (priv->button);

	colormap = gtk_widget_get_colormap (priv->button);
	timezone_icon = gdk_pixmap_colormap_create_from_xpm_d (NULL, colormap, &timezone_mask, NULL, timezone_16_xpm);

	pixmap = gtk_pixmap_new (timezone_icon, timezone_mask);
	gtk_container_add (GTK_CONTAINER (priv->button), pixmap);
	gtk_widget_show (pixmap);
}


/**
 * e_timezone_entry_new:
 *
 * Description: Creates a new #ETimezoneEntry widget which can be used
 * to provide an easy to use way for entering dates and times.
 * 
 * Returns: a new #ETimezoneEntry widget.
 */
GtkWidget *
e_timezone_entry_new			(void)
{
	ETimezoneEntry *tentry;

	tentry = g_object_new (e_timezone_entry_get_type (), NULL);

	return GTK_WIDGET (tentry);
}


static void
e_timezone_entry_destroy		(GtkObject	*object)
{
	ETimezoneEntry *tentry;
	ETimezoneEntryPrivate *priv;

	g_return_if_fail (E_IS_TIMEZONE_ENTRY (object));

	tentry = E_TIMEZONE_ENTRY (object);
	priv = tentry->priv;

	g_free (tentry->priv);
	tentry->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* The arrow button beside the date field has been clicked, so we show the
   popup with the ECalendar in. */
static void
on_button_clicked		(GtkWidget	*widget,
				 ETimezoneEntry	*tentry)
{
	ETimezoneEntryPrivate *priv;
	ETimezoneDialog *timezone_dialog;
	GtkWidget *dialog;

	priv = tentry->priv;

	timezone_dialog = e_timezone_dialog_new ();

	e_timezone_dialog_set_timezone (timezone_dialog, priv->zone);

	dialog = e_timezone_dialog_get_toplevel (timezone_dialog);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		priv->zone = e_timezone_dialog_get_timezone (timezone_dialog);
		e_timezone_entry_set_entry (tentry);
	}

	g_object_unref (timezone_dialog);
}


static void
on_entry_changed			(GtkEntry	*entry,
					 ETimezoneEntry *tentry)
{
	gtk_signal_emit (GTK_OBJECT (tentry), timezone_entry_signals[CHANGED]);
}


icaltimezone*
e_timezone_entry_get_timezone		(ETimezoneEntry	*tentry)
{
	ETimezoneEntryPrivate *priv;

	g_return_val_if_fail (E_IS_TIMEZONE_ENTRY (tentry), NULL);

	priv = tentry->priv;

	return priv->zone;
}


void
e_timezone_entry_set_timezone		(ETimezoneEntry	*tentry,
					 icaltimezone	*zone)
{
	ETimezoneEntryPrivate *priv;

	g_return_if_fail (E_IS_TIMEZONE_ENTRY (tentry));

	priv = tentry->priv;

	priv->zone = zone;

	e_timezone_entry_set_entry (tentry);
}


/* Sets the default timezone. If the current timezone matches this, then the
   entry field is hidden. This is useful since most people do not use timezones
   so it makes the user interface simpler. */
void
e_timezone_entry_set_default_timezone	(ETimezoneEntry	*tentry,
					 icaltimezone	*zone)
{
	ETimezoneEntryPrivate *priv;

	g_return_if_fail (E_IS_TIMEZONE_ENTRY (tentry));

	priv = tentry->priv;

	priv->default_zone = zone;

	e_timezone_entry_set_entry (tentry);
}


static void
e_timezone_entry_set_entry (ETimezoneEntry *tentry)
{
	ETimezoneEntryPrivate *priv;
	const char *display_name;
	char *name_buffer;

	priv = tentry->priv;

	if (priv->zone) {
		display_name = icaltimezone_get_display_name (priv->zone);

		/* We check if it is one of our builtin timezone
		   names, in which case we call gettext to translate
		   it. If it isn't a builtin timezone name, we
		   don't. */
		if (icaltimezone_get_builtin_timezone (display_name))
			display_name = _(display_name);
	} else
		display_name = "";

	name_buffer = g_strdup (display_name);

	gtk_entry_set_text (GTK_ENTRY (priv->entry), name_buffer);

	if (!priv->default_zone || (priv->zone != priv->default_zone))
		gtk_widget_show (priv->entry);
	else
		gtk_widget_hide (priv->entry);

	g_free (name_buffer);
}

