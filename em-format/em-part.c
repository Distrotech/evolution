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

#include "em-part.h"
#include "em-format.h"

G_DEFINE_TYPE (EMPart, em_part, G_TYPE_OBJECT);

struct _EMPartPrivate {

	CamelMimePart *part;

	EMFormat *formatter;
	EMPartWriteFunc write_func;
	EMPartWidgetFunc widget_func;

	gchar *uri;
	gchar *cid;
	gchar *mime_type;

	/* EM_FORMAT_VALIDITY_* flags */
	guint32 validity_type;
	CamelCipherValidity *validity;
	CamelCipherValidity *validity_parent;

	gboolean is_attachment;
};

static void
em_part_finalize (GObject *object)
{
	EMPartPrivate *priv = EM_PART (object)->priv;

	em_part_mutex_lock (EM_PART (object));

	if (priv->cid) {
		g_free (priv->cid);
		priv->cid = NULL;
	}

	if (priv->formatter) {
		g_object_unref (priv->formatter);
		priv->formatter = NULL;
	}

	if (priv->mime_type) {
		g_free (priv->mime_type);
		priv->mime_type = NULL;
	}

	if (priv->part) {
		g_object_unref (priv->part);
		priv->part = NULL;
	}

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->validity) {
		camel_cipher_validity_free (priv->validity);
		priv->validity = NULL;
	}

	if (priv->validity_parent) {
		camel_cipher_validity_free (priv->validity_parent);
		priv->validity_parent = NULL;
	}

	em_part_mutex_unlock (EM_PART (object));
}

static void
em_part_dispose (GObject *object)
{
	EMPartClass *klass;

	klass = EM_PART_GET_CLASS (EM_PART (object));

	/* Make sure the mutex is unlocked */
	g_mutex_lock (klass->mutex);
	g_mutex_unlock (klass->mutex);

	g_mutex_free (klass->mutex);
}

static void
em_part_class_init (EMPartClass *klass)
{
	GObjectClass *object_class;

	klass->mutex = g_mutex_new ();

	g_type_class_add_private (klass, sizeof (EMPartPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = em_part_dispose;
	object_class->finalize = em_part_finalize;

}

static void
em_part_init (EMPart *emp)
{
	emp->priv = G_TYPE_INSTANCE_GET_PRIVATE (emp,
			EM_TYPE_PART, EMPartPrivate);

	emp->priv->cid = NULL;
	emp->priv->formatter = NULL;
	emp->priv->is_attachment = FALSE;
	emp->priv->mime_type = NULL;
	emp->priv->part = NULL;
	emp->priv->uri = NULL;
	emp->priv->validity = NULL;
	emp->priv->validity_parent = NULL;
	emp->priv->validity_type = 0;
	emp->priv->widget_func = NULL;
	emp->priv->write_func = NULL;
}

/**
 * em_part_new:
 * @emf: an #EMFormat
 * @part: a #CamelMimePart which this objects wraps
 * @uri: an URI of the object within hierarchy of mime parts
 * @write_func: an #EMPartWriteFunc that will write parsed content of the
 *              @part to a stream, or %NULL.
 *
 * Constructs a new #EMPart.
 *
 * Returns: A new #EMPart, or %NULL on failure.
 */
EMPart *
em_part_new (EMFormat *emf,
	     CamelMimePart *part,
	     const gchar *uri,
	     EMPartWriteFunc write_func)
{
	EMPart *emp;

	g_return_val_if_fail (EM_IS_FORMAT (emf), NULL);
	g_return_val_if_fail ((part == NULL) || CAMEL_IS_MIME_PART (part), NULL);
	g_return_val_if_fail (uri && *uri, NULL);

	emp = EM_PART (g_object_new (EM_TYPE_PART, NULL));

	em_part_set_mime_part (emp, part);
	em_part_set_formatter (emp, emf);
	em_part_set_uri (emp, uri);

	if (write_func)
		em_part_set_write_func (emp, write_func);

	return emp;
}

void
em_part_set_formatter (EMPart *emp,
		       EMFormat *emf)
{
	g_return_if_fail (EM_IS_PART (emp));
	g_return_if_fail (EM_IS_FORMAT (emf));

	em_part_mutex_lock (emp);

	g_object_ref (emf);

	if (emp->priv->formatter)
		g_object_unref (emp->priv->formatter);

	emp->priv->formatter = emf;

	em_part_mutex_unlock (emp);
}

/**
 * em_part_get_formatter:
 *
 * @emp: an #EMPart
 *
 * Returns: An #EMFormat that has to ne unreferenced when no longer needed.
 */
EMFormat *
em_part_get_formatter (EMPart *emp)
{
	EMFormat *formatter;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);

	if (emp->priv->formatter)
		formatter = g_object_ref (emp->priv->formatter);

	em_part_mutex_unlock (emp);

	return formatter;
}


/**
 * em_part_set_mime_part:
 *
 * @emp: an #EMPart
 * @widget_func: a #CamelMimePart, or %NULL to unset.
 */
void
em_part_set_mime_part (EMPart *emp,
		       CamelMimePart *part)
{
	g_return_if_fail (EM_IS_PART (emp));
	g_return_if_fail ((part == NULL) || CAMEL_IS_MIME_PART (part));

	em_part_mutex_lock (emp);

	if (part)
		g_object_ref (part);

	if (emp->priv->part)
		g_object_unref (emp->priv->part);

	emp->priv->part = part;

	em_part_mutex_unlock (emp);
}

/**
 * em_part_get_mime_part:
 *
 * @emp: an #EMPart
 *
 * Returns: A #CamelMimePart that has to be unreferenced when no longer needed.
 */
CamelMimePart *
em_part_get_mime_part (EMPart *emp)
{
	CamelMimePart *part;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);

	if (emp->priv->part)
		part = g_object_ref (emp->priv->part);

	em_part_mutex_unlock (emp);

	return part;
}

