/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-itip-control.c
 *
 * Copyright (C) 2001  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: JP Rosevear
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib.h>
#include <gtk/gtkmisc.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <libgnomeui/gnome-stock-icons.h>
#include <libgnomeui/gnome-uidefs.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-exception.h>
#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/gtkhtml-embedded.h>
#include <gtkhtml/gtkhtml-stream.h>
#include <libedataserver/e-source-list.h>
#include <libical/ical.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-time-util.h>
#include <libecal/e-cal.h>
#include <e-util/e-time-utils.h>
#include <e-util/e-dialog-widgets.h>
#include <e-util/e-html-utils.h>
#include <widgets/misc/e-source-option-menu.h>
#include "dialogs/delete-error.h"
#include "calendar-config.h"
#include "itip-utils.h"
#include "e-itip-control.h"
#include "common/authentication.h"
#include <e-util/e-icon-factory.h>

struct _EItipControlPrivate {
	GtkWidget *html;

	ESourceList *source_lists[E_CAL_SOURCE_TYPE_LAST];
	GHashTable *ecals[E_CAL_SOURCE_TYPE_LAST];	

	ECal *current_ecal;
	ECalSourceType type;

	char action;
	gboolean rsvp;

	GtkWidget *ok;
	GtkWidget *hbox;
	GtkWidget *vbox;
	
	char *vcalendar;
	ECalComponent *comp;
	icalcomponent *main_comp;
	icalcomponent *ical_comp;
	icalcomponent *top_level;
	icalcompiter iter;
	icalproperty_method method;

	int current;
	int total;

	gchar *calendar_uid;

	EAccountList *accounts;

	gchar *from_address;
	gchar *delegator_address;
	gchar *delegator_name;
	gchar *my_address;
	gint   view_only;
};

#define ACTION_DATA "EItipControl:Action"

/* HTML Strings */
#define HTML_BODY_START "<body bgcolor=\"#ffffff\" text=\"#000000\" link=\"#336699\">"
#define HTML_SEP        "<hr color=#336699 align=\"left\" width=450>"
#define HTML_BODY_END   "</body>"
#define HTML_FOOTER     "</html>"

static void class_init	(EItipControlClass	 *klass);
static void init	(EItipControl		 *itip);
static void destroy	(GtkObject               *obj);

static void find_my_address (EItipControl *itip, icalcomponent *ical_comp, icalparameter_partstat *status);
static void url_requested_cb (GtkHTML *html, const gchar *url, GtkHTMLStream *handle, gpointer data);
static gboolean object_requested_cb (GtkHTML *html, GtkHTMLEmbedded *eb, gpointer data);
static void ok_clicked_cb (GtkWidget *widget, gpointer data);

static GtkVBoxClass *parent_class = NULL;

E_MAKE_TYPE (e_itip_control, "EItipControl", EItipControl, class_init, init,
	     GTK_TYPE_VBOX);

static void
class_init (EItipControlClass *klass)
{
	GObjectClass *object_class;
	GtkObjectClass *gtkobject_class;
	
	object_class = G_OBJECT_CLASS (klass);
	gtkobject_class = GTK_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_peek_parent (klass);

	gtkobject_class->destroy = destroy;
}

static void
set_ok_sens (EItipControl *itip)
{
	EItipControlPrivate *priv;
	gboolean read_only = TRUE;
	
	priv = itip->priv;

	if (!priv->ok)
		return;
	
	if (priv->current_ecal)
		e_cal_is_read_only (priv->current_ecal, &read_only, NULL);
	
	gtk_widget_set_sensitive (priv->ok, priv->current_ecal != NULL && !read_only);
}

static void
cal_opened_cb (ECal *ecal, ECalendarStatus status, gpointer data)
{
	EItipControl *itip = data;
	EItipControlPrivate *priv;
	ESource *source;
	ECalSourceType source_type;
	icaltimezone *zone;

	priv = itip->priv;

	source_type = e_cal_get_source_type (ecal);
	source = e_cal_get_source (ecal);
	
	g_signal_handlers_disconnect_matched (ecal, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, cal_opened_cb, NULL);

	if (status != E_CALENDAR_STATUS_OK) {
		g_hash_table_remove (priv->ecals[source_type], e_source_peek_uid (source));

		return;
	}

	zone = calendar_config_get_icaltimezone ();
	e_cal_set_default_timezone (ecal, zone, NULL);

	priv->current_ecal = ecal;
	set_ok_sens (itip);
}

typedef void (* EItipControlOpenFunc) (ECal *ecal, ECalendarStatus status, gpointer data);

static ECal *
start_calendar_server (EItipControl *itip, ESource *source, ECalSourceType type, EItipControlOpenFunc func, gpointer data)
{
	EItipControlPrivate *priv;
	ECal *ecal;

	priv = itip->priv;
	
	ecal = g_hash_table_lookup (priv->ecals[type], e_source_peek_uid (source));
	if (ecal) {
		priv->current_ecal = ecal;
		set_ok_sens (itip);
		return ecal;		
	}
	
	ecal = auth_new_cal_from_source (source, type);
	g_signal_connect (G_OBJECT (ecal), "cal_opened", G_CALLBACK (func), data);

	g_hash_table_insert (priv->ecals[type], g_strdup (e_source_peek_uid (source)), ecal);
	
	e_cal_open_async (ecal, TRUE);

	return ecal;
}

static ECal *
start_calendar_server_by_uid (EItipControl *itip, const char *uid, ECalSourceType type)
{
	EItipControlPrivate *priv;
	int i;
	
	priv = itip->priv;

	for (i = 0; i < E_CAL_SOURCE_TYPE_LAST; i++) {
		ESource *source;

		source = e_source_list_peek_source_by_uid (priv->source_lists[i], uid);
		if (source)
			return start_calendar_server (itip, source, type, cal_opened_cb, itip);
	}
	
	return NULL;
}

typedef struct {
	EItipControl *itip;	
	char *uid;
	int count;
	gboolean show_selector;
} EItipControlFindData;

static void
source_selected_cb (ESourceOptionMenu *esom, ESource *source, gpointer data)
{
	EItipControl *itip = data;
	EItipControlPrivate *priv;
	
	priv = itip->priv;

	if (priv->ok)
		gtk_widget_set_sensitive (priv->ok, FALSE);

	start_calendar_server (itip, source, priv->type, cal_opened_cb, itip);
}

static void
find_cal_opened_cb (ECal *ecal, ECalendarStatus status, gpointer data)
{
	EItipControlFindData *fd = data;
	EItipControlPrivate *priv;
	ESource *source;
	ECalSourceType source_type;
	icalcomponent *icalcomp;
	icaltimezone *zone;

	source_type = e_cal_get_source_type (ecal);
	source = e_cal_get_source (ecal);

	priv = fd->itip->priv;
	
	fd->count--;
	
	g_signal_handlers_disconnect_matched (ecal, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, find_cal_opened_cb, NULL);

	if (status != E_CALENDAR_STATUS_OK) {
		g_hash_table_remove (priv->ecals[source_type], e_source_peek_uid (source));

		goto cleanup;
	}

	if (e_cal_get_object (ecal, fd->uid, NULL, &icalcomp, NULL)) {
		icalcomponent_free (icalcomp);
		
		priv->current_ecal = ecal;
		set_ok_sens (fd->itip);
	}

	zone = calendar_config_get_icaltimezone ();
	e_cal_set_default_timezone (ecal, zone, NULL);

 cleanup:
	if (fd->count == 0) {
		if (fd->show_selector && !priv->current_ecal && priv->vbox) {
			GtkWidget *esom;
			ESource *source = NULL;
			char *uid;

			switch (priv->type) {
			case E_CAL_SOURCE_TYPE_EVENT:
				uid = calendar_config_get_primary_calendar ();
				break;
			case E_CAL_SOURCE_TYPE_TODO:
				uid = calendar_config_get_primary_tasks ();
				break;
			default:
				uid = NULL;
				g_assert_not_reached ();
			}	
	
			if (uid) {
				source = e_source_list_peek_source_by_uid (priv->source_lists[priv->type], uid);
				g_free (uid);
			}

			/* Try to create a default if there isn't one */
			if (!source)
				source = e_source_list_peek_source_any (priv->source_lists[priv->type]);

			esom = e_source_option_menu_new (priv->source_lists[priv->type]);
			g_signal_connect_object (esom, "source_selected",
						 G_CALLBACK (source_selected_cb), 
						 fd->itip, 0);

			gtk_box_pack_start (GTK_BOX (priv->vbox), esom, FALSE, TRUE, 0);
			gtk_widget_show (esom);

			/* FIXME What if there is no source? */
			if (source)
				e_source_option_menu_select (E_SOURCE_OPTION_MENU (esom), source);
		} else {
			/* FIXME Display error message to user */
		}
		
		g_free (fd->uid);
		g_free (fd);
	}
}

static void
find_server (EItipControl *itip, ECalComponent *comp, gboolean show_selector)
{
	EItipControlPrivate *priv;
	EItipControlFindData *fd = NULL;
	GSList *groups, *l;
	const char *uid;

	priv = itip->priv;
	
	e_cal_component_get_uid (comp, &uid);

	groups = e_source_list_peek_groups (priv->source_lists[priv->type]);
	for (l = groups; l; l = l->next) {
		ESourceGroup *group;
		GSList *sources, *m;
		
		group = l->data;

		sources = e_source_group_peek_sources (group);
		for (m = sources; m; m = m->next) {
			ESource *source;
			ECal *ecal;
			
			source = m->data;
			
			if (!fd) {
				fd = g_new0 (EItipControlFindData, 1);
				fd->itip = itip;
				fd->uid = g_strdup (uid);
				fd->show_selector = show_selector;
			}
			fd->count++;

			ecal = start_calendar_server (itip, source, priv->type, find_cal_opened_cb, fd);				
		}		
	}
}

