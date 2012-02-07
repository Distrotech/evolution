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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "em-part-audio.h"

G_DEFINE_TYPE (EMPartAudio, em_part_audio, EM_TYPE_PART);

struct _EMPartAudioPrivate {

	gchar *filename;

	GstState target_state;
	GstElement *playbin;
	gulong bus_id;


	GtkWidget *play_button;
	GtkWidget *pause_button;
	GtkWidget *stop_button;

	GMutex *mutex;
};

static void
em_part_audio_finalize (GObject *object)
{
	EMPartAudioPrivate *priv = EM_PART_AUDIO (object)->priv;

	g_mutex_lock (priv->mutex);

	if (priv->filename) {
		g_free (priv->filename);
		priv->filename = NULL;
	}

	if (priv->playbin) {
		g_object_unref (priv->playbin);
		priv->playbin = NULL;
	}

	if (priv->play_button) {
		g_object_unref (priv->play_button);
		priv->play_button = NULL;
	}

	if (priv->pause_button) {
		g_object_unref (priv->pause_button);
		priv->pause_button = NULL;
	}

	if (priv->stop_button) {
		g_object_unref (priv->stop_button);
		priv->stop_button = NULL;
	}

	g_mutex_unlock (priv->mutex);
	g_mutex_free (priv->mutex);
}

