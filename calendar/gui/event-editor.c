/* Evolution calendar - Event editor dialog
 *
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Authors: Miguel de Icaza <miguel@helixcode.com>
 *          Federico Mena-Quintero <federico@helixcode.com>
 *          Seth Alves <alves@helixcode.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include <e-util/e-dialog-widgets.h>
#include <widgets/misc/e-dateedit.h>
#include <gal/widgets/e-unicode.h>
#include <cal-util/timeutil.h>
#include "event-editor.h"
#include "e-meeting-edit.h"




typedef struct {
	/* Glade XML data */
	GladeXML *xml;

	/* UI handler */
	BonoboUIHandler *uih;

	/* Client to use */
	CalClient *client;
	
	/* Calendar object/uid we are editing; this is an internal copy */
	CalComponent *comp;

	/* Widgets from the Glade file */

	GtkWidget *app;

	GtkWidget *general_summary;

	GtkWidget *start_time;
	GtkWidget *end_time;
	GtkWidget *all_day_event;

	GtkWidget *description;

	GtkWidget *alarm_display;
	GtkWidget *alarm_program;
	GtkWidget *alarm_audio;
	GtkWidget *alarm_mail;
	GtkWidget *alarm_display_amount;
	GtkWidget *alarm_display_unit;
	GtkWidget *alarm_audio_amount;
	GtkWidget *alarm_audio_unit;
	GtkWidget *alarm_program_amount;
	GtkWidget *alarm_program_unit;
	GtkWidget *alarm_program_run_program;
	GtkWidget *alarm_program_run_program_entry;
	GtkWidget *alarm_mail_amount;
	GtkWidget *alarm_mail_unit;
	GtkWidget *alarm_mail_mail_to;

	GtkWidget *classification_radio;

	GtkWidget *recurrence_rule_notebook;
	GtkWidget *recurrence_rule_none;
	GtkWidget *recurrence_rule_daily;
	GtkWidget *recurrence_rule_weekly;
	GtkWidget *recurrence_rule_monthly;
	GtkWidget *recurrence_rule_yearly;

	GtkWidget *recurrence_rule_daily_days;

	GtkWidget *recurrence_rule_weekly_weeks;
	GtkWidget *recurrence_rule_weekly_sun;
	GtkWidget *recurrence_rule_weekly_mon;
	GtkWidget *recurrence_rule_weekly_tue;
	GtkWidget *recurrence_rule_weekly_wed;
	GtkWidget *recurrence_rule_weekly_thu;
	GtkWidget *recurrence_rule_weekly_fri;
	GtkWidget *recurrence_rule_weekly_sat;

	GtkWidget *recurrence_rule_monthly_on_day;
	GtkWidget *recurrence_rule_monthly_weekday;
	GtkWidget *recurrence_rule_monthly_day_nth;
	GtkWidget *recurrence_rule_monthly_week;
	GtkWidget *recurrence_rule_monthly_weekpos;
	GtkWidget *recurrence_rule_monthly_every_n_months;
	GtkWidget *recurrence_rule_yearly_every_n_years;

	GtkWidget *recurrence_ending_date_repeat_forever;
	GtkWidget *recurrence_ending_date_end_on;
	GtkWidget *recurrence_ending_date_end_on_date;
	GtkWidget *recurrence_ending_date_end_after;
	GtkWidget *recurrence_ending_date_end_after_count;

	GtkWidget *recurrence_exceptions_date;
	GtkWidget *recurrence_exceptions_list;
	GtkWidget *recurrence_exception_add;
	GtkWidget *recurrence_exception_delete;
	GtkWidget *recurrence_exception_change;

	GtkWidget *exception_list;
	GtkWidget *exception_date;
} EventEditorPrivate;



static void event_editor_class_init (EventEditorClass *class);
static void event_editor_init (EventEditor *ee);
static void event_editor_destroy (GtkObject *object);

static GtkObjectClass *parent_class;

extern int day_begin, day_end;
extern char *user_name;
extern int am_pm_flag;
extern int week_starts_on_monday;


static void append_exception (EventEditor *ee, time_t t);
static void check_all_day (EventEditor *ee);
static void set_all_day (GtkWidget *toggle, EventEditor *ee);
static void alarm_toggle (GtkWidget *toggle, EventEditor *ee);
static void check_dates (EDateEdit *dedit, EventEditor *ee);
static void check_times (EDateEdit *dedit, EventEditor *ee);
static void recurrence_toggled (GtkWidget *radio, EventEditor *ee);
static void recurrence_exception_added (GtkWidget *widget, EventEditor *ee);
static void recurrence_exception_deleted (GtkWidget *widget, EventEditor *ee);
static void recurrence_exception_changed (GtkWidget *widget, EventEditor *ee);



/**
 * event_editor_get_type:
 * @void:
 *
 * Registers the #EventEditor class if necessary, and returns the type ID
 * associated to it.
 *
 * Return value: The type ID of the #EventEditor class.
 **/
GtkType
event_editor_get_type (void)
{
	static GtkType event_editor_type = 0;

	if (!event_editor_type) {
		static const GtkTypeInfo event_editor_info = {
			"EventEditor",
			sizeof (EventEditor),
			sizeof (EventEditorClass),
			(GtkClassInitFunc) event_editor_class_init,
			(GtkObjectInitFunc) event_editor_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		event_editor_type = gtk_type_unique (GTK_TYPE_OBJECT, &event_editor_info);
	}

	return event_editor_type;
}

/* Class initialization function for the event editor */
static void
event_editor_class_init (EventEditorClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (GTK_TYPE_OBJECT);

	object_class->destroy = event_editor_destroy;
}

/* Object initialization function for the event editor */
static void
event_editor_init (EventEditor *ee)
{
	EventEditorPrivate *priv;

	priv = g_new0 (EventEditorPrivate, 1);
	ee->priv = priv;
}

/* Frees the rows and the row data in the recurrence exceptions GtkCList */
static void
free_exception_clist_data (GtkCList *clist)
{
	int i;

	for (i = 0; i < clist->rows; i++) {
		gpointer data;

		data = gtk_clist_get_row_data (clist, i);
		g_free (data);
		gtk_clist_set_row_data (clist, i, NULL);
	}

	gtk_clist_clear (clist);
}

/* Destroy handler for the event editor */
static void
event_editor_destroy (GtkObject *object)
{
	EventEditor *ee;
	EventEditorPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_EVENT_EDITOR (object));

	ee = EVENT_EDITOR (object);
	priv = ee->priv;

	if (priv->uih) {
		bonobo_object_unref (BONOBO_OBJECT (priv->uih));
		priv->uih = NULL;
	}

	free_exception_clist_data (GTK_CLIST (priv->recurrence_exceptions_list));

	if (priv->app) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (priv->app), ee);
		gtk_widget_destroy (priv->app);
		priv->app = NULL;
	}

	if (priv->comp) {
		gtk_object_unref (GTK_OBJECT (priv->comp));
		priv->comp = NULL;
	}

	if (priv->client) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (priv->client), ee);
		gtk_object_unref (GTK_OBJECT (priv->client));
		priv->client = NULL;
	}

	if (priv->xml) {
		gtk_object_unref (GTK_OBJECT (priv->xml));
		priv->xml = NULL;
	}

	g_free (priv);
	ee->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

/* Creates an appropriate title for the event editor dialog */
static char *
make_title_from_comp (CalComponent *comp)
{
	const char *summary;
	CalComponentVType type;
	CalComponentText text;
	
	if (!comp)
		return g_strdup (_("Edit Appointment"));

	cal_component_get_summary (comp, &text);
	if (text.value)
		summary = text.value;
	else
		summary =  _("No summary");

	
	type = cal_component_get_vtype (comp);
	switch (type) {
	case CAL_COMPONENT_EVENT:
		return g_strdup_printf (_("Appointment - %s"), summary);

	case CAL_COMPONENT_TODO:
		return g_strdup_printf (_("Task - %s"), summary);

	case CAL_COMPONENT_JOURNAL:
		return g_strdup_printf (_("Journal entry - %s"), summary);

	default:
		g_message ("make_title_from_comp(): Cannot handle object of type %d", type);
		return NULL;
	}
}

/* Gets the widgets from the XML file and returns if they are all available.
 * For the widgets whose values can be simply set with e-dialog-utils, it does
 * that as well.
 */