static void
cleanup_ecal (ECal *ecal) 
{
	/* Clean up any signals */
	g_signal_handlers_disconnect_matched (ecal, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, cal_opened_cb, NULL);
	g_signal_handlers_disconnect_matched (ecal, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, find_cal_opened_cb, NULL);
	
	g_object_unref (ecal);
}

static void
html_destroyed (gpointer data)
{
	EItipControl *itip = data;
	EItipControlPrivate *priv;
	
	priv = itip->priv;
	
	priv->html = NULL;
}

static void
init (EItipControl *itip)
{
	EItipControlPrivate *priv;
	GtkWidget *scrolled_window;
	int i;

	priv = g_new0 (EItipControlPrivate, 1);
	itip->priv = priv;

	/* Addresses */
	priv->accounts = itip_addresses_get ();

	/* Source Lists */
	for (i = 0; i < E_CAL_SOURCE_TYPE_LAST; i++)
		priv->source_lists[i] = NULL;

	priv->source_lists[E_CAL_SOURCE_TYPE_EVENT] = e_source_list_new_for_gconf_default ("/apps/evolution/calendar/sources");
	priv->source_lists[E_CAL_SOURCE_TYPE_TODO] = e_source_list_new_for_gconf_default ("/apps/evolution/tasks/sources");
	
	/* Initialize the ecal hashes */
	for (i = 0; i < E_CAL_SOURCE_TYPE_LAST; i++)
		priv->ecals[i] = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, cleanup_ecal);
	priv->current_ecal = NULL;

	/* Other fields to init */
	priv->calendar_uid = NULL;
	priv->from_address = NULL;
	priv->delegator_address = NULL;
	priv->delegator_name = NULL;
	priv->my_address = NULL;
	priv->view_only = 0;
	
	/* Html Widget */
	priv->html = gtk_html_new ();
	gtk_html_set_default_content_type (GTK_HTML (priv->html), 
					   "text/html; charset=utf-8");
	gtk_html_load_from_string (GTK_HTML (priv->html), " ", 1);
	gtk_widget_show (priv->html);

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_widget_show (scrolled_window);

	gtk_container_add (GTK_CONTAINER (scrolled_window), priv->html);
	g_object_weak_ref (G_OBJECT (priv->html), (GWeakNotify)html_destroyed, itip);
	gtk_widget_set_usize (scrolled_window, 600, 400);
	gtk_box_pack_start (GTK_BOX (itip), scrolled_window, FALSE, FALSE, 6);

	g_signal_connect (priv->html, "url_requested", G_CALLBACK (url_requested_cb), itip);
	g_signal_connect (priv->html, "object_requested", G_CALLBACK (object_requested_cb), itip);
	g_signal_connect (priv->html, "submit", G_CALLBACK (ok_clicked_cb), itip);
}

static void
clean_up (EItipControl *itip)
{
	EItipControlPrivate *priv;

	priv = itip->priv;
	if (!priv)
		return;

	g_free (priv->vcalendar);
	priv->vcalendar = NULL;

	if (priv->comp) {
		g_object_unref (priv->comp);
		priv->comp = NULL;
	}

	if (priv->top_level) {
		icalcomponent_free (priv->top_level);
		priv->top_level = NULL;
	}

	if (priv->main_comp) {
		icalcomponent_free (priv->main_comp);
		priv->main_comp = NULL;
	}
	priv->ical_comp = NULL;

	priv->current = 0;
	priv->total = 0;

	g_free (priv->calendar_uid);
	priv->calendar_uid = NULL;

	g_free (priv->from_address);
	priv->from_address = NULL;
	g_free (priv->delegator_address);
	priv->delegator_address = NULL;
	g_free (priv->delegator_name);
	priv->delegator_name = NULL;
	g_free (priv->my_address);
	priv->my_address = NULL;
}

static void
destroy (GtkObject *obj)
{
	EItipControl *itip = E_ITIP_CONTROL (obj);
	EItipControlPrivate *priv;
	
	priv = itip->priv;

	if (priv) {
		int i;

		clean_up (itip);
		
		priv->accounts = NULL;
		
		for (i = 0; i < E_CAL_SOURCE_TYPE_LAST; i++) {
			if (priv->ecals[i]) {
				g_hash_table_destroy (priv->ecals[i]);
				priv->ecals[i] = NULL;
			}
		}
		
		if (priv->html) {
			g_signal_handlers_disconnect_matched (priv->html, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, itip);
			g_object_weak_unref (G_OBJECT (priv->html), (GWeakNotify)html_destroyed, itip);
		}
		
		g_free (priv);
		itip->priv = NULL;
	}
	
	(* GTK_OBJECT_CLASS (parent_class)->destroy) (obj);
}

GtkWidget *
e_itip_control_new (void)
{
	return g_object_new (E_TYPE_ITIP_CONTROL, NULL);
}

