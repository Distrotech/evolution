/*
 * e-mail-parser.h
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

#ifndef E_MAIL_PARSER_H_
#define E_MAIL_PARSER_H_

#include <em-format/e-mail-part-list.h>
#include <em-format/e-mail-extension-registry.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_PARSER \
	(e_mail_parser_get_type ())
#define E_MAIL_PARSER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_PARSER, EMailParser))
#define E_MAIL_PARSER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_PARSER, EMailParserClass))
#define E_IS_MAIL_PARSER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_PARSER))
#define E_IS_MAIL_PARSER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_PARSER))
#define E_MAIL_PARSER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_PARSER, EMailParserClass))

G_BEGIN_DECLS

typedef struct _EMailParser EMailParser;
typedef struct _EMailParserClass EMailParserClass;
typedef struct _EMailParserPrivate EMailParserPrivate;

struct _EMailParser {
	GObject parent;
	EMailParserPrivate *priv;
};

struct _EMailParserClass {
	GObjectClass parent_class;

	EMailParserExtensionRegistry *extension_registry;
};

GType		e_mail_parser_get_type		(void);

EMailParser *	e_mail_parser_new		(CamelSession *session);

EMailPartList *	e_mail_parser_parse_sync	(EMailParser *parser,
						 CamelFolder *folder,
						 const gchar *message_uid,
						 CamelMimeMessage *message,
						 GCancellable  *cancellable);

void		e_mail_parser_parse		(EMailParser *parser,
						 CamelFolder *folder,
						 const gchar *message_uid,
						 CamelMimeMessage *message,
						 GAsyncReadyCallback callback,
						 GCancellable *cancellable,
						 gpointer user_data);

EMailPartList *	e_mail_parser_parse_finish	(EMailParser *parser,
						 GAsyncResult *result,
						 GError **error);

GSList *	e_mail_parser_parse_part	(EMailParser *parser,
						 CamelMimePart *part,
						 GString *part_id,
						 GCancellable *cancellable);

GSList *	e_mail_parser_parse_part_as	(EMailParser *parser,
						 CamelMimePart *part,
						 GString *part_id,
						 const gchar *mime_type,
						 GCancellable *cancellable);

GSList *	e_mail_parser_error		(EMailParser *parser,
						 GCancellable *cancellable,
						 const gchar *format,
						 ...) G_GNUC_PRINTF (3, 4);

GSList *	e_mail_parser_wrap_as_attachment
						(EMailParser *parser,
						 CamelMimePart *part,
						 GSList *parts,
						 GString *part_id,
						 GCancellable *cancellable);

CamelSession *	e_mail_parser_get_session	(EMailParser *parser);

EMailExtensionRegistry *
		e_mail_parser_get_extension_registry
						(EMailParser *parser);

G_END_DECLS

#endif /* E_MAIL_PARSER_H_ */
