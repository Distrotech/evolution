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

#include "em-format-html-display-parts.h"

#include <glib.h>

struct _EMPartAttachmentBarPrivate {
	EAttachmentStore *store;
};

G_DEFINE_TYPE (EMPartAttachmentBar, em_part_attachment_bar, EM_TYPE_PART);

static void
em_part_attachment_bar_finalize (GObject *object)
{
	EMPartAttachmentBarPrivate *priv =
		EM_PART_ATTACHMENT_BAR (object)->priv;

	em_part_mutex_lock (EM_PART (object));

	if (priv->store) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	em_part_mutex_unlock (EM_PART (object));
}

static void
em_part_attachment_bar_class_init (EMPartAttachmentBarClass *klass)
{
	GObjectClass *object_class;
	
	g_type_class_add_private (klass, sizeof (EMPartAttachmentBarPrivate));
	
	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = em_part_attachment_bar_finalize;
}


static void
em_part_attachment_bar_init (EMPartAttachmentBar *empab)
{

	empab->priv = G_TYPE_INSTANCE_GET_PRIVATE (empab,
			EM_TYPE_PART_ATTACHMENT_BAR, EMPartAttachmentBarPrivate);

	empab->priv->store = NULL;
}

EMPart*
em_part_attachment_bar_new (EMFormat *emf,
			    CamelMimePart *part,
			    const gchar *uri,
			    EMPartWriteFunc write_func)
{
	EMPart *emp;

	g_return_val_if_fail (EM_IS_FORMAT (emf), NULL);
	g_return_val_if_fail ((part == NULL) || CAMEL_IS_MIME_PART (part), NULL);
	g_return_val_if_fail (uri && *uri, NULL);

	emp = (EMPart *) g_object_new (EM_TYPE_PART_ATTACHMENT_BAR, NULL);
	em_part_set_formatter (emp, emf);
	em_part_set_mime_part (emp, part);
	em_part_set_uri (emp, uri);
	em_part_set_write_func (emp, write_func);

	return emp;
}

void
em_part_attachment_bar_set_store (EMPartAttachmentBar *empab,
				  EAttachmentStore *store)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT_BAR (empab));
	g_return_if_fail ((store == NULL) || E_IS_ATTACHMENT_STORE (store));

	em_part_mutex_lock ((EMPart *) empab);

	if (store)
		g_object_ref (store);

	if (empab->priv->store)
		g_object_unref (empab->priv->store);

	empab->priv->store = store;

	em_part_mutex_unlock ((EMPart *) empab);
}


EAttachmentStore*
em_part_attachment_bar_get_store (EMPartAttachmentBar *empab)
{
	EAttachmentStore *store = NULL;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT_BAR (empab), NULL);

	em_part_mutex_lock ((EMPart *) empab);
	if (empab->priv->store)
		store = g_object_ref (empab->priv->store);
	em_part_mutex_unlock ((EMPart *) empab);

	return store;
}

/******************************************************************************/

struct _EMPartAttachmentPrivate {

	gchar *snoop_mime_type;

	/* The > and v buttons */
	GtkWidget *forward;
	GtkWidget *down;
	gint shown:1;

	EAttachment *attachment;
	gchar *view_part_id;
	gchar *description;

	CamelStream *mstream;

	const EMFormatHandler *handler;
};

G_DEFINE_TYPE (EMPartAttachment, em_part_attachment, EM_TYPE_PART);

static void
em_part_attachment_finalize (GObject *object)
{
	EMPartAttachmentPrivate *priv = EM_PART_ATTACHMENT (object)->priv;

	em_part_mutex_lock (EM_PART (object));

	if (priv->attachment) {
		g_object_unref (priv->attachment);
		priv->attachment = NULL;
	}

	if (priv->description) {
		g_free (priv->description);
		priv->description = NULL;
	}

	if (priv->down) {
		g_object_unref (priv->down);
		priv->down = NULL;
	}

	if (priv->forward) {
		g_object_unref (priv->forward);
		priv->forward = NULL;
	}

	if (priv->mstream) {
		g_object_unref (priv->mstream);
		priv->mstream = NULL;
	}

	if (priv->snoop_mime_type) {
		g_free (priv->snoop_mime_type);
		priv->snoop_mime_type = NULL;
	}

	if (priv->view_part_id) {
		g_free (priv->view_part_id);
		priv->view_part_id = NULL;
	}

	em_part_mutex_unlock (EM_PART (object));
}