static void
find_my_address (EItipControl *itip, icalcomponent *ical_comp, icalparameter_partstat *status)
{
	EItipControlPrivate *priv;
	icalproperty *prop;
	char *my_alt_address = NULL;
	
	priv = itip->priv;
	
	for (prop = icalcomponent_get_first_property (ical_comp, ICAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (ical_comp, ICAL_ATTENDEE_PROPERTY)) {
		icalvalue *value;
		icalparameter *param;
		const char *attendee, *name;
		char *attendee_clean, *name_clean;
		EIterator *it;

		value = icalproperty_get_value (prop);
		if (value != NULL) {
			attendee = icalvalue_get_string (value);
			attendee_clean = g_strdup (itip_strip_mailto (attendee));
			attendee_clean = g_strstrip (attendee_clean);
		} else {
			attendee = NULL;
			attendee_clean = NULL;
		}
		
		param = icalproperty_get_first_parameter (prop, ICAL_CN_PARAMETER);
		if (param != NULL) {
			name = icalparameter_get_cn (param);
			name_clean = g_strdup (name);
			name_clean = g_strstrip (name_clean);
		} else {
			name = NULL;
			name_clean = NULL;
		}

		if (priv->delegator_address) {
			char *delegator_clean;
			
			delegator_clean = g_strdup (itip_strip_mailto (attendee));
			delegator_clean = g_strstrip (delegator_clean);
			
			/* If the mailer told us the address to use, use that */
			if (delegator_clean != NULL
			    && !g_ascii_strcasecmp (attendee_clean, delegator_clean)) {
				priv->my_address = g_strdup (itip_strip_mailto (priv->delegator_address));
				priv->my_address = g_strstrip (priv->my_address);

				if (status) {
					param = icalproperty_get_first_parameter (prop, ICAL_PARTSTAT_PARAMETER);
					*status = param ? icalparameter_get_partstat (param) : ICAL_PARTSTAT_NEEDSACTION;
				}
			}

			g_free (delegator_clean);
		} else {
			it = e_list_get_iterator((EList *)priv->accounts);
			while (e_iterator_is_valid(it)) {
				const EAccount *account = e_iterator_get(it);
				
				/* Check for a matching address */
				if (attendee_clean != NULL
				    && !g_ascii_strcasecmp (account->id->address, attendee_clean)) {
					priv->my_address = g_strdup (account->id->address);
					if (status) {
						param = icalproperty_get_first_parameter (prop, ICAL_PARTSTAT_PARAMETER);
						*status = param ? icalparameter_get_partstat (param) : ICAL_PARTSTAT_NEEDSACTION;
					}
					g_free (attendee_clean);
					g_free (name_clean);
					g_free (my_alt_address);
					g_object_unref(it);
					return;
				}
				
				/* Check for a matching cname to fall back on */
				if (name_clean != NULL 
				    && !g_ascii_strcasecmp (account->id->name, name_clean))
					my_alt_address = g_strdup (attendee_clean);
				
				e_iterator_next(it);
			}
			g_object_unref(it);
		}
		
		g_free (attendee_clean);
		g_free (name_clean);
	}

	priv->my_address = my_alt_address;
	if (status)
		*status = ICAL_PARTSTAT_NEEDSACTION;
}

static icalproperty *
find_attendee (icalcomponent *ical_comp, const char *address)
{
	icalproperty *prop;

	if (address == NULL)
		return NULL;

	for (prop = icalcomponent_get_first_property (ical_comp, ICAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (ical_comp, ICAL_ATTENDEE_PROPERTY)) {
		icalvalue *value;
		const char *attendee;
		char *text;

		value = icalproperty_get_value (prop);
		if (!value)
			continue;

		attendee = icalvalue_get_string (value);

		text = g_strdup (itip_strip_mailto (attendee));
		text = g_strstrip (text);
		if (!g_strcasecmp (address, text)) {
			g_free (text);
			break;
		}
		g_free (text);
	}
	
	return prop;
}

static void
write_label_piece (EItipControl *itip, ECalComponentDateTime *dt,
		   char *buffer, int size,
		   const char *stext, const char *etext,
		   gboolean just_date)
{
	EItipControlPrivate *priv;
	struct tm tmp_tm;
	char time_buf[64];
	icaltimezone *zone = NULL;
	char *display_name;

	priv = itip->priv;

	/* UTC times get converted to the current timezone. This is done for
	   the COMPLETED property, which is always in UTC, and also because
	   Outlook sends simple events as UTC times. */
	if (dt->value->is_utc) {
		zone = calendar_config_get_icaltimezone ();
		icaltimezone_convert_time (dt->value, icaltimezone_get_utc_timezone (), zone);
	}

	tmp_tm = icaltimetype_to_tm (dt->value);
	if (just_date)
		tmp_tm.tm_hour = tmp_tm.tm_min = tmp_tm.tm_sec = 0;

	if (stext != NULL)
		strcat (buffer, stext);

	e_time_format_date_and_time (&tmp_tm,
				     calendar_config_get_24_hour_format (),
				     FALSE, FALSE,
				     time_buf, sizeof (time_buf));
	strcat (buffer, time_buf);

	if (!dt->value->is_utc && dt->tzid) {
		zone = icalcomponent_get_timezone (priv->top_level, dt->tzid);
	}

	/* Output timezone after time, e.g. " America/New_York". */
	if (zone && !just_date) {
		/* Note that this returns UTF-8, since all iCalendar data is
		   UTF-8. But it probably is not translated. */
		display_name = icaltimezone_get_display_name (zone);
		if (display_name && *display_name) {
			strcat (buffer, " <font size=-1>[");

			/* We check if it is one of our builtin timezone names,
			   in which case we call gettext to translate it. */
			if (icaltimezone_get_builtin_timezone (display_name)) {
				strcat (buffer, _(display_name));
			} else {
				strcat (buffer, display_name);
			}
			strcat (buffer, "]</font>");
		}
	}

	if (etext != NULL)
		strcat (buffer, etext);
}

static const char *
nth (int n)
{
	if (n == -1)
		return "last";
	else if (n < 1 || n > 31)
		return "?";
	else
		return e_cal_recur_nth[n];
}

static const char *dayname[] = {
	N_("Sunday"),
	N_("Monday"),
	N_("Tuesday"),
	N_("Wednesday"),
	N_("Thursday"),
	N_("Friday"),
	N_("Saturday")
};

static inline char *
get_dayname (struct icalrecurrencetype *r, int i)
{
	enum icalrecurrencetype_weekday day;

	day = icalrecurrencetype_day_day_of_week (r->by_day[i]);
	g_return_val_if_fail (day > 0 && day < 8, "?");

	return _(dayname[day - 1]);
}

static void
write_recurrence_piece (EItipControl *itip, ECalComponent *comp,
			char *buffer, int size)
{
	GSList *rrules;
	struct icalrecurrencetype *r;
	int len, i;

	strcpy (buffer, "<b>Recurring:</b> ");
	len = strlen (buffer);
	buffer += len;
	size -= len;

	if (!e_cal_component_has_simple_recurrence (comp)) {
		strcpy (buffer, _("Yes. (Complex Recurrence)"));
		return;
	}

	e_cal_component_get_rrule_list (comp, &rrules);
	g_return_if_fail (rrules && !rrules->next);

	r = rrules->data;

	switch (r->freq) {
	case ICAL_DAILY_RECURRENCE:
		sprintf (buffer, ngettext("Every day", "Every %d days", r->interval), r->interval);
		break;

	case ICAL_WEEKLY_RECURRENCE:
		if (r->by_day[0] == ICAL_RECURRENCE_ARRAY_MAX) {
			sprintf (buffer, ngettext("Every week", "Every %d weeks", r->interval), r->interval);
		} else {
			sprintf (buffer, ngettext("Every week on ", "Every %d weeks on ", r->interval), r->interval);

			for (i = 1; i < 8 && r->by_day[i] != ICAL_RECURRENCE_ARRAY_MAX; i++) {
				if (i > 1)
					strcat (buffer, ", ");
				strcat (buffer, get_dayname (r, i - 1));
			}
			if (i > 1)
				strcat (buffer, _(" and "));
			strcat (buffer, get_dayname (r, i - 1));
		}
		break;

	case ICAL_MONTHLY_RECURRENCE:
		if (r->by_month_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
			sprintf (buffer, _("The %s day of "),
				 nth (r->by_month_day[0]));
		} else {
			int pos;

			/* Outlook 2000 uses BYDAY=TU;BYSETPOS=2, and will not
			   accept BYDAY=2TU. So we now use the same as Outlook
			   by default. */

			pos = icalrecurrencetype_day_position (r->by_day[0]);
			if (pos == 0)
				pos = r->by_set_pos[0];

			sprintf (buffer, _("The %s %s of "),
				 nth (pos), get_dayname (r, 0));
		}

		len = strlen (buffer);
		buffer += len;
		size -= len;
		sprintf (buffer, ngettext("every month","every %d months", r->interval), r->interval);
		break;

	case ICAL_YEARLY_RECURRENCE:
		sprintf (buffer, ngettext("Every year", "Every %d years", r->interval), r->interval);
		break;

	default:
		g_assert_not_reached ();
	}

	len = strlen (buffer);
	buffer += len;
	size -= len;
	if (r->count) {
		sprintf (buffer, ngettext("a total of %d time", " a total of %d times", r->count), r->count);
	} else if (!icaltime_is_null_time (r->until)) {
		ECalComponentDateTime dt;

		/* FIXME This should get the tzid id, not the whole zone */
		dt.value = &r->until;
		dt.tzid = icaltimezone_get_tzid ((icaltimezone *)r->until.zone);

		write_label_piece (itip, &dt, buffer, size,
				   _(", ending on "), NULL, TRUE);
	}

	strcat (buffer, "<br>");
}

static void
set_date_label (EItipControl *itip, GtkHTML *html, GtkHTMLStream *html_stream,
		ECalComponent *comp)
{
	EItipControlPrivate *priv;
	ECalComponentDateTime datetime;
	static char buffer[1024];
	gchar *str;
	gboolean wrote = FALSE, task_completed = FALSE;
	ECalComponentVType type;

	priv = itip->priv;

	type = e_cal_component_get_vtype (comp);

	buffer[0] = '\0';
	e_cal_component_get_dtstart (comp, &datetime);
	if (datetime.value) {
		str = g_strdup_printf ("<b>%s:</b>", _("Starts"));
		write_label_piece (itip, &datetime, buffer, 1024,
				  str,
				   "<br>", FALSE);
		gtk_html_write (html, html_stream, buffer, strlen(buffer));
		wrote = TRUE;
		g_free (str);
	}
	e_cal_component_free_datetime (&datetime);

	buffer[0] = '\0';
	e_cal_component_get_dtend (comp, &datetime);
	if (datetime.value){
		str = g_strdup_printf ("<b>%s:</b>", _("Ends"));
		write_label_piece (itip, &datetime, buffer, 1024, str, "<br>", FALSE);
		gtk_html_write (html, html_stream, buffer, strlen (buffer));
		wrote = TRUE;
		g_free (str);
	}
	e_cal_component_free_datetime (&datetime);

	buffer[0] = '\0';
	if (e_cal_component_has_recurrences (comp)) {
		write_recurrence_piece (itip, comp, buffer, 1024);
		gtk_html_write (html, html_stream, buffer, strlen (buffer));
		wrote = TRUE;
	}

	buffer[0] = '\0';
	datetime.tzid = NULL;
	e_cal_component_get_completed (comp, &datetime.value);
	if (type == E_CAL_COMPONENT_TODO && datetime.value) {
		/* Pass TRUE as is_utc, so it gets converted to the current
		   timezone. */
		str = g_strdup_printf ("<b>%s:</b>", _("Completed"));
		datetime.value->is_utc = TRUE;
		write_label_piece (itip, &datetime, buffer, 1024, str, "<br>", FALSE);
		gtk_html_write (html, html_stream, buffer, strlen (buffer));
		wrote = TRUE;
		task_completed = TRUE;
		g_free (str);
	}
	e_cal_component_free_datetime (&datetime);

	buffer[0] = '\0';
	e_cal_component_get_due (comp, &datetime);
	if (type == E_CAL_COMPONENT_TODO && !task_completed && datetime.value) {
		str = g_strdup_printf ("<b>%s:</b>", _("Due"));
		write_label_piece (itip, &datetime, buffer, 1024, str, "<br>", FALSE);
		gtk_html_write (html, html_stream, buffer, strlen (buffer));
		wrote = TRUE;
		g_free (str);
	}

	e_cal_component_free_datetime (&datetime);

	if (wrote)
		gtk_html_stream_printf (html_stream, "<br>");
}

static void
set_message (GtkHTML *html, GtkHTMLStream *html_stream, const gchar *message, gboolean err)
{
	if (message == NULL)
		return;


	if (err) {
		gtk_html_stream_printf (html_stream, "<b><font color=\"#ff0000\">%s</font></b><br><br>", message);
	} else {
		gtk_html_stream_printf (html_stream, "<b>%s</b><br><br>", message);
	}
}

static void
write_error_html (EItipControl *itip, const gchar *itip_err)
{
	EItipControlPrivate *priv;
	GtkHTMLStream *html_stream;
	gchar *filename;

	priv = itip->priv;

	/* Html widget */
	html_stream = gtk_html_begin (GTK_HTML (priv->html));
	gtk_html_stream_printf (html_stream,
				"<html><head><title>%s</title></head>",
				_("iCalendar Information"));

	gtk_html_write (GTK_HTML (priv->html), html_stream,
			HTML_BODY_START, strlen(HTML_BODY_START));

	/* The table */
	gtk_html_stream_printf (html_stream, "<table width=450 cellspacing=\"0\" cellpadding=\"4\" border=\"0\">");
	/* The column for the image */
	gtk_html_stream_printf (html_stream, "<tr><td width=48 align=\"center\" valign=\"top\" rowspan=\"8\">");
	/* The image */
	filename = e_icon_factory_get_icon_filename ("stock_new-meeting", E_ICON_SIZE_DIALOG);
	gtk_html_stream_printf (html_stream, "<img src=\"%s\"></td>", filename);
	g_free (filename);

	gtk_html_stream_printf (html_stream, "<td align=\"left\" valign=\"top\">");

	/* Title */
	set_message (GTK_HTML (priv->html), html_stream, _("iCalendar Error"), TRUE);

	/* Error */
	gtk_html_write (GTK_HTML (priv->html), html_stream, itip_err, strlen(itip_err));

	/* Clean up */
	gtk_html_stream_printf (html_stream, "</td></tr></table>");

	gtk_html_write (GTK_HTML (priv->html), html_stream,
			HTML_BODY_END, strlen(HTML_BODY_END));
	gtk_html_write (GTK_HTML (priv->html), html_stream,
			HTML_FOOTER, strlen(HTML_FOOTER));

	gtk_html_end (GTK_HTML (priv->html), html_stream, GTK_HTML_STREAM_OK);
}

static void
write_html (EItipControl *itip, const gchar *itip_desc, const gchar *itip_title, const gchar *options)
{
	EItipControlPrivate *priv;
	GtkHTMLStream *html_stream;
	ECalComponentText text;
	ECalComponentOrganizer organizer;
	ECalComponentAttendee *attendee;
	GSList *attendees, *l = NULL;
	const char *string;
	gchar *html;
	const gchar *const_html;
	gchar *filename;
	gchar *str;

	priv = itip->priv;

	if (priv->html == NULL)
		return;
	
	/* Html widget */
	html_stream = gtk_html_begin (GTK_HTML (priv->html));
	gtk_html_stream_printf (html_stream,
				"<html><head><title>%s</title></head>",
				_("iCalendar Information"));
	gtk_html_write (GTK_HTML (priv->html), html_stream,
			HTML_BODY_START, strlen(HTML_BODY_START));

	/* The table */
	const_html = "<table width=450 cellspacing=\"0\" cellpadding=\"4\" border=\"0\">";
	gtk_html_write (GTK_HTML (priv->html), html_stream, const_html, strlen(const_html));

	/* The column for the image */
	const_html = "<tr><td width=48 align=\"center\" valign=\"top\" rowspan=\"8\">";
	gtk_html_write (GTK_HTML (priv->html), html_stream, const_html, strlen(const_html));

	/* The image */
	filename = e_icon_factory_get_icon_filename ("stock_new-meeting", E_ICON_SIZE_DIALOG);
	gtk_html_stream_printf (html_stream, "<img src=\"%s\"></td>", filename);
	g_free (filename);

	const_html = "<td align=\"left\" valign=\"top\">";
	gtk_html_write (GTK_HTML (priv->html), html_stream, const_html, strlen(const_html));

	switch (priv->method) {
	case ICAL_METHOD_REFRESH:
	case ICAL_METHOD_REPLY:
		/* An attendee sent this */
		e_cal_component_get_attendee_list (priv->comp, &attendees);
		if (attendees != NULL) {
			attendee = attendees->data;
			html = g_strdup_printf (itip_desc,
						attendee->cn ?
						attendee->cn :
						itip_strip_mailto (attendee->value));
		} else {
			html = g_strdup_printf (itip_desc, _("An unknown person"));
		}
		break;
	case ICAL_METHOD_REQUEST:
		/* The organizer sent this */
		e_cal_component_get_organizer (priv->comp, &organizer);
		if (priv->delegator_address != NULL) {
			if (organizer.value != NULL)
				html = g_strdup_printf (itip_desc,
							organizer.cn ?
							organizer.cn :
							itip_strip_mailto (organizer.value),
							priv->delegator_name ?
							priv->delegator_name :
					                priv->delegator_address);
			else
				html = g_strdup_printf (itip_desc, _("An unknown person"),
							priv->delegator_name ?
							priv->delegator_name :
					                priv->delegator_address);
		} else {
			if (organizer.value != NULL)
				html = g_strdup_printf (itip_desc,
							organizer.cn ?
							organizer.cn :
							itip_strip_mailto (organizer.value));
			else
				html = g_strdup_printf (itip_desc, _("An unknown person"));
		}
		
		break;
		
	case ICAL_METHOD_PUBLISH:
	case ICAL_METHOD_ADD:
	case ICAL_METHOD_CANCEL:
	default:
		/* The organizer sent this */
		e_cal_component_get_organizer (priv->comp, &organizer);
		if (organizer.value != NULL)
			html = g_strdup_printf (itip_desc,
						organizer.cn ?
						organizer.cn :
						itip_strip_mailto (organizer.value));
		else
			html = g_strdup_printf (itip_desc, _("An unknown person"));
		break;
	}
	gtk_html_write (GTK_HTML (priv->html), html_stream, html, strlen(html));
	g_free (html);

	/* Describe what the user can do */
	const_html = _("<br> Please review the following information, "
			"and then select an action from the menu below.");
	gtk_html_write (GTK_HTML (priv->html), html_stream, const_html, strlen(const_html));

	/* Separator */
	gtk_html_write (GTK_HTML (priv->html), html_stream, HTML_SEP, strlen (HTML_SEP));

	/* Title */
	set_message (GTK_HTML (priv->html), html_stream, itip_title, FALSE);

	/* Date information */
	set_date_label (itip, GTK_HTML (priv->html), html_stream, priv->comp);

	/* Summary */
	e_cal_component_get_summary (priv->comp, &text);
	str = g_strdup_printf ("<i>%s:</i>", _("None"));

	html = text.value ? e_text_to_html_full (text.value, E_TEXT_TO_HTML_CONVERT_NL, 0) : str;
	gtk_html_stream_printf (html_stream, "<b>%s</b><br>%s<br><br>",
				_("Summary:"), html);
	g_free (str);
	if (text.value)
		g_free (html);
	
	/* Location */
	e_cal_component_get_location (priv->comp, &string);
	if (string != NULL) {
		html = e_text_to_html_full (string, E_TEXT_TO_HTML_CONVERT_NL, 0);
		gtk_html_stream_printf (html_stream, "<b>%s</b><br>%s<br><br>", 
					_("Location:"), html);
		g_free (html);
	}
	
	/* Status */
	if (priv->method == ICAL_METHOD_REPLY) {
		GSList *alist;

		e_cal_component_get_attendee_list (priv->comp, &alist);
		
		if (alist != NULL) {
			ECalComponentAttendee *a = alist->data;

			gtk_html_stream_printf (html_stream, "<b>%s</b><br>",
						_("Status:"));
			
			switch (a->status) {
			case ICAL_PARTSTAT_ACCEPTED:
				gtk_html_stream_printf (html_stream, "%s<br><br>",
							_("Accepted"));
				break;
			case ICAL_PARTSTAT_TENTATIVE:
				gtk_html_stream_printf (html_stream, "%s<br><br>",
							_("Tentatively Accepted"));
				break;
			case ICAL_PARTSTAT_DECLINED:
				gtk_html_stream_printf (html_stream, "%s<br><br>",
							_("Declined"));
				break;
			default:
				gtk_html_stream_printf (html_stream, "%s<br><br>",
							_("Unknown"));
			}
		}
		
		e_cal_component_free_attendee_list (alist);
	}
	
	/* Description */
	e_cal_component_get_description_list (priv->comp, &l);
	if (l)
		text = *((ECalComponentText *)l->data);

	if (l && text.value) {
		html = e_text_to_html_full (text.value, E_TEXT_TO_HTML_CONVERT_NL, 0);
		gtk_html_stream_printf (html_stream, "<b>%s</b><br>%s",
					_("Description:"), html);
		g_free (html);
	}
	e_cal_component_free_text_list (l);

	/* Separator */
	gtk_html_write (GTK_HTML (priv->html), html_stream, HTML_SEP, strlen (HTML_SEP));

	/* Options */
	if (!e_itip_control_get_view_only (itip)) {
		if (options != NULL) {
			const_html = "</td></tr><tr><td valign=\"center\">";
			gtk_html_write (GTK_HTML (priv->html), html_stream, const_html, strlen (const_html));
			gtk_html_write (GTK_HTML (priv->html), html_stream, options, strlen (options));
		}
	}
	
	const_html = "</td></tr></table>";
	gtk_html_write (GTK_HTML (priv->html), html_stream, const_html, strlen(const_html));

	gtk_html_write (GTK_HTML (priv->html), html_stream,
			HTML_BODY_END, strlen(HTML_BODY_END));
	gtk_html_write (GTK_HTML (priv->html), html_stream,
			HTML_FOOTER, strlen(HTML_FOOTER));

	gtk_html_end (GTK_HTML (priv->html), html_stream, GTK_HTML_STREAM_OK);
}


static char*
get_publish_options (void)
{
	return g_strdup_printf ("<object classid=\"itip:publish_options\"></object>");
}

static char*
get_request_options (void)
{
	return g_strdup_printf ("<object classid=\"itip:request_options\"></object>");
}

static char*
get_request_fb_options ()
{
	return g_strdup_printf ("<object classid=\"itip:freebusy_options\"></object>");
}

static char*
get_reply_options ()
{
	return g_strdup_printf ("<object classid=\"itip:reply_options\"></object>");
}

static char*
get_refresh_options ()
{
	return g_strdup_printf ("<object classid=\"itip:refresh_options\"></object>");
}

static char*
get_cancel_options (gboolean found, icalcomponent_kind kind)
{ 
	if (!found) {
		switch (kind) {
		case ICAL_VEVENT_COMPONENT:
			return g_strdup_printf ("<i>%s</i>", _("The meeting has been cancelled, however it could not be found in your calendars"));
		case ICAL_VTODO_COMPONENT:
 			return g_strdup_printf ("<i>%s</i>", _("The task has been cancelled, however it could not be found in your task lists"));
		default:
			g_assert_not_reached ();
			return NULL;
		}		
	}

	return g_strdup_printf ("<object classid=\"itip:cancel_options\"></object>");
}


static ECalComponent *
get_real_item (EItipControl *itip) 
{
	EItipControlPrivate *priv;
	ECalComponent *comp;
	icalcomponent *icalcomp;
	gboolean found = FALSE;
	const char *uid;
	
	priv = itip->priv;

	e_cal_component_get_uid (priv->comp, &uid);

	found = e_cal_get_object (priv->current_ecal, uid, NULL, &icalcomp, NULL);
	if (!found)
		return NULL;

	comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
		g_object_unref (comp);
		icalcomponent_free (icalcomp);
		return NULL;
	}

	return comp;
}

static void
adjust_item (EItipControl *itip, ECalComponent *comp)
{
	ECalComponent *real_comp;
	
	real_comp = get_real_item (itip);
	if (real_comp != NULL) {
		ECalComponentText text;
		const char *string;
		GSList *l;
		
		e_cal_component_get_summary (real_comp, &text);
		e_cal_component_set_summary (comp, &text);
		e_cal_component_get_location (real_comp, &string);
		e_cal_component_set_location (comp, string);
		e_cal_component_get_description_list (real_comp, &l);
		e_cal_component_set_description_list (comp, l);
		e_cal_component_free_text_list (l);
		
		g_object_unref (real_comp);
	} else {
		ECalComponentText text = {_("Unknown"), NULL};
		
		e_cal_component_set_summary (comp, &text);
	}
}

static void
show_current_event (EItipControl *itip)
{
	EItipControlPrivate *priv;
	const gchar *itip_title, *itip_desc;
	char *options;
	gboolean show_selector = FALSE;
	
	priv = itip->priv;

	priv->type = E_CAL_SOURCE_TYPE_EVENT;

	switch (priv->method) {
	case ICAL_METHOD_PUBLISH:
		itip_desc = _("<b>%s</b> has published meeting information.");
		itip_title = _("Meeting Information");
		options = get_publish_options ();
		show_selector = TRUE;
		break;
	case ICAL_METHOD_REQUEST:
		if (priv->delegator_address != NULL)
			itip_desc = _("<b>%s</b> requests the presence of %s at a meeting.");
		else
			itip_desc = _("<b>%s</b> requests your presence at a meeting.");
		itip_title = _("Meeting Proposal");
		options = get_request_options ();
		show_selector = TRUE;
		break;
	case ICAL_METHOD_ADD:
		/* FIXME Whats going on here? */
		itip_desc = _("<b>%s</b> wishes to add to an existing meeting.");
		itip_title = _("Meeting Update");
		options = get_publish_options ();
		break;
	case ICAL_METHOD_REFRESH:
		itip_desc = _("<b>%s</b> wishes to receive the latest meeting information.");
		itip_title = _("Meeting Update Request");
		options = get_refresh_options ();

		/* Provide extra info, since its not in the component */
		adjust_item (itip, priv->comp);
		break;
	case ICAL_METHOD_REPLY:
		itip_desc = _("<b>%s</b> has replied to a meeting request.");
		itip_title = _("Meeting Reply");
		options = get_reply_options ();

		/* Provide extra info, since might not be in the component */
		adjust_item (itip, priv->comp);
		break;
	case ICAL_METHOD_CANCEL:
		itip_desc = _("<b>%s</b> has cancelled a meeting.");
		itip_title = _("Meeting Cancellation");
		/* FIXME priv->current_ecal will always be NULL so the
		 * user won't see an error message, the OK button will
		 * just be de-sensitized */
		options = get_cancel_options (TRUE, ICAL_VEVENT_COMPONENT);

		/* Provide extra info, since might not be in the component */
		adjust_item (itip, priv->comp);
		break;
	default:
		itip_desc = _("<b>%s</b> has sent an unintelligible message.");
		itip_title = _("Bad Meeting Message");
		options = NULL;
	}

	write_html (itip, itip_desc, itip_title, options);
	g_free (options);

	if (priv->calendar_uid)
		priv->current_ecal = start_calendar_server_by_uid (itip, priv->calendar_uid, priv->type);
	else 
		find_server (itip, priv->comp, show_selector);
}

static void
show_current_todo (EItipControl *itip)
{
	EItipControlPrivate *priv;
	const gchar *itip_title, *itip_desc;
	char *options;
	gboolean show_selector = FALSE;
	
	priv = itip->priv;

	priv->type = E_CAL_SOURCE_TYPE_TODO;

	switch (priv->method) {
	case ICAL_METHOD_PUBLISH:
		itip_desc = _("<b>%s</b> has published task information.");
		itip_title = _("Task Information");
		options = get_publish_options ();
		show_selector = TRUE;
		break;
	case ICAL_METHOD_REQUEST:
		/* FIXME Does this need to handle like events above? */
		if (priv->delegator_address != NULL)
			itip_desc = _("<b>%s</b> requests %s to perform a task.");
		else
			itip_desc = _("<b>%s</b> requests you perform a task.");
		itip_title = _("Task Proposal");
		options = get_request_options ();
		show_selector = TRUE;
		break;
	case ICAL_METHOD_ADD:
		/* FIXME Whats going on here? */
		itip_desc = _("<b>%s</b> wishes to add to an existing task.");
		itip_title = _("Task Update");
		options = get_publish_options ();
		break;
	case ICAL_METHOD_REFRESH:
		itip_desc = _("<b>%s</b> wishes to receive the latest task information.");
		itip_title = _("Task Update Request");
		options = get_refresh_options ();

		/* Provide extra info, since its not in the component */
		adjust_item (itip, priv->comp);
		break;
	case ICAL_METHOD_REPLY:
		itip_desc = _("<b>%s</b> has replied to a task assignment.");
		itip_title = _("Task Reply");
		options = get_reply_options ();

		/* Provide extra info, since might not be in the component */
		adjust_item (itip, priv->comp);
		break;
	case ICAL_METHOD_CANCEL:
		itip_desc = _("<b>%s</b> has cancelled a task.");
		itip_title = _("Task Cancellation");
		/* FIXME priv->current_ecal will always be NULL so the
		 * user won't see an error message, the OK button will
		 * just be de-sensitized */
		options = get_cancel_options (TRUE, ICAL_VTODO_COMPONENT);

		/* Provide extra info, since might not be in the component */
		adjust_item (itip, priv->comp);
		break;
	default:
		itip_desc = _("<b>%s</b> has sent an unintelligible message.");
		itip_title = _("Bad Task Message");
		options = NULL;
	}

	write_html (itip, itip_desc, itip_title, options);
	g_free (options);

	if (priv->calendar_uid)
		priv->current_ecal = start_calendar_server_by_uid (itip, priv->calendar_uid, priv->type);
	else 
		find_server (itip, priv->comp, show_selector);
}

static void
show_current_freebusy (EItipControl *itip)
{
	EItipControlPrivate *priv;
	const gchar *itip_title, *itip_desc;
	char *options;

	priv = itip->priv;

	switch (priv->method) {
	case ICAL_METHOD_PUBLISH:
		itip_desc = _("<b>%s</b> has published free/busy information.");
		itip_title = _("Free/Busy Information");
		options = NULL;
		break;
	case ICAL_METHOD_REQUEST:
		itip_desc = _("<b>%s</b> requests your free/busy information.");
		itip_title = _("Free/Busy Request");
		options = get_request_fb_options ();
		break;
	case ICAL_METHOD_REPLY:
		itip_desc = _("<b>%s</b> has replied to a free/busy request.");
		itip_title = _("Free/Busy Reply");
		options = NULL;
		break;
	default:
		itip_desc = _("<b>%s</b> has sent an unintelligible message.");
		itip_title = _("Bad Free/Busy Message");
		options = NULL;
	}

	write_html (itip, itip_desc, itip_title, options);
	g_free (options);
}

static icalcomponent *
get_next (icalcompiter *iter)
{
	icalcomponent *ret = NULL;
	icalcomponent_kind kind;

	do {
		icalcompiter_next (iter);
		ret = icalcompiter_deref (iter);
		if (ret == NULL)
			break;
		kind = icalcomponent_isa (ret);
	} while (ret != NULL 
		 && kind != ICAL_VEVENT_COMPONENT
		 && kind != ICAL_VTODO_COMPONENT
		 && kind != ICAL_VFREEBUSY_COMPONENT);

	return ret;
}

static void
show_current (EItipControl *itip)
{
	EItipControlPrivate *priv;
	ECalComponentVType type;
	icalcomponent *alarm_comp;
	icalcompiter alarm_iter;
	icalproperty *prop;

	priv = itip->priv;

	g_object_ref (itip);
	
	if (priv->comp)
		g_object_unref (priv->comp);
	priv->current_ecal = NULL;

	/* Determine any delegate sections */
	prop = icalcomponent_get_first_property (priv->ical_comp, ICAL_X_PROPERTY);
	while (prop) {
		const char *x_name, *x_val;

		x_name = icalproperty_get_x_name (prop);
		x_val = icalproperty_get_x (prop);

		if (!strcmp (x_name, "X-EVOLUTION-DELEGATOR-CALENDAR-UID"))
			e_itip_control_set_calendar_uid (itip, x_val);
		else if (!strcmp (x_name, "X-EVOLUTION-DELEGATOR-CALENDAR-URI"))
			g_warning (G_STRLOC ": X-EVOLUTION-DELEGATOR-CALENDAR-URI used");
		else if (!strcmp (x_name, "X-EVOLUTION-DELEGATOR-ADDRESS"))
			e_itip_control_set_delegator_address (itip, x_val);
		else if (!strcmp (x_name, "X-EVOLUTION-DELEGATOR-NAME"))
			e_itip_control_set_delegator_name (itip, x_val);

		prop = icalcomponent_get_next_property (priv->ical_comp, ICAL_X_PROPERTY);
	}
	
	/* Strip out alarms for security purposes */
	alarm_iter = icalcomponent_begin_component (priv->ical_comp, ICAL_VALARM_COMPONENT);
	while ((alarm_comp = icalcompiter_deref (&alarm_iter)) != NULL) {
		icalcomponent_remove_component (priv->ical_comp, alarm_comp);
		
		icalcompiter_next (&alarm_iter);
	}

	priv->comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (priv->comp, priv->ical_comp)) {
		write_error_html (itip, _("The message does not appear to be properly formed"));
		g_object_unref (priv->comp);
		priv->comp = NULL;
		g_object_unref (itip);
		return;
	};

	/* Add default reminder if the config says so */
	if (calendar_config_get_use_default_reminder ()) {
		ECalComponentAlarm *acomp;
		int interval;
		CalUnits units;
		ECalComponentAlarmTrigger trigger;

		interval = calendar_config_get_default_reminder_interval ();
		units = calendar_config_get_default_reminder_units ();

		acomp = e_cal_component_alarm_new ();

		e_cal_component_alarm_set_action (acomp, E_CAL_COMPONENT_ALARM_DISPLAY);

		trigger.type = E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START;
		memset (&trigger.u.rel_duration, 0, sizeof (trigger.u.rel_duration));

		trigger.u.rel_duration.is_neg = TRUE;

		switch (units) {
		case CAL_MINUTES:
			trigger.u.rel_duration.minutes = interval;
			break;
		case CAL_HOURS:	
			trigger.u.rel_duration.hours = interval;
			break;
		case CAL_DAYS:	
			trigger.u.rel_duration.days = interval;
			break;
		default:
			g_assert_not_reached ();
		}

		e_cal_component_alarm_set_trigger (acomp, trigger);
		e_cal_component_add_alarm (priv->comp, acomp);

		e_cal_component_alarm_free (acomp);
	}

	type = e_cal_component_get_vtype (priv->comp);

	switch (type) {
	case E_CAL_COMPONENT_EVENT:
		show_current_event (itip);
		break;
	case E_CAL_COMPONENT_TODO:
		show_current_todo (itip);
		break;
	case E_CAL_COMPONENT_FREEBUSY:
		show_current_freebusy (itip);
		break;
	default:
		write_error_html (itip, _("The message contains only unsupported requests."));
	}

	find_my_address (itip, priv->ical_comp, NULL);

	g_object_unref (itip);
}