/**
 * em_part_set_write_func:
 *
 * @emp: an #EMPart
 * @write_func: an #EMPartWriteFunc, or %NULL to unset.
 */
void
em_part_set_write_func (EMPart *emp,
			EMPartWriteFunc write_func)
{
	g_return_if_fail (EM_IS_PART (emp));

	em_part_mutex_lock (emp);
	emp->priv->write_func = write_func;
	em_part_mutex_unlock (emp);
}

EMPartWriteFunc
em_part_get_write_func (EMPart *emp)
{
	EMPartWriteFunc write_func;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);
	write_func = emp->priv->write_func;
	em_part_mutex_unlock (emp);

	return write_func;
}

/**
 * em_part_set_widget_func:
 *
 * @emp: an #EMPart
 * @widget_func: an #EMPartWidgetFunc, or %NULL to unset.
 */
void
em_part_set_widget_func (EMPart *emp,
			 EMPartWidgetFunc widget_func)
{
	g_return_if_fail (EM_IS_PART (emp));

	em_part_mutex_lock (emp);
	emp->priv->widget_func = widget_func;
	em_part_mutex_unlock (emp);
}

EMPartWidgetFunc
em_part_get_widget_func (EMPart *emp)
{
	EMPartWidgetFunc widget_func;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);
	widget_func = emp->priv->widget_func;
	em_part_mutex_unlock (emp);

	return widget_func;
}

void
em_part_set_uri (EMPart *emp,
		 const gchar *uri)
{
	g_return_if_fail (EM_IS_PART (emp));
	g_return_if_fail (uri && *uri);

	em_part_mutex_lock (emp);

	if (emp->priv->uri)
		g_free (emp->priv->uri);

	emp->priv->uri = g_strdup (uri);

	em_part_mutex_unlock (emp);
}

gchar *
em_part_get_uri (EMPart *emp)
{
	gchar *uri = NULL;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);

	if (emp->priv->uri)
		uri = g_strdup (emp->priv->uri);

	em_part_mutex_unlock (emp);

	return uri;
}

/**
 * em_part_set_cid:
 *
 * @emp: an #EMPart
 * @cid: part content ID, or %NULL to unset
 */
