/*
 * e-mail-display.c
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
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define LIBSOUP_USE_UNSTABLE_REQUEST_API

#include "e-mail-display.h"

#include <glib/gi18n.h>
#include <gdk/gdk.h>

#include "e-util/e-marshal.h"
#include "e-util/e-util.h"
#include "e-util/e-plugin-ui.h"
#include "mail/em-composer-utils.h"
#include "mail/em-utils.h"
#include "mail/e-mail-request.h"
#include "mail/em-format-html-display.h"
#include "mail/e-mail-attachment-bar.h"
#include "widgets/misc/e-attachment-button.h"

#include <camel/camel.h>

#include <libsoup/soup.h>
#include <libsoup/soup-requester.h>

#include <JavaScriptCore/JavaScript.h>

#define d(x)

#define E_MAIL_DISPLAY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_DISPLAY, EMailDisplayPrivate))

struct _EMailDisplayPrivate {
	GtkWidget *box;

	ESearchBar *searchbar;
	EMFormatHTML *formatter;

	EMFormatWriteMode mode;
	gboolean headers_collapsable;
	gboolean headers_collapsed;

	GList *webviews;

        GtkWidget *current_webview;

	GtkActionGroup *mailto_actions;
        GtkActionGroup *images_actions;

        guint caret_mode:1;
        guint force_image_load:1;
	gfloat zoom_level;

	WebKitWebSettings *settings;
        WebKitWebSettings *headers_settings;
};

enum {
	PROP_0,
	PROP_FORMATTER,
	PROP_MODE,
	PROP_HEADERS_COLLAPSABLE,
	PROP_HEADERS_COLLAPSED,
        PROP_CARET_MODE,
	PROP_ZOOM_LEVEL
};

enum {
	POPUP_EVENT,
	STATUS_MESSAGE,
        LAST_SIGNAL
};

static gint signals[LAST_SIGNAL];

static gpointer parent_class;

static CamelDataCache *emd_global_http_cache = 0;

static const gchar *ui =
"<ui>"
"  <popup name='context'>"
"    <placeholder name='custom-actions-1'>"
"      <menuitem action='add-to-address-book'/>"
"      <menuitem action='send-reply'/>"
"    </placeholder>"
"    <placeholder name='custom-actions-3'>"
"      <menu action='search-folder-menu'>"
"        <menuitem action='search-folder-recipient'/>"
"        <menuitem action='search-folder-sender'/>"
"      </menu>"
"    </placeholder>"
"  </popup>"
"</ui>";

static const gchar *image_ui =
"<ui>"
"  <popup name='context'>"
"    <placeholder name='custom-actions-2'>"
"      <menuitem action='image-save'/>"
"    </placeholder>"
"  </popup>"
"</ui>";

static GtkActionEntry mailto_entries[] = {

	{ "add-to-address-book",
	  "contact-new",
	  N_("_Add to Address Book..."),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  NULL   /* Handled by EMailReader */ },

	{ "search-folder-recipient",
	  NULL,
	  N_("_To This Address"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  NULL   /* Handled by EMailReader */ },

	{ "search-folder-sender",
	  NULL,
	  N_("_From This Address"),
	  NULL,
	  NULL,  /* XXX Add a tooltip! */
	  NULL   /* Handled by EMailReader */ },

	{ "send-reply",
	  NULL,
	  N_("Send _Reply To..."),
	  NULL,
	  N_("Send a reply message to this address"),
	  NULL   /* Handled by EMailReader */ },

	/*** Menus ***/

	{ "search-folder-menu",
	  "folder-saved-search",
	  N_("Create Search _Folder"),
	  NULL,
	  NULL,
	  NULL }
};


static GtkActionEntry image_entries[] = {

        { "image-save",
        GTK_STOCK_SAVE,
        N_("Save _Image..."),
        NULL,
        N_("Save the image to a file"),
        NULL    /* Handled by EMailReader */ },

};


static gboolean
mail_display_webview_enter_notify_event (GtkWidget *widget,
                                         GdkEvent *event,
                                         gpointer user_data)
{
      EMailDisplay *display = user_data;

      /* This handler should always be connected to EWebView
       * signals only! */
      g_return_val_if_fail (E_IS_WEB_VIEW (widget), FALSE);

      if (event->crossing.mode == GDK_CROSSING_NORMAL)
              display->priv->current_webview = widget;

      return FALSE;
}

static void
mail_display_webview_update_actions (EWebView *web_view,
                                     gpointer user_data)
{
        const gchar *image_src;
        gboolean visible;
        GtkAction *action;

        g_return_if_fail (web_view != NULL);

        image_src = e_web_view_get_cursor_image_src (web_view);
        visible = image_src && g_str_has_prefix (image_src, "cid:");
        if (!visible && image_src) {
                CamelStream *image_stream;

                image_stream = camel_data_cache_get (emd_global_http_cache, "http", image_src, NULL);

                visible = image_stream != NULL;

                if (image_stream)
                        g_object_unref (image_stream);
        }

        action = e_web_view_get_action (web_view, "image-save");
        if (action)
                gtk_action_set_visible (action, visible);
}



static void
formatter_image_loading_policy_changed_cb (GObject *object,
                                           GParamSpec *pspec,
                                           gpointer user_data)
{
        EMailDisplay *display = user_data;

        e_mail_display_load_images (display);
}