void
e_itip_control_set_data (EItipControl *itip, const gchar *text)
{
	EItipControlPrivate *priv;
	icalproperty *prop;
	icalcomponent_kind kind = ICAL_NO_COMPONENT;
	icalcomponent *tz_comp;
	icalcompiter tz_iter;

	priv = itip->priv;

	clean_up (itip);

	if (text == NULL || *text == '\0') {
		gtk_html_load_from_string (GTK_HTML (priv->html), " ", 1);
		return;
	}
	
	priv->vcalendar = g_strdup (text);
	priv->top_level = e_cal_util_new_top_level ();

	priv->main_comp = icalparser_parse_string (priv->vcalendar);
	if (priv->main_comp == NULL) {
		write_error_html (itip, _("The attachment does not contain a valid calendar message"));
		return;
	}

	prop = icalcomponent_get_first_property (priv->main_comp, ICAL_METHOD_PROPERTY);
	if (prop == NULL) {
		priv->method = ICAL_METHOD_PUBLISH;
	} else {
		priv->method = icalproperty_get_method (prop);
	}

	tz_iter = icalcomponent_begin_component (priv->main_comp, ICAL_VTIMEZONE_COMPONENT);
	while ((tz_comp = icalcompiter_deref (&tz_iter)) != NULL) {
		icalcomponent *clone;

		clone = icalcomponent_new_clone (tz_comp);
		icalcomponent_add_component (priv->top_level, clone);

		icalcompiter_next (&tz_iter);
	}

	priv->iter = icalcomponent_begin_component (priv->main_comp, ICAL_ANY_COMPONENT);
	priv->ical_comp = icalcompiter_deref (&priv->iter);
	if (priv->ical_comp != NULL) {
		kind = icalcomponent_isa (priv->ical_comp);
		if (kind != ICAL_VEVENT_COMPONENT
		    && kind != ICAL_VTODO_COMPONENT
		    && kind != ICAL_VFREEBUSY_COMPONENT)
			priv->ical_comp = get_next (&priv->iter);
	}

	if (priv->ical_comp == NULL) {
		write_error_html (itip, _("The attachment has no viewable calendar items"));		
		return;
	}

	priv->total = icalcomponent_count_components (priv->main_comp, ICAL_VEVENT_COMPONENT);
	priv->total += icalcomponent_count_components (priv->main_comp, ICAL_VTODO_COMPONENT);
	priv->total += icalcomponent_count_components (priv->main_comp, ICAL_VFREEBUSY_COMPONENT);

	if (priv->total > 0)
		priv->current = 1;
	else
		priv->current = 0;

	show_current (itip);
}

