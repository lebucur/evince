/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; c-indent-level: 8 -*- */
/* this file is part of evince, a gnome document viewer
 *
 *  Copyright (C) 2004 Martin Kretzschmar
 *  Copyright (C) 2004 Red Hat, Inc.
 *  Copyright (C) 2000, 2001, 2002, 2003, 2004 Marco Pesenti Gritti
 *  Copyright (C) 2003, 2004 Christian Persch
 *
 *  Author:
 *    Martin Kretzschmar <martink@gnome.org>
 *
 * Evince is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Evince is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ev-window.h"
#include "ev-sidebar.h"
#include "ev-sidebar-bookmarks.h"
#include "ev-sidebar-thumbnails.h"
#include "ev-view.h"
#include "ev-print-job.h"
#include "ev-document-find.h"
#include "eggfindbar.h"

#include "pdf-document.h"
#include "pixbuf-document.h"
#include "gtkgs.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomeprintui/gnome-print-dialog.h>

#include <string.h>

#include "ev-application.h"
#include "ev-stock-icons.h"

enum {
	PROP_0,
	PROP_ATTRIBUTE
};

enum {
	SIGNAL,
	N_SIGNALS
};

struct _EvWindowPrivate {
	GtkWidget *main_box;
	GtkWidget *hpaned;
	GtkWidget *sidebar;
	GtkWidget *find_bar;
	GtkWidget *view;
	GtkActionGroup *action_group;
	GtkUIManager *ui_manager;
	GtkWidget *statusbar;
	guint help_message_cid;
	GtkWidget *exit_fullscreen_popup;

	EvDocument *document;

	gboolean fullscreen_mode;
};

#if 0
/* enable these to add support for signals */
static guint ev_window_signals [N_SIGNALS] = { 0 };
#endif

static void update_fullscreen_popup (EvWindow *window);

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (EvWindow, ev_window, GTK_TYPE_WINDOW)

#define EV_WINDOW_GET_PRIVATE(object) \
	(G_TYPE_INSTANCE_GET_PRIVATE ((object), EV_TYPE_WINDOW, EvWindowPrivate))

#if 0
const char *
ev_window_get_attribute (EvWindow *self)
{
	g_return_val_if_fail (self != NULL && EV_IS_WINDOW (self), NULL);
	
	return self->priv->attribute;
}

void
ev_window_set_attribute (EvWindow* self, const char *attribute)
{
	g_assert (self != NULL && EV_IS_WINDOW (self));
	g_assert (attribute != NULL);

	if (self->priv->attribute != NULL) {
		g_free (self->priv->attribute);
	}

	self->priv->attribute = g_strdup (attribute);

	g_object_notify (G_OBJECT (self), "attribute");
}

static void
ev_window_get_property (GObject *object, guint prop_id, GValue *value,
			GParamSpec *param_spec)
{
	EvWindow *self;

	self = EV_WINDOW (object);

	switch (prop_id) {
	case PROP_ATTRIBUTE:
		g_value_set_string (value, self->priv->attribute);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object,
						   prop_id,
						   param_spec);
		break;
	}
}

static void
ev_window_set_property (GObject *object, guint prop_id, const GValue *value,
			GParamSpec *param_spec)
{
	EvWindow *self;
	
	self = EV_WINDOW (object);
	
	switch (prop_id) {
	case PROP_ATTRIBUTE:
		ev_window_set_attribute (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object,
						   prop_id,
						   param_spec);
		break;
	}
}
#endif

static void
set_action_sensitive (EvWindow   *ev_window,
		      const char *name,
		      gboolean    sensitive)
{
	GtkAction *action = gtk_action_group_get_action (ev_window->priv->action_group,
							 name);
	gtk_action_set_sensitive (action, sensitive);
}

static void
update_action_sensitivity (EvWindow *ev_window)
{
	int n_pages;
	int page;

	if (ev_window->priv->document)
		n_pages = ev_document_get_n_pages (ev_window->priv->document);
	else
		n_pages = 1;

	page = ev_view_get_page (EV_VIEW (ev_window->priv->view));

	set_action_sensitive (ev_window, "GoFirstPage", page > 1);
	set_action_sensitive (ev_window, "GoPreviousPage", page > 1);
	set_action_sensitive (ev_window, "GoNextPage", page < n_pages);
	set_action_sensitive (ev_window, "GoLastPage", page < n_pages);
}

gboolean
ev_window_is_empty (const EvWindow *ev_window)
{
	g_return_val_if_fail (EV_IS_WINDOW (ev_window), FALSE);
	
	return ev_window->priv->document == NULL;
}