static void
mail_display_update_formatter_colors (EMailDisplay *display)
{
	EMFormatHTMLColorType type;
	EMFormatHTML *formatter;
	GdkColor *color;
	GtkStateType state;
	GtkStyle *style;

	state = gtk_widget_get_state (GTK_WIDGET (display));
	formatter = display->priv->formatter;

	if (!display->priv->formatter)
		return;

	style = gtk_widget_get_style (GTK_WIDGET (display));
	if (style == NULL)
		return;

	g_object_freeze_notify (G_OBJECT (formatter));

	color = &style->bg[state];
	type = EM_FORMAT_HTML_COLOR_BODY;
	em_format_html_set_color (formatter, type, color);

	color = &style->base[GTK_STATE_NORMAL];
	type = EM_FORMAT_HTML_COLOR_CONTENT;
	em_format_html_set_color (formatter, type, color);

	color = &style->dark[state];
	type = EM_FORMAT_HTML_COLOR_FRAME;
	em_format_html_set_color (formatter, type, color);

	color = &style->fg[state];
	type = EM_FORMAT_HTML_COLOR_HEADER;
	em_format_html_set_color (formatter, type, color);

	color = &style->text[state];
	type = EM_FORMAT_HTML_COLOR_TEXT;
	em_format_html_set_color (formatter, type, color);

	g_object_thaw_notify (G_OBJECT (formatter));
}

