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

#include <glib.h>
#include <glib/gi18n.h>
#include <X11/Xlib.h>

#ifdef HAVE_DBUS_1_1
#include <dbus/dbus.h>
#endif

#ifdef HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-watch.h>
#include <avahi-glib/glib-malloc.h>
#endif /* HAVE_AVAHI */

#ifdef HAVE_HOWL
/* Workaround broken howl installing config.h */
#undef PACKAGE
#undef VERSION
#include <howl.h>
#endif /* HAVE_HOWL */

#include <gconf/gconf-client.h>

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#endif

#include "user_share.h"
#include "user_share-private.h"
#include "http.h"

#ifdef HAVE_DBUS_1_1
static char *dbus_session_id;
#endif

static pid_t httpd_pid = 0;

static int
get_port (void)
{
	int sock;
	struct sockaddr_in addr;
	int reuse;
	socklen_t len;

	sock = socket (PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		return -1;
	}

	memset (&addr, 0, sizeof (addr));
	addr.sin_port = 0;
	addr.sin_addr.s_addr = INADDR_ANY;

	reuse = 1;
	setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse));
	if (bind (sock, (struct sockaddr *)&addr, sizeof (addr)) == -1) {
		close (sock);
		return -1;
	}

	len = sizeof (addr);
	if (getsockname (sock, (struct sockaddr *)&addr, &len) == -1) {
		close (sock);
		return -1;
	}

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
	/* XXX This exposes a potential race condition, but without this,
	 * httpd will not start on the above listed platforms due to the fact
	 * that SO_REUSEADDR is also needed when Apache binds to the listening
	 * socket.  At this time, Apache does not support that socket option.
	 */
	close (sock);
#endif
	return ntohs (addr.sin_port);
}


static char *
get_share_name (void)
{
	static char *name = NULL;
	const char *host_name;

	if (name == NULL) {
		host_name = g_get_host_name ();
		if (strcmp (host_name, "localhost") == 0) {
			/* Translators: The %s will get filled in with the user name
			   of the user, to form a genitive. If this is difficult to
			   translate correctly so that it will work correctly in your
			   language, you may use something equivalent to
			   "Public files of %s", or leave out the %s altogether.
			   In the latter case, please put "%.0s" somewhere in the string,
			   which will match the user name string passed by the C code,
			   but not put the user name in the final string. This is to
			   avoid the warning that msgfmt might otherwise generate. */
			name = g_strdup_printf (_("%s's public files"), g_get_user_name ());
		} else {
			/* Translators: This is similar to the string before, only it
			   has the hostname in it too. */
			name = g_strdup_printf (_("%s's public files on %s"),
						g_get_user_name (),
						host_name);
		}

	}
	return name;
}

#ifdef HAVE_DBUS_1_1
static void
init_dbus() {
	/* The only use we make of D-BUS is to fetch the session BUS ID so we can export
	 * it via mDNS, so we connect and then immediately disconnect. If we were using
	 * the D-BUS session BUS for something persistent, the following code should use
	 * dbus_bus_get() and skip the shutdown. (Avahi uses the D-BUS  _system_ bus
	 * internally.)
	 */

	DBusError derror;
	DBusConnection *connection;

	dbus_error_init(&derror);

	connection = dbus_bus_get_private(DBUS_BUS_SESSION, &derror);
	if (connection == NULL) {
		g_printerr("Failed to connect to session bus: %s", derror.message);
		dbus_error_free(&derror);
		return;
	}

	dbus_session_id = dbus_bus_get_id(connection, &derror);
	if (dbus_session_id == NULL) {
		/* This can happen if the D-BUS library has been upgraded to 1.1, but the
		 * user's session hasn't yet been restarted
		 */
		g_printerr("Failed to get session BUS ID: %s", derror.message);
		dbus_error_free(&derror);
	}

	dbus_connection_set_exit_on_disconnect(connection, FALSE);
	dbus_connection_close(connection);
	dbus_connection_unref(connection);
}
#endif

#ifdef HAVE_AVAHI

static AvahiClient *avahi_client = NULL;
static gboolean avahi_should_publish = FALSE;
static gboolean avahi_running = FALSE;
static int avahi_port = 0;
static AvahiEntryGroup *entry_group = NULL;
static char *avahi_name = NULL;

static AvahiStringList*
new_text_record_list (const char *first_key,
		      const char *first_value,
		      ...)
{
	va_list args;
	const char *k;
	const char *v;
	AvahiStringList *list;

	if (first_key == NULL)
		return NULL;

	list = NULL;

	list = avahi_string_list_add_pair (list, first_key, first_value);

	va_start (args, first_value);
	k = va_arg (args, const char*);
	if (k)
		v = va_arg (args, const char*);
	while (k != NULL) {
		list = avahi_string_list_add_pair (list, k, v);

		k = va_arg (args, const char*);
		if (k)
			v = va_arg (args, const char*);
	}

	va_end(args);

	return list;
}

