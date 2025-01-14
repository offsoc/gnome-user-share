/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */

/*
 *  Copyright (C) 2004-2008 Red Hat, Inc.
 *
 *  Nautilus is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  Nautilus is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Authors: Alexander Larsson <alexl@redhat.com>
 *
 */

#include "config.h"

#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <X11/Xlib.h>

#include "user_share.h"
#include "user_share-private.h"
#include "http.h"
#include "obexftp.h"
#include "obexpush.h"

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include <gconf/gconf-client.h>

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

/* ConsoleKit */
#define CK_NAME			"org.freedesktop.ConsoleKit"
#define CK_INTERFACE		"org.freedesktop.ConsoleKit"
#define CK_MANAGER_PATH		"/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE	"org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE	"org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE	"org.freedesktop.ConsoleKit.Session"

static guint disabled_timeout_tag = 0;

static void
obex_services_start (void)
{
	GConfClient *client;

	client = gconf_client_get_default ();
	
	if (gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_OBEXPUSH_ENABLED, NULL) == TRUE) {
	    obexpush_up ();
	}
	if (gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_ENABLED, NULL) == TRUE) {
	    obexftp_up ();
	}

	g_object_unref (client);
}
    
static void
obex_services_shutdown (void)
{
	obexpush_down ();
	obexftp_down ();
}

static void
sessionchanged_cb (void)
{
	DBusGConnection *dbus_connection;
	DBusGProxy *proxy_ck_manager;
	DBusGProxy *proxy_ck_session;

	gchar *ck_session_path;
	gboolean is_active = FALSE;
	GError *error = NULL;

	g_message ("Active session changed");

	dbus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (!dbus_connection) {
	    g_warning ("Unable to connect to dbus");
	    dbus_g_connection_unref (dbus_connection);
	    return;
	}
    
	proxy_ck_manager = dbus_g_proxy_new_for_name (dbus_connection,
			   			      CK_NAME,
		    				      CK_MANAGER_PATH,
					              CK_MANAGER_INTERFACE);
	if (dbus_g_proxy_call (proxy_ck_manager, "GetCurrentSession",
			       &error, G_TYPE_INVALID,
			       DBUS_TYPE_G_OBJECT_PATH, &ck_session_path,
			       G_TYPE_INVALID) == FALSE) {
	    g_warning ("Couldn't request the name: %s", error->message);
	    dbus_g_connection_unref (dbus_connection);
	    g_object_unref (proxy_ck_manager);
	    g_error_free (error);
	    return;
	}
	
	proxy_ck_session = dbus_g_proxy_new_for_name (dbus_connection,
						      CK_NAME,
						      ck_session_path,
						      CK_SESSION_INTERFACE);

	if (dbus_g_proxy_call (proxy_ck_session, "IsActive",
			       &error, G_TYPE_INVALID,
			       G_TYPE_BOOLEAN, &is_active, 
			       G_TYPE_INVALID) == FALSE) {
	    
	    g_warning ("Couldn't request the name: %s", error->message);
	    dbus_g_connection_unref (dbus_connection);
	    g_object_unref (proxy_ck_manager);
	    g_object_unref (proxy_ck_session);
	    g_error_free (error);
	    return;
	}
	
	if (is_active) {
		obex_services_start ();
	} else {
		obex_services_shutdown (); 
	}

	dbus_g_connection_unref (dbus_connection);
	g_free (ck_session_path);
	g_object_unref (proxy_ck_manager);
	g_object_unref (proxy_ck_session);
	if (error != NULL) {
	    g_error_free (error);
	}
}