static void
mail_display_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	EMailDisplayPrivate *priv = E_MAIL_DISPLAY (object)->priv;

	switch (property_id) {
		case PROP_FORMATTER:
			e_mail_display_set_formatter (
				E_MAIL_DISPLAY (object),
				g_value_get_object (value));
			return;
		case PROP_MODE:
			e_mail_display_set_mode (
				E_MAIL_DISPLAY (object),
				g_value_get_int (value));
			return;
		case PROP_HEADERS_COLLAPSABLE:
			e_mail_display_set_headers_collapsable (
				E_MAIL_DISPLAY (object),
				g_value_get_boolean (value));
			return;
		case PROP_HEADERS_COLLAPSED:
			e_mail_display_set_headers_collapsed (
				E_MAIL_DISPLAY (object),
				g_value_get_boolean (value));
			return;
                case PROP_CARET_MODE:
			priv->caret_mode = g_value_get_boolean (value);
                        return;
		case PROP_ZOOM_LEVEL:
			priv->zoom_level = g_value_get_float (value);
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_display_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	EMailDisplayPrivate *priv = E_MAIL_DISPLAY (object)->priv;

	switch (property_id) {
		case PROP_FORMATTER:
			g_value_set_object (
				value, e_mail_display_get_formatter (
				E_MAIL_DISPLAY (object)));
			return;
		case PROP_MODE:
			g_value_set_int (
				value, e_mail_display_get_mode (
				E_MAIL_DISPLAY (object)));
			return;
		case PROP_HEADERS_COLLAPSABLE:
			g_value_set_boolean (
				value, e_mail_display_get_headers_collapsable (
				E_MAIL_DISPLAY (object)));
			return;
		case PROP_HEADERS_COLLAPSED:
			g_value_set_boolean (
				value, e_mail_display_get_headers_collapsed (
				E_MAIL_DISPLAY (object)));
			return;
                case PROP_CARET_MODE:
                        g_value_set_boolean (value, priv->caret_mode);
                        return;
		case PROP_ZOOM_LEVEL:
			g_value_set_float (value, priv->zoom_level);
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_display_dispose (GObject *object)
{
	EMailDisplayPrivate *priv;

	priv = E_MAIL_DISPLAY_GET_PRIVATE (object);

	if (priv->formatter) {
		g_object_unref (priv->formatter);
		priv->formatter = NULL;
	}

	if (priv->searchbar) {
		g_object_unref (priv->searchbar);
		priv->searchbar = NULL;
	}

	if (priv->webviews) {
		g_list_free (priv->webviews);
		priv->webviews = NULL;
	}

	if (priv->settings) {
		g_object_unref (priv->settings);
		priv->settings = NULL;
	}

	if (priv->headers_settings) {
                g_object_unref (priv->headers_settings);
                priv->headers_settings = NULL;
        }

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
mail_display_realize (GtkWidget *widget)
{
	/* Chain up to parent's realize() method. */
	GTK_WIDGET_CLASS (parent_class)->realize (widget);

	mail_display_update_formatter_colors (E_MAIL_DISPLAY (widget));
}

static void
mail_display_style_set (GtkWidget *widget,
                        GtkStyle *previous_style)
{
	EMailDisplay *display = E_MAIL_DISPLAY (widget);

	/* Chain up to parent's style_set() method. */
	GTK_WIDGET_CLASS (parent_class)->style_set (widget, previous_style);

	mail_display_update_formatter_colors (display);

	e_mail_display_reload (display);
}

static void
mail_display_emit_status_message (EWebView *web_view,
				  const gchar *message,
				  gpointer user_data)
{
	EMailDisplay *display = user_data;

	g_signal_emit (display, signals[STATUS_MESSAGE], 0, message);
}

static gboolean
mail_display_emit_popup_event (EWebView *web_view,
			       GdkEventButton *event,
			       const gchar *uri,
			       gpointer user_data)
{
	EMailDisplay *display = user_data;
	gboolean event_handled;

	g_signal_emit (display, signals[POPUP_EVENT], 0, event, uri, &event_handled);

	return event_handled;
}

static gboolean
mail_display_process_mailto (EWebView *web_view,
                             const gchar *mailto_uri,
                             gpointer user_data)
{
	EMailDisplay *display = user_data;

	g_return_val_if_fail (web_view != NULL, FALSE);
	g_return_val_if_fail (mailto_uri != NULL, FALSE);
	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), FALSE);

	if (g_ascii_strncasecmp (mailto_uri, "mailto:", 7) == 0) {
		EMailDisplayPrivate *priv;
		EMFormat *format;
		CamelFolder *folder = NULL;
		EShell *shell;

		priv = E_MAIL_DISPLAY_GET_PRIVATE (display);
		g_return_val_if_fail (priv->formatter != NULL, FALSE);

		format = EM_FORMAT (priv->formatter);

		if (format != NULL && format->folder != NULL)
			folder = format->folder;

		shell = e_shell_get_default ();
		em_utils_compose_new_message_with_mailto (
			shell, mailto_uri, folder);

		return TRUE;
	}

	return FALSE;
}

static gboolean
mail_display_link_clicked (WebKitWebView *web_view,
			   WebKitWebFrame *frame,
			   WebKitNetworkRequest *request,
			   WebKitWebNavigationAction *navigation_action,
			   WebKitWebPolicyDecision *policy_decision,
			   gpointer user_data)
{
	EMailDisplay *display = user_data;
	EMailDisplayPrivate *priv;
	const gchar *uri = webkit_network_request_get_uri (request);

	priv = E_MAIL_DISPLAY_GET_PRIVATE (display);
	g_return_val_if_fail (priv->formatter != NULL, FALSE);

	if (mail_display_process_mailto (E_WEB_VIEW (web_view), uri, display)) {
		/* do nothing, function handled the "mailto:" uri already */
		webkit_web_policy_decision_ignore (policy_decision);
		return TRUE;

	} else if (g_ascii_strncasecmp (uri, "thismessage:", 12) == 0) {
		/* ignore */ ;
		webkit_web_policy_decision_ignore (policy_decision);
		return TRUE;

	} else if (g_ascii_strncasecmp (uri, "cid:", 4) == 0) {
		/* ignore */ ;
		webkit_web_policy_decision_ignore (policy_decision);
		return TRUE;

	}

	/* Let webkit handle it */
	return FALSE;
}

static void
webkit_request_load_from_file (WebKitNetworkRequest *request,
			       const gchar *path)
{
	gchar *data = NULL;
	gsize length = 0;
	gboolean status;
	gchar *b64, *new_uri;
	gchar *ct;

	status = g_file_get_contents (path, &data, &length, NULL);
	if (!status)
		return;

	b64 = g_base64_encode ((guchar*) data, length);
	ct = g_content_type_guess (path, NULL, 0, NULL);

	new_uri =  g_strdup_printf ("data:%s;base64,%s", ct, b64);
	webkit_network_request_set_uri (request, new_uri);

	g_free (b64);
	g_free (new_uri);
	g_free (ct);
	g_free (data);
}

static void
mail_display_resource_requested (WebKitWebView *web_view,
				 WebKitWebFrame *frame,
				 WebKitWebResource *resource,
				 WebKitNetworkRequest *request,
				 WebKitNetworkResponse *response,
				 gpointer user_data)
{
	EMailDisplay *display = user_data;
	EMFormat *formatter = EM_FORMAT (display->priv->formatter);
	const gchar *uri = webkit_network_request_get_uri (request);

        /* Redirect cid:part_id to mail://mail_id/cid:part_id */
        if (g_str_has_prefix (uri, "cid:")) {
		gchar *new_uri = em_format_build_mail_uri (formatter->folder,
			formatter->message_uid, "part_id", G_TYPE_STRING, uri, NULL);

                webkit_network_request_set_uri (request, new_uri);

                g_free (new_uri);

        /* WebKit won't allow to load a local file when displaing "remote" mail://,
           protocol, so we need to handle this manually */
        } else if (g_str_has_prefix (uri, "file:")) {
		gchar *path;

		path = g_filename_from_uri (uri, NULL, NULL);
		if (!path)
			return;

		webkit_request_load_from_file (request, path);

		g_free (path);

        /* Redirect http(s) request to evo-http(s) protocol. See EMailRequest for
         * further details about this. */
        } else if (g_str_has_prefix (uri, "http:") || g_str_has_prefix (uri, "https")) {

                gchar *new_uri, *mail_uri, *enc;
                SoupURI *soup_uri;
                GHashTable *query;
                gchar *uri_md5;
                CamelStream *stream;

                /* Open Evolution's cache */
                uri_md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, uri, -1);
                stream = camel_data_cache_get (
                                emd_global_http_cache, "http", uri_md5, NULL);
                g_free (uri_md5);

                /* If the URI is not cached and we are not allowed to load it
                 * then redirect to invalid URI, so that webkit would display
                 * a native placeholder for it. */
                if (!stream && !display->priv->force_image_load &&
                    !em_format_html_can_load_images (display->priv->formatter)) {
                        webkit_network_request_set_uri (request, "invalid://protocol");
                        return;
                }

                new_uri = g_strconcat ("evo-", uri, NULL);
                /* Don't free, will be freed when the hash table is destroyed */
                mail_uri = em_format_build_mail_uri (formatter->folder,
                                formatter->message_uid, NULL, NULL);

                soup_uri = soup_uri_new (new_uri);
                if (soup_uri->query) {
                        query = soup_form_decode (soup_uri->query);
                } else {
                        query = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                       g_free, g_free);
                }
                enc = soup_uri_encode (mail_uri, NULL);
                g_hash_table_insert (query, g_strdup ("__evo-mail"), enc);

                if (display->priv->force_image_load) {
                        g_hash_table_insert (query,
                                g_strdup ("__evo-load-images"),
                                             g_strdup ("true"));
                }

                g_free (mail_uri);

                soup_uri_set_query_from_form (soup_uri, query);
                g_free (new_uri);

                new_uri = soup_uri_to_string (soup_uri, FALSE);
                webkit_network_request_set_uri (request, new_uri);

                g_free (new_uri);
                soup_uri_free (soup_uri);
                g_hash_table_unref (query);
        }
}