static gboolean
create_service (void) {
	int ret;
	AvahiStringList *txt_records;

	if (avahi_name == NULL) {
		avahi_name = g_strdup (get_share_name ());
	}

	txt_records = new_text_record_list ("u", "guest",
#ifdef HAVE_DBUS_1_1
					    /* This must be last */
					    dbus_session_id != NULL ? "org.freedesktop.od.session" : NULL, dbus_session_id,
#endif									   
					    NULL);

	ret = avahi_entry_group_add_service_strlst (entry_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 
						    AVAHI_PUBLISH_USE_MULTICAST,
						    avahi_name, "_webdav._tcp", NULL, NULL, 
						    avahi_port,
						    txt_records);

	avahi_string_list_free(txt_records);

	if (ret < 0) {
		return FALSE;
	}

	ret = avahi_entry_group_commit (entry_group);

	if (ret < 0) {
		return FALSE;
	}

	return TRUE;
}

static void 
entry_group_callback (AvahiEntryGroup *g, 
		      AvahiEntryGroupState state, 
		      void *userdata) 
{
	if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
	} else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
		char *n;

		/* A service name collision happened. Let's pick a new name */
		n = avahi_alternative_service_name (avahi_name);
		g_free (avahi_name);
		avahi_name = n;

		fprintf (stderr, "Service name collision, renaming service to '%s'\n", avahi_name);

		/* And recreate the services */
		create_service();
	}
}

static void
avahi_client_callback (AvahiClient *client, AvahiClientState state, void *userdata)
{
	if (state == AVAHI_CLIENT_S_RUNNING) {
		avahi_running = TRUE;
		if (avahi_should_publish) {
			create_service ();
		}
	} else if (state == AVAHI_CLIENT_S_COLLISION) {
		avahi_entry_group_reset (entry_group);
	} else if (state == AVAHI_CLIENT_FAILURE) {
		avahi_running = FALSE;
	}
}

static gboolean
init_avahi (void)
{
	AvahiGLibPoll *poll;
	int error;

	avahi_set_allocator (avahi_glib_allocator ());

	poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);

	/* Create a new AvahiClient instance */
	avahi_client = avahi_client_new (avahi_glib_poll_get (poll),
					 AVAHI_CLIENT_NO_FAIL,
					 avahi_client_callback,
					 NULL,
					 &error);
	if (avahi_client == NULL) {
		return FALSE;
	}

	entry_group = avahi_entry_group_new (avahi_client, entry_group_callback, NULL);
	if (entry_group == NULL) {
		return FALSE;
	}

	return TRUE;
}


static gboolean
publish_service (int port)
{
	avahi_should_publish = TRUE;
	avahi_port = port;
	if (avahi_running) {
		create_service ();
	}
	return TRUE;
}

static void
stop_publishing (void)
{
	avahi_should_publish = FALSE;
	avahi_entry_group_reset (entry_group);
}
#endif

#ifdef HAVE_HOWL

static sw_discovery_publish_id published_id = 0;
static sw_discovery howl_session;

static gboolean
howl_input (GIOChannel  *io_channel,
	    GIOCondition cond,
	    gpointer     callback_data)
{
	sw_discovery session;
	session = callback_data;
	sw_salt salt;

	if (sw_discovery_salt (session, &salt) == SW_OKAY) {
		sw_salt_lock (salt);
		sw_discovery_read_socket (session);
		sw_salt_unlock (salt);
	}
	return TRUE;
}

static void
set_up_howl_session (sw_discovery session)
{
	int fd;
	GIOChannel *channel;

	fd = sw_discovery_socket (session);

	channel = g_io_channel_unix_new (fd);
	g_io_add_watch (channel,
			G_IO_IN,
			howl_input, session);
	g_io_channel_unref (channel);
}

static sw_result
publish_reply (sw_discovery			discovery,
	       sw_discovery_publish_status	status,
	       sw_discovery_oid			id,
	       sw_opaque			extra)
{
	return SW_OKAY;
}


static gboolean
publish_service (int port)
{
	sw_result result;

	result = sw_discovery_publish (howl_session, 0,
				       get_share_name (),
				       "_webdav._tcp",
				       NULL, NULL,
				       port,
				       /* TODO: should be u=guest */
				       /* text */ (unsigned char *) "", 0,
				       publish_reply, NULL, &published_id);
	if (result != SW_OKAY) {
		return FALSE;
	}
	return TRUE;
}

static void
stop_publishing (void)
{
	if (published_id != 0)
		sw_discovery_cancel (howl_session, published_id);
	published_id = 0;
}
#endif /* HAVE_HOWL */


static void
ensure_conf_dir (void)
{
	char *dirname;

	dirname = g_build_filename (g_get_home_dir (), ".gnome2", "user-share", NULL);
	g_mkdir_with_parents (dirname, 0755);
	g_free (dirname);
}