static void
consolekit_init (void)
{
	DBusGConnection *dbus_connection;
	DBusGProxy *proxy_ck_manager;
	DBusGProxy *proxy_ck_session;
	DBusGProxy *proxy_ck_seat;
	gchar *ck_session_path;
	gchar *ck_seat_path;
	GError *error = NULL;

	dbus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);

	if (!dbus_connection) {
	    g_warning ("Unable to connect to dbus");
	    dbus_g_connection_unref (dbus_connection);
	    return;
	}
    
	proxy_ck_manager = dbus_g_proxy_new_for_name (dbus_connection,
						      CK_NAME,
						      CK_MANAGER_PATH,
						      CK_MANAGER_INTERFACE);
	if (dbus_g_proxy_call (proxy_ck_manager, "GetCurrentSession",
			       &error, G_TYPE_INVALID,
			       DBUS_TYPE_G_OBJECT_PATH, &ck_session_path,
			       G_TYPE_INVALID) == FALSE) {
	    
	    g_warning ("Couldn't request the name: %s", error->message);
	    g_object_unref (proxy_ck_manager);
	    return;
	}
	
	proxy_ck_session = dbus_g_proxy_new_for_name (dbus_connection,
						      CK_NAME,
						      ck_session_path,
						      CK_SESSION_INTERFACE);
	if (dbus_g_proxy_call (proxy_ck_session, "GetSeatId",
			       &error, G_TYPE_INVALID,
			       DBUS_TYPE_G_OBJECT_PATH, &ck_seat_path,
			       G_TYPE_INVALID) == FALSE) {
	    
	    g_warning ("Couldn't request the name: %s", error->message);
	    g_object_unref (proxy_ck_session); 
	    return;
	}
	
	proxy_ck_seat = dbus_g_proxy_new_for_name (dbus_connection,
						   CK_NAME,
						   ck_seat_path,
						   CK_SEAT_INTERFACE);
	dbus_g_proxy_add_signal (proxy_ck_seat, "ActiveSessionChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy_ck_seat, "ActiveSessionChanged",
				     G_CALLBACK (sessionchanged_cb), NULL, NULL);
	if (error != NULL) {
	    g_error_free (error);
	}
	g_object_unref (proxy_ck_manager);
	g_object_unref (proxy_ck_session);
	g_free (ck_seat_path);
	dbus_g_connection_unref (dbus_connection);
}

char *
lookup_public_dir (void)
{
	const char *public_dir;
	char *dir;

	public_dir = g_get_user_special_dir (G_USER_DIRECTORY_PUBLIC_SHARE);
	if (public_dir != NULL && strcmp (public_dir, g_get_home_dir ()) != 0) {
		g_mkdir_with_parents (public_dir, 0755);
		return g_strdup (public_dir);
	}

	dir = g_build_filename (g_get_home_dir (), "Public", NULL);
	g_mkdir_with_parents (dir, 0755);
	return dir;
}

char *
lookup_download_dir (void)
{
	const char *download_dir;
	char *dir;

	download_dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
	if (download_dir != NULL && strcmp (download_dir, g_get_home_dir ()) != 0) {
		g_mkdir_with_parents (download_dir, 0755);
		return g_strdup (download_dir);
	}

	dir = g_build_filename (g_get_home_dir (), "Download", NULL);
	g_mkdir_with_parents (dir, 0755);
	return dir;
}

static void
migrate_old_configuration (void)
{
	const char *old_config_dir;
	const char *new_config_dir;

	old_config_dir = g_build_filename (g_get_home_dir (), ".gnome2", "user-share", NULL);
	new_config_dir = g_build_filename (g_get_user_config_dir (), "user-share", NULL);
	if (g_file_test (old_config_dir, G_FILE_TEST_IS_DIR)) {
	    g_rename (old_config_dir, new_config_dir);
	}

}

static void
require_password_changed (GConfClient* client,
			  guint cnxn_id,
			  GConfEntry *entry,
			  gpointer data)
{
	/* Need to restart to get new password setting */
	if (http_get_pid () != 0) {
		http_down ();
		http_up ();
	}
}

/* File sharing was disabled for some time, exit now */
/* If we re-enable it in the ui, this will be restarted anyway */
static gboolean
disabled_timeout_callback (gpointer user_data)
{
	GConfClient* client = (GConfClient *) user_data;
	http_down ();

	if (gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_ENABLED, NULL) == FALSE &&
	    gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_OBEXPUSH_ENABLED, NULL) == FALSE)
		_exit (0);
	return FALSE;
}