static void
unable_to_load (EvWindow   *ev_window,
		const char *error_message)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new (GTK_WINDOW (ev_window),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 _("Unable to open document"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", error_message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/* Would be nice to have this in gdk-pixbuf */
static gboolean
mime_type_supported_by_gdk_pixbuf (const gchar *mime_type)
{
	GSList *formats, *list;
	gboolean retval = FALSE;
	
	formats = gdk_pixbuf_get_formats ();
	
	list = formats;
	while (list) {
		GdkPixbufFormat *format = list->data;
		int i;
		gchar **mime_types;
		
		if (gdk_pixbuf_format_is_disabled (format))
			continue;

		mime_types = gdk_pixbuf_format_get_mime_types (format);
		
		for (i = 0; mime_types[i] != NULL; i++) {
			if (strcmp (mime_types[i], mime_type) == 0) {
				retval = TRUE;
				break;
			}
		}
		
		if (retval)
			break;
		
		list = list->next;
	}
	
	g_slist_free (formats);

	return retval;
}

void
ev_window_open (EvWindow *ev_window, const char *uri)
{
	EvDocument *document = NULL;
	char *mime_type;

	mime_type = gnome_vfs_get_mime_type (uri);

	if (!strcmp (mime_type, "application/pdf"))
		document = g_object_new (PDF_TYPE_DOCUMENT, NULL);
	else if (!strcmp (mime_type, "application/postscript"))
		document = g_object_new (GTK_GS_TYPE, NULL);
	else if (mime_type_supported_by_gdk_pixbuf (mime_type))
		document = g_object_new (PIXBUF_TYPE_DOCUMENT, NULL);
	
	if (document) {
		GError *error = NULL;

		if (ev_document_load (document, uri, &error)) {
			if (ev_window->priv->document)
				g_object_unref (ev_window->priv->document);
			ev_window->priv->document = document;

			ev_view_set_document (EV_VIEW (ev_window->priv->view),
					      document);
			ev_sidebar_set_document (EV_SIDEBAR (ev_window->priv->sidebar),
						 document);

			update_action_sensitivity (ev_window);
		
		} else {
			g_assert (error != NULL);
			g_object_unref (document);
			unable_to_load (ev_window, error->message);
			g_error_free (error);
		}
	} else {
		char *error_message;

		error_message = g_strdup_printf (_("Unhandled MIME type: '%s'"),
						 mime_type);
		unable_to_load (ev_window, error_message);
		g_free (error_message);
	}

	g_free (mime_type);
}

static void
ev_window_cmd_file_open (GtkAction *action, EvWindow *ev_window)
{
	ev_application_open (EV_APP, NULL);
}

static gboolean
using_postscript_printer (GnomePrintConfig *config)
{
	const guchar *driver;
	const guchar *transport;

	driver = gnome_print_config_get (
		config, (const guchar *)"Settings.Engine.Backend.Driver");
	
	transport = gnome_print_config_get (
		config, (const guchar *)"Settings.Transport.Backend");
	
	if (driver) {
		if (!strcmp ((const gchar *)driver, "gnome-print-ps"))
			return TRUE;
		else 
			return FALSE;
	} else 	if (transport) {
		if (!strcmp ((const gchar *)transport, "CUPS"))
			return TRUE;
	}
	
	return FALSE;
}

static void
ev_window_print (EvWindow *ev_window)
{
	GnomePrintConfig *config;
	GnomePrintJob *job;
	GtkWidget *print_dialog;
	EvPrintJob *print_job = NULL;

        g_return_if_fail (EV_IS_WINDOW (ev_window));
	g_return_if_fail (ev_window->priv->document != NULL);

	config = gnome_print_config_default ();
	job = gnome_print_job_new (config);

	print_dialog = gnome_print_dialog_new (job, _("Print"),
					       (GNOME_PRINT_DIALOG_RANGE |
						GNOME_PRINT_DIALOG_COPIES));
	gtk_dialog_set_response_sensitive (GTK_DIALOG (print_dialog),
					   GNOME_PRINT_DIALOG_RESPONSE_PREVIEW,
					   FALSE);
	
	while (TRUE) {
		int response;
		response = gtk_dialog_run (GTK_DIALOG (print_dialog));
		
		if (response != GNOME_PRINT_DIALOG_RESPONSE_PRINT)
			break;

		/* FIXME: Change this when we have the first backend
		 * that can print more than postscript
		 */
		if (!using_postscript_printer (config)) {
			GtkWidget *dialog;
			
			dialog = gtk_message_dialog_new (
				GTK_WINDOW (print_dialog), GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				_("Printing is not supported on this printer."));
			gtk_message_dialog_format_secondary_text (
				GTK_MESSAGE_DIALOG (dialog),
				_("You were trying to print to a printer using the \"%s\" driver. This program requires a PostScript printer driver."),
				gnome_print_config_get (
					config, "Settings.Engine.Backend.Driver"));
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);

			continue;
		}

		print_job = g_object_new (EV_TYPE_PRINT_JOB,
					  "gnome_print_job", job,
					  "document", ev_window->priv->document,
					  "print_dialog", print_dialog,
					  NULL);
		break;
	}
				
	gtk_widget_destroy (print_dialog);

	if (print_job != NULL)
		ev_print_job_print (print_job, GTK_WINDOW (ev_window));
}

static void
ev_window_cmd_file_print (GtkAction *action, EvWindow *ev_window)
{
	ev_window_print (ev_window);
}

static void
ev_window_cmd_file_close_window (GtkAction *action, EvWindow *ev_window)
{
	g_return_if_fail (EV_IS_WINDOW (ev_window));

	gtk_widget_destroy (GTK_WIDGET (ev_window));
}

static void
find_not_supported_dialog (EvWindow   *ev_window)
{
	GtkWidget *dialog;

	/* If you change this so it isn't modal, be sure you don't
	 * allow multiple copies of the dialog...
	 */
	
 	dialog = gtk_message_dialog_new (GTK_WINDOW (ev_window),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 _("The \"Find\" feature will not work with this document"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
 						  _("Searching for text is only supported for PDF documents."));
	gtk_dialog_run (GTK_DIALOG (dialog));
 	gtk_widget_destroy (dialog);
}

static void
ev_window_cmd_edit_find (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	if (ev_window->priv->document == NULL) {
		g_printerr ("We should have set the Find menu item insensitive since there's no document\n");
	} else if (!EV_IS_DOCUMENT_FIND (ev_window->priv->document)) {
		find_not_supported_dialog (ev_window);
	} else {
		gtk_widget_show (ev_window->priv->find_bar);

		if (ev_window->priv->exit_fullscreen_popup) 
			update_fullscreen_popup (ev_window);
	
		egg_find_bar_grab_focus (EGG_FIND_BAR (ev_window->priv->find_bar));
	}
}

static void
ev_window_cmd_edit_copy (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

        /* FIXME */
}

static void
update_fullscreen_popup (EvWindow *window)
{
	GtkWidget *popup = window->priv->exit_fullscreen_popup;
	int popup_width, popup_height;
	GdkRectangle screen_rect;

	g_return_if_fail (popup != NULL);

	if (!popup)
		return;
	
	popup_width = popup->requisition.width;
	popup_height = popup->requisition.height;

	/* FIXME multihead */
	gdk_screen_get_monitor_geometry (gdk_screen_get_default (),
			gdk_screen_get_monitor_at_window
                        (gdk_screen_get_default (),
                         GTK_WIDGET (window)->window),
                         &screen_rect);

	if (GTK_WIDGET_VISIBLE (window->priv->find_bar)) {
		GtkRequisition req;

		gtk_widget_size_request (window->priv->find_bar, &req);
		
		screen_rect.height -= req.height;
	}
	
	if (gtk_widget_get_direction (popup) == GTK_TEXT_DIR_RTL)
	{
		gtk_window_move (GTK_WINDOW (popup),
				 screen_rect.x + screen_rect.width - popup_width,
				 screen_rect.height - popup_height);
	}
	else
	{
		gtk_window_move (GTK_WINDOW (popup),
                	        screen_rect.x, screen_rect.height - popup_height);
	}
}

static void
screen_size_changed_cb (GdkScreen *screen,
			EvWindow *window)
{
	update_fullscreen_popup (window);
}

static void
destroy_exit_fullscreen_popup (EvWindow *window)
{
	if (window->priv->exit_fullscreen_popup != NULL)
	{
		/* FIXME multihead */
		g_signal_handlers_disconnect_by_func
			(gdk_screen_get_default (),
			 G_CALLBACK (screen_size_changed_cb), window);

		gtk_widget_destroy (window->priv->exit_fullscreen_popup);
		window->priv->exit_fullscreen_popup = NULL;
	}
}

static void
exit_fullscreen_button_clicked_cb (GtkWidget *button, EvWindow *window)
{
	GtkAction *action;

	action = gtk_action_group_get_action (window->priv->action_group, "ViewFullscreen");
	g_return_if_fail (action != NULL);

	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);
}

static void
fullscreen_popup_size_request_cb (GtkWidget *popup, GtkRequisition *req, EvWindow *window)
{
	update_fullscreen_popup (window);
}

static void
ev_window_fullscreen (EvWindow *window)
{
	GtkWidget *popup, *button, *icon, *label, *hbox, *main_menu;

	window->priv->fullscreen_mode = TRUE;

	popup = gtk_window_new (GTK_WINDOW_POPUP);
	window->priv->exit_fullscreen_popup = popup;

	button = gtk_button_new ();
	g_signal_connect (button, "clicked",
			  G_CALLBACK (exit_fullscreen_button_clicked_cb),
			  window);
	gtk_widget_show (button);
	gtk_container_add (GTK_CONTAINER (popup), button);

	hbox = gtk_hbox_new (FALSE, 2);
	gtk_widget_show (hbox);
	gtk_container_add (GTK_CONTAINER (button), hbox);

	icon = gtk_image_new_from_stock (GTK_STOCK_QUIT, GTK_ICON_SIZE_BUTTON);
	gtk_widget_show (icon);
	gtk_box_pack_start (GTK_BOX (hbox), icon, FALSE, FALSE, 0);

	label = gtk_label_new (_("Exit Fullscreen"));
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

	gtk_window_set_resizable (GTK_WINDOW (popup), FALSE);

	/* FIXME multihead */
	g_signal_connect (gdk_screen_get_default (), "size-changed",
			  G_CALLBACK (screen_size_changed_cb), window);
	g_signal_connect (popup, "size_request",
			  G_CALLBACK (fullscreen_popup_size_request_cb), window);

	main_menu = gtk_ui_manager_get_widget (window->priv->ui_manager, "/MainMenu");
	gtk_widget_hide (main_menu);
	gtk_widget_hide (window->priv->statusbar);
	
	update_fullscreen_popup (window);

	gtk_widget_show (popup);
}

static void
ev_window_unfullscreen (EvWindow *window)
{
	GtkWidget *main_menu;
	
	window->priv->fullscreen_mode = FALSE;

	main_menu = gtk_ui_manager_get_widget (window->priv->ui_manager, "/MainMenu");
	gtk_widget_show (main_menu);
	gtk_widget_show (window->priv->statusbar);
	
	destroy_exit_fullscreen_popup (window);
}
 
static void
ev_window_cmd_view_fullscreen (GtkAction *action, EvWindow *ev_window)
{
	gboolean fullscreen;
	
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	fullscreen = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));

	if (fullscreen) {
		gtk_window_fullscreen (GTK_WINDOW (ev_window));
	} else {
		gtk_window_unfullscreen (GTK_WINDOW (ev_window));
	}
}