gchar *
e_itip_control_get_data (EItipControl *itip)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	return g_strdup (priv->vcalendar);
}

gint
e_itip_control_get_data_size (EItipControl *itip)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	if (priv->vcalendar == NULL)
		return 0;

	return strlen (priv->vcalendar);
}

void
e_itip_control_set_from_address (EItipControl *itip, const gchar *address)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	if (priv->from_address)
		g_free (priv->from_address);

	priv->from_address = g_strdup (address);
}

const gchar *
e_itip_control_get_from_address (EItipControl *itip)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	return priv->from_address;
}

void
e_itip_control_set_view_only (EItipControl *itip, gint view_only)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	priv->view_only = view_only;
}

gint
e_itip_control_get_view_only (EItipControl *itip)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	return priv->view_only;
}


void
e_itip_control_set_delegator_address (EItipControl *itip, const gchar *address)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	if (priv->delegator_address)
		g_free (priv->delegator_address);

	priv->delegator_address = g_strdup (address);
}

const gchar *
e_itip_control_get_delegator_address (EItipControl *itip)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	return priv->delegator_address;
}


void
e_itip_control_set_delegator_name (EItipControl *itip, const gchar *name)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	if (priv->delegator_name)
		g_free (priv->delegator_name);

	priv->delegator_name = g_strdup (name);
}