void
em_part_set_cid (EMPart *emp,
		 const gchar *cid)
{
	g_return_if_fail (EM_IS_PART (emp));

	em_part_mutex_lock (emp);

	if (emp->priv->cid)
		g_free (emp->priv->cid);

	if (cid)
		emp->priv->cid = g_strdup (cid);
	else
		emp->priv->cid = NULL;

	em_part_mutex_unlock (emp);
}

gchar *
em_part_get_cid (EMPart *emp)
{
	gchar *cid = NULL;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);

	if (cid)
		cid = g_strdup (emp->priv->cid);

	em_part_mutex_unlock (emp);

	return cid;
}

void
em_part_set_mime_type (EMPart *emp,
		       const gchar *mime_type)
{
	g_return_if_fail (EM_IS_PART (emp));
	g_return_if_fail (mime_type && *mime_type);

	em_part_mutex_lock (emp);

	if (emp->priv->mime_type)
		g_free (emp->priv->mime_type);

	emp->priv->mime_type = g_strdup (mime_type);

	em_part_mutex_unlock (emp);
}

gchar *
em_part_get_mime_type (EMPart *emp)
{
	gchar *mime_type = NULL;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);

	if (mime_type)
		mime_type = g_strdup (emp->priv->mime_type);

	em_part_mutex_unlock (emp);

	return mime_type;
}



void
em_part_set_validity_type (EMPart *emp,
			   guint32 validity_type)
{
	g_return_if_fail (EM_IS_PART (emp));

	em_part_mutex_lock (emp);
	emp->priv->validity_type = validity_type;
	em_part_mutex_unlock (emp);
}

guint32
em_part_get_validity_type (EMPart *emp)
{
	guint32 validity_type;

	g_return_val_if_fail (EM_IS_PART (emp), 0);

	em_part_mutex_lock (emp);
	validity_type = emp->priv->validity_type;
	em_part_mutex_unlock (emp);

	return validity_type;
}

/**
 * em_part_set_validity:
 *
 * @emp: an #EMPart
 * @validity: a #CamelCipherValidity object, or %NULL to unset
 */
void
em_part_set_validity (EMPart *emp,
		      CamelCipherValidity *validity)
{
	g_return_if_fail (EM_IS_PART (emp));

	em_part_mutex_lock (emp);

	if (emp->priv->validity)
		camel_cipher_validity_free (emp->priv->validity);

	if (validity)
		emp->priv->validity = camel_cipher_validity_clone (validity);
	else
		emp->priv->validity = NULL;

	em_part_mutex_unlock (emp);
}

/**
 * em_part_get_validity:
 *
 * @emp: an #EMPart
 *
 * Returns: A copy of #CamelCipherValidity. Must be freed when no longer needed.
 */
CamelCipherValidity *
em_part_get_validity (EMPart *emp)
{
	CamelCipherValidity *validity = NULL;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);

	if (emp->priv->validity)
		validity = camel_cipher_validity_clone (emp->priv->validity);

	em_part_mutex_unlock (emp);

	return validity;
}

/**
 * em_part_set_validity_parent:
 *
 * @emp: an #EMPart
 * @validity: a #CamelCipherValidity object, or %NULL to unset
 */
void
em_part_set_validity_parent (EMPart *emp,
			     CamelCipherValidity *validity)
{
	g_return_if_fail (EM_IS_PART (emp));

	em_part_mutex_lock (emp);

	if (emp->priv->validity_parent)
		camel_cipher_validity_free (emp->priv->validity_parent);

	if (validity)
		emp->priv->validity_parent = camel_cipher_validity_clone (validity);
	else
		emp->priv->validity_parent = NULL;

	em_part_mutex_unlock (emp);
}

/**
 * em_part_get_validity_parent:
 *
 * @emp: an #EMPart
 *
 * Returns: A copy of #CamelCipherValidity. Must be freed when no longer needed.
 */