static void
mail_display_headers_collapsed_state_changed (EWebView *web_view,
					      size_t arg_count,
					      const JSValueRef args[],
					      gpointer user_data)
{
	EMailDisplay *display = user_data;
	JSGlobalContextRef ctx = e_web_view_get_global_context (web_view);

	e_mail_display_set_headers_collapsed (display, JSValueToBoolean (ctx, args[0]));
}

static void
mail_display_install_js_callbacks (WebKitWebView *web_view,
			           WebKitWebFrame *frame,
				   gpointer context,
				   gpointer window_object,
				   gpointer user_data)
{
	if (frame != webkit_web_view_get_main_frame (web_view))
		return;

	e_web_view_install_js_callback (E_WEB_VIEW (web_view), "headers_collapsed",
		(EWebViewJSFunctionCallback) mail_display_headers_collapsed_state_changed, user_data);
}

static EWebView*
mail_display_setup_webview (EMailDisplay *display,
			    gboolean is_header)
{
	EWebView *web_view;
	GtkUIManager *ui_manager;
	GError *error = NULL;

	web_view = E_WEB_VIEW (e_web_view_new ());
	webkit_web_view_set_full_content_zoom (WEBKIT_WEB_VIEW (web_view), TRUE);

        if (is_header) {
                e_web_view_set_settings (web_view, display->priv->headers_settings);
        } else {
	        e_web_view_set_settings (web_view, display->priv->settings);
        }

	g_signal_connect (web_view, "navigation-policy-decision-requested",
		G_CALLBACK (mail_display_link_clicked), display);
	g_signal_connect (web_view, "window-object-cleared",
		G_CALLBACK (mail_display_install_js_callbacks), display);
	g_signal_connect (web_view, "resource-request-starting",
		G_CALLBACK (mail_display_resource_requested), display);
	g_signal_connect (web_view, "process-mailto",
		G_CALLBACK (mail_display_process_mailto), display);
	g_signal_connect (web_view, "status-message",
		G_CALLBACK (mail_display_emit_status_message), display);
	g_signal_connect (web_view, "popup-event",
		G_CALLBACK (mail_display_emit_popup_event), display);
        g_signal_connect (web_view, "enter-notify-event",
                G_CALLBACK (mail_display_webview_enter_notify_event), display);
        g_signal_connect (web_view, "update-actions",
                G_CALLBACK (mail_display_webview_update_actions), display);

	g_object_bind_property (web_view, "zoom-level", display, "zoom-level", 
		G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

	/* Because we are loading from a hard-coded string, there is
	 * no chance of I/O errors.  Failure here implies a malformed
	 * UI definition.  Full stop. */
	ui_manager = e_web_view_get_ui_manager (web_view);
	gtk_ui_manager_insert_action_group (ui_manager, display->priv->mailto_actions, 0);
	gtk_ui_manager_add_ui_from_string (ui_manager, ui, -1, &error);

        if (error != NULL) {
                g_error ("%s", error->message);
                g_error_free (error);
        }

        error = NULL;
        gtk_ui_manager_insert_action_group (ui_manager, display->priv->images_actions, 0);
        gtk_ui_manager_add_ui_from_string (ui_manager, image_ui, -1, &error);

	if (error != NULL) {
		g_error ("%s", error->message);
                g_error_free (error);
        }

	return web_view;
}

static void
mail_display_on_web_view_sw_hadjustment_changed (GtkAdjustment* adjustment,
                                                 gpointer user_data)
{
        GtkWidget *scrolled_window = user_data;
        GtkWidget *vscrollbar;
        GtkWidget *web_view;
        gint new_width, height;

        web_view = gtk_bin_get_child (GTK_BIN (scrolled_window));
        gtk_widget_get_preferred_width (web_view, NULL, &new_width);

        vscrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (scrolled_window));
        if (vscrollbar && gtk_widget_get_visible (vscrollbar)) {
                gint scrollbar_width;
                gtk_widget_get_preferred_width (vscrollbar, &scrollbar_width, NULL);
                new_width += scrollbar_width;
        }

        gtk_widget_get_size_request (scrolled_window, NULL, &height);
        gtk_widget_set_size_request (scrolled_window, new_width, height);
}


static void
mail_display_on_web_view_sw_vadjustment_changed (GtkAdjustment* adjustment,
						 gpointer user_data)
{
	GtkWidget *scrolled_window = user_data;
	GtkWidget *hscrollbar;
	GtkWidget *web_view;
	gint new_height, width;

	web_view = gtk_bin_get_child (GTK_BIN (scrolled_window));
	gtk_widget_get_preferred_height (web_view, &new_height, NULL);

	/* We now have height of the webview's view, but to correctly resize the
	 * parent scrolled window we need to add height of horizontal scrollbar (if visible) */
	hscrollbar = gtk_scrolled_window_get_hscrollbar (GTK_SCROLLED_WINDOW (scrolled_window));
	if (hscrollbar && gtk_widget_get_visible (hscrollbar)) {
		gint scrollbar_height;
		gtk_widget_get_preferred_height (hscrollbar, &scrollbar_height, NULL);
		new_height += scrollbar_height;
	}

	gtk_widget_get_size_request (scrolled_window, &width, NULL);
	gtk_widget_set_size_request (scrolled_window, width, new_height);
}