static gboolean
get_widgets (EventEditor *ee)
{
	EventEditorPrivate *priv;

	priv = ee->priv;

#define GW(name) glade_xml_get_widget (priv->xml, name)

	priv->app = GW ("event-editor-dialog");

	priv->general_summary = GW ("general-summary");

	priv->start_time = GW ("start-time");
	priv->end_time = GW ("end-time");
	priv->all_day_event = GW ("all-day-event");

	priv->description = GW ("description");

	priv->alarm_display = GW ("alarm-display");
	priv->alarm_program = GW ("alarm-program");
	priv->alarm_audio = GW ("alarm-audio");
	priv->alarm_mail = GW ("alarm-mail");
	priv->alarm_display_amount = GW ("alarm-display-amount");
	priv->alarm_display_unit = GW ("alarm-display-unit");
	priv->alarm_audio_amount = GW ("alarm-audio-amount");
	priv->alarm_audio_unit = GW ("alarm-audio-unit");
	priv->alarm_program_amount = GW ("alarm-program-amount");
	priv->alarm_program_unit = GW ("alarm-program-unit");
	priv->alarm_program_run_program = GW ("alarm-program-run-program");
	priv->alarm_program_run_program_entry = GW ("alarm-program-run-program-entry");
	priv->alarm_mail_amount = GW ("alarm-mail-amount");
	priv->alarm_mail_unit = GW ("alarm-mail-unit");
	priv->alarm_mail_mail_to = GW ("alarm-mail-mail-to");

	priv->classification_radio = GW ("classification-radio");

	priv->recurrence_rule_notebook = GW ("recurrence-rule-notebook");
	priv->recurrence_rule_none = GW ("recurrence-rule-none");
	priv->recurrence_rule_daily = GW ("recurrence-rule-daily");
	priv->recurrence_rule_weekly = GW ("recurrence-rule-weekly");
	priv->recurrence_rule_monthly = GW ("recurrence-rule-monthly");
	priv->recurrence_rule_yearly = GW ("recurrence-rule-yearly");

	priv->recurrence_rule_daily_days = GW ("recurrence-rule-daily-days");

	priv->recurrence_rule_weekly_weeks = GW ("recurrence-rule-weekly-weeks");
	priv->recurrence_rule_weekly_sun = GW ("recurrence-rule-weekly-sun");
	priv->recurrence_rule_weekly_mon = GW ("recurrence-rule-weekly-mon");
	priv->recurrence_rule_weekly_tue = GW ("recurrence-rule-weekly-tue");
	priv->recurrence_rule_weekly_wed = GW ("recurrence-rule-weekly-wed");
	priv->recurrence_rule_weekly_thu = GW ("recurrence-rule-weekly-thu");
	priv->recurrence_rule_weekly_fri = GW ("recurrence-rule-weekly-fri");
	priv->recurrence_rule_weekly_sat = GW ("recurrence-rule-weekly-sat");

	priv->recurrence_rule_monthly_on_day = GW ("recurrence-rule-monthly-on-day");
	priv->recurrence_rule_monthly_weekday = GW ("recurrence-rule-monthly-weekday");
	priv->recurrence_rule_monthly_day_nth = GW ("recurrence-rule-monthly-day-nth");
	priv->recurrence_rule_monthly_week = GW ("recurrence-rule-monthly-week");
	priv->recurrence_rule_monthly_weekpos = GW ("recurrence-rule-monthly-weekpos");
	priv->recurrence_rule_monthly_every_n_months = GW ("recurrence-rule-monthly-every-n-months");
	priv->recurrence_rule_yearly_every_n_years = GW ("recurrence-rule-yearly-every-n-years");

	priv->recurrence_ending_date_repeat_forever = GW ("recurrence-ending-date-repeat-forever");
	priv->recurrence_ending_date_end_on = GW ("recurrence-ending-date-end-on");
	priv->recurrence_ending_date_end_on_date = GW ("recurrence-ending-date-end-on-date");
	priv->recurrence_ending_date_end_after = GW ("recurrence-ending-date-end-after");
	priv->recurrence_ending_date_end_after_count = GW ("recurrence-ending-date-end-after-count");

	priv->recurrence_exceptions_date = GW ("recurrence-exceptions-date");
	priv->recurrence_exceptions_list = GW ("recurrence-exceptions-list");
	priv->recurrence_exception_add = GW ("recurrence-exceptions-add");
	priv->recurrence_exception_delete = GW ("recurrence-exceptions-delete");
	priv->recurrence_exception_change = GW ("recurrence-exceptions-change");

	priv->exception_list = GW ("recurrence-exceptions-list");
	priv->exception_date = GW ("recurrence-exceptions-date");

#undef GW

	return (priv->general_summary
		&& priv->start_time
		&& priv->end_time
		&& priv->all_day_event
		&& priv->description
		&& priv->alarm_display
		&& priv->alarm_program
		&& priv->alarm_audio
		&& priv->alarm_mail
		&& priv->alarm_display_amount
		&& priv->alarm_display_unit
		&& priv->alarm_audio_amount
		&& priv->alarm_audio_unit
		&& priv->alarm_program_amount
		&& priv->alarm_program_unit
		&& priv->alarm_program_run_program
		&& priv->alarm_program_run_program_entry
		&& priv->alarm_mail_amount
		&& priv->alarm_mail_unit
		&& priv->alarm_mail_mail_to
		&& priv->classification_radio
		&& priv->recurrence_rule_notebook
		&& priv->recurrence_rule_none
		&& priv->recurrence_rule_daily
		&& priv->recurrence_rule_weekly
		&& priv->recurrence_rule_monthly
		&& priv->recurrence_rule_yearly
		&& priv->recurrence_rule_daily_days
		&& priv->recurrence_rule_weekly_weeks
		&& priv->recurrence_rule_monthly_on_day
		&& priv->recurrence_rule_monthly_weekday
		&& priv->recurrence_rule_monthly_day_nth
		&& priv->recurrence_rule_monthly_week
		&& priv->recurrence_rule_monthly_weekpos
		&& priv->recurrence_rule_monthly_every_n_months
		&& priv->recurrence_rule_yearly_every_n_years
		&& priv->recurrence_ending_date_repeat_forever
		&& priv->recurrence_ending_date_end_on
		&& priv->recurrence_ending_date_end_on_date
		&& priv->recurrence_ending_date_end_after
		&& priv->recurrence_ending_date_end_after_count
		&& priv->recurrence_exceptions_date
		&& priv->recurrence_exceptions_list
		&& priv->recurrence_exception_add
		&& priv->recurrence_exception_delete
		&& priv->recurrence_exception_change
		&& priv->exception_list
		&& priv->exception_date);
}

static const int classification_map[] = {
	CAL_COMPONENT_CLASS_PUBLIC,
	CAL_COMPONENT_CLASS_PRIVATE,
	CAL_COMPONENT_CLASS_CONFIDENTIAL,
	-1
};

#if 0
static const int alarm_unit_map[] = {
	ALARM_MINUTES,
	ALARM_HOURS,
	ALARM_DAYS,
	-1
};

static void
alarm_unit_set (GtkWidget *widget, enum AlarmUnit unit)
{
	e_dialog_option_menu_set (widget, unit, alarm_unit_map);
}

static enum AlarmUnit
alarm_unit_get (GtkWidget *widget)
{
	return e_dialog_option_menu_get (widget, alarm_unit_map);
}
#endif

/* Recurrence types for mapping them to radio buttons */
static const int recur_options_map[] = {
	ICAL_NO_RECURRENCE,
	ICAL_DAILY_RECURRENCE,
	ICAL_WEEKLY_RECURRENCE,
	ICAL_MONTHLY_RECURRENCE,
	ICAL_YEARLY_RECURRENCE,
	-1
};

static icalrecurrencetype_frequency
recur_options_get (GtkWidget *widget)
{
	return e_dialog_radio_get (widget, recur_options_map);
}

static const int month_pos_map[] = { 0, 1, 2, 3, 4, -1 };
static const int weekday_map[] = { 0, 1, 2, 3, 4, 5, 6, -1 };

