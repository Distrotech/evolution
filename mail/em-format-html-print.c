
#include <libgnomeprint/gnome-print-job.h>
#include <libgnomeprintui/gnome-print-job-preview.h>

#include <gtkhtml/gtkhtml.h>
#include <gtk/gtkwindow.h>

#include <camel/camel-i18n.h>
#include "em-format-html-print.h"

#include <string.h>

static void efhp_builtin_init(EMFormatHTMLPrintClass *efhc);

static EMFormatHTMLClass *efhp_parent;

static void
efhp_init(GObject *o)
{
	EMFormatHTMLPrint *efhp = (EMFormatHTMLPrint *)o;
	GtkWidget *html = (GtkWidget *)efhp->formathtml.html;

	/* ?? */
	gtk_widget_set_name(html, "EvolutionMailPrintHTMLWidget");

	/* gtk widgets don't like to be realized outside top level widget
	   so we put new html widget into gtk window */
	efhp->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_add((GtkContainer *)efhp->window, html);
	gtk_widget_realize(html);
}

static void
efhp_finalise(GObject *o)
{
	EMFormatHTMLPrint *efhp = (EMFormatHTMLPrint *)o;

	gtk_widget_destroy(efhp->window);

	((GObjectClass *)efhp_parent)->finalize(o);
}

static void
efhp_class_init(GObjectClass *klass)
{
	klass->finalize = efhp_finalise;

	efhp_builtin_init((EMFormatHTMLPrintClass *)klass);
}

GType
em_format_html_print_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMFormatHTMLPrintClass),
			NULL, NULL,
			(GClassInitFunc)efhp_class_init,
			NULL, NULL,
			sizeof(EMFormatHTMLPrint), 0,
			(GInstanceInitFunc)efhp_init
		};
		efhp_parent = g_type_class_ref(em_format_html_get_type());
		type = g_type_register_static(em_format_html_get_type(), "EMFormatHTMLPrint", &info, 0);
	}

	return type;
}

EMFormatHTMLPrint *em_format_html_print_new(void)
{
	EMFormatHTMLPrint *efhp;

	efhp = g_object_new(em_format_html_print_get_type(), 0);

	return efhp;
}

struct footer_info {
	GnomeFont *local_font;
	gint page_num, pages;
};

static void
efhp_footer_cb(GtkHTML *html, GnomePrintContext *print_context, double x, double y, double width, double height, void *data)
{
	struct footer_info *info = data;

	/* do we want anything nicer here, like who its from, etc? */
	if (info->local_font) {
		char *text = g_strdup_printf (_("Page %d of %d"), info->page_num, info->pages);
		/*gdouble tw = gnome_font_get_width_string (info->local_font, text);*/
		/* FIXME: work out how to measure this */
		gdouble tw = strlen(text) * 8;
		
		gnome_print_gsave(print_context);
		gnome_print_newpath(print_context);
		gnome_print_setrgbcolor(print_context, .0, .0, .0);
		gnome_print_moveto(print_context, x + width - tw, y - gnome_font_get_ascender(info->local_font));
		gnome_print_setfont(print_context, info->local_font);
		gnome_print_show(print_context, text);
		gnome_print_grestore(print_context);
		
		g_free(text);
		info->page_num++;
	}
}

/* perform preview, or print */
/* returns GNOME_PRINT_OK on success */
int
em_format_html_print_print(EMFormatHTMLPrint *efhp, GnomePrintConfig *print_config, int preview)
{
	GnomePrintContext *print_context;
	GnomePrintJob *print_job;
	gdouble line = 0.0;
	struct footer_info info;
	int res = GNOME_PRINT_OK;

	print_job = gnome_print_job_new(print_config);
	print_context = gnome_print_job_get_context(print_job);

	gtk_html_print_set_master(efhp->formathtml.html, print_job);
	info.local_font = gnome_font_find_closest("Helvetica", 10.0);	
	if (info.local_font) {
		line = gnome_font_get_ascender(info.local_font) - gnome_font_get_descender(info.local_font);	
		info.page_num = 1;
		info.pages = gtk_html_print_get_pages_num(efhp->formathtml.html, print_context, 0.0, line);
		gtk_html_print_with_header_footer(efhp->formathtml.html, print_context, 0.0, line, NULL, efhp_footer_cb, &info);
		gnome_font_unref(info.local_font);
	} else {
		gtk_html_print(efhp->formathtml.html, print_context);
	}
	gtk_html_print_set_master(efhp->formathtml.html, NULL);

	gnome_print_job_close(print_job);

	if (preview)
		gtk_widget_show(gnome_print_job_preview_new(print_job, _("Print Preview")));
	else
		res = gnome_print_job_print(print_job);

	g_object_unref(print_job);

	return res;
}

/* ********************************************************************** */

/* if only ... but i doubt this is possible with gnome print/gtkhtml */
static EMFormatHandler type_builtin_table[] = {
	/*{ "application/postscript", (EMFormatFunc)efhp_application_postscript },*/
};

static void
efhp_builtin_init(EMFormatHTMLPrintClass *efhc)
{
	int i;

	for (i=0;i<sizeof(type_builtin_table)/sizeof(type_builtin_table[0]);i++)
		em_format_class_add_handler((EMFormatClass *)efhc, &type_builtin_table[i]);
}