static void
em_part_attachment_class_init (EMPartAttachmentClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (EMPartAttachmentPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = em_part_attachment_finalize;
}

static void
em_part_attachment_init (EMPartAttachment *empa)
{
	empa->priv = G_TYPE_INSTANCE_GET_PRIVATE (empa,
			EM_TYPE_PART_ATTACHMENT, EMPartAttachmentPrivate);

	empa->priv->attachment = NULL;
	empa->priv->description = NULL;
	empa->priv->down = NULL;
	empa->priv->forward = NULL;
	empa->priv->mstream = NULL;
	empa->priv->shown = FALSE;
	empa->priv->snoop_mime_type = NULL;
	empa->priv->view_part_id = NULL;
	empa->priv->handler = NULL;
}

EMPart*
em_part_attachment_new (EMFormat *emf,
			CamelMimePart *part,
			const gchar *uri,
			EMPartWriteFunc write_func)
{
	EMPart *emp;

	g_return_val_if_fail (EM_IS_FORMAT (emf), NULL);
	g_return_val_if_fail ((part == NULL) || (CAMEL_IS_MIME_PART (part)), NULL);
	g_return_val_if_fail (uri && *uri, NULL);

	emp = (EMPart *) g_object_new (EM_TYPE_PART_ATTACHMENT, NULL);
	em_part_set_formatter (emp, emf);
	em_part_set_mime_part (emp, part);
	em_part_set_uri (emp, uri);
	em_part_set_write_func (emp, write_func);

	return emp;
}

void
em_part_attachment_set_snoop_mime_type (EMPartAttachment *empa,
					const gchar *snoop_mime_type)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));

	em_part_mutex_lock ((EMPart *) empa);

	if (empa->priv->snoop_mime_type)
		g_free (empa->priv->snoop_mime_type);

	if (snoop_mime_type)
		empa->priv->snoop_mime_type = g_strdup (snoop_mime_type);
	else
		empa->priv->snoop_mime_type = NULL;

	em_part_mutex_unlock ((EMPart *) empa);
}

gchar*
em_part_attachment_get_snoop_mime_part (EMPartAttachment *empa)
{
	gchar *mime_type = NULL;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), NULL);

	em_part_mutex_lock ((EMPart *) empa);
	if (empa->priv->snoop_mime_type)
		mime_type = g_strdup (empa->priv->snoop_mime_type);
	em_part_mutex_unlock ((EMPart *) empa);

	return mime_type;
}

void
em_part_attachment_set_forward_widget (EMPartAttachment *empa,
				       GtkWidget *forward)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));
	g_return_if_fail ((forward == NULL) || GTK_IS_WIDGET (forward));

	em_part_mutex_lock ((EMPart *) empa);
	
	if (forward)
		g_object_ref (forward);

	if (empa->priv->forward)
		g_object_unref (empa->priv->forward);

	if (forward)
		empa->priv->forward = forward;
	else
		empa->priv->forward = NULL;

	em_part_mutex_unlock ((EMPart *) empa);
}

GtkWidget*
em_part_attachment_get_forward_widget (EMPartAttachment *empa)
{
	GtkWidget *widget = NULL;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), NULL);

	em_part_mutex_lock ((EMPart *) empa);
	if (empa->priv->down)
		widget = g_object_ref (empa->priv->down);
	em_part_mutex_unlock ((EMPart *) empa);

	return widget;
}


void 
em_part_attachment_set_down_widget (EMPartAttachment *empa,
				    GtkWidget *down)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));
	g_return_if_fail ((down == NULL) || GTK_IS_WIDGET (down));

	em_part_mutex_lock ((EMPart *) empa);

	if (down)
		g_object_ref (down);

	if (empa->priv->down)
		g_object_unref (empa->priv->down);

	if (down)
		empa->priv->down = down;
	else
		empa->priv->down = NULL;

	em_part_mutex_unlock ((EMPart *) empa);
}

GtkWidget*
em_part_attachment_get_down_widget (EMPartAttachment *empa)
{
	GtkWidget *widget = NULL;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), NULL);

	em_part_mutex_lock ((EMPart *) empa);
	if (empa->priv->down)
		widget = g_object_ref (empa->priv->down);
	em_part_mutex_unlock ((EMPart *) empa);

	return widget;
}

void
em_part_attachment_set_is_shown (EMPartAttachment *empa,
				 gboolean is_shown)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));

	em_part_mutex_lock ((EMPart *) empa);
	empa->priv->shown = is_shown;
	em_part_mutex_unlock ((EMPart *) empa);
}