/* Hooks the widget signals */
static void
init_widgets (EventEditor *ee)
{
	EventEditorPrivate *priv;

	priv = ee->priv;

	/* Start and end times */

	gtk_signal_connect (GTK_OBJECT (priv->start_time), "date_changed",
			    GTK_SIGNAL_FUNC (check_dates), ee);
	gtk_signal_connect (GTK_OBJECT (priv->start_time), "time_changed",
			    GTK_SIGNAL_FUNC (check_times), ee);

	gtk_signal_connect (GTK_OBJECT (priv->end_time), "date_changed",
			    GTK_SIGNAL_FUNC (check_dates), ee);
	gtk_signal_connect (GTK_OBJECT (priv->end_time), "time_changed",
			    GTK_SIGNAL_FUNC (check_times), ee);

	gtk_signal_connect (GTK_OBJECT (priv->all_day_event), "toggled",
			    GTK_SIGNAL_FUNC (set_all_day), ee);

	/* Alarms */

	gtk_signal_connect (GTK_OBJECT (priv->alarm_display), "toggled",
			    GTK_SIGNAL_FUNC (alarm_toggle), ee);
	gtk_signal_connect (GTK_OBJECT (priv->alarm_program), "toggled",
			    GTK_SIGNAL_FUNC (alarm_toggle), ee);
	gtk_signal_connect (GTK_OBJECT (priv->alarm_audio), "toggled",
			    GTK_SIGNAL_FUNC (alarm_toggle), ee);
	gtk_signal_connect (GTK_OBJECT (priv->alarm_mail), "toggled",
			    GTK_SIGNAL_FUNC (alarm_toggle), ee);

	/* Recurrence types */

	gtk_signal_connect (GTK_OBJECT (priv->recurrence_rule_none), "toggled",
			    GTK_SIGNAL_FUNC (recurrence_toggled), ee);
	gtk_signal_connect (GTK_OBJECT (priv->recurrence_rule_daily), "toggled",
			    GTK_SIGNAL_FUNC (recurrence_toggled), ee);
	gtk_signal_connect (GTK_OBJECT (priv->recurrence_rule_weekly), "toggled",
			    GTK_SIGNAL_FUNC (recurrence_toggled), ee);
	gtk_signal_connect (GTK_OBJECT (priv->recurrence_rule_monthly), "toggled",
			    GTK_SIGNAL_FUNC (recurrence_toggled), ee);
	gtk_signal_connect (GTK_OBJECT (priv->recurrence_rule_yearly), "toggled",
			    GTK_SIGNAL_FUNC (recurrence_toggled), ee);

	/* Exception buttons */

	gtk_signal_connect (GTK_OBJECT (priv->recurrence_exception_add), "clicked",
			    GTK_SIGNAL_FUNC (recurrence_exception_added), ee);
	gtk_signal_connect (GTK_OBJECT (priv->recurrence_exception_delete), "clicked",
			    GTK_SIGNAL_FUNC (recurrence_exception_deleted), ee);
	gtk_signal_connect (GTK_OBJECT (priv->recurrence_exception_change), "clicked",
			    GTK_SIGNAL_FUNC (recurrence_exception_changed), ee);
}

/* Fills the widgets with default values */
static void
clear_widgets (EventEditor *ee)
{
	EventEditorPrivate *priv;
	time_t now;

	priv = ee->priv;

	now = time (NULL);

	/* Summary, description */
	e_dialog_editable_set (priv->general_summary, NULL);
	e_dialog_editable_set (priv->description, NULL);

	/* Start and end times */

	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_time), ee);
	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_time), ee);

	e_date_edit_set_time (E_DATE_EDIT (priv->start_time), now);
	e_date_edit_set_time (E_DATE_EDIT (priv->end_time), now);

	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_time), ee);
	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_time), ee);

	check_all_day (ee);

	/* Alarms */

	/* FIXMe: these should use configurable defaults */

	e_dialog_toggle_set (priv->alarm_display, FALSE);
	e_dialog_toggle_set (priv->alarm_program, FALSE);
	e_dialog_toggle_set (priv->alarm_audio, FALSE);
	e_dialog_toggle_set (priv->alarm_mail, FALSE);

	e_dialog_spin_set (priv->alarm_display_amount, 15);
	e_dialog_spin_set (priv->alarm_audio_amount, 15);
	e_dialog_spin_set (priv->alarm_program_amount, 15);
	e_dialog_spin_set (priv->alarm_mail_amount, 15);

#if 0
	alarm_unit_set (priv->alarm_display_unit, ALARM_MINUTES);
	alarm_unit_set (priv->alarm_audio_unit, ALARM_MINUTES);
	alarm_unit_set (priv->alarm_program_unit, ALARM_MINUTES);
	alarm_unit_set (priv->alarm_mail_unit, ALARM_MINUTES);
#endif

	e_dialog_editable_set (priv->alarm_program_run_program_entry, NULL);
	e_dialog_editable_set (priv->alarm_mail_mail_to, NULL);

	/* Classification */

	e_dialog_radio_set (priv->classification_radio, 
			    CAL_COMPONENT_CLASS_PRIVATE, classification_map);

	/* Recurrences */

	e_dialog_radio_set (priv->recurrence_rule_none, ICAL_NO_RECURRENCE, recur_options_map);

	e_dialog_spin_set (priv->recurrence_rule_daily_days, 1);

	e_dialog_spin_set (priv->recurrence_rule_weekly_weeks, 1);
	e_dialog_toggle_set (priv->recurrence_rule_weekly_sun, FALSE);
	e_dialog_toggle_set (priv->recurrence_rule_weekly_mon, FALSE);
	e_dialog_toggle_set (priv->recurrence_rule_weekly_tue, FALSE);
	e_dialog_toggle_set (priv->recurrence_rule_weekly_wed, FALSE);
	e_dialog_toggle_set (priv->recurrence_rule_weekly_thu, FALSE);
	e_dialog_toggle_set (priv->recurrence_rule_weekly_fri, FALSE);
	e_dialog_toggle_set (priv->recurrence_rule_weekly_sat, FALSE);

	e_dialog_toggle_set (priv->recurrence_rule_monthly_on_day, TRUE);
	e_dialog_spin_set (priv->recurrence_rule_monthly_day_nth, 1);
	e_dialog_spin_set (priv->recurrence_rule_monthly_every_n_months, 1);
	e_dialog_option_menu_set (priv->recurrence_rule_monthly_week, 0, month_pos_map);
	e_dialog_option_menu_set (priv->recurrence_rule_monthly_weekpos, 0, weekday_map);
	e_dialog_spin_set (priv->recurrence_rule_monthly_every_n_months, 1);

	e_dialog_spin_set (priv->recurrence_rule_yearly_every_n_years, 1);

	e_dialog_toggle_set (priv->recurrence_ending_date_repeat_forever, TRUE);
	e_dialog_spin_set (priv->recurrence_ending_date_end_after_count, 1);
	e_date_edit_set_time (E_DATE_EDIT (priv->recurrence_ending_date_end_on_date), time_add_day (time (NULL), 1));

	/* Exceptions list */

	free_exception_clist_data (GTK_CLIST (priv->recurrence_exceptions_list));
}

/* Fills in the widgets with the proper values */
static void
fill_widgets (EventEditor *ee)
{
	EventEditorPrivate *priv;
	CalComponentText text;
	CalComponentClassification cl;
	CalComponentDateTime d;
	GSList *list, *l;
	time_t dtstart, dtend;

	priv = ee->priv;

	clear_widgets (ee);

	if (!priv->comp)
		return;

	cal_component_get_summary (priv->comp, &text);
	e_dialog_editable_set (priv->general_summary, text.value);

	cal_component_get_description_list (priv->comp, &l);
	if (l) {
		text = *(CalComponentText *)l->data;
		e_dialog_editable_set (priv->description, text.value);
	}
	cal_component_free_text_list (l);
	
	/* Start and end times */

	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_time), ee);
	gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_time), ee);

	/* All-day events are inclusive, i.e. if the end date shown is 2nd Feb
	   then the event includes all of the 2nd Feb. We would normally show
	   3rd Feb as the end date, since it really ends at midnight on 3rd,
	   so we have to subtract a day so we only show the 2nd. */
	cal_component_get_dtstart (priv->comp, &d);
	dtstart = icaltime_as_timet (*d.value);
	cal_component_get_dtend (priv->comp, &d);
	dtend = icaltime_as_timet (*d.value);
	if (time_day_begin (dtstart) == dtstart
	    && time_day_begin (dtend) == dtend) {
		dtend = time_add_day (dtend, -1);
	}

	e_date_edit_set_time (E_DATE_EDIT (priv->start_time), dtstart);
	e_date_edit_set_time (E_DATE_EDIT (priv->end_time), dtend);

	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_time), ee);
	gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_time), ee);

	check_all_day (ee);

	/* Alarms */
#if 0
	e_dialog_toggle_set (priv->alarm_display, priv->ico->dalarm.enabled);
	e_dialog_toggle_set (priv->alarm_program, priv->ico->palarm.enabled);
	e_dialog_toggle_set (priv->alarm_audio, priv->ico->aalarm.enabled);
	e_dialog_toggle_set (priv->alarm_mail, priv->ico->malarm.enabled);
#endif
	/* Alarm data */
#if 0
	e_dialog_spin_set (priv->alarm_display_amount, priv->ico->dalarm.count);
	e_dialog_spin_set (priv->alarm_audio_amount, priv->ico->aalarm.count);
	e_dialog_spin_set (priv->alarm_program_amount, priv->ico->palarm.count);
	e_dialog_spin_set (priv->alarm_mail_amount, priv->ico->malarm.count);

	alarm_unit_set (priv->alarm_display_unit, priv->ico->dalarm.units);
	alarm_unit_set (priv->alarm_audio_unit, priv->ico->aalarm.units);
	alarm_unit_set (priv->alarm_program_unit, priv->ico->palarm.units);
	alarm_unit_set (priv->alarm_mail_unit, priv->ico->malarm.units);

	e_dialog_editable_set (priv->alarm_program_run_program_entry, priv->ico->palarm.data);
	e_dialog_editable_set (priv->alarm_mail_mail_to, priv->ico->malarm.data);
