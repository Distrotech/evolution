/* Evolution calendar - Timezone selector dialog
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Damon Chaplin <damon@ximian.com>
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
 */
#include "e-send-options-utils.h"
#include "../calendar-config.h"

ESendOptionsDialog *sod = NULL;


void 
e_sendoptions_utils_set_default_data (ESendOptionsDialog *sod) 
{
	ESendOptionsGeneral *gopts;
	ESendOptionsStatusTracking *sopts;	
	
	gopts = sod->data->gopts;
	sopts = sod->data->sopts;
	
	gopts->priority = E_PRIORITY_STANDARD;
	gopts->reply_enabled = FALSE;
	gopts->expiration_enabled = FALSE;
	gopts->delay_enabled = FALSE;
	gopts->priority = E_PRIORITY_STANDARD;
	
	sopts->tracking_enabled = TRUE;
	sopts->track_when = E_DELIVERED_OPENED;
	sopts->opened = E_RETURN_NOTIFY_NONE;
	sopts->accepted = E_RETURN_NOTIFY_NONE;
	sopts->declined = E_RETURN_NOTIFY_NONE;
	sopts->completed = E_RETURN_NOTIFY_NONE;
}

void 
e_sendoptions_utils_fill_component (ESendOptionsDialog *sod, ECalComponent *comp) 
{
	int i = 1;
	icalproperty *prop;
	icalcomponent *icalcomp;
	ESendOptionsGeneral *gopts;
	ESendOptionsStatusTracking *sopts;

	gopts = sod->data->gopts;
	sopts = sod->data->sopts;

	e_cal_component_set_sequence (comp, &i);
	icalcomp = e_cal_component_get_icalcomponent (comp);

	if (e_sendoptions_get_need_general_options (sod)) {
		prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", gopts->priority));
		icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-PRIORITY");
		icalcomponent_add_property (icalcomp, prop);	

		if (gopts->reply_enabled) {
			if (gopts->reply_convenient) 
				prop = icalproperty_new_x ("convenient");	
			else 
				prop = icalproperty_new_x ((const char *) g_strdup_printf ( "%d", gopts->reply_within));
			icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-REPLY");
			icalcomponent_add_property (icalcomp, prop);
		}

		if (gopts->expiration_enabled && gopts->expire_after) {
			prop = icalproperty_new_x ((const char *) g_strdup_printf ( "%d", gopts->expire_after));	
			icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-EXPIRE");
			icalcomponent_add_property (icalcomp, prop);
		}

		if (gopts->delay_enabled) {
			struct icaltimetype temp;
			icaltimezone *zone = calendar_config_get_icaltimezone ();
			temp = icaltime_from_timet_with_zone (gopts->delay_until, FALSE, zone);	
			prop = icalproperty_new_x (icaltime_as_ical_string (temp));
			icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-DELAY");
			icalcomponent_add_property (icalcomp, prop);
		}
	}
	
	if (sopts->tracking_enabled) 
		prop = icalproperty_new_x ((const char *) g_strdup_printf ( "%d", sopts->track_when));	
	else
		prop = icalproperty_new_x ("0");

	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-TRACKINFO");
	icalcomponent_add_property (icalcomp, prop);
		
	
	prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", sopts->opened));	
	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-OPENED");
	icalcomponent_add_property (icalcomp, prop);
	
	prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", sopts->accepted));	
	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-ACCEPTED");
	icalcomponent_add_property (icalcomp, prop);

	prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", sopts->declined));	
	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-DECLINED");
	icalcomponent_add_property (icalcomp, prop);

	prop = icalproperty_new_x ((const char *) g_strdup_printf ("%d", sopts->completed));	
	icalproperty_set_x_name (prop, "X-EVOLUTION-OPTIONS-COMPLETED");
	icalcomponent_add_property (icalcomp, prop);
}