static gboolean
ev_window_state_event_cb (GtkWidget *widget, GdkEventWindowState *event, EvWindow *window)
{
	if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
	{
		GtkActionGroup *action_group;
		GtkAction *action;
		gboolean fullscreen;

		fullscreen = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;

		if (fullscreen)
		{
			ev_window_fullscreen (window);
		}
		else
		{
			ev_window_unfullscreen (window);
		}

		action_group = window->priv->action_group;

		action = gtk_action_group_get_action (action_group, "ViewFullscreen");
		g_signal_handlers_block_by_func
			(action, G_CALLBACK (ev_window_cmd_view_fullscreen), window);
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), fullscreen);
		g_signal_handlers_unblock_by_func
			(action, G_CALLBACK (ev_window_cmd_view_fullscreen), window);

	}

	return FALSE;
}

static gboolean
ev_window_focus_out_cb (GtkWidget *widget, GdkEventFocus *event, EvWindow *ev_window)
{
	gtk_window_unfullscreen (GTK_WINDOW (ev_window));
	
	return FALSE;
}


static void
ev_window_cmd_view_zoom_in (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_zoom_in (EV_VIEW (ev_window->priv->view));
}

static void
ev_window_cmd_view_zoom_out (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_zoom_out (EV_VIEW (ev_window->priv->view));
}

