/*
 * e-mail-part-audio.h
 *
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

#ifndef E_MAIL_PART_AUDIO_H
#define E_MAIL_PART_AUDIO_H

#include <em-format/e-mail-part.h>
#include <gst/gst.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_PART_AUDIO \
	(e_mail_part_audio_get_type ())
#define E_MAIL_PART_AUDIO(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_PART_AUDIO, EMailPartAudio))
#define E_MAIL_PART_AUDIO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_PART_AUDIO, EMailPartAudioClass))
#define E_IS_MAIL_PART_AUDIO(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_PART_AUDIO))
#define E_IS_MAIL_PART_AUDIO_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_PART_AUDIO))
#define E_MAIL_PART_AUDIO_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_PART_AUDIO, EMailPartAudioClass))

G_BEGIN_DECLS

typedef struct _EMailPartAudio EMailPartAudio;
typedef struct _EMailPartAudioClass EMailPartAudioClass;
typedef struct _EMailPartAudioPrivate EMailPartAudioPrivate;

struct _EMailPartAudio {
	EMailPart parent;
	EMailPartAudioPrivate *priv;

	gchar *filename;
	GstElement *playbin;
	gulong      bus_id;
	GstState    target_state;
	GtkWidget  *play_button;
	GtkWidget  *pause_button;
	GtkWidget  *stop_button;
};

struct _EMailPartAudioClass {
	EMailPartClass parent_class;
};

GType		e_mail_part_audio_get_type	(void) G_GNUC_CONST;
void		e_mail_part_audio_type_register	(GTypeModule *type_module);
EMailPart *	e_mail_part_audio_new		(CamelMimePart *mime_part,
						 const gchar *id);

G_END_DECLS

#endif /* E_MAIL_PART_AUDIO_H */

