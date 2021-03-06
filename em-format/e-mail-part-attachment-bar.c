/*
 * e-mail-part-attachment-bar.c
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

#include "e-mail-part-attachment-bar.h"

#define E_MAIL_PART_ATTACHMENT_BAR_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_PART_ATTACHMENT_BAR, EMailPartAttachmentBarPrivate))

struct _EMailPartAttachmentBarPrivate {
	EAttachmentStore *store;
};

G_DEFINE_TYPE (
	EMailPartAttachmentBar,
	e_mail_part_attachment_bar,
	E_TYPE_MAIL_PART)

static void
mail_part_attachment_bar_dispose (GObject *object)
{
	EMailPartAttachmentBarPrivate *priv;

	priv = E_MAIL_PART_ATTACHMENT_BAR_GET_PRIVATE (object);

	g_clear_object (&priv->store);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_part_attachment_bar_parent_class)->
		dispose (object);
}

static void
e_mail_part_attachment_bar_class_init (EMailPartAttachmentBarClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (
		class, sizeof (EMailPartAttachmentBarPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = mail_part_attachment_bar_dispose;
}

static void
e_mail_part_attachment_bar_init (EMailPartAttachmentBar *part)
{
	GtkTreeModel *tree_model;

	part->priv = E_MAIL_PART_ATTACHMENT_BAR_GET_PRIVATE (part);

	tree_model = e_attachment_store_new ();
	part->priv->store = E_ATTACHMENT_STORE (tree_model);
}

EMailPart *
e_mail_part_attachment_bar_new (CamelMimePart *mime_part,
                                const gchar *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	return g_object_new (
		E_TYPE_MAIL_PART_ATTACHMENT_BAR,
		"id", id, "mime-part", mime_part, NULL);
}

EAttachmentStore *
e_mail_part_attachment_bar_get_store (EMailPartAttachmentBar *part)
{
	g_return_val_if_fail (E_IS_MAIL_PART_ATTACHMENT_BAR (part), NULL);

	return part->priv->store;
}