static void
ev_window_cmd_view_normal_size (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_normal_size (EV_VIEW (ev_window->priv->view));
}

static void
ev_window_cmd_view_best_fit (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_best_fit (EV_VIEW (ev_window->priv->view));
}

static void
ev_window_cmd_view_page_width (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_fit_width (EV_VIEW (ev_window->priv->view));
}

static void
ev_window_cmd_go_back (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

        /* FIXME */
}

static void
ev_window_cmd_go_forward (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

        /* FIXME */
}

static void
ev_window_cmd_go_previous_page (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_set_page (EV_VIEW (ev_window->priv->view),
			  ev_view_get_page (EV_VIEW (ev_window->priv->view)) - 1);
}

static void
ev_window_cmd_go_next_page (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_set_page (EV_VIEW (ev_window->priv->view),
			  ev_view_get_page (EV_VIEW (ev_window->priv->view)) + 1);
}

static void
ev_window_cmd_go_first_page (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_set_page (EV_VIEW (ev_window->priv->view), 1);
}

static void
ev_window_cmd_go_last_page (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

	ev_view_set_page (EV_VIEW (ev_window->priv->view), G_MAXINT);
}

static void
ev_window_cmd_help_contents (GtkAction *action, EvWindow *ev_window)
{
        g_return_if_fail (EV_IS_WINDOW (ev_window));

        /* FIXME */
}