static void
file_sharing_enabled_changed (GConfClient* client,
			      guint cnxn_id,
			      GConfEntry *entry,
			      gpointer data)
{
	gboolean enabled;

	if (disabled_timeout_tag != 0) {
		g_source_remove (disabled_timeout_tag);
		disabled_timeout_tag = 0;
	}

	enabled = gconf_client_get_bool (client,
					 FILE_SHARING_ENABLED, NULL);
	if (enabled) {
		if (http_get_pid () == 0) {
			http_up ();
		}
	} else {
		http_down ();
		disabled_timeout_tag = g_timeout_add (3*1000,
						      (GSourceFunc)disabled_timeout_callback,
						      client);
	}
}

static void
file_sharing_bluetooth_allow_write_changed (GConfClient* client,
					    guint cnxn_id,
					    GConfEntry *entry,
					    gpointer data)
{
	if (gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_ENABLED, NULL) != FALSE)
		obexftp_restart ();
}

static void
file_sharing_bluetooth_require_pairing_changed (GConfClient* client,
						guint cnxn_id,
						GConfEntry *entry,
						gpointer data)
{
	if (gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_ENABLED, NULL) != FALSE) {
		/* We need to fully reset the session,
		 * otherwise the new setting isn't taken into account */
		obexftp_down ();
		obexftp_up ();
	}
}

static void
file_sharing_bluetooth_enabled_changed (GConfClient* client,
					guint cnxn_id,
					GConfEntry *entry,
					gpointer data)
{
	if (gconf_client_get_bool (client,
				   FILE_SHARING_BLUETOOTH_ENABLED, NULL) == FALSE) {
		obexftp_down ();
		if (gconf_client_get_bool (client, FILE_SHARING_ENABLED, NULL) == FALSE &&
		    gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_OBEXPUSH_ENABLED, NULL) == FALSE) {
			_exit (0);
		}
	} else {
		obexftp_up ();
	}
}

static void
file_sharing_bluetooth_obexpush_enabled_changed (GConfClient* client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 gpointer data)
{
	if (gconf_client_get_bool (client,
				   FILE_SHARING_BLUETOOTH_OBEXPUSH_ENABLED, NULL) == FALSE) {
		obexpush_down ();
		if (gconf_client_get_bool (client, FILE_SHARING_ENABLED, NULL) == FALSE &&
		    gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_ENABLED, NULL) == FALSE) {
			_exit (0);
		}
	} else {
		obexpush_up ();
	}
}

static void
file_sharing_bluetooth_obexpush_accept_files_changed (GConfClient* client,
						      guint cnxn_id,
						      GConfEntry *entry,
						      gpointer data)
{
	AcceptSetting setting;
	char *str;

	str = gconf_client_get_string (client, FILE_SHARING_BLUETOOTH_OBEXPUSH_ACCEPT_FILES, NULL);
	setting = accept_setting_from_string (str);
	g_free (str);

	obexpush_set_accept_files_policy (setting);
}

static void
file_sharing_bluetooth_obexpush_notify_changed (GConfClient* client,
						guint cnxn_id,
						GConfEntry *entry,
						gpointer data)
{
	obexpush_set_notify (gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_OBEXPUSH_NOTIFY, NULL));
}

static RETSIGTYPE
cleanup_handler (int sig)
{
	http_down ();
	obexftp_down ();
	_exit (2);
}

static int
x_io_error_handler (Display *xdisplay)
{
	http_down ();
	obexftp_down ();
	_exit (2);
}

