
#ifndef _EM_FOLDER_BROWSER_H
#define _EM_FOLDER_BROWSER_H

#include "em-folder-view.h"

typedef struct _EMFolderBrowser EMFolderBrowser;
typedef struct _EMFolderBrowserClass EMFolderBrowserClass;

struct _EMFolderBrowser {
	EMFolderView view;

	GtkWidget *vpane;

	struct _EMFolderBrowserPrivate *priv;
};

struct _EMFolderBrowserClass {
	EMFolderViewClass parent_class;
};

GType em_folder_browser_get_type(void);

GtkWidget *em_folder_browser_new(void);

void em_folder_browser_show_preview(EMFolderBrowser *emfv, gboolean state);

#endif /* ! _EM_FOLDER_BROWSER_H */
