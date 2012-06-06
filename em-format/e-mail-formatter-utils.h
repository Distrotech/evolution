/*
 * e-mail-formatter-utils.h
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

#ifndef E_MAIL_FORMATTER_UTILS_H_
#define E_MAIL_FORMATTER_UTILS_H_

#include <camel/camel.h>
#include <em-format/e-mail-formatter.h>

G_BEGIN_DECLS

void		e_mail_formatter_format_header (EMailFormatter *formatter,
						GString *buffer,
						CamelMedium *part,
						struct _camel_header_raw *header,
						guint32 flags,
						const gchar *charset);

void		e_mail_formatter_format_text_header
						(EMailFormatter *formatter,
						 GString *buffer,
						 const gchar *label,
						 const gchar *value,
						 guint32 flags);

gchar *		e_mail_formatter_format_address (EMailFormatter *formatter,
						 GString *out,
						 struct _camel_header_address *a,
						 gchar *field,
						 gboolean no_links,
						 gboolean elipsize);

void		e_mail_formatter_canon_header_name
						(gchar *name);

GSList *		e_mail_formatter_find_rfc822_end_iter
						(GSList *rfc822_start_iter);

G_END_DECLS

#endif /* E_MAIL_FORMATTER_UTILS_H_ */