gboolean
em_part_attachment_get_is_shown (EMPartAttachment *empa)
{
	gboolean is_shown;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), FALSE);

	em_part_mutex_lock ((EMPart *) empa);
	is_shown = empa->priv->shown;
	em_part_mutex_unlock ((EMPart *) empa);

	return is_shown;
}

void
em_part_attachment_set_attachment (EMPartAttachment *empa,
				   EAttachment *attachment)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));
	g_return_if_fail ((attachment == NULL) || E_IS_ATTACHMENT (attachment));

	if (attachment)
		g_object_ref (attachment);

	em_part_mutex_lock ((EMPart *) empa);

	if (empa->priv->attachment)
		g_object_unref (empa->priv->attachment);

	if (attachment)
		empa->priv->attachment = attachment;
	else
		empa->priv->attachment = NULL;

	em_part_mutex_unlock ((EMPart *) empa);
}

EAttachment*
em_part_attachment_get_attachment (EMPartAttachment *empa)
{
	EAttachment *attachment = NULL;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), NULL);

	em_part_mutex_lock ((EMPart *) empa);
	if (empa->priv->attachment)
		attachment = g_object_ref (empa->priv->attachment);
	em_part_mutex_unlock ((EMPart *) empa);

	return attachment;
}

void
em_part_attachment_set_view_part_id (EMPartAttachment *empa,
				     const gchar *view_part_id)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));

	em_part_mutex_lock ((EMPart *) empa);

	if (empa->priv->view_part_id)
		g_free (empa->priv->view_part_id);

	if (view_part_id)
		empa->priv->view_part_id = g_strdup (view_part_id);
	else
		empa->priv->view_part_id = NULL;

	em_part_mutex_unlock ((EMPart *) empa);
}

gchar*
em_part_attachment_get_view_part_id (EMPartAttachment *empa)
{
	gchar *view_part_id = NULL;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), NULL);
	
	em_part_mutex_lock ((EMPart *) empa);
	if (empa->priv->view_part_id)
		view_part_id = g_strdup (view_part_id);
	em_part_mutex_unlock ((EMPart *) empa);

	return view_part_id;	
}

void
em_part_attachment_set_description (EMPartAttachment *empa,
				    const gchar *description)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));

	em_part_mutex_lock ((EMPart *) empa);

	if (empa->priv->description)
		g_free (empa->priv->description);

	if (description)
		empa->priv->description = g_strdup (description);
	else
		empa->priv->description = NULL;

	em_part_mutex_unlock ((EMPart *) empa);
}

gchar*
em_part_attachment_get_description (EMPartAttachment *empa)
{
	gchar *description = NULL;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), NULL);

	em_part_mutex_lock ((EMPart *) empa);
	if (empa->priv->description)
		description = g_strdup (empa->priv->description);
	em_part_mutex_unlock ((EMPart *) empa);

	return description;
}

void 
em_part_attachment_set_mstream (EMPartAttachment *empa,
				CamelStream *mstream)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));
	g_return_if_fail ((mstream == NULL) || CAMEL_IS_STREAM (mstream));

	em_part_mutex_lock ((EMPart *) empa);
	if (mstream)
		g_object_ref (mstream);

	if (empa->priv->mstream)
		g_object_unref (empa->priv->mstream);

	empa->priv->mstream = mstream;
		
	em_part_mutex_unlock ((EMPart *) empa);
}

CamelStream*
em_part_attachment_get_mstream (EMPartAttachment *empa)
{
	CamelStream *stream = NULL;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), NULL);

	em_part_mutex_lock ((EMPart *) empa);
	if (empa->priv->mstream)
		stream = g_object_ref (empa->priv->mstream);
	em_part_mutex_unlock ((EMPart *) empa);

	return stream;
}

void
em_part_attachment_set_handler (EMPartAttachment *empa,
				const EMFormatHandler *handler)
{
	g_return_if_fail (EM_IS_PART_ATTACHMENT (empa));

	em_part_mutex_lock ((EMPart *) empa);
	empa->priv->handler = handler;
	em_part_mutex_unlock ((EMPart *) empa);
}

const EMFormatHandler*
em_part_attachment_get_handler (EMPartAttachment *empa)
{
	const EMFormatHandler *handler;

	g_return_val_if_fail (EM_IS_PART_ATTACHMENT (empa), NULL);

	em_part_mutex_lock ((EMPart *) empa);
	handler = empa->priv->handler;
	em_part_mutex_unlock ((EMPart *) empa);

	return handler;
}



/******************************************************************************/