static GtkWidget*
mail_display_insert_web_view (EMailDisplay *display,
			      EWebView *web_view)
{
	GtkWidget *scrolled_window;
	GtkAdjustment *adjustment;
        GdkWindow *window;

	display->priv->webviews = g_list_append (display->priv->webviews, web_view);
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	g_object_set (G_OBJECT (scrolled_window),
		"vexpand", FALSE,
		"vexpand-set", TRUE,
                "hexpand", FALSE,
                "hexpand-set", TRUE,
                "shadow-type", GTK_SHADOW_NONE,
		NULL);

	adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scrolled_window));
	g_signal_connect (G_OBJECT (adjustment), "changed",
		G_CALLBACK (mail_display_on_web_view_sw_vadjustment_changed), scrolled_window);

        adjustment = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (scrolled_window));
        g_signal_connect (G_OBJECT (adjustment), "changed",
                G_CALLBACK (mail_display_on_web_view_sw_hadjustment_changed), scrolled_window);
	
	gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (web_view));

        gtk_box_pack_start (GTK_BOX (display->priv->box), scrolled_window, FALSE, TRUE, 0);
	gtk_widget_show_all (scrolled_window);

        /* Enable enter-notify event */
        window = gtk_widget_get_window (GTK_WIDGET (web_view));
        if (!(gdk_window_get_events (window) & GDK_ENTER_NOTIFY_MASK)) {
              gdk_window_set_events (window,
                      gdk_window_get_events (window) | GDK_ENTER_NOTIFY_MASK);
        }

        return scrolled_window;
}

static void
mail_display_load_as_source (EMailDisplay *display,
			     const gchar *msg_uid)
{
	EWebView *web_view;
	EMFormat *emf = (EMFormat *) display->priv->formatter;
	gchar *uri;

	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	e_mail_display_clear (display);

	web_view = mail_display_setup_webview (display, TRUE);
	mail_display_insert_web_view (display, web_view);

	uri = em_format_build_mail_uri (emf->folder, emf->message_uid,
		"part_id", G_TYPE_STRING, ".message",
		"mode", G_TYPE_INT, display->priv->mode,
		NULL);
	e_web_view_load_uri (web_view, uri);

	gtk_widget_show_all (display->priv->box);
}

