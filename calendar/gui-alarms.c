#include <config.h>
#include <unistd.h>
#include "calendar.h"
#include "alarm.h"
#include "timeutil.h"
#include "../libversit/vcc.h"

static void
try_add (iCalObject *ico, CalendarAlarm *alarm, time_t start, time_t end)
{
	alarm->trigger = start-alarm->offset;
	
	if (alarm->trigger < calendar_day_begin)
		return;
	if (alarm->trigger > calendar_day_end)
		return;
	alarm_add (alarm, &calendar_notify, ico);
}

static int
add_alarm (iCalObject *obj, time_t start, time_t end, void *closure)
{
	if (obj->aalarm.enabled)
		try_add (obj, &obj->aalarm, start, end);
	if (obj->dalarm.enabled)
		try_add (obj, &obj->dalarm, start, end);
	if (obj->palarm.enabled)
		try_add (obj,&obj->palarm, start, end);
	if (obj->malarm.enabled)
		try_add (obj, &obj->malarm, start, end);
	
	return TRUE;
}

#define max(a,b) ((a > b) ? a : b)

void
ical_object_try_alarms (iCalObject *obj)
{
	int ao, po, od, mo;
	int max_o;
	
	ao = alarm_compute_offset (&obj->aalarm);
	po = alarm_compute_offset (&obj->palarm);
	od = alarm_compute_offset (&obj->dalarm);
	mo = alarm_compute_offset (&obj->malarm);
	
	max_o = max (ao, max (po, max (od, mo)));
	if (max_o == -1)
		return;
	
	ical_object_generate_events (obj, calendar_day_begin, calendar_day_end + max_o, add_alarm, obj);
}