#endif
	/* Classification */
	cal_component_get_classification (priv->comp, &cl);
	switch (cl) {
	case CAL_COMPONENT_CLASS_PUBLIC:
	    	e_dialog_radio_set (priv->classification_radio, CAL_COMPONENT_CLASS_PUBLIC,
				    classification_map);
	case CAL_COMPONENT_CLASS_PRIVATE:
	    	e_dialog_radio_set (priv->classification_radio, CAL_COMPONENT_CLASS_PRIVATE,
				    classification_map);
	case CAL_COMPONENT_CLASS_CONFIDENTIAL:
	    	e_dialog_radio_set (priv->classification_radio, CAL_COMPONENT_CLASS_CONFIDENTIAL,
				    classification_map);
	default:
		/* What do do?  We can't g_assert_not_reached() since it is a
		 * value from an external file.
		 */
	}
	
	/* Recurrences */
#ifndef NO_WARNINGS
#warning "FIX ME"
#endif

	/* Need to handle recurrence dates as well as recurrence rules */
	/* Need to handle more than one rrule */
	if (cal_component_has_rrules (priv->comp)) {
		struct icalrecurrencetype *r;
		int i;
		
		cal_component_get_rrule_list (priv->comp, &list);
		r = list->data;
		
		switch (r->freq) {
		case ICAL_DAILY_RECURRENCE:
			e_dialog_radio_set (priv->recurrence_rule_daily, ICAL_DAILY_RECURRENCE,
					    recur_options_map);
			e_dialog_spin_set (priv->recurrence_rule_daily_days, r->interval);
			break;

		case ICAL_WEEKLY_RECURRENCE:
			e_dialog_radio_set (priv->recurrence_rule_weekly, ICAL_WEEKLY_RECURRENCE,
					    recur_options_map);
			e_dialog_spin_set (priv->recurrence_rule_weekly_weeks, r->interval);

			e_dialog_toggle_set (priv->recurrence_rule_weekly_sun, FALSE);
			e_dialog_toggle_set (priv->recurrence_rule_weekly_mon, FALSE);
			e_dialog_toggle_set (priv->recurrence_rule_weekly_tue, FALSE);
			e_dialog_toggle_set (priv->recurrence_rule_weekly_wed, FALSE);
			e_dialog_toggle_set (priv->recurrence_rule_weekly_thu, FALSE);
			e_dialog_toggle_set (priv->recurrence_rule_weekly_fri, FALSE);
			e_dialog_toggle_set (priv->recurrence_rule_weekly_sat, FALSE);

			for (i=0; i<8 && r->by_day[i] != SHRT_MAX; i++) {
				switch (r->by_day[i]) {
				case ICAL_SUNDAY_WEEKDAY:
					e_dialog_toggle_set (priv->recurrence_rule_weekly_sun, TRUE);
					break;
				case ICAL_MONDAY_WEEKDAY:
					e_dialog_toggle_set (priv->recurrence_rule_weekly_mon, TRUE);
					break;
				case ICAL_TUESDAY_WEEKDAY:
					e_dialog_toggle_set (priv->recurrence_rule_weekly_tue, TRUE);
					break;
				case ICAL_WEDNESDAY_WEEKDAY:
					e_dialog_toggle_set (priv->recurrence_rule_weekly_wed, TRUE);
					break;
				case ICAL_THURSDAY_WEEKDAY:
					e_dialog_toggle_set (priv->recurrence_rule_weekly_thu, TRUE);
					break;
				case ICAL_FRIDAY_WEEKDAY:
					e_dialog_toggle_set (priv->recurrence_rule_weekly_fri, TRUE);
					break;
				case ICAL_SATURDAY_WEEKDAY:
					e_dialog_toggle_set (priv->recurrence_rule_weekly_sat, TRUE);
					break;
				case ICAL_NO_WEEKDAY:
					break;
				}
			}
			break;

		case ICAL_MONTHLY_RECURRENCE:
			e_dialog_radio_set (priv->recurrence_rule_monthly, ICAL_MONTHLY_RECURRENCE,
					    recur_options_map);
			
			if (r->by_month_day[0] != SHRT_MAX) {
				e_dialog_toggle_set (priv->recurrence_rule_monthly_on_day, TRUE);
				e_dialog_spin_set (priv->recurrence_rule_monthly_day_nth, 
						   r->by_month_day[0]);
			} else if (r->by_day[0] != SHRT_MAX) {
				e_dialog_toggle_set (priv->recurrence_rule_monthly_weekday, TRUE);
				/* libical does not handle ints in by day */
/*  				e_dialog_option_menu_set (priv->recurrence_rule_monthly_week, */
/*  							  priv->ico->recur->u.month_pos, */
/*  							  month_pos_map); */
/*  				e_dialog_option_menu_set (priv->recurrence_rule_monthly_weekpos, */
/*  							  priv->ico->recur->weekday, */
/*  							  weekday_map); */
			}
			
			e_dialog_spin_set (priv->recurrence_rule_monthly_every_n_months,
					   r->interval);
			break;

		case ICAL_YEARLY_RECURRENCE:
			e_dialog_radio_set (priv->recurrence_rule_yearly, ICAL_YEARLY_RECURRENCE,
					    recur_options_map);
			e_dialog_spin_set (priv->recurrence_rule_yearly_every_n_years,
					   r->interval);
			break;

		default:
			g_assert_not_reached ();
		}

		if (r->until.year == 0) {
			if (r->count == 0)
				e_dialog_toggle_set (priv->recurrence_ending_date_repeat_forever,
						     TRUE);
			else {
				e_dialog_toggle_set (priv->recurrence_ending_date_end_after, TRUE);
				e_dialog_spin_set (priv->recurrence_ending_date_end_after_count,
						   r->count);
			}
		} else {
			time_t t = icaltime_as_timet (r->until);
			e_dialog_toggle_set (priv->recurrence_ending_date_end_on, TRUE);
			/* Shorten by one day, as we store end-on date a day ahead */
			/* FIXME is this correct? */
			e_date_edit_set_time (E_DATE_EDIT (priv->recurrence_ending_date_end_on_date), time_add_day (t, -1));
		}
		cal_component_free_recur_list (list);
	}

	/* Exceptions list */
#ifndef NO_WARNINGS
#warning "FIX ME"
#endif
	/* Need to handle exception rules as well as dates */
	cal_component_get_exdate_list (priv->comp, &list);
	for (l = list; l; l = l->next) {
		struct icaltimetype *t;
		time_t ext;
		
		t = l->data;
		ext = icaltime_as_timet (*t);
		append_exception (ee, ext);
	}
	cal_component_free_exdate_list (list);
}



/* Decode the radio button group for classifications */
static CalComponentClassification
classification_get (GtkWidget *widget)
{
	return e_dialog_radio_get (widget, classification_map);
}