const gchar *
e_itip_control_get_delegator_name (EItipControl *itip)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	return priv->delegator_name;
}

void
e_itip_control_set_calendar_uid (EItipControl *itip, const gchar *uri)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	if (priv->calendar_uid)
		g_free (priv->calendar_uid);

	priv->calendar_uid = g_strdup (uri);
}

const gchar *
e_itip_control_get_calendar_uid (EItipControl *itip)
{
	EItipControlPrivate *priv;

	priv = itip->priv;

	return priv->calendar_uid;
}


static gboolean
change_status (icalcomponent *ical_comp, const char *address, icalparameter_partstat status)
{
	icalproperty *prop;

	prop = find_attendee (ical_comp, address);
	if (prop) {
		icalparameter *param;

		icalproperty_remove_parameter (prop, ICAL_PARTSTAT_PARAMETER);
		param = icalparameter_new_partstat (status);
		icalproperty_add_parameter (prop, param);
	} else {
		icalparameter *param;
		
		if (address != NULL) {
			prop = icalproperty_new_attendee (address);
			icalcomponent_add_property (ical_comp, prop);

			param = icalparameter_new_role (ICAL_ROLE_OPTPARTICIPANT);
			icalproperty_add_parameter (prop, param);

			param = icalparameter_new_partstat (status);
			icalproperty_add_parameter (prop, param);
		} else {
			EAccount *a;

			a = itip_addresses_get_default ();
			
			prop = icalproperty_new_attendee (a->id->address);
			icalcomponent_add_property (ical_comp, prop);
			
			param = icalparameter_new_cn (a->id->name);
			icalproperty_add_parameter (prop, param);	

			param = icalparameter_new_role (ICAL_ROLE_REQPARTICIPANT);
			icalproperty_add_parameter (prop, param);
			
			param = icalparameter_new_partstat (status);
			icalproperty_add_parameter (prop, param);
		}
	}

	return TRUE;
}