static void
mail_display_load_normal (EMailDisplay *display,
			  const gchar *msg_uri)
{
	EWebView *web_view;
	EMFormatPURI *puri;
	EMFormat *emf = (EMFormat *) display->priv->formatter;
	EAttachmentView *attachment_view;
	gchar *uri;
	GList *iter;
	GtkBox *box;
        gchar *start_part_id;
        GtkWidget *attachment_button;

	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	/* Don't use gtk_widget_show_all() to display all widgets at once,
	   it makes all parts of EMailAttachmentBar visible and that's not
	   what we want.
	   FIXME: Maybe using gtk_widget_set_no_show_all() in EAttachmentView
	          could help...
	*/

	/* First remove all widgets left after previous message */
	e_mail_display_clear (display);
        attachment_view = NULL;
        attachment_button = NULL;

	box = GTK_BOX (display->priv->box);
	gtk_widget_show (display->priv->box);

        /* If msg_uri contains part_id, then find it and start 'writing' from
         * this part_id. When there's no part_id or it's invalid, start from first
         * PURI in the list. */
        if ((start_part_id = strstr (msg_uri, "part_id=")) != NULL) {
                gchar *end_part_id;
                start_part_id = start_part_id + strlen("part_id=");
                end_part_id = strstr (start_part_id, "&");
                if (!end_part_id)
                        start_part_id = g_strdup (start_part_id);
                else
                        start_part_id = g_strndup (start_part_id, end_part_id - start_part_id);

                iter = g_hash_table_lookup (emf->mail_part_table, start_part_id);
                if (!iter)
                        iter = emf->mail_part_list;
                else
                        iter = iter->next; /* Prevent endless recursion */
        } else {
                iter = emf->mail_part_list;
        }

        g_free (start_part_id);

        for (iter = iter; iter; iter = iter->next) {
		GtkWidget *widget = NULL;

		puri = iter->data;

                if (g_str_has_suffix (puri->uri, ".end"))
                        break;

		uri = em_format_build_mail_uri (emf->folder, emf->message_uid,
			"part_id", G_TYPE_STRING, puri->uri,
			"mode", G_TYPE_INT, display->priv->mode,
			"headers_collapsable", G_TYPE_BOOLEAN, display->priv->headers_collapsable,
			"headers_collapsed", G_TYPE_BOOLEAN, display->priv->headers_collapsed,
			NULL);

		if (puri->widget_func) {
                        gboolean expandible = FALSE;

			widget = puri->widget_func (emf, puri, NULL);
                        d(printf("%p: added %s for PURI %s\n", 
                                   display, G_OBJECT_TYPE_NAME (widget), puri->uri));

			if (!GTK_IS_WIDGET (widget)) {
				g_message ("Part %s didn't provide a valid widget, skipping!", puri->uri);
                                g_free (uri);
				continue;
			}

                        gtk_box_pack_start (box, widget, FALSE, TRUE, 0);
			if (attachment_button) {

                                /* If attachment_button is set and it was followed by
                                 * another attachment button, then something is wrong.
                                 * Make the previous button unexpandable and continue */
                                if (E_IS_ATTACHMENT_BUTTON (widget)) {
                                        e_attachment_button_set_expandable (
                                                E_ATTACHMENT_BUTTON (attachment_button), FALSE);

                                } else {
                                        g_object_bind_property (attachment_button, "expanded",
                                                widget, "visible", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
                                        e_attachment_button_set_expandable (
                                                E_ATTACHMENT_BUTTON (attachment_button), TRUE);
                                        attachment_button = NULL;
                                        expandible = TRUE;
                                }
                        }

			if (E_IS_ATTACHMENT_BUTTON (widget) && attachment_view) {
				e_attachment_button_set_view (E_ATTACHMENT_BUTTON (widget),
					attachment_view);
                                attachment_button = widget;
                        }

			if (E_IS_ATTACHMENT_VIEW (widget)) {
				EAttachmentStore *store;

				attachment_view = E_ATTACHMENT_VIEW (widget);
				store = e_attachment_view_get_store (attachment_view);

				if (e_attachment_store_get_num_attachments (store) > 0)
					gtk_widget_show (widget);
				else
					gtk_widget_hide (widget);
                                g_free (uri);
                                continue;

                        } else if (E_IS_MAIL_DISPLAY (widget)) {
                                EMFormatPURI *iter_puri;

                                if (!expandible)
                                        gtk_widget_show (widget);

                                /* Find the PURI with ".end" suffix and continue writing
                                the message from following PURI */
                                do {
                                        iter = iter->next;
                                        if (iter)
                                                iter_puri = iter->data;
                                } while (iter && (!g_str_has_suffix (iter_puri->uri, ".end")));

                                g_free (uri);
                                continue;

                        } else {
                                gtk_widget_show (widget);
                        }
		}

		if ((!puri->is_attachment && puri->write_func) || (puri->is_attachment && puri->write_func && puri->widget_func)) {
                        GtkWidget *container;

                        web_view = mail_display_setup_webview (display, g_str_has_suffix (puri->uri, ".headers"));
			container = mail_display_insert_web_view (display, web_view);

                        e_web_view_load_uri (web_view, uri);

                        if (attachment_button) {
				g_object_bind_property (widget, "expanded",
					container, "visible", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
                                attachment_button = NULL;
			}

                        d(printf("%p: added EWebView for PURI %s\n", display, puri->uri));
		}

		g_free (uri);
	}

	/* If we created an attachment_button but didn't attach any widget to it,
         * then make sure it's not expandable. */
	if (attachment_button) {
                e_attachment_button_set_expandable (
                        E_ATTACHMENT_BUTTON (attachment_button), FALSE);
        }
}

static void
mail_display_class_init (EMailDisplayClass *class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMailDisplayPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_display_set_property;
	object_class->get_property = mail_display_get_property;
	object_class->dispose = mail_display_dispose;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->realize = mail_display_realize;
	widget_class->style_set = mail_display_style_set;

	g_object_class_install_property (
		object_class,
		PROP_FORMATTER,
		g_param_spec_object (
			"formatter",
			"HTML Formatter",
			NULL,
			EM_TYPE_FORMAT_HTML,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_MODE,
		g_param_spec_int (
			"mode",
			"Display Mode",
			NULL,
			EM_FORMAT_WRITE_MODE_NORMAL,
			EM_FORMAT_WRITE_MODE_SOURCE,
			EM_FORMAT_WRITE_MODE_NORMAL,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_HEADERS_COLLAPSABLE,
		g_param_spec_boolean (
			"headers-collapsable",
			"Headers Collapsable",
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_HEADERS_COLLAPSED,
		g_param_spec_boolean (
			"headers-collapsed",
			"Headers Collapsed",
			NULL,
			FALSE,
			G_PARAM_READWRITE));

        g_object_class_install_property (
                object_class,
                PROP_CARET_MODE,
                g_param_spec_boolean (
                        "caret-mode",
                        "Caret Mode",
                        NULL,
                        FALSE,
                        G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_ZOOM_LEVEL,
		g_param_spec_float (
			"zoom-level",
			"Zoom Level",
			NULL,
			G_MINFLOAT,
			G_MAXFLOAT,
			1.0,
			G_PARAM_READWRITE));
			

	signals[POPUP_EVENT] = g_signal_new (
		"popup-event",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EMailDisplayClass, popup_event),
		g_signal_accumulator_true_handled, NULL,
		e_marshal_BOOLEAN__BOXED_STRING,
		G_TYPE_BOOLEAN, 2,
		GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE,
		G_TYPE_STRING);

	signals[STATUS_MESSAGE] = g_signal_new (
		"status-message",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EMailDisplayClass, status_message),
		NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);
}

static void
mail_display_init (EMailDisplay *display)
{
	SoupSession *session;
	SoupSessionFeature *feature;
	const gchar *user_cache_dir;

	display->priv = E_MAIL_DISPLAY_GET_PRIVATE (display);

	display->priv->settings = e_web_view_get_default_settings (GTK_WIDGET (display));
	g_object_bind_property (display, "caret-mode",
		display->priv->settings, "enable-caret-browsing", 
		G_BINDING_SYNC_CREATE);
        g_object_set (display->priv->settings,
                "enable-scripts", FALSE, NULL);

        display->priv->headers_settings = e_web_view_get_default_settings (GTK_WIDGET (display));
        g_object_bind_property (display, "caret-mode",
                display->priv->settings, "enable-caret-browsing",
                G_BINDING_SYNC_CREATE);

	

	display->priv->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_set_reallocate_redraws (GTK_CONTAINER (display->priv->box), TRUE);
	gtk_container_add (GTK_CONTAINER (display), display->priv->box);

	display->priv->webviews = NULL;

        display->priv->force_image_load = FALSE;
	display->priv->mailto_actions = gtk_action_group_new ("mailto");
	gtk_action_group_add_actions (display->priv->mailto_actions, mailto_entries, 
		G_N_ELEMENTS (mailto_entries), NULL);

        display->priv->images_actions = gtk_action_group_new ("image");
        gtk_action_group_add_actions (display->priv->images_actions, image_entries,
                G_N_ELEMENTS (image_entries), NULL);


	/* WEBKIT TODO: ESearchBar */

	/* Register our own handler for our own mail:// protocol */
	session = webkit_get_default_session ();
	feature = SOUP_SESSION_FEATURE (soup_requester_new ());
	soup_session_feature_add_feature (feature, E_TYPE_MAIL_REQUEST);
	soup_session_add_feature (session, feature);
	g_object_unref (feature);

	/* cache expiry - 2 hour access, 1 day max */
	user_cache_dir = e_get_user_cache_dir ();
	emd_global_http_cache = camel_data_cache_new (user_cache_dir, NULL);
	if (emd_global_http_cache) {
		camel_data_cache_set_expire_age (emd_global_http_cache, 24*60*60);
		camel_data_cache_set_expire_access (emd_global_http_cache, 2*60*60);
	}
}

GType
e_mail_display_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EMailDisplayClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) mail_display_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EMailDisplay),
			0,     /* n_preallocs */
			(GInstanceInitFunc) mail_display_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_VIEWPORT, "EMailDisplay", &type_info, 0);
	}

	return type;
}

