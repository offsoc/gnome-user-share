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
 *  Authors: Bastien Nocera <hadess@hadess.net>
 *
 */

#include "config.h"

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gconf/gconf-client.h>

#include <string.h>

#include "obexftp.h"
#include "user_share.h"
#include "user_share-private.h"

static DBusGConnection *connection = NULL;
static DBusGProxy *manager_proxy = NULL;
static DBusGProxy *server_proxy = NULL;

void
obexftp_up (void)
{
	GError *err = NULL;
	GConfClient *client;
	char *public_dir, *server;
	gboolean allow_write, require_pairing;

	client = gconf_client_get_default ();
	require_pairing = gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_REQUIRE_PAIRING, NULL);

	server = NULL;
	if (manager_proxy == NULL) {
		manager_proxy = dbus_g_proxy_new_for_name (connection,
							   "org.openobex",
							   "/org/openobex",
							   "org.openobex.Manager");
		if (dbus_g_proxy_call (manager_proxy, "CreateBluetoothServer",
				       &err, G_TYPE_STRING, "00:00:00:00:00:00", G_TYPE_STRING, "ftp", G_TYPE_BOOLEAN, require_pairing, G_TYPE_INVALID,
				       DBUS_TYPE_G_OBJECT_PATH, &server, G_TYPE_INVALID) == FALSE) {
			g_printerr ("Creating Bluetooth ObexFTP server failed: %s\n",
				    err->message);
			g_error_free (err);
			g_object_unref (manager_proxy);
			manager_proxy = NULL;
			return;
		}
	}

	public_dir = lookup_public_dir ();
	allow_write = gconf_client_get_bool (client, FILE_SHARING_BLUETOOTH_ALLOW_WRITE, NULL);
	g_object_unref (client);

	if (server_proxy == NULL) {
		server_proxy = dbus_g_proxy_new_for_name (connection,
							   "org.openobex",
							   server,
							   "org.openobex.Server");
		g_free (server);
	}
	if (dbus_g_proxy_call (server_proxy, "Start", &err,
			   G_TYPE_STRING, public_dir, G_TYPE_BOOLEAN, allow_write, G_TYPE_BOOLEAN, TRUE, G_TYPE_INVALID,
			   G_TYPE_INVALID) == FALSE) {
		g_printerr ("Starting Bluetooth ObexFTP server failed: %s\n",
			    err->message);
		g_error_free (err);
		g_free (public_dir);
		g_object_unref (server_proxy);
		server_proxy = NULL;
		g_object_unref (manager_proxy);
		manager_proxy = NULL;
		return;
	}

	g_free (public_dir);
}

static void
obexftp_stop (gboolean stop_manager)
{
	GError *err = NULL;

	if (server_proxy == NULL)
		return;

	if (dbus_g_proxy_call (server_proxy, "Close", &err, G_TYPE_INVALID, G_TYPE_INVALID) == FALSE) {
		const gchar *error_name = NULL;

		if (err != NULL && err->code == DBUS_GERROR_REMOTE_EXCEPTION)
			error_name = dbus_g_error_get_name (err);
		if (error_name == NULL ||
		    (error_name != NULL && strcmp (error_name, "org.openobex.Error.NotStarted") != 0)) {
			g_printerr ("Stopping Bluetooth ObexFTP server failed: %s\n",
				    err->message);
			g_error_free (err);
			return;
		}
		g_error_free (err);
	}

	if (stop_manager != FALSE) {
		g_object_unref (server_proxy);
		server_proxy = NULL;
		g_object_unref (manager_proxy);
		manager_proxy = NULL;
	}
}

void
obexftp_down (void)
{
	obexftp_stop (TRUE);
}

void
obexftp_restart (void)
{
	obexftp_stop (FALSE);
	obexftp_up ();
}

gboolean
obexftp_init (void)
{
	GError *err = NULL;

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &err);
	if (connection == NULL) {
		g_printerr ("Connecting to session bus failed: %s\n",
			    err->message);
		g_error_free (err);
		return FALSE;
	}

	dbus_connection_set_exit_on_disconnect (dbus_g_connection_get_connection (connection), FALSE);
	return TRUE;
}

