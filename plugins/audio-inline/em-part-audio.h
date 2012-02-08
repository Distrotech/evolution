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

#ifndef EM_PART_AUDIO_H
#define EM_PART_AUDIO_H

#include <em-format/em-part.h>
#include <gst/gst.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define EM_TYPE_PART_AUDIO \
	(em_part_audio_get_type ())
#define EM_PART_AUDIO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EM_TYPE_PART_AUDIO, EMPartAudio))
#define EM_PART_AUDIO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EM_TYPE_PART_AUDIO, EMPartAudioClass))
#define EM_IS_PART_AUDIO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EM_TYPE_PART_AUDIO))
#define EM_IS_PART_AUDIO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EM_TYPE_PART_AUDIO))
#define EM_PART_AUDIO_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EM_TYPE_PART_AUDIO, EMPartAudioClass))

G_BEGIN_DECLS

typedef struct _EMPartAudio EMPartAudio;
typedef struct _EMPartAudioClass EMPartAudioClass;
typedef struct _EMPartAudioPrivate EMPartAudioPrivate;


struct _EMPartAudio {
	EMPart parent;
	EMPartAudioPrivate *priv;
};

struct _EMPartAudioClass {
	EMPartClass parent_class;
};

EMPart*			em_part_audio_new      	(EMFormat *emf,
						 CamelMimePart *part,
						 const gchar *uri,
						 EMPartWriteFunc write_func);

GType                   em_part_audio_get_type ();

void			em_part_audio_set_filename
						(EMPartAudio *empa,
						 const gchar *filename);

gchar*			em_part_audio_get_filename
						(EMPartAudio *empa);

void			em_part_audio_set_playbin
						(EMPartAudio *empa,
						 GstElement *playbin);
GstElement*		em_part_audio_get_playbin
						(EMPartAudio *empa);

void			em_part_audio_set_bus_id
						(EMPartAudio *empa,
						 gulong bus_id);
gulong			em_part_audio_get_bus_id
						(EMPartAudio *empa);

void			em_part_audio_set_target_state
						(EMPartAudio *empa,
						 GstState state);
GstState		em_part_audio_get_target_state
						(EMPartAudio *empa);

void			em_part_audio_set_play_button
						(EMPartAudio *empa,
						 GtkWidget *button);
GtkWidget*		em_part_audio_get_play_button
						(EMPartAudio *empa);

void			em_part_audio_set_pause_button
						(EMPartAudio *empa,
						 GtkWidget *button);
GtkWidget*		em_part_audio_get_pause_button
						(EMPartAudio *empa);

void			em_part_audio_set_stop_button
						(EMPartAudio *empa,
						 GtkWidget *button);
GtkWidget*		em_part_audio_get_stop_button
						(EMPartAudio *empa);

G_END_DECLS

#endif /* EM_PART_AUDIO_H */ 