EMFormatHTML *
e_mail_display_get_formatter (EMailDisplay *display)
{
	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), NULL);

	return display->priv->formatter;
}

void
e_mail_display_set_formatter (EMailDisplay *display,
                              EMFormatHTML *formatter)
{
        g_return_if_fail (E_IS_MAIL_DISPLAY (display));
	g_return_if_fail (EM_IS_FORMAT_HTML (formatter));

	g_object_ref (formatter);
	
	if (display->priv->formatter != NULL) {
		/* The formatter might still exist after unrefing it, so 
		 * we need to stop listening to it's request for redrawing */
		g_signal_handlers_disconnect_by_func (
			display->priv->formatter, e_mail_display_reload, display);
		g_object_unref (display->priv->formatter);
	}

	display->priv->formatter = formatter;

	mail_display_update_formatter_colors (display);

        g_signal_connect (formatter, "notify::image-loading-policy",
                G_CALLBACK (formatter_image_loading_policy_changed_cb), display);
	g_signal_connect_swapped (formatter, "redraw-requested",
		G_CALLBACK (e_mail_display_reload), display);
        g_signal_connect_swapped (formatter, "notify::charset",
                G_CALLBACK (e_mail_display_reload), display);

	g_object_notify (G_OBJECT (display), "formatter");
}

EMFormatWriteMode
e_mail_display_get_mode (EMailDisplay *display)
{
	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display),
			EM_FORMAT_WRITE_MODE_NORMAL);

	return display->priv->mode;
}

void
e_mail_display_set_mode (EMailDisplay *display,
			 EMFormatWriteMode mode)
{
	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	if (display->priv->mode == mode)
		return;

	display->priv->mode = mode;
	if (mode == EM_FORMAT_WRITE_MODE_SOURCE)
		mail_display_load_as_source (display, NULL);
	else
		e_mail_display_reload (display);

	g_object_notify (G_OBJECT (display), "mode");
}

gboolean
e_mail_display_get_headers_collapsable (EMailDisplay *display)
{
	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), FALSE);

	return display->priv->headers_collapsable;
}

void
e_mail_display_set_headers_collapsable (EMailDisplay *display,
					gboolean collapsable)
{
	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	if (display->priv->headers_collapsable == collapsable)
		return;

	display->priv->headers_collapsable = collapsable;
	e_mail_display_reload (display);

	g_object_notify (G_OBJECT (display), "headers-collapsable");
}

gboolean
e_mail_display_get_headers_collapsed (EMailDisplay *display)
{
	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), FALSE);

	if (display->priv->headers_collapsable)
		return display->priv->headers_collapsed;

	return FALSE;
}

void
e_mail_display_set_headers_collapsed (EMailDisplay *display,
				      gboolean collapsed)
{
	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	if (display->priv->headers_collapsed == collapsed)
		return;

	display->priv->headers_collapsed = collapsed;

	g_object_notify (G_OBJECT (display), "headers-collapsed");
}

void
e_mail_display_load (EMailDisplay *display,
		     const gchar *msg_uri)
{
        display->priv->force_image_load = FALSE;

	if (display->priv->mode == EM_FORMAT_WRITE_MODE_SOURCE)
		mail_display_load_as_source  (display, msg_uri);
	else
		mail_display_load_normal (display, msg_uri);
}

void
e_mail_display_reload (EMailDisplay *display)
{
	GList *iter;

	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	/* We can't just call e_web_view_reload() here, we need the URI queries
	   to reflect possible changes in write mode and headers properties.
	   Unfortunatelly, nothing provides API good enough to do this more
	   simple way... */

	for (iter = display->priv->webviews; iter; iter = iter->next) {
		EWebView *web_view;
		const gchar *uri;
		gchar *base;
		GString *new_uri;
		GHashTable *table;
		GHashTableIter table_iter;
		gpointer key, val;
		char separator;

		web_view = (EWebView *) iter->data;
		uri = e_web_view_get_uri (web_view);

		if (!uri)
			continue;

		base = g_strndup (uri, strstr (uri, "?") - uri);
		new_uri = g_string_new (base);
		g_free (base);

		table = soup_form_decode (strstr (uri, "?") + 1);
		g_hash_table_insert (table, g_strdup ("mode"), g_strdup_printf ("%d", display->priv->mode));
		g_hash_table_insert (table, g_strdup ("headers_collapsable"), g_strdup_printf ("%d", display->priv->headers_collapsable));
		g_hash_table_insert (table, g_strdup ("headers_collapsed"), g_strdup_printf ("%d", display->priv->headers_collapsed));

		g_hash_table_iter_init (&table_iter, table);
		separator = '?';
		while (g_hash_table_iter_next (&table_iter, &key, &val)) {
			g_string_append_printf (new_uri, "%c%s=%s", separator,
				(gchar *) key, (gchar *) val);

			if (separator == '?')
				separator = '&';
		}

		e_web_view_load_uri (web_view, new_uri->str);

		g_string_free (new_uri, TRUE);
		g_hash_table_destroy (table);
	}
}

