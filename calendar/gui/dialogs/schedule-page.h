/* Evolution calendar - Scheduling page
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: JP Rosevear <jpr@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#ifndef SCHEDULE_PAGE_H
#define SCHEDULE_PAGE_H

#include "../e-meeting-model.h"
#include "comp-editor-page.h"

BEGIN_GNOME_DECLS



#define TYPE_SCHEDULE_PAGE            (schedule_page_get_type ())
#define SCHEDULE_PAGE(obj)            (GTK_CHECK_CAST ((obj), TYPE_SCHEDULE_PAGE, SchedulePage))
#define SCHEDULE_PAGE_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), TYPE_SCHEDULE_PAGE, SchedulePageClass))
#define IS_SCHEDULE_PAGE(obj)         (GTK_CHECK_TYPE ((obj), TYPE_SCHEDULE_PAGE))
#define IS_SCHEDULE_PAGE_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((obj), TYPE_SCHEDULE_PAGE))

typedef struct _SchedulePagePrivate SchedulePagePrivate;

typedef struct {
	CompEditorPage page;

	/* Private data */
	SchedulePagePrivate *priv;
} SchedulePage;

typedef struct {
	CompEditorPageClass parent_class;
} SchedulePageClass;


GtkType      schedule_page_get_type  (void);
SchedulePage *schedule_page_construct (SchedulePage *mpage, EMeetingModel *emm);
SchedulePage *schedule_page_new       (EMeetingModel *emm);



END_GNOME_DECLS

#endif
