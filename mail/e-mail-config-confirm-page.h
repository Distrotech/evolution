/*
 * e-mail-config-confirm-page.h
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

#ifndef E_MAIL_CONFIG_CONFIRM_PAGE_H
#define E_MAIL_CONFIG_CONFIRM_PAGE_H

#include <gtk/gtk.h>

#include <mail/e-mail-config-page.h>

/* Standard GObject macros */
#define E_TYPE_MAIL_CONFIG_CONFIRM_PAGE \
	(e_mail_config_confirm_page_get_type ())
#define E_MAIL_CONFIG_CONFIRM_PAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_MAIL_CONFIG_CONFIRM_PAGE, EMailConfigConfirmPage))
#define E_MAIL_CONFIG_CONFIRM_PAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_MAIL_CONFIG_CONFIRM_PAGE, EMailConfigConfirmPageClass))
#define E_IS_MAIL_CONFIG_CONFIRM_PAGE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_MAIL_CONFIG_CONFIRM_PAGE))
#define E_IS_MAIL_CONFIG_CONFIRM_PAGE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_MAIL_CONFIG_CONFIRM_PAGE))
#define E_MAIL_CONFIG_CONFIRM_PAGE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_MAIL_CONFIG_CONFIRM_PAGE, EMailConfigConfirmPageClass))

#define E_MAIL_CONFIG_CONFIRM_PAGE_SORT_ORDER (600)

G_BEGIN_DECLS

typedef struct _EMailConfigConfirmPage EMailConfigConfirmPage;
typedef struct _EMailConfigConfirmPageClass EMailConfigConfirmPageClass;
typedef struct _EMailConfigConfirmPagePrivate EMailConfigConfirmPagePrivate;

struct _EMailConfigConfirmPage {
	GtkBox parent;
	EMailConfigConfirmPagePrivate *priv;
};

struct _EMailConfigConfirmPageClass {
	GtkBoxClass parent_class;
};

GType		e_mail_config_confirm_page_get_type
						(void) G_GNUC_CONST;
EMailConfigPage *
		e_mail_config_confirm_page_new	(void);
const gchar *	e_mail_config_confirm_page_get_text
						(EMailConfigConfirmPage *page);
void		e_mail_config_confirm_page_set_text
						(EMailConfigConfirmPage *page,
						 const gchar *text);

G_END_DECLS

#endif /* E_MAIL_CONFIG_CONFIRM_PAGE_H */