/* Get the values of the widgets in the event editor and put them in the iCalObject */
static void
dialog_to_comp_object (EventEditor *ee)
{
	EventEditorPrivate *priv;
	CalComponent *comp;
	CalComponentText *text;
	CalComponentDateTime date;
	struct icalrecurrencetype recur;
	time_t t;
	gboolean all_day_event;
	GtkCList *exception_list;
	GSList *list;
	int i, pos = 0;
	
	priv = ee->priv;
	comp = priv->comp;

	text = g_new0 (CalComponentText, 1);
	text->value = e_dialog_editable_get (priv->general_summary);
	cal_component_set_summary (comp, text);

	list = NULL;
	text->value  = e_dialog_editable_get (priv->description);
	list = g_slist_prepend (list, text);
	cal_component_set_description_list (comp, list);
	cal_component_free_text_list (list);
	
	date.value = g_new (struct icaltimetype, 1);
	t = e_date_edit_get_time (E_DATE_EDIT (priv->start_time));
	*date.value = icaltime_from_timet (t, FALSE, FALSE);
	date.tzid = NULL;
	cal_component_set_dtstart (comp, &date);

	/* If the all_day toggle is set, the end date is inclusive of the
	   entire day on which it points to. */
	all_day_event = e_dialog_toggle_get (priv->all_day_event);
	t = e_date_edit_get_time (E_DATE_EDIT (priv->end_time));
	if (all_day_event)
		t = time_day_end (t);

	*date.value = icaltime_from_timet (t, FALSE, FALSE);
	cal_component_set_dtend (comp, &date);
	g_free (date.value);

#if 0
	ico->dalarm.enabled = e_dialog_toggle_get (priv->alarm_display);
	ico->aalarm.enabled = e_dialog_toggle_get (priv->alarm_program);
	ico->palarm.enabled = e_dialog_toggle_get (priv->alarm_audio);
	ico->malarm.enabled = e_dialog_toggle_get (priv->alarm_mail);

	ico->dalarm.count = e_dialog_spin_get_int (priv->alarm_display_amount);
	ico->aalarm.count = e_dialog_spin_get_int (priv->alarm_audio_amount);
	ico->palarm.count = e_dialog_spin_get_int (priv->alarm_program_amount);
	ico->malarm.count = e_dialog_spin_get_int (priv->alarm_mail_amount);

	ico->dalarm.units = alarm_unit_get (priv->alarm_display_unit);
	ico->aalarm.units = alarm_unit_get (priv->alarm_audio_unit);
	ico->palarm.units = alarm_unit_get (priv->alarm_program_unit);
	ico->malarm.units = alarm_unit_get (priv->alarm_mail_unit);

	if (ico->palarm.data)
		g_free (ico->palarm.data);

	if (ico->malarm.data)
		g_free (ico->malarm.data);

	ico->palarm.data = e_dialog_editable_get (priv->alarm_program_run_program_entry);
	ico->malarm.data = e_dialog_editable_get (priv->alarm_mail_mail_to);
#endif

	cal_component_set_classification (comp, classification_get (priv->classification_radio));

	/* Recurrence information */
  	icalrecurrencetype_clear (&recur);
	recur.freq = recur_options_get (priv->recurrence_rule_none);

	switch (recur.freq) {
	case ICAL_NO_RECURRENCE:
		/* nothing */
		break;

	case ICAL_DAILY_RECURRENCE:
		recur.interval = e_dialog_spin_get_int (priv->recurrence_rule_daily_days);
		break;

	case ICAL_WEEKLY_RECURRENCE:		
		recur.interval = e_dialog_spin_get_int (priv->recurrence_rule_weekly_weeks);
		
		if (e_dialog_toggle_get (priv->recurrence_rule_weekly_sun))
			recur.by_day[pos++] = ICAL_SUNDAY_WEEKDAY;		
		if (e_dialog_toggle_get (priv->recurrence_rule_weekly_mon))
			recur.by_day[pos++] = ICAL_MONDAY_WEEKDAY;
		if (e_dialog_toggle_get (priv->recurrence_rule_weekly_tue))
			recur.by_day[pos++] = ICAL_TUESDAY_WEEKDAY;
		if (e_dialog_toggle_get (priv->recurrence_rule_weekly_wed))
			recur.by_day[pos++] = ICAL_WEDNESDAY_WEEKDAY;
		if (e_dialog_toggle_get (priv->recurrence_rule_weekly_thu))
			recur.by_day[pos++] = ICAL_THURSDAY_WEEKDAY;
		if (e_dialog_toggle_get (priv->recurrence_rule_weekly_fri))
			recur.by_day[pos++] = ICAL_FRIDAY_WEEKDAY;
		if (e_dialog_toggle_get (priv->recurrence_rule_weekly_sat))
			recur.by_day[pos++] = ICAL_SATURDAY_WEEKDAY;

		break;

	case ICAL_MONTHLY_RECURRENCE:

		if (e_dialog_toggle_get (priv->recurrence_rule_monthly_on_day)) {
				/* by day of in the month (ex: the 5th) */
			recur.by_month_day[0] = 
				e_dialog_spin_get_int (priv->recurrence_rule_monthly_day_nth);
		} else if (e_dialog_toggle_get (priv->recurrence_rule_monthly_weekday)) {

/* "recurrence-rule-monthly-weekday" is TRUE */
				/* by position on the calendar (ex: 2nd monday) */
			/* libical does not handle this yet */
/*  			ico->recur->u.month_pos = e_dialog_option_menu_get ( */
/*  				priv->recurrence_rule_monthly_week, */
/*  				month_pos_map); */
/*  			ico->recur->weekday = e_dialog_option_menu_get ( */
/*  				priv->recurrence_rule_monthly_weekpos, */
/*  				weekday_map); */

		} else
			g_assert_not_reached ();

		recur.interval = e_dialog_spin_get_int (priv->recurrence_rule_monthly_every_n_months);

		break;

	case ICAL_YEARLY_RECURRENCE:
		recur.interval = e_dialog_spin_get_int (priv->recurrence_rule_yearly_every_n_years);
		break;
		
	default:
		g_assert_not_reached ();
	}

	if (recur.freq != ICAL_NO_RECURRENCE) {
		/* recurrence start of week */
		if (week_starts_on_monday)
			recur.week_start = ICAL_MONDAY_WEEKDAY;
		else
			recur.week_start = ICAL_SUNDAY_WEEKDAY;

		/* recurrence ending date */
		if (e_dialog_toggle_get (priv->recurrence_ending_date_end_on)) {
			/* Also here, to ensure that the event is used, we add a day
			 * to get the next day, in accordance to the RFC
			 */
			t = e_date_edit_get_time (E_DATE_EDIT (priv->recurrence_ending_date_end_on_date));
			t = time_add_day (t, 1);
			recur.until = icaltime_from_timet (t, TRUE, FALSE);
		} else if (e_dialog_toggle_get (priv->recurrence_ending_date_end_after)) {
			recur.count = e_dialog_spin_get_int (priv->recurrence_ending_date_end_after_count);
		}
		list = NULL;
		list = g_slist_append (list, &recur);
		cal_component_set_rrule_list (comp, list);
		g_slist_free (list);
	} else {
		list = NULL;
		cal_component_set_rrule_list (comp, list);		
	}
	
	/* Set exceptions */
	list = NULL;
	exception_list = GTK_CLIST (priv->recurrence_exceptions_list);
	for (i = 0; i < exception_list->rows; i++) {
		struct icaltimetype *tt;
		time_t *t;
		
		t = gtk_clist_get_row_data (exception_list, i);
		tt = g_new0 (struct icaltimetype, 1);
		*tt = icaltime_from_timet (*t, FALSE, FALSE);
		
		list = g_slist_prepend (list, tt);
	}
	cal_component_set_exdate_list (comp, list);
	if (list)
		cal_component_free_exdate_list (list);

	cal_component_commit_sequence (comp);
}

static void
save_event_object (EventEditor *ee)
{
	EventEditorPrivate *priv;
	char *title;

	priv = ee->priv;

	if (!priv->comp)
		return;

	dialog_to_comp_object (ee);

	title = make_title_from_comp (priv->comp);
	gtk_window_set_title (GTK_WINDOW (priv->app), title);
	g_free (title);

	if (!cal_client_update_object (priv->client, priv->comp))
		g_message ("save_event_object(): Could not update the object!");
}

/* Closes the dialog box and emits the appropriate signals */
static void
close_dialog (EventEditor *ee)
{
	EventEditorPrivate *priv;

	priv = ee->priv;

	g_assert (priv->app != NULL);

	gtk_object_destroy (GTK_OBJECT (ee));
}



/* File/Save callback */
static void
file_save_cb (GtkWidget *widget, gpointer data)
{
	EventEditor *ee;

	ee = EVENT_EDITOR (data);
	save_event_object (ee);
}

/* File/Save and Close callback */
static void
file_save_and_close_cb (GtkWidget *widget, gpointer data)
{
	EventEditor *ee;

	ee = EVENT_EDITOR (data);
	save_event_object (ee);
	close_dialog (ee);
}

/* File/Delete callback */
static void
file_delete_cb (GtkWidget *widget, gpointer data)
{
	EventEditor *ee;
	EventEditorPrivate *priv;
	const char *uid;
	
	ee = EVENT_EDITOR (data);

	g_return_if_fail (IS_EVENT_EDITOR (ee));

	priv = ee->priv;
	
	g_return_if_fail (priv->comp);

	cal_component_get_uid (priv->comp, &uid);

	/* We don't check the return value; FALSE can mean the object was not in
	 * the server anyways.
	 */
	cal_client_remove_object (priv->client, uid);

	close_dialog (ee);
}

/* File/Close callback */
static void
file_close_cb (GtkWidget *widget, gpointer data)
{
	EventEditor *ee;

	ee = EVENT_EDITOR (data);

	g_return_if_fail (IS_EVENT_EDITOR (ee));

	close_dialog (ee);
}

static void
schedule_meeting_cb (GtkWidget *widget, gpointer data)
{
	EventEditor *ee;
	EventEditorPrivate *priv;
	EMeetingEditor *editor;

	ee = EVENT_EDITOR (data);

	g_return_if_fail (IS_EVENT_EDITOR (ee));

	priv = (EventEditorPrivate *)ee->priv;

	editor = e_meeting_editor_new (priv->comp, priv->client);
	e_meeting_edit (editor);
	e_meeting_editor_free (editor);
}





/* Menu bar */