CamelCipherValidity *
em_part_get_validity_parent (EMPart *emp)
{
	CamelCipherValidity *validity = NULL;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);

	if (emp->priv->validity_parent)
		validity = camel_cipher_validity_clone (emp->priv->validity_parent);

	em_part_mutex_unlock (emp);

	return validity;
}

void
em_part_set_is_attachment (EMPart *emp,
			   gboolean is_attachment)
{
	g_return_if_fail (EM_IS_PART (emp));

	em_part_mutex_lock (emp);
	emp->priv->is_attachment = is_attachment;
	em_part_mutex_unlock (emp);
}

gboolean
em_part_get_is_attachment (EMPart *emp)
{
	gboolean is_attachment;

	g_return_val_if_fail (EM_IS_PART (emp), FALSE);

	em_part_mutex_lock (emp);
	is_attachment = emp->priv->is_attachment;
	em_part_mutex_unlock (emp);

	return is_attachment;
}

/**
 * em_part_get_widget:
 *
 * @emp: an #EMPart
 * @cancellable: optional #GCancellable object, or %NULL
 *
 * Calls assigned #EMPartWidgetFunc to get GtkWidget
 * representing content of the mime part.
 *
 * Returns: a #GtkWidget that has to be unreferenced when no longer needed.
 */
GtkWidget *
em_part_get_widget (EMPart *emp,
		    GCancellable *cancellable)
{
	EMPartWidgetFunc widget_func;
	EMFormat *formatter;
	GtkWidget *widget;

	g_return_val_if_fail (EM_IS_PART (emp), NULL);

	em_part_mutex_lock (emp);
	widget_func = emp->priv->widget_func;
	formatter = g_object_ref (emp->priv->formatter);
	em_part_mutex_unlock (emp);

	if (!widget_func)
		return NULL;

	g_object_ref (emp);
	widget = widget_func (formatter, emp, cancellable);
	g_object_unref (emp);

	return widget;
}

/**
 * em_part_write:
 *
 * @emp: an #EMPart
 * @stream: a #CamelStream to which the data should be written
 * @info: additional information for write function, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 *
 * Calls assigned #EMPartWriteFunc to write parsed
 * content of mime part to the @stream.
 */
void
em_part_write (EMPart *emp,
	       CamelStream *stream,
	       EMFormatWriterInfo *info,
	       GCancellable *cancellable)
{
	EMPartWriteFunc write_func;
	EMFormat *formatter;
	gchar *mime_type = NULL;

	g_return_if_fail (EM_IS_PART (emp));
	g_return_if_fail (CAMEL_IS_STREAM (stream));

	em_part_mutex_lock (emp);
	write_func = emp->priv->write_func;
	formatter = g_object_ref (emp->priv->formatter);

	if (emp->priv->mime_type)
		mime_type = g_strdup (emp->priv->mime_type);

	em_part_mutex_unlock (emp);

	g_object_ref (emp);

	if (info->mode == EM_FORMAT_WRITE_MODE_SOURCE) {
		const EMFormatHandler *handler;
		handler = em_format_find_handler (formatter, "x-evolution/message/source");
		handler->write_func (formatter, emp, stream, info, cancellable);

	} else {

		if (write_func) {
			write_func (formatter, emp, stream, info, cancellable);
		} else {
			const EMFormatHandler *handler;

			if (!mime_type)
				mime_type = g_strdup ("plain/text");

			handler = em_format_find_handler (formatter, mime_type);

			if (handler && handler->write_func) {
				handler->write_func (formatter, emp,
				     stream, info, cancellable);
			}
		}
	}

	g_object_unref (emp);
	g_object_unref (formatter);

	if (mime_type)
		g_free (mime_type);
}

void
em_part_mutex_lock (EMPart *emp)
{
        g_return_if_fail (EM_IS_PART (emp));

        g_mutex_lock (EM_PART_GET_CLASS (emp)->mutex);
}

void em_part_mutex_unlock (EMPart *emp)
{
        g_return_if_fail (EM_IS_PART (emp));

        g_mutex_unlock (EM_PART_GET_CLASS (emp)->mutex);
}
