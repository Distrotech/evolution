/*
 * e-alert-bar.h
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

#ifndef E_ALERT_BAR_H
#define E_ALERT_BAR_H

#include <gtk/gtk.h>
#include <e-util/e-alert.h>

/* Standard GObject macros */
#define E_TYPE_ALERT_BAR \
	(e_alert_bar_get_type ())
#define E_ALERT_BAR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_ALERT_BAR, EAlertBar))
#define E_ALERT_BAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_ALERT_BAR, EAlertBarClass))
#define E_IS_ALERT_BAR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_ALERT_BAR))
#define E_IS_ALERT_BAR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_ALERT_BAR))
#define E_ALERT_BAR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_ALERT_BAR, EAlertBarClass))

G_BEGIN_DECLS

typedef struct _EAlertBar EAlertBar;
typedef struct _EAlertBarClass EAlertBarClass;
typedef struct _EAlertBarPrivate EAlertBarPrivate;

struct _EAlertBar {
	GtkInfoBar parent;
	EAlertBarPrivate *priv;
};

struct _EAlertBarClass {
	GtkInfoBarClass parent_class;
};

GType		e_alert_bar_get_type		(void);
GtkWidget *	e_alert_bar_new			(void);
void		e_alert_bar_clear		(EAlertBar *alert_bar);
void		e_alert_bar_add_alert		(EAlertBar *alert_bar,
						 EAlert *alert);

G_END_DECLS

#endif /* E_ALERT_BAR_H */
