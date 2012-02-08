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

#ifndef EM_PART_H
#define EM_PART_H

#include <camel/camel.h>
#include <glib-object.h>

#include <em-format/em-format.h>

/* Standard GObject macros */
#define EM_TYPE_PART \
	(em_part_get_type ())
#define EM_PART(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EM_TYPE_PART, EMPart))
#define EM_PART_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EM_TYPE_PART, EMPartClass))
#define EM_IS_PART(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EM_TYPE_PART))
#define EM_IS_PART_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EM_TYPE_PART))
#define EM_PART_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EM_TYPE_PART, EMPartClass))

G_BEGIN_DECLS

typedef struct _EMPart EMPart;
typedef struct _EMPartClass EMPartClass;
typedef struct _EMPartPrivate EMPartPrivate;


typedef void		(*EMPartWriteFunc)	(EMFormat *emf,
						 EMPart *emp,
						 CamelStream *stream,
						 EMFormatWriterInfo *info,
						 GCancellable *cancellable);
typedef GtkWidget*	(*EMPartWidgetFunc)	(EMFormat *emf,
						 EMPart *emp,
						 GCancellable *cancellable);


struct _EMPart {
        GObject parent;
        EMPartPrivate *priv;
};

struct _EMPartClass {
        GObjectClass parent_class;

        GMutex *mutex;
};

EMPart*			em_part_new      	(EMFormat *emf,
                                                 CamelMimePart *part,
                                                 const gchar *uri,
                                                 EMPartWriteFunc write_func);

GType                   em_part_get_type ();

void                    em_part_set_formatter	(EMPart *emp,
                                                 EMFormat *emf);

EMFormat*               em_part_get_formatter	(EMPart *emp);

void                    em_part_set_mime_part	(EMPart *emp,
                                                 CamelMimePart *part);
CamelMimePart*          em_part_get_mime_part	(EMPart * emp);

void                    em_part_set_write_func	(EMPart *emp,
                                                 EMPartWriteFunc write_func);
EMPartWriteFunc		em_part_get_write_func	(EMPart *emp);

void                    em_part_set_widget_func	(EMPart *emp,
                                                 EMPartWidgetFunc widget_func);
EMPartWidgetFunc	em_part_get_widget_func	(EMPart *emp);

void                    em_part_set_uri  	(EMPart *emp,
                                                 const gchar *uri);
gchar*                  em_part_get_uri  	(EMPart *emp);

void                    em_part_set_cid  	(EMPart *emp,
                                                 const gchar *cid);
gchar*                  em_part_get_cid  	(EMPart *emp);

void			em_part_set_mime_type
						(EMPart *emp,
						 const gchar *mime_type);
gchar*			em_part_get_mime_type	(EMPart *emp);

void                    em_part_set_validity_type
						(EMPart *emp,
                                                 guint32 validity_type);
guint32                 em_part_get_validity_type
                                                (EMPart *emp);

void                    em_part_set_validity	(EMPart *emp,
                                                 CamelCipherValidity *validity);
CamelCipherValidity*    em_part_get_validity	(EMPart *emp);

void                    em_part_set_validity_parent
                                                (EMPart *emp,
                                                 CamelCipherValidity *validity);
CamelCipherValidity*    em_part_get_validity_parent
                                                (EMPart *emp);

void                    em_part_set_is_attachment
                                                (EMPart *emp,
                                                 gboolean is_attachment);
gboolean                em_part_get_is_attachment
                                                (EMPart *emp);

GtkWidget*              em_part_get_widget	(EMPart *emp,
                                                 GCancellable *cancellable);

void                    em_part_write		(EMPart *emp,
                                                 CamelStream *stream,
                                                 EMFormatWriterInfo *info,
                                                 GCancellable *cancellable);

void                    em_part_mutex_lock	(EMPart *emp);

void                    em_part_mutex_unlock    (EMPart *emp);


G_END_DECLS

#endif /* EM_PART_H */