struct _EMPartSMIMEPrivate {
	gchar *description;
	gint signature;
	GtkWidget *widget;
};

G_DEFINE_TYPE (EMPartSMIME, em_part_smime, EM_TYPE_PART);


static void
em_part_smime_finalize (GObject *object)
{
	EMPartSMIMEPrivate *priv = EM_PART_SMIME (object)->priv;
	
	em_part_mutex_lock (EM_PART (object));
	
	if (priv->description) {
		g_free (priv->description);
		priv->description = NULL;
	}

	if (priv->widget) {
		g_object_unref (priv->widget);
		priv->widget = NULL;
	}
	
	em_part_mutex_lock (EM_PART (object));
}

static void
em_part_smime_class_init (EMPartSMIMEClass *klass)
{
	GObjectClass *object_class;
	
	g_type_class_add_private (klass, sizeof (EMPartSMIMEPrivate));
	
	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = em_part_smime_finalize;
}

static void
em_part_smime_init (EMPartSMIME *emps)
{
	emps->priv = G_TYPE_INSTANCE_GET_PRIVATE (emps,
			EM_TYPE_PART_SMIME, EMPartSMIMEPrivate);
	
	emps->priv->description = NULL;
	emps->priv->signature = 0;
	emps->priv->widget = NULL;
}

EMPart*
em_part_smime_new (EMFormat *emf,
		   CamelMimePart *part,
		   const gchar *uri,
		   EMPartWriteFunc write_func)
{
	EMPart *emp;

	g_return_val_if_fail (EM_IS_FORMAT (emf), NULL);
	g_return_val_if_fail ((part == NULL) || CAMEL_IS_MIME_PART (part), NULL);
	g_return_val_if_fail (uri && *uri, NULL);

	emp = (EMPart *) g_object_new (EM_TYPE_PART_SMIME, NULL);
	em_part_set_formatter (emp, emf);
	em_part_set_mime_part (emp, part);
	em_part_set_uri (emp, uri);
	em_part_set_write_func (emp, write_func);

	return emp;
}

void
em_part_smime_set_description (EMPartSMIME *emps,
			       const gchar *description)
{
	g_return_if_fail (EM_IS_PART_SMIME (emps));

	em_part_mutex_lock ((EMPart *) emps);

	if (emps->priv->description)
		g_free (emps->priv->description);

	if (description)
		emps->priv->description = g_strdup (description);
	else
		emps->priv->description = NULL;

	em_part_mutex_unlock ((EMPart *) emps);
}

gchar*
em_part_smime_get_description (EMPartSMIME *emps)
{
	gchar *description = NULL;

	g_return_val_if_fail (EM_IS_PART_SMIME (emps), NULL);

	em_part_mutex_lock ((EMPart *) emps);
	if (emps->priv->description)
		description = g_strdup (emps->priv->description);
	em_part_mutex_unlock ((EMPart *) emps);

	return description;
}

void
em_part_smime_set_signature (EMPartSMIME *emps,
			     gint signature)
{
	g_return_if_fail (EM_IS_PART_SMIME (emps));

	em_part_mutex_lock ((EMPart *) emps);
	emps->priv->signature = signature;
	em_part_mutex_unlock ((EMPart *) emps);
}

gint
em_part_smime_get_signature (EMPartSMIME *emps)
{
	gint signature;

	g_return_val_if_fail (EM_IS_PART_SMIME (emps), 0);

	em_part_mutex_lock ((EMPart *) emps);
	signature = emps->priv->signature;
	em_part_mutex_unlock ((EMPart *) emps);

	return signature;
}

void
em_part_smime_set_widget (EMPartSMIME *emps,
			  GtkWidget *widget)
{
	g_return_if_fail (EM_IS_PART_SMIME (emps));
	g_return_if_fail ((widget == NULL) || GTK_IS_WIDGET (widget));

	if (widget)
		g_object_ref (widget);

	em_part_mutex_lock ((EMPart *) emps);

	if (emps->priv->widget)
		g_object_unref (emps->priv->widget);

	emps->priv->widget = widget;

	em_part_mutex_unlock ((EMPart *) emps);
}

GtkWidget*
em_part_smime_get_widget (EMPartSMIME *emps)
{
	GtkWidget *widget = NULL;

	g_return_val_if_fail (EM_IS_PART_SMIME (emps), NULL);

	em_part_mutex_lock ((EMPart *) emps);
	if (emps->priv->widget)
		widget = g_object_ref (emps->priv->widget);
	em_part_mutex_unlock ((EMPart *) emps);

	return widget;
}
