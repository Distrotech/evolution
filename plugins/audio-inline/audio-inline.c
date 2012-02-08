/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Radek Doulik <rodo@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "em-part-audio.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "e-util/e-mktemp.h"
#include "mail/em-format-hook.h"
#include "mail/em-format-html.h"
#include <gst/gst.h>

#define d(x)

gint e_plugin_lib_enable (EPlugin *ep, gint enable);

gint
e_plugin_lib_enable (EPlugin *ep,
                     gint enable)
{
	return 0;
}

void org_gnome_audio_inline_format (gpointer ep, EMFormatHookTarget *t);

static volatile gint org_gnome_audio_class_id_counter = 0;


static void
org_gnome_audio_inline_pause_clicked (GtkWidget *button,
				      EMPartAudio *empa)
{
	GstElement *playbin = em_part_audio_get_playbin (empa);

	if (playbin) {
		/* pause playing */
		gst_element_set_state (playbin, GST_STATE_PAUSED);
		g_object_unref (playbin);
	}
}

static void
org_gnome_audio_inline_stop_clicked (GtkWidget *button,
				     EMPartAudio *empa)
{
	GstElement *playbin = em_part_audio_get_playbin (empa);

	if (playbin) {
		/* ready to play */
		gst_element_set_state (playbin, GST_STATE_READY);
		em_part_audio_set_target_state (empa, GST_STATE_READY);
		g_object_unref (playbin);
	}
}

static void
org_gnome_audio_inline_set_audiosink (GstElement *playbin)
{
	GstElement *audiosink;

	/* now it's time to get the audio sink */
	audiosink = gst_element_factory_make ("gconfaudiosink", "play_audio");
	if (audiosink == NULL) {
		audiosink = gst_element_factory_make ("autoaudiosink", "play_audio");
	}

	if (audiosink) {
		g_object_set (playbin, "audio-sink", audiosink, NULL);
	}
}

static gboolean
org_gnome_audio_inline_gst_callback (GstBus *bus,
                                     GstMessage *message,
                                     gpointer data)
{
	EMPartAudio *empa = data;
	GstMessageType msg_type;
	GstElement *playbin;

	g_return_val_if_fail (data != NULL, TRUE);

	playbin = em_part_audio_get_playbin (empa);
	g_return_val_if_fail (playbin != NULL, TRUE);

	msg_type = GST_MESSAGE_TYPE (message);

	switch (msg_type) {
		case GST_MESSAGE_ERROR:
			gst_element_set_state (playbin, GST_STATE_NULL);
			break;
		case GST_MESSAGE_EOS:
			gst_element_set_state (playbin, GST_STATE_READY);
			break;
		case GST_MESSAGE_STATE_CHANGED:
			{
				GstState old_state, new_state;
				GtkWidget *button;

				if (GST_MESSAGE_SRC (message) != GST_OBJECT (playbin))
					break;

				gst_message_parse_state_changed (message, &old_state, &new_state, NULL);

				if (old_state == new_state)
					break;

				button = em_part_audio_get_play_button (empa);
				if (button) {
					gtk_widget_set_sensitive (button, 
						new_state <= GST_STATE_PAUSED);
					g_object_unref (button);
				}

				button = em_part_audio_get_pause_button (empa);
				if (button) {
					gtk_widget_set_sensitive (button, 
						new_state > GST_STATE_PAUSED);
					g_object_unref (button);
				}

				button = em_part_audio_get_stop_button (empa);
			      	if (button) {
					gtk_widget_set_sensitive (button,
						new_state >= GST_STATE_PAUSED);
					g_object_unref (button);
				}
			}

			break;
		default:
			break;
	}

	g_object_unref (playbin);

	return TRUE;
}