static void
ev_window_cmd_help_about (GtkAction *action, EvWindow *ev_window)
{
	const char *authors[] = {
		N_("Many..."),
		NULL
	};

	const char *documenters[] = {
		N_("Not so many..."),
		NULL
	};

	const char *license[] = {
		N_("Evince is free software; you can redistribute it and/or modify\n"
		   "it under the terms of the GNU General Public License as published by\n"
		   "the Free Software Foundation; either version 2 of the License, or\n"
		   "(at your option) any later version.\n"),		
		N_("Evince is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details.\n"),
		N_("You should have received a copy of the GNU General Public License\n"
		   "along with Evince; if not, write to the Free Software Foundation, Inc.,\n"
		   "59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n")
	};

	char *license_trans;

#ifdef ENABLE_NLS
	const char **p;

	for (p = authors; *p; ++p)
		*p = _(*p);

	for (p = documenters; *p; ++p)
		*p = _(*p);
#endif

	license_trans = g_strconcat (_(license[0]), "\n", _(license[1]), "\n",
				     _(license[2]), "\n", NULL);

	gtk_show_about_dialog (
		GTK_WINDOW (ev_window),
		"name", _("Evince"),
		"version", VERSION,
		"copyright",
		_("\xc2\xa9 1996-2004 The Evince authors"),
		"license", license_trans,
		"website", "http://www.gnome.org/projects/evince",
		"comments", _("PostScript and PDF File Viewer."),
		"authors", authors,
		"documenters", documenters,
		"translator-credits", _("translator-credits"),
		NULL);

	g_free (license_trans);
}

static void
ev_window_view_toolbar_cb (GtkAction *action, EvWindow *ev_window)
{
	g_object_set (
		G_OBJECT (gtk_ui_manager_get_widget (
				  ev_window->priv->ui_manager,
				  "/ToolBar")),
		"visible",
		gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)),
		NULL);
}

static void
ev_window_view_statusbar_cb (GtkAction *action, EvWindow *ev_window)
{
	g_object_set (
		ev_window->priv->statusbar,
		"visible",
		gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)),
		NULL);
}

static void
ev_window_view_sidebar_cb (GtkAction *action, EvWindow *ev_window)
{
        /* FIXME */
}

static void
menu_item_select_cb (GtkMenuItem *proxy, EvWindow *ev_window)
{
	GtkAction *action;
	char *message;

	action = g_object_get_data (G_OBJECT (proxy), "gtk-action");
	g_return_if_fail (action != NULL);
	
	g_object_get (G_OBJECT (action), "tooltip", &message, NULL);
	if (message) {
		gtk_statusbar_push (GTK_STATUSBAR (ev_window->priv->statusbar),
				    ev_window->priv->help_message_cid, message);
		g_free (message);
	}
}

static void
menu_item_deselect_cb (GtkMenuItem *proxy, EvWindow *ev_window)
{
	gtk_statusbar_pop (GTK_STATUSBAR (ev_window->priv->statusbar),
			   ev_window->priv->help_message_cid);
}

static void
connect_proxy_cb (GtkUIManager *ui_manager, GtkAction *action,
		  GtkWidget *proxy, EvWindow *ev_window)
{
	if (GTK_IS_MENU_ITEM (proxy)) {
		g_signal_connect (proxy, "select",
				  G_CALLBACK (menu_item_select_cb), ev_window);
		g_signal_connect (proxy, "deselect",
				  G_CALLBACK (menu_item_deselect_cb),
				  ev_window);
	}
}

static void
disconnect_proxy_cb (GtkUIManager *ui_manager, GtkAction *action,
		     GtkWidget *proxy, EvWindow *ev_window)
{
	if (GTK_IS_MENU_ITEM (proxy)) {
		g_signal_handlers_disconnect_by_func
			(proxy, G_CALLBACK (menu_item_select_cb), ev_window);
		g_signal_handlers_disconnect_by_func
			(proxy, G_CALLBACK (menu_item_deselect_cb), ev_window);
	}
}

static void
view_page_changed_cb (EvView   *view,
		      EvWindow *ev_window)
{
	update_action_sensitivity (ev_window);
}

static void
view_find_status_changed_cb (EvView   *view,
			     EvWindow *ev_window)
{
	char *text;

	text = ev_view_get_find_status_message (view);

	egg_find_bar_set_status_text (EGG_FIND_BAR (ev_window->priv->find_bar),
				      text);

	g_free (text);
}

static void
find_bar_previous_cb (EggFindBar *find_bar,
		      EvWindow   *ev_window)
{
	/* FIXME - highlight previous result */
	g_printerr ("Find Previous\n");

}

static void
find_bar_next_cb (EggFindBar *find_bar,
		  EvWindow   *ev_window)
{
	/* FIXME - highlight next result */
	g_printerr ("Find Next\n");
}

static void
find_bar_close_cb (EggFindBar *find_bar,
		   EvWindow   *ev_window)
{
	gtk_widget_hide (ev_window->priv->find_bar);

	if (ev_window->priv->exit_fullscreen_popup)
		update_fullscreen_popup (ev_window);
}