static void
em_part_audio_class_init (EMPartAudioClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (EMPartAudioPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = em_part_audio_finalize;
}

static void
em_part_audio_init (EMPartAudio *empa)
{
	empa->priv = G_TYPE_INSTANCE_GET_PRIVATE (empa,
			EM_TYPE_PART_AUDIO, EMPartAudioPrivate);
	
	empa->priv->mutex = g_mutex_new ();
	empa->priv->filename = NULL;
	empa->priv->pause_button = NULL;
	empa->priv->play_button = NULL;
	empa->priv->stop_button = NULL;
	empa->priv->playbin = NULL;
	empa->priv->target_state = GST_STATE_NULL;
	empa->priv->bus_id = 0;
}

EMPart *
em_part_audio_new (EMFormat *emf,
		   CamelMimePart *part,
		   const gchar *uri,
		   EMPartWidgetFunc widget_func)
{
	EMPart *emp;
	
	g_return_val_if_fail (EM_IS_FORMAT (emf), NULL);
	g_return_val_if_fail ((part == NULL) || CAMEL_IS_MIME_PART (part), NULL);
	g_return_val_if_fail (uri && *uri, NULL);
	
	emp = EM_PART (g_object_new (EM_TYPE_PART_AUDIO, NULL));
	em_part_set_mime_part (emp, part);
	em_part_set_formatter (emp, emf);
	em_part_set_uri (emp, uri);
	
	if (widget_func)
		em_part_set_widget_func (emp, widget_func);
	
	return emp;
}

void
em_part_audio_set_filename (EMPartAudio *empa,
			    const gchar *filename)
{
	g_return_if_fail (EM_IS_PART_AUDIO (empa));

	g_mutex_lock (empa->priv->mutex);
	if (empa->priv->filename)
		g_free (empa->priv->filename);

	if (filename)
		empa->priv->filename = g_strdup (filename);
	else
		empa->priv->filename = NULL;

	g_mutex_unlock (empa->priv->mutex);
}

gchar*
em_part_audio_get_filename (EMPartAudio *empa)
{
	gchar *filename = NULL;

	g_return_val_if_fail (EM_IS_PART_AUDIO (empa), NULL);

	g_mutex_lock (empa->priv->mutex);
	if (empa->priv->filename)
		filename = g_strdup (empa->priv->filename);
	g_mutex_unlock (empa->priv->mutex);

	return filename;
}

void em_part_audio_set_playbin (EMPartAudio *empa,
				GstElement *playbin)
{
	g_return_if_fail (EM_IS_PART_AUDIO (empa));
	g_return_if_fail (playbin == NULL || GST_IS_ELEMENT (playbin));

	g_mutex_lock (empa->priv->mutex);

	if (playbin)
		g_object_ref (playbin);

	if (empa->priv->playbin)
		g_object_unref (empa->priv->playbin);

	empa->priv->playbin = playbin;

	g_mutex_unlock (empa->priv->mutex);
}

GstElement*
em_part_audio_get_playbin (EMPartAudio *empa)
{
	GstElement *element = NULL;

	g_return_val_if_fail (EM_IS_PART_AUDIO (empa), NULL);

	g_mutex_lock (empa->priv->mutex);
	if (empa->priv->playbin)
		element = g_object_ref (empa->priv->playbin);
	g_mutex_unlock (empa->priv->mutex);

	return element;	
}

void em_part_audio_set_bus_id (EMPartAudio *empa,
			       gulong bus_id)
{
	g_return_if_fail (EM_IS_PART_AUDIO (empa));

	g_mutex_lock (empa->priv->mutex);
	empa->priv->bus_id = bus_id;
	g_mutex_unlock (empa->priv->mutex);
}

gulong
em_part_audio_get_bus_id (EMPartAudio *empa)
{
	gulong bus_id = 0;

	g_return_val_if_fail (EM_IS_PART_AUDIO (empa), 0);

	g_mutex_lock (empa->priv->mutex);
	bus_id = empa->priv->bus_id;
	g_mutex_unlock (empa->priv->mutex);

	return bus_id;
}

void
em_part_audio_set_target_state (EMPartAudio *empa,
				GstState state)
{
	g_return_if_fail (EM_IS_PART_AUDIO (empa));

	g_mutex_lock (empa->priv->mutex);
	empa->priv->target_state = state;
	g_mutex_unlock (empa->priv->mutex);
}

GstState
em_part_audio_get_target_state (EMPartAudio *empa)
{
	GstState state;

	g_return_val_if_fail (EM_IS_PART_AUDIO (empa), GST_STATE_NULL);

	g_mutex_lock (empa->priv->mutex);
	state = empa->priv->target_state;
	g_mutex_unlock (empa->priv->mutex);

	return state;	
}

void
em_part_audio_set_play_button (EMPartAudio *empa,
			       GtkWidget *button)
{
	g_return_if_fail (EM_IS_PART_AUDIO (empa));
	g_return_if_fail (button == NULL || GTK_IS_WIDGET (button));

	g_mutex_lock (empa->priv->mutex);

	if (button)
		g_object_ref (button);

	if (empa->priv->play_button)
		g_object_unref (empa->priv->play_button);

	empa->priv->play_button = button;

	g_mutex_unlock (empa->priv->mutex);
}

GtkWidget*
em_part_audio_get_play_button (EMPartAudio *empa)
{
	GtkWidget *widget = NULL;

	g_return_val_if_fail (EM_IS_PART_AUDIO (empa), NULL);

	g_mutex_lock (empa->priv->mutex);
	if (empa->priv->play_button)
		widget = g_object_ref (empa->priv->play_button);
	g_mutex_unlock (empa->priv->mutex);

	return widget;
}

void
em_part_audio_set_pause_button (EMPartAudio *empa,
				GtkWidget *button)
{
	g_return_if_fail (EM_IS_PART_AUDIO (empa));
	g_return_if_fail (button == NULL || GTK_IS_WIDGET (button));

	g_mutex_lock (empa->priv->mutex);

	if (button)
		g_object_ref (button);

	if (empa->priv->pause_button)
		g_object_unref (empa->priv->pause_button);

	empa->priv->pause_button = button;

	g_mutex_unlock (empa->priv->mutex);
}

GtkWidget*
em_part_audio_get_pause_button (EMPartAudio *empa)
{
	GtkWidget *widget = NULL;

	g_return_val_if_fail (EM_IS_PART_AUDIO (empa), NULL);

	g_mutex_lock (empa->priv->mutex);
	if (empa->priv->pause_button)
		widget = g_object_ref (empa->priv->pause_button);
	g_mutex_unlock (empa->priv->mutex);

	return widget;
}

void
em_part_audio_set_stop_button (EMPartAudio *empa,
			       GtkWidget *button)
{
	g_return_if_fail (EM_IS_PART_AUDIO (empa));
	g_return_if_fail (button == NULL || GTK_IS_WIDGET (button));

	g_mutex_lock (empa->priv->mutex);

	if (button)
		g_object_ref (button);

	if (empa->priv->stop_button)
		g_object_unref (empa->priv->stop_button);

	empa->priv->stop_button = button;

	g_mutex_unlock (empa->priv->mutex);
}

GtkWidget*
em_part_audio_get_stop_button (EMPartAudio *empa)
{
	GtkWidget *widget;

	g_return_val_if_fail (EM_IS_PART_AUDIO (empa), NULL);

	g_mutex_lock (empa->priv->mutex);
	if (empa->priv->stop_button)
		widget = g_object_ref (empa->priv->stop_button);
	g_mutex_unlock (empa->priv->stop_button);

	return widget;
}
