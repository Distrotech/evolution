
#ifndef _EM_SUBSCRIBE_EDITOR_H
#define _EM_SUBSCRIBE_EDITOR_H

#include "e-util/e-msgport.h"
#include <gtk/gtkdialog.h>

typedef struct _EMSubscribeEditor EMSubscribeEditor;
typedef struct _EMSubscribeEditorClass EMSubscribeEditorClass;

struct _EMSubscribeEditor {
	GtkDialog dialog;

	EDList stores;

	struct _EMSubscribe *current; /* the current one, if any */

	GtkWidget *vbox;	/* where new stores are added */
	GtkWidget *optionmenu;
	GtkWidget *none_selected; /* 'please select a xxx' message */
	GtkWidget *subscribe_button;
	GtkWidget *unsubscribe_button;
};

struct _EMSubscribeEditorClass {
	GtkDialogClass dialog_class;
};

GType em_subscribe_editor_get_type(void);
GtkWidget *em_subscribe_editor_new(void);

#endif /* ! _EM_SUBSCRIBE_EDITOR_H */