static void
find_bar_search_changed_cb (EggFindBar *find_bar,
			    GParamSpec *param,
			    EvWindow   *ev_window)
{
	gboolean case_sensitive;
	gboolean visible;
	const char *search_string;

	g_return_if_fail (EV_IS_WINDOW (ev_window));
	
	/* Either the string or case sensitivity could have changed,
	 * we connect this callback to both. We also connect it
	 * to ::visible so when the find bar is hidden, we should
	 * pretend the search string is NULL/""
	 */

	case_sensitive = egg_find_bar_get_case_sensitive (find_bar);
	visible = GTK_WIDGET_VISIBLE (find_bar);
	search_string = egg_find_bar_get_search_string (find_bar);
	
#if 0
	g_printerr ("search for '%s'\n", search_string ? search_string : "(nil)");
#endif

	if (ev_window->priv->document &&
	    EV_IS_DOCUMENT_FIND (ev_window->priv->document)) {
		if (visible && search_string) {
			ev_document_find_begin (EV_DOCUMENT_FIND (ev_window->priv->document), search_string, case_sensitive);
		} else {
			ev_document_find_cancel (EV_DOCUMENT_FIND (ev_window->priv->document));
			egg_find_bar_set_status_text (EGG_FIND_BAR (ev_window->priv->find_bar),
						      NULL);
		}
	}
}