static void
org_gnome_audio_inline_play_clicked (GtkWidget *button,
				     EMPartAudio *empa)
{
	GstState cur_state;
	GstElement *playbin;
	gchar *filename;

	d(printf ("audio inline formatter: play\n"));

	filename = em_part_audio_get_filename (empa);
	if (!filename) {
		CamelStream *stream;
		CamelMimePart *part;
		CamelDataWrapper *data;
		GError *error = NULL;
		gint argc = 1;
		const gchar *argv [] = { "org_gnome_audio_inline", NULL };

		/* FIXME this is ugly, we should stream this directly to gstreamer */
		
		filename = e_mktemp ("org-gnome-audio-inline-file-XXXXXX");
		em_part_audio_set_filename (empa, filename);

		d(printf ("audio inline formatter: write to temp file %s\n", filename));

		stream = camel_stream_fs_new_with_name (filename, O_RDWR | O_CREAT | O_TRUNC, 0600, NULL);
		part = em_part_get_mime_part (EM_PART (empa));
		data = camel_medium_get_content (CAMEL_MEDIUM (part));
		camel_data_wrapper_decode_to_stream_sync (
			data, stream, NULL, NULL);
		camel_stream_flush (stream, NULL, NULL);
		g_object_unref (stream);
		g_object_unref (part);

		d(printf ("audio inline formatter: init gst playbin\n"));

		if (gst_init_check (&argc, (gchar ***) &argv, &error)) {
			gchar *uri;
			GstBus *bus;

			/* create a disk reader */
			playbin = gst_element_factory_make ("playbin", "playbin");
			if (playbin == NULL) {
				g_printerr ("Failed to create gst_element_factory playbin; check your installation\n");
				return;

			}
			em_part_audio_set_playbin (empa, playbin);

			uri = g_filename_to_uri (filename, NULL, NULL);
			g_object_set (playbin, "uri", uri, NULL);
			g_free (uri);
			org_gnome_audio_inline_set_audiosink (playbin);

			bus = gst_element_get_bus (playbin);
			em_part_audio_set_bus_id (empa, 
				gst_bus_add_watch (bus, org_gnome_audio_inline_gst_callback, empa));

			gst_object_unref (bus);

		} else {
			g_printerr ("GStreamer failed to initialize: %s",error ? error->message : "");
			g_error_free (error);
		}

		g_free (filename);
	} else {
		playbin = em_part_audio_get_playbin (empa);
	}

	gst_element_get_state (playbin, &cur_state, NULL, 0);

	if (cur_state >= GST_STATE_PAUSED) {
		gst_element_set_state (playbin, GST_STATE_READY);
	}

	if (playbin) {
		/* start playing */
		gst_element_set_state (playbin, GST_STATE_PLAYING);
	}

	g_object_unref (playbin);
}

static GtkWidget *
org_gnome_audio_inline_add_button (GtkWidget *box,
                                   const gchar *stock_icon,
                                   GCallback cb,
                                   gpointer data,
                                   gboolean sensitive)
{
	GtkWidget *button;

	button = gtk_button_new_from_stock (stock_icon);
	gtk_widget_set_sensitive (button, sensitive);
	g_signal_connect (button, "clicked", cb, data);

	gtk_widget_show (button);
	gtk_box_pack_end (GTK_BOX (box), button, TRUE, TRUE, 0);

	return button;
}

static GtkWidget*
org_gnome_audio_inline_button_panel (EMFormat *emf,
				     EMPartAudio *empa,
				     GCancellable *cancellable)
{
	GtkWidget *box;
	/* it is OK to call UI functions here, since we are called from UI thread */

	box = gtk_hbutton_box_new ();

        em_part_audio_set_play_button (empa,
                org_gnome_audio_inline_add_button (box, GTK_STOCK_MEDIA_PLAY,
                        G_CALLBACK (org_gnome_audio_inline_play_clicked),
                        empa, TRUE));
        em_part_audio_set_pause_button (empa,
	        org_gnome_audio_inline_add_button (box, GTK_STOCK_MEDIA_PAUSE,
                        G_CALLBACK (org_gnome_audio_inline_pause_clicked),
                        empa, FALSE));
        em_part_audio_set_stop_button (empa,
                org_gnome_audio_inline_add_button (box, GTK_STOCK_MEDIA_STOP,
                        G_CALLBACK (org_gnome_audio_inline_stop_clicked),
                        empa, FALSE));

	g_signal_connect (box, "unrealize",
		G_CALLBACK (org_gnome_audio_inline_stop_clicked), empa);

	gtk_widget_show_all (box);

	return box;
}

void
org_gnome_audio_inline_format (gpointer ep,
                               EMFormatHookTarget *t)
{
        EMPart *emp;
	gchar *classid;

	classid = g_strdup_printf (
		"%s.org-gnome-audio-inline-button-panel-%d",
		t->part_id->str,
		org_gnome_audio_class_id_counter);

	org_gnome_audio_class_id_counter++;

	d(printf ("audio inline formatter: format classid %s\n", classid));

        emp = em_part_audio_new (t->format, t->part, classid, NULL);
        em_part_set_widget_func (emp, (EMPartWidgetFunc) org_gnome_audio_inline_button_panel);

        em_format_add_part_object (t->format, emp);

	g_free (classid);
}