static GnomeUIInfo file_new_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Mail Message"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Contact"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Task"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Task _Request"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Journal Entry"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Note"), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Ch_oose Form..."), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo file_page_setup_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Memo Style"), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Define Print _Styles..."), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo file_menu[] = {
	GNOMEUIINFO_MENU_NEW_SUBTREE (file_new_menu),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: S_end"), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_MENU_SAVE_ITEM (file_save_cb, NULL),
	GNOMEUIINFO_MENU_SAVE_AS_ITEM (NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Save Attac_hments..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_STOCK (N_("_Delete"), NULL, 
				file_delete_cb, GNOME_STOCK_PIXMAP_TRASH),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Move to Folder..."), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Cop_y to Folder..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_SUBTREE (N_("Page Set_up"), file_page_setup_menu),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Print Pre_view"), NULL, NULL),
	GNOMEUIINFO_MENU_PRINT_ITEM (NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_MENU_PROPERTIES_ITEM (NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_MENU_CLOSE_ITEM (file_close_cb, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo edit_object_menu[] = {
	GNOMEUIINFO_ITEM_NONE ("FIXME: what goes here?", NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo edit_menu[] = {
	GNOMEUIINFO_MENU_UNDO_ITEM (NULL, NULL),
	GNOMEUIINFO_MENU_REDO_ITEM (NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_MENU_CUT_ITEM (NULL, NULL),
	GNOMEUIINFO_MENU_COPY_ITEM (NULL, NULL),
	GNOMEUIINFO_MENU_PASTE_ITEM (NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Paste _Special..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_MENU_CLEAR_ITEM (NULL, NULL),
	GNOMEUIINFO_MENU_SELECT_ALL_ITEM (NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Mark as U_nread"), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_MENU_FIND_ITEM (NULL, NULL),
	GNOMEUIINFO_MENU_FIND_AGAIN_ITEM (NULL, NULL),
	GNOMEUIINFO_SUBTREE (N_("_Object"), edit_object_menu),
	GNOMEUIINFO_END
};

static GnomeUIInfo view_previous_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Item"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Unread Item"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Fi_rst Item in Folder"), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo view_next_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Item"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Unread Item"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Last Item in Folder"), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo view_toolbars_menu[] = {
	{ GNOME_APP_UI_TOGGLEITEM, N_("FIXME: _Standard"), NULL, NULL, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, NULL, 0, 0, NULL },
	{ GNOME_APP_UI_TOGGLEITEM, N_("FIXME: _Formatting"), NULL, NULL, NULL, NULL,
	  GNOME_APP_PIXMAP_NONE, NULL, 0, 0, NULL },
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Customize..."), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo view_menu[] = {
	GNOMEUIINFO_SUBTREE (N_("Pre_vious"), view_previous_menu),
	GNOMEUIINFO_SUBTREE (N_("Ne_xt"), view_next_menu),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Ca_lendar..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_SUBTREE (N_("_Toolbars"), view_toolbars_menu),
	GNOMEUIINFO_END
};

static GnomeUIInfo insert_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _File..."), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: It_em..."), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Object..."), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo format_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Font..."), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Paragraph..."), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo tools_forms_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Ch_oose Form..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Desi_gn This Form"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: D_esign a Form..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Publish _Form..."), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Pu_blish Form As..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Script _Debugger"), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo tools_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _Spelling..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Chec_k Names"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Address _Book..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_SUBTREE (N_("_Forms"), tools_forms_menu),
	GNOMEUIINFO_END
};

static GnomeUIInfo actions_menu[] = {
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: _New Appointment"), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Rec_urrence..."), NULL, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("Schedule _Meeting..."), NULL, schedule_meeting_cb),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: Forward as v_Calendar"), NULL, NULL),
	GNOMEUIINFO_ITEM_NONE (N_("FIXME: For_ward"), NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo help_menu[] = {
	GNOMEUIINFO_ITEM_NONE ("FIXME: fix Bonobo so it supports help items!", NULL, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] = {
	GNOMEUIINFO_MENU_FILE_TREE (file_menu),
	GNOMEUIINFO_MENU_EDIT_TREE (edit_menu),
	GNOMEUIINFO_MENU_VIEW_TREE (view_menu),
	GNOMEUIINFO_SUBTREE (N_("_Insert"), insert_menu),
	GNOMEUIINFO_SUBTREE (N_("F_ormat"), format_menu),
	GNOMEUIINFO_SUBTREE (N_("_Tools"), tools_menu),
	GNOMEUIINFO_SUBTREE (N_("Actio_ns"), actions_menu),
	GNOMEUIINFO_MENU_HELP_TREE (help_menu),
	GNOMEUIINFO_END
};

/* Creates the menu bar for the event editor */
static void
create_menu (EventEditor *ee)
{
	EventEditorPrivate *priv;
	BonoboUIHandlerMenuItem *list;

	priv = ee->priv;

	bonobo_ui_handler_create_menubar (priv->uih);

	list = bonobo_ui_handler_menu_parse_uiinfo_list_with_data (main_menu, ee);
	bonobo_ui_handler_menu_add_list (priv->uih, "/", list);
}



/* Toolbar */

static GnomeUIInfo toolbar[] = {
	GNOMEUIINFO_ITEM_STOCK (N_("Save and Close"),
				N_("Save and close this appointment"),
				file_save_and_close_cb,
				GNOME_STOCK_PIXMAP_SAVE),

	GNOMEUIINFO_ITEM_STOCK (N_("Delete"),
				N_("Delete this appointment"), 
				file_delete_cb,
				GNOME_STOCK_PIXMAP_TRASH),

	GNOMEUIINFO_ITEM_STOCK (N_("Close"),
				N_("Close this appointment"),
				file_close_cb,
				GNOME_STOCK_PIXMAP_CLOSE),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (N_("FIXME: Print..."),
				N_("Print this item"), NULL,
				GNOME_STOCK_PIXMAP_PRINT),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (N_("FIXME: Insert File..."),
				N_("Insert a file as an attachment"), NULL,
				GNOME_STOCK_PIXMAP_ATTACH),
#if 0
	GNOMEUIINFO_ITEM_STOCK (N_("FIXME: Invite Attendees..."),
				N_("Invite attendees to a meeting"), NULL,
				GNOME_STOCK_PIXMAP_MULTIPLE),
#endif
	GNOMEUIINFO_SEPARATOR,


	GNOMEUIINFO_ITEM_STOCK (N_("FIXME: Previous"),
				N_("Go to the previous item"), NULL,
				GNOME_STOCK_PIXMAP_BACK),
	GNOMEUIINFO_ITEM_STOCK (N_("FIXME: Next"),
				N_("Go to the next item"), NULL,
				GNOME_STOCK_PIXMAP_FORWARD),
	GNOMEUIINFO_ITEM_STOCK (N_("FIXME: Help"),
				N_("See online help"), NULL, 
				GNOME_STOCK_PIXMAP_HELP),
	GNOMEUIINFO_END
};

/* Creates the toolbar for the event editor */
static void
create_toolbar (EventEditor *ee)
{
	EventEditorPrivate *priv;
	BonoboUIHandlerToolbarItem *list;

	priv = ee->priv;

	bonobo_ui_handler_create_toolbar (priv->uih, "Toolbar");
	list = bonobo_ui_handler_toolbar_parse_uiinfo_list_with_data (toolbar, ee);
	bonobo_ui_handler_toolbar_add_list (priv->uih, "/Toolbar", list);
}



/* Callback used when the dialog box is destroyed */
static gint
app_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	EventEditor *ee;

	/* FIXME: need to check for a dirty object */

	ee = EVENT_EDITOR (data);
	close_dialog (ee);

	return TRUE;
}

/**
 * event_editor_construct:
 * @ee: An event editor.
 * 
 * Constructs an event editor by loading its Glade data.
 * 
 * Return value: The same object as @ee, or NULL if the widgets could not be
 * created.  In the latter case, the event editor will automatically be
 * destroyed.
 **/
EventEditor *
event_editor_construct (EventEditor *ee)
{
	EventEditorPrivate *priv;
	GtkWidget *bonobo_win;

	g_return_val_if_fail (ee != NULL, NULL);
	g_return_val_if_fail (IS_EVENT_EDITOR (ee), NULL);

	priv = ee->priv;

	/* Load the content widgets */

	priv->xml = glade_xml_new (EVOLUTION_GLADEDIR "/event-editor-dialog.glade", NULL);
	if (!priv->xml) {
		g_message ("event_editor_construct(): Could not load the Glade XML file!");
		goto error;
	}

	if (!get_widgets (ee)) {
		g_message ("event_editor_construct(): Could not find all widgets in the XML file!");
		goto error;
	}

	init_widgets (ee);

	/* Construct the app */
	bonobo_win = bonobo_win_new ("event-editor-dialog", "Event Editor");

	/* FIXME: The sucking bit */
	{
		GtkWidget *contents;

		contents = gnome_dock_get_client_area (
			GNOME_DOCK (GNOME_APP (priv->app)->dock));
		if (!contents) {
			g_message ("event_editor_construct(): Could not get contents");
			goto error;
		}
		gtk_widget_ref (contents);
		gtk_container_remove (GTK_CONTAINER (contents->parent), contents);
		bonobo_win_set_contents (BONOBO_WIN (bonobo_win), contents);
		gtk_widget_destroy (priv->app);
		priv->app = bonobo_win;
	}

	priv->uih = bonobo_ui_handler_new ();
	if (!priv->uih) {
		g_message ("event_editor_construct(): Could not create the UI handler");
		goto error;
	}

	bonobo_ui_handler_set_app (priv->uih, BONOBO_WIN (priv->app));

	create_menu (ee);
	create_toolbar (ee);

	/* Hook to destruction of the dialog */

	gtk_signal_connect (GTK_OBJECT (priv->app), "delete_event",
			    GTK_SIGNAL_FUNC (app_delete_event_cb), ee);

	/* Show the dialog */

	gtk_widget_show (priv->app);

	/* Add focus to the summary entry*/

	gtk_widget_grab_focus (GTK_OBJECT (priv->general_summary));

	return ee;

 error:

	gtk_object_unref (GTK_OBJECT (ee));
	return NULL;
}

/**
 * event_editor_new:
 * 
 * Creates a new event editor dialog.
 * 
 * Return value: A newly-created event editor dialog, or NULL if the event
 * editor could not be created.
 **/
EventEditor *
event_editor_new (void)
{
	EventEditor *ee;

	ee = EVENT_EDITOR (gtk_type_new (TYPE_EVENT_EDITOR));
	return event_editor_construct (EVENT_EDITOR (ee));
}

/* Brings attention to a window by raising it and giving it focus */
static void
raise_and_focus (GtkWidget *widget)
{
	g_assert (GTK_WIDGET_REALIZED (widget));
	gdk_window_show (widget->window);
	gtk_widget_grab_focus (widget);
}

/* Callback used when the calendar client tells us that an object changed */
static void
obj_updated_cb (CalClient *client, const char *uid, gpointer data)
{
	EventEditor *ee;
	EventEditorPrivate *priv;
	CalComponent *comp;
	CalClientGetStatus status;
	const gchar *editing_uid;

	ee = EVENT_EDITOR (data);

	g_return_if_fail (IS_EVENT_EDITOR (ee));

	priv = ee->priv;
	
	/* If we aren't showing the object which has been updated, return. */
	if (!priv->comp)
	  return;
	cal_component_get_uid (priv->comp, &editing_uid);
	if (strcmp (uid, editing_uid))
	  return;


	/* Get the event from the server. */
	status = cal_client_get_object (priv->client, uid, &comp);

	switch (status) {
	case CAL_CLIENT_GET_SUCCESS:
		/* Everything is fine */
		break;

	case CAL_CLIENT_GET_SYNTAX_ERROR:
		g_message ("obj_updated_cb(): Syntax error when getting object `%s'", uid);
		return;

	case CAL_CLIENT_GET_NOT_FOUND:
		/* The object is no longer in the server, so do nothing */
		return;

	default:
		g_assert_not_reached ();
		return;
	}

	raise_and_focus (priv->app);
}

/* Callback used when the calendar client tells us that an object was removed */
static void
obj_removed_cb (CalClient *client, const char *uid, gpointer data)
{
	EventEditor *ee;
	EventEditorPrivate *priv;
	const gchar *editing_uid;

	ee = EVENT_EDITOR (data);

	g_return_if_fail (ee != NULL);
	g_return_if_fail (IS_EVENT_EDITOR (ee));

	priv = ee->priv;

	/* If we aren't showing the object which has been updated, return. */
	if (!priv->comp)
	  return;
	cal_component_get_uid (priv->comp, &editing_uid);
	if (strcmp (uid, editing_uid))
	  return;


	raise_and_focus (priv->app);
}

void 
event_editor_set_cal_client (EventEditor *ee, CalClient *client)
{
	EventEditorPrivate *priv;

	g_return_if_fail (ee != NULL);
	g_return_if_fail (IS_EVENT_EDITOR (ee));

	priv = ee->priv;

	if (client == priv->client)
		return;

	if (client)
		g_return_if_fail (IS_CAL_CLIENT (client));

	if (client)
		g_return_if_fail (cal_client_is_loaded (client));	
	
	if (client)
		gtk_object_ref (GTK_OBJECT (client));

	if (priv->client) {
		gtk_signal_disconnect_by_data (GTK_OBJECT (priv->client), ee);
		gtk_object_unref (GTK_OBJECT (priv->client));
	}

	priv->client = client;

	if (priv->client) {
		gtk_signal_connect (GTK_OBJECT (priv->client), "obj_updated",
				    GTK_SIGNAL_FUNC (obj_updated_cb), ee);
		gtk_signal_connect (GTK_OBJECT (priv->client), "obj_removed",
				    GTK_SIGNAL_FUNC (obj_removed_cb), ee);
	}
}

/**
 * event_editor_set_event_object:
 * @ee: An event editor.
 * @comp: A calendar object.
 * 
 * Sets the calendar object that an event editor dialog will manipulate.
 **/
void
event_editor_set_event_object (EventEditor *ee, CalComponent *comp)
{
	EventEditorPrivate *priv;
	char *title;

	g_return_if_fail (ee != NULL);
	g_return_if_fail (IS_EVENT_EDITOR (ee));

	priv = ee->priv;

	if (priv->comp) {
		gtk_object_unref (GTK_OBJECT (priv->comp));
		priv->comp = NULL;
	}

	if (comp) {
		priv->comp = cal_component_clone (comp);
	}

	title = make_title_from_comp (priv->comp);
	gtk_window_set_title (GTK_WINDOW (priv->app), title);
	g_free (title);

	fill_widgets (ee);
}

/**
 * event_editor_focus:
 * @ee: An event editor.
 * 
 * Makes sure an event editor is shown, on top of other windows, and focused.
 **/
void
event_editor_focus (EventEditor *ee)
{
	EventEditorPrivate *priv;

	g_return_if_fail (ee != NULL);
	g_return_if_fail (IS_EVENT_EDITOR (ee));

	priv = ee->priv;
	gtk_widget_show_now (priv->app);
	raise_and_focus (priv->app);
}

static void
alarm_toggle (GtkWidget *toggle, EventEditor *ee)
{
	EventEditorPrivate *priv;
	GtkWidget *alarm_amount = NULL;
	GtkWidget *alarm_unit = NULL;
	gboolean active;

	priv = ee->priv;

	active = GTK_TOGGLE_BUTTON (toggle)->active;

	if (toggle == priv->alarm_display) {
		alarm_amount = priv->alarm_display_amount;
		alarm_unit = priv->alarm_display_unit;
	} else if (toggle == priv->alarm_audio) {
		alarm_amount = priv->alarm_audio_amount;
		alarm_unit = priv->alarm_audio_unit;
	} else if (toggle == priv->alarm_program) {
		alarm_amount = priv->alarm_program_amount;
		alarm_unit = priv->alarm_program_unit;
		gtk_widget_set_sensitive (priv->alarm_program_run_program, active);
	} else if (toggle == priv->alarm_mail) {
		alarm_amount = priv->alarm_mail_amount;
		alarm_unit = priv->alarm_mail_unit;
		gtk_widget_set_sensitive (priv->alarm_mail_mail_to, active);
	} else
		g_assert_not_reached ();

	gtk_widget_set_sensitive (alarm_amount, active);
	gtk_widget_set_sensitive (alarm_unit, active);
}

/*
 * Checks if the day range occupies all the day, and if so, check the
 * box accordingly
 */
static void
check_all_day (EventEditor *ee)
{
	EventEditorPrivate *priv;
	time_t ev_start, ev_end;

	priv = ee->priv;

	ev_start = e_date_edit_get_time (E_DATE_EDIT (priv->start_time));
	ev_end = e_date_edit_get_time (E_DATE_EDIT (priv->end_time));

	/* all day event checkbox */
	if (time_day_begin (ev_start) == ev_start
	    && time_day_begin (ev_end) == ev_end)
		e_dialog_toggle_set (priv->all_day_event, TRUE);
	else
		e_dialog_toggle_set (priv->all_day_event, FALSE);
}

/*
 * Callback: all day event box clicked
 */
static void
set_all_day (GtkWidget *toggle, EventEditor *ee)
{
	EventEditorPrivate *priv;
	struct tm start_tm, end_tm;
	time_t start_t, end_t;
	gboolean all_day;

	priv = ee->priv;

	/* If the all_day toggle is set, the end date is inclusive of the
	   entire day on which it points to. */
	all_day = GTK_TOGGLE_BUTTON (toggle)->active;

	start_t = e_date_edit_get_time (E_DATE_EDIT (priv->start_time));
	start_tm = *localtime (&start_t);
	start_tm.tm_min  = 0;
	start_tm.tm_sec  = 0;

	if (all_day)
		start_tm.tm_hour = 0;
	else
		start_tm.tm_hour = day_begin;

	e_date_edit_set_time (E_DATE_EDIT (priv->start_time),
			      mktime (&start_tm));

	end_t = e_date_edit_get_time (E_DATE_EDIT (priv->end_time));
	end_tm = *localtime (&end_t);
	end_tm.tm_min  = 0;
	end_tm.tm_sec  = 0;

	if (all_day) {
		/* mktime() will fix this if we go past the end of the month.*/
		end_tm.tm_hour = 0;
	} else {
		if (end_tm.tm_year == start_tm.tm_year
		    && end_tm.tm_mon == start_tm.tm_mon
		    && end_tm.tm_mday == start_tm.tm_mday
		    && end_tm.tm_hour <= start_tm.tm_hour)
			end_tm.tm_hour = start_tm.tm_hour + 1;
	}

	e_date_edit_set_show_time (E_DATE_EDIT (priv->start_time), !all_day);
	e_date_edit_set_show_time (E_DATE_EDIT (priv->end_time), !all_day);
	e_date_edit_set_time (E_DATE_EDIT (priv->end_time), mktime (&end_tm));
}

/*
 * Callback: checks that the dates are start < end
 */
static void
check_dates (EDateEdit *dedit, EventEditor *ee)
{
	EventEditorPrivate *priv;
	time_t start, end;
	struct tm tm_start, tm_end;

	priv = ee->priv;

	start = e_date_edit_get_time (E_DATE_EDIT (priv->start_time));
	end = e_date_edit_get_time (E_DATE_EDIT (priv->end_time));

	if (start > end) {
		tm_start = *localtime (&start);
		tm_end = *localtime (&end);

		if (GTK_WIDGET (dedit) == priv->start_time) {
			tm_end.tm_year = tm_start.tm_year;
			tm_end.tm_mon  = tm_start.tm_mon;
			tm_end.tm_mday = tm_start.tm_mday;

			gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_time), ee);
			e_date_edit_set_time (E_DATE_EDIT (priv->end_time),
					      mktime (&tm_end));
			gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_time), ee);
		} else if (GTK_WIDGET (dedit) == priv->end_time) {
			tm_start.tm_year = tm_end.tm_year;
			tm_start.tm_mon  = tm_end.tm_mon;
			tm_start.tm_mday = tm_end.tm_mday;

#if 0
			gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_time), ee);
			e_date_edit_set_time (E_DATE_EDIT (priv->start_time),
					      mktime (&tm_start));
			gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_time), ee);