static void
ev_window_dispose (GObject *object)
{
	EvWindowPrivate *priv;

	g_return_if_fail (object != NULL && EV_IS_WINDOW (object));

	priv = EV_WINDOW (object)->priv;

	if (priv->ui_manager) {
		g_object_unref (priv->ui_manager);
		priv->ui_manager = NULL;
	}

	if (priv->action_group) {
		g_object_unref (priv->action_group);
		priv->action_group = NULL;
	}
	
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
ev_window_class_init (EvWindowClass *ev_window_class)
{
	GObjectClass *g_object_class;

	parent_class = g_type_class_peek_parent (ev_window_class);

	g_object_class = G_OBJECT_CLASS (ev_window_class);
	g_object_class->dispose = ev_window_dispose;

	g_type_class_add_private (g_object_class, sizeof (EvWindowPrivate));

#if 0
	/* setting up signal system */
	ev_window_class->signal = ev_window_signal;

	ev_window_signals [SIGNAL] = g_signal_new (
		"signal",
		EV_TYPE_WINDOW,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EvWindowClass,
				 signal),
		NULL,
		NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE,
		0);
	/* setting up property system */
	g_object_class->set_property = ev_window_set_property;
	g_object_class->get_property = ev_window_get_property;

	g_object_class_install_property (
		g_object_class,
		PROP_ATTRIBUTE,
		g_param_spec_string ("attribute",
				     "Attribute",
				     "A simple unneccessary attribute that "
				     "does nothing special except being a "
				     "demonstration for the correct implem"
				     "entation of a GObject property",
				     "default_value",
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
#endif
}

/* Normal items */
static GtkActionEntry entries[] = {
	{ "File", NULL, N_("_File") },
        { "Edit", NULL, N_("_Edit") },
	{ "View", NULL, N_("_View") },
        { "Go", NULL, N_("_Go") },
	{ "Help", NULL, N_("_Help") },

	/* File menu */
	{ "FileOpen", GTK_STOCK_OPEN, N_("_Open"), "<control>O",
	  N_("Open a file"),
	  G_CALLBACK (ev_window_cmd_file_open) },
        { "FilePrint", GTK_STOCK_PRINT, N_("_Print"), "<control>P",
	  N_("Print this document"),
	  G_CALLBACK (ev_window_cmd_file_print) },
	{ "FileCloseWindow", GTK_STOCK_CLOSE, N_("_Close"), "<control>W",
	  N_("Close this window"),
	  G_CALLBACK (ev_window_cmd_file_close_window) },

        /* Edit menu */
        { "EditCopy", GTK_STOCK_COPY, N_("_Copy"), "<control>C",
          N_("Copy text from the document"),
          G_CALLBACK (ev_window_cmd_edit_copy) },
        
        { "EditFind", GTK_STOCK_FIND, N_("_Find"), "<control>F",
          N_("Find a word or phrase in the document"),
          G_CALLBACK (ev_window_cmd_edit_find) },

        /* View menu */
        { "ViewZoomIn", GTK_STOCK_ZOOM_IN, N_("Zoom _In"), "<control>plus",
          N_("Enlarge the document"),
          G_CALLBACK (ev_window_cmd_view_zoom_in) },
        { "ViewZoomOut", GTK_STOCK_ZOOM_OUT, N_("Zoom _Out"), "<control>minus",
          N_("Shrink the document"),
          G_CALLBACK (ev_window_cmd_view_zoom_out) },
        { "ViewNormalSize", GTK_STOCK_ZOOM_100, N_("_Normal Size"), "<control>0",
          N_("Zoom to the normal size"),
          G_CALLBACK (ev_window_cmd_view_normal_size) },
        { "ViewBestFit", GTK_STOCK_ZOOM_FIT, N_("_Best Fit"), NULL,
          N_("Zoom to fit the document to the current window"),
          G_CALLBACK (ev_window_cmd_view_best_fit) },
        { "ViewPageWidth", EV_STOCK_ZOOM_FIT_WIDTH, N_("Fit Page _Width"), NULL,
          N_("Zoom to fit the width of the current window "),
          G_CALLBACK (ev_window_cmd_view_page_width) },

        /* Go menu */
        { "GoBack", GTK_STOCK_GO_BACK, N_("_Back"), "<mod1>Left",
          N_("Go to the page viewed before this one"),
          G_CALLBACK (ev_window_cmd_go_back) },
        { "GoForward", GTK_STOCK_GO_FORWARD, N_("Fo_rward"), "<mod1>Right",
          N_("Go to the page viewed before this one"),
          G_CALLBACK (ev_window_cmd_go_forward) },
        { "GoPreviousPage", GTK_STOCK_GO_BACK, N_("_Previous Page"), "<control>Page_Up",
          N_("Go to the previous page"),
          G_CALLBACK (ev_window_cmd_go_previous_page) },
        { "GoNextPage", GTK_STOCK_GO_FORWARD, N_("_Next Page"), "<control>Page_Down",
          N_("Go to the next page"),
          G_CALLBACK (ev_window_cmd_go_next_page) },
        { "GoFirstPage", GTK_STOCK_GOTO_FIRST, N_("_First Page"), "<control>Home",
          N_("Go to the first page"),
          G_CALLBACK (ev_window_cmd_go_first_page) },        
        { "GoLastPage", GTK_STOCK_GOTO_LAST, N_("_Last Page"), "<control>End",
          N_("Go to the last page"),
          G_CALLBACK (ev_window_cmd_go_last_page) },
        
	/* Help menu */
	{ "HelpContents", GTK_STOCK_HELP, N_("_Contents"), NULL,
	  N_("Display help for the viewer application"),
	  G_CALLBACK (ev_window_cmd_help_contents) },
        
	{ "HelpAbout", GTK_STOCK_ABOUT, N_("_About"), NULL,
	  N_("Display credits for the document viewer creators"),
	  G_CALLBACK (ev_window_cmd_help_about) },
};

/* Toggle items */
static GtkToggleActionEntry toggle_entries[] = {
	/* View Menu */
	{ "ViewToolbar", NULL, N_("_Toolbar"), "<shift><control>T",
	  N_("Show or hide toolbar"),
	  G_CALLBACK (ev_window_view_toolbar_cb), TRUE },
	{ "ViewStatusbar", NULL, N_("_Statusbar"), NULL,
	  N_("Show or hide statusbar"),
	  G_CALLBACK (ev_window_view_statusbar_cb), TRUE },
        { "ViewSidebar", NULL, N_("Side_bar"), "F9",
	  N_("Show or hide sidebar"),
	  G_CALLBACK (ev_window_view_sidebar_cb), FALSE },
        { "ViewFullscreen", NULL, N_("_Fullscreen"), "F11",
          N_("Expand the window to fill the screen"),
          G_CALLBACK (ev_window_cmd_view_fullscreen) },
};

static void
ev_window_init (EvWindow *ev_window)
{
	GtkActionGroup *action_group;
	GtkAccelGroup *accel_group;
	GError *error = NULL;
	GtkWidget *scrolled_window;
	GtkWidget *menubar;
	GtkWidget *toolbar;
	GtkWidget *sidebar_widget;

	ev_window->priv = EV_WINDOW_GET_PRIVATE (ev_window);

	gtk_window_set_title (GTK_WINDOW (ev_window), _("Document Viewer"));

	ev_window->priv->main_box = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (ev_window), ev_window->priv->main_box);
	gtk_widget_show (ev_window->priv->main_box);
	
	action_group = gtk_action_group_new ("MenuActions");
	ev_window->priv->action_group = action_group;
	gtk_action_group_set_translation_domain (action_group, NULL);
	gtk_action_group_add_actions (action_group, entries,
				      G_N_ELEMENTS (entries), ev_window);
	gtk_action_group_add_toggle_actions (action_group, toggle_entries,
					     G_N_ELEMENTS (toggle_entries),
					     ev_window);

	ev_window->priv->ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (ev_window->priv->ui_manager,
					    action_group, 0);

	accel_group =
		gtk_ui_manager_get_accel_group (ev_window->priv->ui_manager);
	gtk_window_add_accel_group (GTK_WINDOW (ev_window), accel_group);

	g_signal_connect (ev_window->priv->ui_manager, "connect_proxy",
			  G_CALLBACK (connect_proxy_cb), ev_window);
	g_signal_connect (ev_window->priv->ui_manager, "disconnect_proxy",
			  G_CALLBACK (disconnect_proxy_cb), ev_window);

	if (!gtk_ui_manager_add_ui_from_file (ev_window->priv->ui_manager,
					      DATADIR"/evince-ui.xml",
					      &error)) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}

	menubar = gtk_ui_manager_get_widget (ev_window->priv->ui_manager,
					     "/MainMenu");
	gtk_box_pack_start (GTK_BOX (ev_window->priv->main_box), menubar,
			    FALSE, FALSE, 0);

	toolbar = gtk_ui_manager_get_widget (ev_window->priv->ui_manager,
					     "/ToolBar");
	gtk_box_pack_start (GTK_BOX (ev_window->priv->main_box), toolbar,
			    FALSE, FALSE, 0);

	/* Add the main area */
	ev_window->priv->hpaned = gtk_hpaned_new ();
	gtk_widget_show (ev_window->priv->hpaned);
	gtk_box_pack_start (GTK_BOX (ev_window->priv->main_box), ev_window->priv->hpaned,
			    TRUE, TRUE, 0);

	ev_window->priv->sidebar = ev_sidebar_new ();
	gtk_widget_show (ev_window->priv->sidebar);
	gtk_paned_add1 (GTK_PANED (ev_window->priv->hpaned),
			ev_window->priv->sidebar);

	/* Stub sidebar, for now */
	sidebar_widget = ev_sidebar_bookmarks_new ();
	gtk_widget_show (sidebar_widget);
	ev_sidebar_add_page (EV_SIDEBAR (ev_window->priv->sidebar),
			     "bookmarks",
			     _("Bookmarks"),
			     sidebar_widget);

	sidebar_widget = ev_sidebar_thumbnails_new ();
	gtk_widget_show (sidebar_widget);
	ev_sidebar_add_page (EV_SIDEBAR (ev_window->priv->sidebar),
			     "thumbnails",
			     _("Thumbnails"),
			     sidebar_widget);
	
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (scrolled_window);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_paned_add2 (GTK_PANED (ev_window->priv->hpaned),
			scrolled_window);

	ev_window->priv->view = ev_view_new ();
	gtk_widget_show (ev_window->priv->view);
	gtk_container_add (GTK_CONTAINER (scrolled_window),
			   ev_window->priv->view);
	g_signal_connect (ev_window->priv->view,
			  "page-changed",
			  G_CALLBACK (view_page_changed_cb),
			  ev_window);
	g_signal_connect (ev_window->priv->view,
			  "find-status-changed",
			  G_CALLBACK (view_find_status_changed_cb),
			  ev_window);
	
	ev_window->priv->statusbar = gtk_statusbar_new ();
	gtk_widget_show (ev_window->priv->statusbar);
	gtk_box_pack_end (GTK_BOX (ev_window->priv->main_box),
			  ev_window->priv->statusbar,
			  FALSE, TRUE, 0);
	ev_window->priv->help_message_cid = gtk_statusbar_get_context_id
		(GTK_STATUSBAR (ev_window->priv->statusbar), "help_message");

	ev_window->priv->find_bar = egg_find_bar_new ();
	gtk_box_pack_end (GTK_BOX (ev_window->priv->main_box),
			  ev_window->priv->find_bar,
			  FALSE, TRUE, 0);
	
	/* Connect to find bar signals */
	g_signal_connect (ev_window->priv->find_bar,
			  "previous",
			  G_CALLBACK (find_bar_previous_cb),
			  ev_window);
	g_signal_connect (ev_window->priv->find_bar,
			  "next",
			  G_CALLBACK (find_bar_next_cb),
			  ev_window);
	g_signal_connect (ev_window->priv->find_bar,
			  "close",
			  G_CALLBACK (find_bar_close_cb),
			  ev_window);
	g_signal_connect (ev_window->priv->find_bar,
			  "notify::search-string",
			  G_CALLBACK (find_bar_search_changed_cb),
			  ev_window);
	g_signal_connect (ev_window->priv->find_bar,
			  "notify::case-sensitive",
			  G_CALLBACK (find_bar_search_changed_cb),
			  ev_window);
	g_signal_connect (ev_window->priv->find_bar,
			  "notify::visible",
			  G_CALLBACK (find_bar_search_changed_cb),
			  ev_window);

	g_signal_connect (ev_window, "window-state-event",
			  G_CALLBACK (ev_window_state_event_cb),
			  ev_window);
	g_signal_connect (ev_window, "focus_out_event",
			  G_CALLBACK (ev_window_focus_out_cb),
			  ev_window);
	
	/* Give focus to the scrolled window */
	gtk_widget_grab_focus (scrolled_window);
	
	update_action_sensitivity (ev_window);
} 