static void
httpd_child_setup (gpointer user_data)
{
#ifdef HAVE_SELINUX
	char *mycon;
	/* If selinux is enabled, avoid transitioning to the httpd_t context,
	   as this normally means you can't read the users homedir. */
	if (is_selinux_enabled()) {
		if (getcon (&mycon) < 0) {
			abort ();
		}
		if (setexeccon (mycon) < 0)
			abort ();
		freecon (mycon);
	}
#endif
}

static gboolean
spawn_httpd (int port, pid_t *pid_out)
{
	char *free1, *free2, *free3;
	gboolean res;
	char *argv[10];
	char *env[10];
	int i;
	gint status;
	char *pid_filename;
	char *pidfile;
	GError *error;
	gboolean got_pidfile;
	GConfClient *client;
	char *str;
	char *public_dir;

	public_dir = lookup_public_dir ();
	ensure_conf_dir ();

	i = 0;
	argv[i++] = HTTPD_PROGRAM;
	argv[i++] = "-f";
	argv[i++] = HTTPD_CONFIG;
	argv[i++] = "-C";
	free1 = argv[i++] = g_strdup_printf ("Listen %d", port);

	client = gconf_client_get_default ();
	str = gconf_client_get_string (client,
				       FILE_SHARING_REQUIRE_PASSWORD, NULL);

	if (str && strcmp (str, "never") == 0) {
		/* Do nothing */
	} else if (str && strcmp (str, "on_write") == 0){
		argv[i++] = "-D";
		argv[i++] = "RequirePasswordOnWrite";
	} else {
		/* always, or safe fallback */
		argv[i++] = "-D";
		argv[i++] = "RequirePasswordAlways";
	}
	g_free (str);
	g_object_unref (client);

	argv[i] = NULL;

	i = 0;
	free2 = env[i++] = g_strdup_printf("HOME=%s", g_get_home_dir());
	free3 = env[i++] = g_strdup_printf("XDG_PUBLICSHARE_DIR=%s", public_dir);
	env[i++] = "LANG=C";
	env[i] = NULL;

	pid_filename = g_build_filename (g_get_home_dir (), ".gnome2/user-share/pid", NULL);

	/* Remove pid file before spawning to avoid races with child and old pidfile */
	unlink (pid_filename);

	error = NULL;
	res = g_spawn_sync (g_get_home_dir(),
			    argv, env, 0,
			    httpd_child_setup, NULL,
			    NULL, NULL,
			    &status,
			    &error);
	g_free (free1);
	g_free (free2);
	g_free (free3);
	g_free (public_dir);

	if (!res) {
		fprintf (stderr, "error spawning httpd: %s\n",
			 error->message);
		g_error_free (error);
		return FALSE;
	}

	if (status != 0) {
		g_free (pid_filename);
		return FALSE;
	}

	got_pidfile = FALSE;
	error = NULL;
	for (i = 0; i < 5; i++) {
		if (error != NULL) 
			g_error_free (error); 
		error = NULL;
		if (g_file_get_contents (pid_filename, &pidfile, NULL, &error)) {
			got_pidfile = TRUE;
			*pid_out = atoi (pidfile);
			g_free (pidfile);
			break;
		}
		sleep (1);
	}

	g_free (pid_filename);

	if (!got_pidfile) {
		fprintf (stderr, "error opening httpd pidfile: %s\n", error->message);
		g_error_free (error);
		return FALSE;
	}
	return TRUE;
}

static void
kill_httpd (void)
{
	if (httpd_pid != 0) {
		kill (httpd_pid, SIGTERM);

		/* Allow child time to die, we can't waitpid, because its
		   not a direct child */
		sleep (1);
	}
	httpd_pid = 0;
}

void
http_up (void)
{
	guint port;

	port = get_port ();
	if (!spawn_httpd (port, &httpd_pid)) {
		fprintf (stderr, "spawning httpd failed\n");
	} else {
		if (!publish_service (port)) {
			fprintf (stderr, "publishing failed\n");
		}
	}
}

void
http_down (void)
{
	kill_httpd ();
	stop_publishing ();
}

gboolean
http_init (void)
{
#ifdef HAVE_DBUS_1_1
	init_dbus();
#endif	

#ifdef HAVE_AVAHI
	if (!init_avahi ()) {
		/* Print out the error string */
		fprintf (stderr, "avahi init failed\n");
		return FALSE;
	}
#endif
#ifdef HAVE_HOWL
	if (sw_discovery_init (&howl_session) != SW_OKAY) {
		fprintf (stderr, "howl init failed\n");
		return FALSE;
	}
	set_up_howl_session (howl_session);
#endif

	return TRUE;
}

pid_t
http_get_pid (void)
{
	return httpd_pid;
}