static void
update_item (EItipControl *itip)
{
	EItipControlPrivate *priv;
	struct icaltimetype stamp;
	icalproperty *prop;
	icalcomponent *clone;
	GtkWidget *dialog;
	GError *error = NULL;

	priv = itip->priv;

	/* Set X-MICROSOFT-CDO-REPLYTIME to record the time at which
	 * the user accepted/declined the request. (Outlook ignores
	 * SEQUENCE in REPLY reponses and instead requires that each
	 * updated response have a later REPLYTIME than the previous
	 * one.) This also ends up getting saved in our own copy of
	 * the meeting, though there's currently no way to see that
	 * information (unless it's being saved to an Exchange folder
	 * and you then look at it in Outlook).
	 */
	stamp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	prop = icalproperty_new_x (icaltime_as_ical_string (stamp));
	icalproperty_set_x_name (prop, "X-MICROSOFT-CDO-REPLYTIME");
	icalcomponent_add_property (priv->ical_comp, prop);

	clone = icalcomponent_new_clone (priv->ical_comp);
	icalcomponent_add_component (priv->top_level, clone);
	icalcomponent_set_method (priv->top_level, priv->method);

	if (!e_cal_receive_objects (priv->current_ecal, priv->top_level, &error)) {
		dialog = gnome_warning_dialog (error->message);
		g_error_free (error);
	} else {
		dialog = gnome_ok_dialog (_("Update complete\n"));
	}
	gnome_dialog_run_and_close (GNOME_DIALOG (dialog));

	icalcomponent_remove_component (priv->top_level, clone);
}

static void
update_attendee_status (EItipControl *itip)
{
	EItipControlPrivate *priv;
	ECalComponent *comp = NULL;
	icalcomponent *icalcomp = NULL;
	const char *uid;
	GtkWidget *dialog;
	GError *error;
	
	priv = itip->priv;
	
	/* Obtain our version */
	e_cal_component_get_uid (priv->comp, &uid);
	if (e_cal_get_object (priv->current_ecal, uid, NULL, &icalcomp, NULL)) {
		GSList *attendees;

		comp = e_cal_component_new ();
		if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
			icalcomponent_free (icalcomp);

			dialog = gnome_warning_dialog (_("Object is invalid and cannot be updated\n"));
		} else {
			e_cal_component_get_attendee_list (priv->comp, &attendees);
			if (attendees != NULL) {
				ECalComponentAttendee *a = attendees->data;
				icalproperty *prop;

				prop = find_attendee (icalcomp, itip_strip_mailto (a->value));

				if (prop == NULL) {
					dialog = gnome_question_dialog_modal (_("This response is not from a current "
										"attendee.  Add as an attendee?"),
									      NULL, NULL);
					if (gnome_dialog_run_and_close (GNOME_DIALOG (dialog)) == GNOME_YES) {
						change_status (icalcomp,
							       itip_strip_mailto (a->value),
							       a->status);
						e_cal_component_rescan (comp);
					} else {
						goto cleanup;
					}
				} else if (a->status == ICAL_PARTSTAT_NONE || a->status == ICAL_PARTSTAT_X) {
					dialog = gnome_warning_dialog (_("Attendee status could "
									 "not be updated because "
									 "of an invalid status!\n"));
					goto cleanup;
				} else {
					change_status (icalcomp,
						       itip_strip_mailto (a->value),
						       a->status);
					e_cal_component_rescan (comp);			
				}
			}
		}

		if (!e_cal_modify_object (priv->current_ecal, icalcomp, CALOBJ_MOD_ALL, &error)) {
			dialog = gnome_warning_dialog (error->message);
			g_error_free (error);
		} else {
			dialog = gnome_ok_dialog (_("Attendee status updated\n"));
		}
	} else {
		dialog = gnome_warning_dialog (_("Attendee status can not be updated "
						 "because the item no longer exists"));
	}

 cleanup:
	if (comp != NULL)
		g_object_unref (comp);
	gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
}

static void
remove_item (EItipControl *itip)
{
	EItipControlPrivate *priv;
	const char *uid;
	GtkWidget *dialog;
	GError *error = NULL;
	
	priv = itip->priv;

	/* FIXME Is this check necessary? */
	if (!priv->current_ecal)
		return;
	
	e_cal_component_get_uid (priv->comp, &uid);
	e_cal_remove_object (priv->current_ecal, uid, &error);
	if (!error || error->code == E_CALENDAR_STATUS_OBJECT_NOT_FOUND) {
		dialog = gnome_ok_dialog (_("Removal Complete"));
		gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
	} else {
		delete_error_dialog (error, e_cal_component_get_vtype (priv->comp));
	}
	
	g_clear_error (&error);
}

static void
send_item (EItipControl *itip)
{
	EItipControlPrivate *priv;
	ECalComponent *comp;
	GtkWidget *dialog;

	priv = itip->priv;

	comp = get_real_item (itip);
	
	if (comp != NULL) {
		itip_send_comp (E_CAL_COMPONENT_METHOD_REQUEST, comp, priv->current_ecal, NULL);
		g_object_unref (comp);
		dialog = gnome_ok_dialog (_("Item sent!\n"));
	} else {
		dialog = gnome_warning_dialog (_("The item could not be sent!\n"));
	}
	gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
}

static void
send_freebusy (EItipControl *itip)
{
	EItipControlPrivate *priv;
	ECalComponentDateTime datetime;
	time_t start, end;
	GtkWidget *dialog;
	GList *comp_list = NULL;
	icaltimezone *zone;

	priv = itip->priv;

	e_cal_component_get_dtstart (priv->comp, &datetime);
	if (datetime.tzid) {
		zone = icalcomponent_get_timezone (priv->top_level,
						   datetime.tzid);
	} else {
		zone = NULL;
	}
	start = icaltime_as_timet_with_zone (*datetime.value, zone);
	e_cal_component_free_datetime (&datetime);

	e_cal_component_get_dtend (priv->comp, &datetime);
	if (datetime.tzid) {
		zone = icalcomponent_get_timezone (priv->top_level,
						   datetime.tzid);
	} else {
		zone = NULL;
	}
	end = icaltime_as_timet_with_zone (*datetime.value, zone);
	e_cal_component_free_datetime (&datetime);

	if (e_cal_get_free_busy (priv->current_ecal, NULL, start, end, &comp_list, NULL)) {
		GList *l;

		for (l = comp_list; l; l = l->next) {
			ECalComponent *comp = E_CAL_COMPONENT (l->data);
			itip_send_comp (E_CAL_COMPONENT_METHOD_REPLY, comp, priv->current_ecal, NULL);

			g_object_unref (comp);
		}
		dialog = gnome_ok_dialog (_("Item sent!\n"));

		g_list_free (comp_list);
	} else {
		dialog = gnome_warning_dialog (_("The item could not be sent!\n"));
	}
	gnome_dialog_run_and_close (GNOME_DIALOG (dialog));
}

static void
url_requested_cb (GtkHTML *html, const gchar *url, GtkHTMLStream *handle, gpointer data)
{	unsigned char buffer[4096];
	int len, fd;

	if ((fd = open (url, O_RDONLY)) == -1) {
		g_warning ("%s", g_strerror (errno));
		return;
	}

       	while ((len = read (fd, buffer, 4096)) > 0) {
		gtk_html_write (html, handle, buffer, len);
	}

	if (len < 0) {
		/* check to see if we stopped because of an error */
		gtk_html_end (html, handle, GTK_HTML_STREAM_ERROR);
		g_warning ("%s", g_strerror (errno));
		return;
	}
	/* done with no errors */
	gtk_html_end (html, handle, GTK_HTML_STREAM_OK);
	close (fd);
}

static void
option_activated_cb (GtkWidget *widget, gpointer data)
{
	EItipControl *itip = E_ITIP_CONTROL (data);	
	EItipControlPrivate *priv;
	
	priv = itip->priv;

	priv->action = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), ACTION_DATA));
}

static void
add_option (EItipControl *itip, GtkWidget *menu, const char *text, char action) 
{
	GtkWidget *item;
	
	item = gtk_menu_item_new_with_label (text);
	g_signal_connect (item, "activate", G_CALLBACK (option_activated_cb), itip);
	g_object_set_data (G_OBJECT (item), ACTION_DATA, GINT_TO_POINTER ((int)action));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
	
}

static void
insert_boxes (GtkHTMLEmbedded *eb, EItipControl *itip)
{
	EItipControlPrivate *priv;
	
	priv = itip->priv;

	priv->vbox = gtk_vbox_new (FALSE, 12);
	g_object_add_weak_pointer (G_OBJECT (priv->vbox), ((gpointer *)&priv->vbox));	

	gtk_container_add (GTK_CONTAINER (eb), priv->vbox);
	gtk_widget_show (priv->vbox);

	priv->hbox = gtk_hbox_new (FALSE, 6);
	g_object_add_weak_pointer (G_OBJECT (priv->hbox), ((gpointer *)&priv->hbox));

	gtk_box_pack_start (GTK_BOX (priv->vbox), priv->hbox, FALSE, TRUE, 0);
	gtk_widget_show (priv->hbox);
}

static void
insert_label (GtkWidget *hbox) 
{
	GtkWidget *label;
	char *text;
	
	text = g_strdup_printf ("<b>%s</b>", _("Choose an action:"));
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), text);
	g_free (text);

	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
	gtk_widget_show (label);	
}

static void
rsvp_clicked_cb (GtkWidget *widget, gpointer data)
{
	EItipControl *itip = E_ITIP_CONTROL (data);	
	EItipControlPrivate *priv;
	
	priv = itip->priv;

	priv->rsvp = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
}