#endif
		}
	}
}

/*
 * Callback: checks that start_time < end_time and whether the
 * selected hour range spans all of the day
 */
static void
check_times (EDateEdit *dedit, EventEditor *ee)
{
	EventEditorPrivate *priv;
	time_t start, end;
	struct tm tm_start, tm_end;

	priv = ee->priv;
#if 0
	gdk_pointer_ungrab (GDK_CURRENT_TIME);
	gdk_flush ();
#endif
	start = e_date_edit_get_time (E_DATE_EDIT (priv->start_time));
	end = e_date_edit_get_time (E_DATE_EDIT (priv->end_time));

	if (start > end) {
		tm_start = *localtime (&start);
		tm_end = *localtime (&end);

		if (GTK_WIDGET (dedit) == priv->start_time) {
			tm_end.tm_min  = tm_start.tm_min;
			tm_end.tm_sec  = tm_start.tm_sec;
			tm_end.tm_hour = tm_start.tm_hour + 1;

			if (tm_end.tm_hour >= 24) {
				tm_end.tm_hour = 24; /* mktime() will bump the day */
				tm_end.tm_min = 0;
				tm_end.tm_sec = 0;
			}

			gtk_signal_handler_block_by_data (GTK_OBJECT (priv->end_time), ee);
			e_date_edit_set_time (E_DATE_EDIT (priv->end_time),
					      mktime (&tm_end));
			gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->end_time), ee);
		} else if (GTK_WIDGET (dedit) == priv->end_time) {
			tm_start.tm_min  = tm_end.tm_min;
			tm_start.tm_sec  = tm_end.tm_sec;
			tm_start.tm_hour = tm_end.tm_hour - 1;

			if (tm_start.tm_hour < 0) {
				tm_start.tm_hour = 0;
				tm_start.tm_min = 0;
				tm_start.tm_min = 0;
			}

			gtk_signal_handler_block_by_data (GTK_OBJECT (priv->start_time), ee);
			e_date_edit_set_time (E_DATE_EDIT (priv->start_time),
					      mktime (&tm_start));
			gtk_signal_handler_unblock_by_data (GTK_OBJECT (priv->start_time), ee);
		}
	}

	/* Check whether the event spans the whole day */

	check_all_day (ee);
}