EWebView*
e_mail_display_get_current_web_view (EMailDisplay *display)
{
	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), NULL);

        return E_WEB_VIEW (display->priv->current_webview);
}

GtkAction*
e_mail_display_get_action (EMailDisplay *display,
			   const gchar *action_name)
{
        GtkAction *action;

	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), NULL);
	g_return_val_if_fail (action_name != NULL, NULL);

	action = gtk_action_group_get_action (display->priv->mailto_actions, action_name);
        if (!action)
                action = gtk_action_group_get_action (display->priv->images_actions, action_name);

        return action;
}

void
e_mail_display_set_status (EMailDisplay *display,
			   const gchar *status)
{
	GtkWidget *label;

	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	e_mail_display_clear (display);

	label = gtk_label_new (status);
        gtk_box_pack_start (GTK_BOX (display->priv->box), label, TRUE, TRUE, 0);
	gtk_widget_show_all (display->priv->box);
}

static void
remove_widget (GtkWidget *widget, gpointer user_data)
{
	EMailDisplay *display = user_data;

	if (!GTK_IS_WIDGET (widget))
		return;

	gtk_container_remove  (GTK_CONTAINER (display->priv->box), widget);
}

void
e_mail_display_clear (EMailDisplay *display)
{
	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	gtk_widget_hide (display->priv->box);

	gtk_container_foreach (GTK_CONTAINER (display->priv->box),
		(GtkCallback) remove_widget, display);

	g_list_free (display->priv->webviews);
	display->priv->webviews = NULL;
}

ESearchBar*
e_mail_display_get_search_bar (EMailDisplay *display)
{
	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), NULL);

	return display->priv->searchbar;
}

gboolean
e_mail_display_is_selection_active (EMailDisplay *display)
{
	EWebView *web_view;

	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), FALSE);

	web_view = e_mail_display_get_current_web_view (display);
	if (!web_view)
		return FALSE;
	else
		return e_web_view_is_selection_active (web_view);
}

gchar*
e_mail_display_get_selection_plain_text (EMailDisplay *display,
					 gint *len)
{
	EWebView *web_view;
	WebKitWebFrame *frame;
	const gchar *frame_name;
	GValue value = {0};
	GType type;
	const gchar *str;

	g_return_val_if_fail (E_IS_MAIL_DISPLAY (display), NULL);

	web_view = e_mail_display_get_current_web_view (display);
	if (!web_view)
		return NULL;

	frame = webkit_web_view_get_focused_frame (WEBKIT_WEB_VIEW (web_view));
	frame_name = webkit_web_frame_get_name (frame);

	type = e_web_view_frame_exec_script (web_view, frame_name, "window.getSelection().toString()", &value);
	g_return_val_if_fail (type == G_TYPE_STRING, NULL);

	str = g_value_get_string (&value);

	if (len)
		*len = strlen (str);

	return g_strdup (str);
}

void
e_mail_display_zoom_100 (EMailDisplay *display)
{
	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	display->priv->zoom_level = 1.0;

	g_object_notify (G_OBJECT (display), "zoom-level");
}

void
e_mail_display_zoom_in (EMailDisplay *display)
{
	gfloat step;

	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	g_object_get (G_OBJECT (display->priv->settings), 
		"zoom-step", &step, NULL);

	display->priv->zoom_level += step;

	g_object_notify (G_OBJECT (display), "zoom-level");
}

void
e_mail_display_zoom_out (EMailDisplay *display)
{
	float step;

	g_return_if_fail (E_IS_MAIL_DISPLAY (display));

	g_object_get (G_OBJECT (display->priv->settings),
		"zoom-step", &step, NULL);

	display->priv->zoom_level -= step;

	g_object_notify (G_OBJECT (display), "zoom-level");
}

static void
load_images (GtkWidget *widget,
             gpointer user_data)
{
        if (GTK_IS_SCROLLED_WINDOW (widget)) {

                EWebView *web_view;

                if (!E_IS_WEB_VIEW (gtk_bin_get_child (GTK_BIN (widget))))
                  return;

                web_view = E_WEB_VIEW (gtk_bin_get_child (GTK_BIN (widget)));
                e_web_view_reload (web_view);

        } else if (E_IS_MAIL_DISPLAY (widget)) {

                e_mail_display_load_images (E_MAIL_DISPLAY (widget));

        }
}

void
e_mail_display_load_images (EMailDisplay * display)
{
        g_return_if_fail (E_IS_MAIL_DISPLAY (display));

        display->priv->force_image_load = TRUE;

        gtk_container_foreach (GTK_CONTAINER (display->priv->box),
                        (GtkCallback) load_images, NULL);
}


void
e_mail_display_scroll (EMailDisplay* display,
                       GdkScrollDirection direction)
{
        GtkAdjustment *vadjustment;
        gint d;
        gdouble step;

        g_return_if_fail (E_IS_MAIL_DISPLAY (display));
        g_return_if_fail ((direction == GDK_SCROLL_UP) || (direction == GDK_SCROLL_DOWN));

        vadjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (display));

        if (gtk_adjustment_get_upper (vadjustment) == 0)
                return;

        d = (direction == GDK_SCROLL_DOWN) ? 1 : -1;
        step = d * gtk_adjustment_get_page_increment (vadjustment);
        gtk_adjustment_set_value (vadjustment,
                gtk_adjustment_get_value (vadjustment) + step);
}