static void
insert_rsvp (GtkWidget *hbox, EItipControl *itip) 
{
	EItipControlPrivate *priv;
	GtkWidget *btn;

	priv = itip->priv;
	
	btn = gtk_check_button_new_with_label ("RSVP");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), TRUE);
	priv->rsvp = TRUE;

	g_signal_connect (btn, "clicked", G_CALLBACK (rsvp_clicked_cb), itip);

	gtk_box_pack_start (GTK_BOX (hbox), btn, FALSE, TRUE, 0);
	gtk_widget_show (btn);	
}

static void
insert_ok (GtkWidget *hbox, EItipControl *itip) 
{
	EItipControlPrivate *priv;
	
	priv = itip->priv;
	
	priv->ok = gtk_button_new_from_stock (GTK_STOCK_OK);
	g_object_add_weak_pointer (G_OBJECT (priv->ok), ((gpointer *)&priv->ok));

	g_signal_connect (priv->ok, "clicked", G_CALLBACK (ok_clicked_cb), itip);

	set_ok_sens (itip);

	gtk_box_pack_start (GTK_BOX (hbox), priv->ok, FALSE, TRUE, 0);
	gtk_widget_show (priv->ok);	
}

static gboolean
publish_options_object (EItipControl *itip, GtkHTML *html, GtkHTMLEmbedded *eb)
{
	EItipControlPrivate *priv;
	GtkWidget *option, *menu;
	
	priv = itip->priv;

	insert_boxes (eb, itip);
	insert_label (priv->hbox);

	option = gtk_option_menu_new ();
	
	menu = gtk_menu_new ();

	add_option (itip, menu, _("Update"), 'U');
	priv->action = 'U';
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (option), menu);

	gtk_box_pack_start (GTK_BOX (priv->hbox), option, FALSE, TRUE, 0);
	gtk_widget_show (option);

	insert_ok (priv->hbox, itip);

	return TRUE;
}

static gboolean
request_options_object (EItipControl *itip, GtkHTML *html, GtkHTMLEmbedded *eb)
{
	EItipControlPrivate *priv;
	GtkWidget *option, *menu;
	
	priv = itip->priv;

	insert_boxes (eb, itip);
	insert_label (priv->hbox);
	
	option = gtk_option_menu_new ();
	
	menu = gtk_menu_new ();

	add_option (itip, menu, _("Accept"), 'A');
	add_option (itip, menu, _("Tentatively accept"), 'T');
	add_option (itip, menu, _("Decline"), 'D');
	priv->action = 'A';
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (option), menu);

	gtk_box_pack_start (GTK_BOX (priv->hbox), option, FALSE, TRUE, 0);
	gtk_widget_show (option);

	insert_rsvp (priv->hbox, itip);
	insert_ok (priv->hbox, itip);

	return TRUE;
}

static gboolean
freebusy_options_object (EItipControl *itip, GtkHTML *html, GtkHTMLEmbedded *eb)
{
	EItipControlPrivate *priv;
	GtkWidget *option, *menu;
	
	priv = itip->priv;

	insert_boxes (eb, itip);
	insert_label (priv->hbox);

	option = gtk_option_menu_new ();
	
	menu = gtk_menu_new ();

	add_option (itip, menu, _("Send Free/Busy Information"), 'F');
	priv->action = 'F';
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (option), menu);

	gtk_container_add (GTK_CONTAINER (priv->hbox), option);
	gtk_widget_show (option);

	insert_ok (priv->hbox, itip);

	return TRUE;
}

static gboolean
reply_options_object (EItipControl *itip, GtkHTML *html, GtkHTMLEmbedded *eb)
{
	EItipControlPrivate *priv;
	GtkWidget *option, *menu;
	
	priv = itip->priv;

	insert_boxes (eb, itip);
	insert_label (priv->hbox);

	option = gtk_option_menu_new ();
	
	menu = gtk_menu_new ();

	add_option (itip, menu, _("Update respondent status"), 'R');
	priv->action = 'R';
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (option), menu);

	gtk_container_add (GTK_CONTAINER (priv->hbox), option);
	gtk_widget_show (option);

	insert_ok (priv->hbox, itip);

	return TRUE;
}

static gboolean
refresh_options_object (EItipControl *itip, GtkHTML *html, GtkHTMLEmbedded *eb)
{
	EItipControlPrivate *priv;
	GtkWidget *option, *menu;
	
	priv = itip->priv;

	insert_boxes (eb, itip);
	insert_label (priv->hbox);

	option = gtk_option_menu_new ();
	
	menu = gtk_menu_new ();

	add_option (itip, menu, _("Send Latest Information"), 'S');
	priv->action = 'R';
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (option), menu);

	gtk_container_add (GTK_CONTAINER (priv->hbox), option);
	gtk_widget_show (option);

	insert_ok (priv->hbox, itip);

	return TRUE;
}

static gboolean
cancel_options_object (EItipControl *itip, GtkHTML *html, GtkHTMLEmbedded *eb)
{
	EItipControlPrivate *priv;
	GtkWidget *option, *menu;
	
	priv = itip->priv;

	insert_boxes (eb, itip);
	insert_label (priv->hbox);

	option = gtk_option_menu_new ();
	
	menu = gtk_menu_new ();

	add_option (itip, menu, _("Cancel"), 'C');
	priv->action = 'C';
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (option), menu);

	gtk_container_add (GTK_CONTAINER (priv->hbox), option);
	gtk_widget_show (option);

	insert_ok (priv->hbox, itip);

	return TRUE;
}

static gboolean
object_requested_cb (GtkHTML *html, GtkHTMLEmbedded *eb, gpointer data) 
{
	EItipControl *itip = E_ITIP_CONTROL (data);

	if (!strcmp (eb->classid, "itip:publish_options"))
		return publish_options_object (itip, html, eb);
	else if (!strcmp (eb->classid, "itip:request_options"))
		return request_options_object (itip, html, eb);
	else if (!strcmp (eb->classid, "itip:freebusy_options"))
		return freebusy_options_object (itip, html, eb);
	else if (!strcmp (eb->classid, "itip:reply_options"))
		return reply_options_object (itip, html, eb);
	else if (!strcmp (eb->classid, "itip:refresh_options"))
		return refresh_options_object (itip, html, eb);
	else if (!strcmp (eb->classid, "itip:cancel_options"))
		return cancel_options_object (itip, html, eb);
	
	return FALSE;
}

static void
ok_clicked_cb (GtkWidget *widget, gpointer data)
{
	EItipControl *itip = E_ITIP_CONTROL (data);
	EItipControlPrivate *priv;
	gboolean status = FALSE;

	priv = itip->priv;

	if (!priv->my_address && priv->current_ecal != NULL)
		e_cal_get_cal_address (priv->current_ecal, &priv->my_address, NULL);

	switch (priv->action) {
	case 'U':
		update_item (itip);
		break;
	case 'A':
		status = change_status (priv->ical_comp, priv->my_address, 
					ICAL_PARTSTAT_ACCEPTED);
		if (status) {
			e_cal_component_rescan (priv->comp);
			update_item (itip);
		}
		break;
	case 'T':
		status = change_status (priv->ical_comp, priv->my_address,
					ICAL_PARTSTAT_TENTATIVE);
		if (status) {
			e_cal_component_rescan (priv->comp);
			update_item (itip);
		}
		break;
	case 'D':
		status = change_status (priv->ical_comp, priv->my_address,
					ICAL_PARTSTAT_DECLINED);
		if (status) {
			e_cal_component_rescan (priv->comp);
			update_item (itip);
		}
		break;
	case 'F':
		send_freebusy (itip);
		break;
	case 'R':
		update_attendee_status (itip);
		break;
	case 'S':
		send_item (itip);
		break;
	case 'C':
		update_item (itip);
		break;
	}

	if (e_cal_get_save_schedules (priv->current_ecal))
		return;

	if (priv->rsvp && status) {
		ECalComponent *comp = NULL;
		ECalComponentVType vtype;
		icalcomponent *ical_comp;
		icalproperty *prop;
		icalvalue *value;
		const char *attendee;
		GSList *l, *list = NULL;
		
		comp = e_cal_component_clone (priv->comp);
		if (comp == NULL)
			return;
		vtype = e_cal_component_get_vtype (comp);
		
		if (priv->my_address == NULL)
			find_my_address (itip, priv->ical_comp, NULL);
		g_assert (priv->my_address != NULL);
		
		ical_comp = e_cal_component_get_icalcomponent (comp);
		
		for (prop = icalcomponent_get_first_property (ical_comp, ICAL_ATTENDEE_PROPERTY);
		     prop != NULL;
		     prop = icalcomponent_get_next_property (ical_comp, ICAL_ATTENDEE_PROPERTY))
		{
			char *text;
			
			value = icalproperty_get_value (prop);
			if (!value)
				continue;
			
			attendee = icalvalue_get_string (value);
			
			text = g_strdup (itip_strip_mailto (attendee));
			text = g_strstrip (text);
			if (g_strcasecmp (priv->my_address, text))
				list = g_slist_prepend (list, prop);
			g_free (text);
		}
		
		for (l = list; l; l = l->next) {
			prop = l->data;
			icalcomponent_remove_property (ical_comp, prop);
			icalproperty_free (prop);
		}
		g_slist_free (list);
		
		e_cal_component_rescan (comp);
		itip_send_comp (E_CAL_COMPONENT_METHOD_REPLY, comp, priv->current_ecal, priv->top_level);

		g_object_unref (comp);
	}
}