int
main (int argc, char **argv)
{
	GConfClient *client;
	Display *xdisplay;
	int x_fd;
	Window selection_owner;
	Atom xatom;

	gtk_init (&argc, &argv);

	if (g_strcmp0 (g_get_real_name (), "root") == 0) {
		g_warning ("gnome-user-share cannot be started as root for security reasons.");
		return 1;
	}

	signal (SIGPIPE, SIG_IGN);
	signal (SIGINT, cleanup_handler);
	signal (SIGHUP, cleanup_handler);
	signal (SIGTERM, cleanup_handler);

	xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
	if (xdisplay == NULL) {
		fprintf (stderr, "Can't open display\n");
		return 1;
	}

	xatom = XInternAtom (xdisplay, "_GNOME_USER_SHARE", FALSE);
	selection_owner = XGetSelectionOwner (xdisplay, xatom);

	if (selection_owner != None) {
		/* There is an owner already, quit */
		return 1;
	}

	selection_owner = XCreateSimpleWindow (xdisplay,
					       RootWindow (xdisplay, 0),
					       0, 0, 1, 1,
					       0, 0, 0);
	XSetSelectionOwner (xdisplay, xatom, selection_owner, CurrentTime);

	if (XGetSelectionOwner (xdisplay, xatom) != selection_owner) {
		/* Didn't get the selection */
		return 1;
	}

	migrate_old_configuration ();

	client = gconf_client_get_default ();
	if (gconf_client_get_bool (client, FILE_SHARING_ENABLED, NULL) == FALSE &&
	    gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_ENABLED, NULL) == FALSE &&
	    gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_OBEXPUSH_ENABLED, NULL) == FALSE)
		return 1;

	x_fd = ConnectionNumber (xdisplay);
	XSetIOErrorHandler (x_io_error_handler);

	if (http_init () == FALSE)
		return 1;
	if (obexftp_init () == FALSE)
		return 1;
	if (obexpush_init () == FALSE)
		return 1;

	gconf_client_add_dir (client,
			      FILE_SHARING_DIR,
			      GCONF_CLIENT_PRELOAD_RECURSIVE,
			      NULL);

	gconf_client_notify_add (client,
				 FILE_SHARING_ENABLED,
				 file_sharing_enabled_changed,
				 NULL,
				 NULL,
				 NULL);
	gconf_client_notify_add (client,
				 FILE_SHARING_REQUIRE_PASSWORD,
				 require_password_changed,
				 NULL,
				 NULL,
				 NULL);
	gconf_client_notify_add (client,
				 FILE_SHARING_BLUETOOTH_ENABLED,
				 file_sharing_bluetooth_enabled_changed,
				 NULL,
				 NULL,
				 NULL);
	gconf_client_notify_add (client,
				 FILE_SHARING_BLUETOOTH_ALLOW_WRITE,
				 file_sharing_bluetooth_allow_write_changed,
				 NULL,
				 NULL,
				 NULL);
	gconf_client_notify_add (client,
				 FILE_SHARING_BLUETOOTH_REQUIRE_PAIRING,
				 file_sharing_bluetooth_require_pairing_changed,
				 NULL,
				 NULL,
				 NULL);
	gconf_client_notify_add (client,
				 FILE_SHARING_BLUETOOTH_OBEXPUSH_ENABLED,
				 file_sharing_bluetooth_obexpush_enabled_changed,
				 NULL,
				 NULL,
				 NULL);
	gconf_client_notify_add (client,
				 FILE_SHARING_BLUETOOTH_OBEXPUSH_ACCEPT_FILES,
				 file_sharing_bluetooth_obexpush_accept_files_changed,
				 NULL,
				 NULL,
				 NULL);
	gconf_client_notify_add (client,
				 FILE_SHARING_BLUETOOTH_OBEXPUSH_NOTIFY,
				 file_sharing_bluetooth_obexpush_notify_changed,
				 NULL,
				 NULL,
				 NULL);

	g_object_unref (client);

	/* Initial setting */
	file_sharing_enabled_changed (client, 0, NULL, NULL);
	file_sharing_bluetooth_enabled_changed (client, 0, NULL, NULL);
	file_sharing_bluetooth_obexpush_accept_files_changed (client, 0, NULL, NULL);
	file_sharing_bluetooth_obexpush_notify_changed (client, 0, NULL, NULL);
	file_sharing_bluetooth_obexpush_enabled_changed (client, 0, NULL, NULL);

	consolekit_init ();

	gtk_main ();

	return 0;
}

