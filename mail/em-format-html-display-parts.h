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

#ifndef EM_FORMAT_HTML_DISPLAY_PARTS_H
#define EM_FORMAT_HTML_DISPLAY_PARTS_H

#include <em-format/em-part.h>
#include <widgets/misc/e-attachment-store.h>
#include <widgets/misc/e-attachment.h>
#include <camel/camel.h>
#include <gtk/gtk.h>


/* Standard GObject macros */
#define EM_TYPE_PART_ATTACHMENT_BAR \
	(em_part_attachment_bar_get_type ())
#define EM_PART_ATTACHMENT_BAR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EM_TYPE_PART_ATTACHMENT_BAR, EMPartAttachmentBar))
#define EM_PART_ATTACHMENT_BAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EM_TYPE_PART_ATTACHMENT_BAR, EMPartAttachmentBarClass))
#define EM_IS_PART_ATTACHMENT_BAR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EM_TYPE_PART_ATTACHMENT_BAR))
#define EM_IS_PART_ATTACHMENT_BAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EM_TYPE_PART_ATTACHMENT_BAR))
#define EM_PART_ATTACHMENT_BAR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EM_TYPE_PART_ATTACHMENT_BAR, EMPartAttachmentBarClass))

G_BEGIN_DECLS	
	
typedef struct _EMPartAttachmentBar EMPartAttachmentBar;
typedef struct _EMPartAttachmentBarClass EMPartAttachmentBarClass;
typedef struct _EMPartAttachmentBarPrivate EMPartAttachmentBarPrivate;

struct _EMPartAttachmentBar {
	EMPart parent;
	EMPartAttachmentBarPrivate *priv;
};

struct _EMPartAttachmentBarClass {
	EMPartClass parent_class;
};

EMPart*			em_part_attachment_bar_new 
					(EMFormat *emf,
					 CamelMimePart *part,
					 const gchar *uri,
					 EMPartWriteFunc write_func);

GType			em_part_attachment_bar_get_type ();

void			em_part_attachment_bar_set_store
					(EMPartAttachmentBar *empab,
					 EAttachmentStore *store);

EAttachmentStore*	em_part_attachment_bar_get_store
					(EMPartAttachmentBar *empab);

G_END_DECLS

/******************************************************************************/

/* Standard GObject macros */
#define EM_TYPE_PART_ATTACHMENT \
	(em_part_attachment_get_type ())
#define EM_PART_ATTACHMENT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EM_TYPE_PART_ATTACHMENT, EMPartAttachment))
#define EM_PART_ATTACHMENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EM_TYPE_PART_ATTACHMENT, EMPartAttachmentClass))
#define EM_IS_PART_ATTACHMENT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EM_TYPE_PART_ATTACHMENT))
#define EM_IS_PART_ATTACHMENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EM_TYPE_PART_ATTACHMENT))
#define EM_PART_ATTACHMENT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EM_TYPE_PART_ATTACHMENT, EMPartAttachmentClass))

G_BEGIN_DECLS

typedef struct _EMPartAttachment EMPartAttachment;
typedef struct _EMPartAttachmentClass EMPartAttachmentClass;
typedef struct _EMPartAttachmentPrivate EMPartAttachmentPrivate;

struct _EMPartAttachment {
	EMPart parent;
	EMPartAttachmentPrivate *priv;
};

struct _EMPartAttachmentClass {
	EMPartClass parent_class;
};

EMPart*			em_part_attachment_new (EMFormat *emf,
						CamelMimePart *part,
					     	const gchar *uri,
					     	EMPartWriteFunc write_func);

GType			em_part_attachment_get_type ();

void			em_part_attachment_set_snoop_mime_type
						(EMPartAttachment *empa,
						 const gchar *snoop_mime_type);
gchar*			em_part_attachment_get_snoop_mime_part
						(EMPartAttachment *empa);

void			em_part_attachment_set_forward_widget
						(EMPartAttachment *empa,
						 GtkWidget *forward);
GtkWidget*		em_part_attachment_get_forward_widget
						(EMPartAttachment *empa);

void			em_part_attachment_set_down_widget
						(EMPartAttachment *empa,
						 GtkWidget *down);
GtkWidget*		em_part_attachment_get_down_widget
						(EMPartAttachment *empa);

void			em_part_attachment_set_is_shown
						(EMPartAttachment *empa,
						 gboolean is_shown);
gboolean		em_part_attachment_get_is_shown
						(EMPartAttachment *empa);

void			em_part_attachment_set_attachment
						(EMPartAttachment *empa,
						 EAttachment *attachment);
EAttachment*		em_part_attachment_get_attachment
						(EMPartAttachment *empa);

void			em_part_attachment_set_view_part_id
						(EMPartAttachment *empa,
						 const gchar *view_part_id);
gchar*			em_part_attachment_get_view_part_id
						(EMPartAttachment *empa);

void			em_part_attachment_set_description
						(EMPartAttachment *empa,
						 const gchar *description);
gchar*			em_part_attachment_get_description
						(EMPartAttachment *empa);

void			em_part_attachment_set_mstream
						(EMPartAttachment *empa,
						 CamelStream *mstream);
CamelStream*		em_part_attachment_get_mstream
						(EMPartAttachment *empa);

void			em_part_attachment_set_handler
						(EMPartAttachment *empa,
						 const EMFormatHandler *handler);

const EMFormatHandler*	em_part_attachment_get_handler
						(EMPartAttachment *empa);

G_END_DECLS

/******************************************************************************/

/* Standard GObject macros */
#define EM_TYPE_PART_SMIME \
	(em_part_smime_get_type ())
#define EM_PART_SMIME(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), EM_TYPE_PART_SMIME, EMPartSMIME))
#define EM_PART_SMIME_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), EM_TYPE_PART_SMIME, EMPartSMIMEClass))
#define EM_IS_PART_SMIME(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), EM_TYPE_PART_SMIME))
#define EM_IS_PART_SMIME_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), EM_TYPE_PART_SMIME))
#define EM_PART_SMIME_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), EM_TYPE_PART_SMIME, EMPartSMIMEClass))

G_BEGIN_DECLS

typedef struct _EMPartSMIME EMPartSMIME;
typedef struct _EMPartSMIMEClass EMPartSMIMEClass;
typedef struct _EMPartSMIMEPrivate EMPartSMIMEPrivate;

struct _EMPartSMIME {
	EMPart parent;
	EMPartSMIMEPrivate *priv;
};

struct _EMPartSMIMEClass {
	EMPartClass parent_class;
};

EMPart*			em_part_smime_new 
					(EMFormat *emf,
					 CamelMimePart *part,
				  	 const gchar *uri,
				  	 EMPartWriteFunc write_func);

GType			em_part_smime_get_type ();

void			em_part_smime_set_description
					(EMPartSMIME *emps,
					 const gchar *description);
gchar*			em_part_smime_get_description
					(EMPartSMIME *emps);

void			em_part_smime_set_signature
					(EMPartSMIME *emps,
					 gint signature);
gint			em_part_smime_get_signature
					(EMPartSMIME *emps);

void			em_part_smime_set_widget
					(EMPartSMIME *emps,
					 GtkWidget *widget);
GtkWidget*		em_part_smime_get_widget
					(EMPartSMIME *emps);

G_END_DECLS

#endif /* EM_FORMAT_HTML_DISPLAY_PARTS_H */