static void
recurrence_toggled (GtkWidget *radio, EventEditor *ee)
{
	EventEditorPrivate *priv;
	icalrecurrencetype_frequency rf;

	priv = ee->priv;

	if (!GTK_TOGGLE_BUTTON (radio)->active)
		return;

	rf = e_dialog_radio_get (radio, recur_options_map);

	/* This is a hack to get things working */
	gtk_notebook_set_page (GTK_NOTEBOOK (priv->recurrence_rule_notebook), 
			       (int) (rf - ICAL_HOURLY_RECURRENCE));
}


static char *
get_exception_string (time_t t)
{
	static char buf[256];

	strftime (buf, sizeof (buf), _("%a %b %d %Y"), localtime (&t));
	return buf;
}


static void
append_exception (EventEditor *ee, time_t t)
{
	EventEditorPrivate *priv;
	time_t *tt;
	char *c[1];
	int i;
	GtkCList *clist;

	priv = ee->priv;

	tt = g_new (time_t, 1);
	*tt = t;

	clist = GTK_CLIST (priv->recurrence_exceptions_list);

	c[0] = get_exception_string (t);
	i = e_utf8_gtk_clist_append (clist, c);

	gtk_clist_set_row_data (clist, i, tt);
	gtk_clist_select_row (clist, i, 0);

/*  	gtk_widget_set_sensitive (ee->recur_ex_vbox, TRUE); */
}


static void
recurrence_exception_added (GtkWidget *widget, EventEditor *ee)
{
	EventEditorPrivate *priv;
	time_t t;

	priv = ee->priv;

	t = e_date_edit_get_time (E_DATE_EDIT (priv->recurrence_exceptions_date));
	append_exception (ee, t);
}


static void
recurrence_exception_deleted (GtkWidget *widget, EventEditor *ee)
{
	EventEditorPrivate *priv;
	GtkCList *clist;
	int sel;

	priv = ee->priv;

	clist = GTK_CLIST (priv->recurrence_exceptions_list);
	if (!clist->selection)
		return;

	sel = GPOINTER_TO_INT (clist->selection->data);

	g_free (gtk_clist_get_row_data (clist, sel)); /* free the time_t stored there */

	gtk_clist_remove (clist, sel);
	if (sel >= clist->rows)
		sel--;

	gtk_clist_select_row (clist, sel, 0);
}


static void
recurrence_exception_changed (GtkWidget *widget, EventEditor *ee)
{
	EventEditorPrivate *priv;
	GtkCList *clist;
	time_t *t;
	int sel;

	priv = ee->priv;

	clist = GTK_CLIST (priv->recurrence_exceptions_list);
	if (!clist->selection)
		return;

	sel = GPOINTER_TO_INT (clist->selection->data);

	t = gtk_clist_get_row_data (clist, sel);
	*t = e_date_edit_get_time (E_DATE_EDIT (priv->recurrence_exceptions_date));

	e_utf8_gtk_clist_set_text (clist, sel, 0, get_exception_string (*t));
}


GtkWidget *
make_date_edit (void)
{
	return date_edit_new (time (NULL), FALSE);
}


GtkWidget *
make_date_edit_with_time (void)
{
	return date_edit_new (time (NULL), TRUE);
}


GtkWidget *
date_edit_new (time_t the_time, int show_time)
{
	GtkWidget *dedit;

	dedit = e_date_edit_new ();
	/* FIXME: Set other options. */
	e_date_edit_set_show_time (E_DATE_EDIT (dedit), show_time);
	e_date_edit_set_time_popup_range (E_DATE_EDIT (dedit), 8, 18);
	return dedit;
}



GtkWidget *
make_spin_button (int val, int low, int high)
{
	GtkAdjustment *adj;
	GtkWidget *spin;

	adj = GTK_ADJUSTMENT (gtk_adjustment_new (val, low, high, 1, 10, 10));
	spin = gtk_spin_button_new (adj, 0.5, 0);
	gtk_widget_set_usize (spin, 60, 0);

	return spin;
}


/* todo

   build some of the recur stuff by hand to take into account
   the start-on-monday preference?

   get the apply button to work right

   make the properties stuff unglobal

   figure out why alarm units aren't sticking between edits

   closing the dialog window with the wm caused a crash
   Gtk-WARNING **: invalid cast from `(unknown)' to `GnomeDialog'
   on line 669:  gnome_dialog_close (GNOME_DIALOG(dialog->dialog));
 */
