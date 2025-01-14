/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */

/*
 *  Copyright (C) 2004 Red Hat, Inc.
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

#include <string.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gconf/gconf-client.h>

#include "user_share-private.h"

#define REALM "Please log in as the user guest"
#define USER "guest"

static GtkBuilder* builder;


static void
write_out_password (const char *password)
{
    char *to_hash;
    char *ascii_digest;
    char *line;
    char *filename;
    FILE *file;

    to_hash = g_strdup_printf ("%s:%s:%s", USER, REALM, password);
    ascii_digest = g_compute_checksum_for_string (G_CHECKSUM_MD5, to_hash, strlen (to_hash));
    g_free (to_hash);
    line = g_strdup_printf ("%s:%s:%s\n", USER, REALM, ascii_digest);
    g_free (ascii_digest);

    filename = g_build_filename (g_get_user_config_dir (), "user-share", "passwd", NULL);

    file = fopen (filename, "w");
    if (file != NULL) {
	fwrite (line, strlen (line), 1, file);
	fclose (file);
    }

    g_free (filename);
    g_free (line);
}

static void
flush_password (void)
{
    GtkWidget *password_entry;
    const char *password;

    password_entry = GTK_WIDGET (gtk_builder_get_object (builder, "password_entry"));

    if (g_object_get_data (G_OBJECT( password_entry), "user_edited")) {
	password = gtk_entry_get_text (GTK_ENTRY (password_entry));
	if (password != NULL && password[0] != 0) 
	    write_out_password (password);
    }
}


static void
update_ui (void)
{
    GConfClient *client;
    gboolean enabled, bluetooth_enabled, bluetooth_write_enabled, require_pairing_enabled;
    gboolean bluetooth_obexpush_enabled, bluetooth_obexpush_notify;
    char *str;
    PasswordSetting password_setting;
    AcceptSetting accept_setting;
    GtkWidget *check;
    GtkWidget *password_combo;
    GtkWidget *password_entry;
    GtkWidget *bluetooth_check;
    GtkWidget *allow_write_bluetooth_check;
    GtkWidget *require_pairing_check;
    GtkWidget *enable_obexpush_check;
    GtkWidget *accept_obexpush_combo;
    GtkWidget *notify_received_obexpush_check;

    client = gconf_client_get_default ();

    enabled = gconf_client_get_bool (client,
				     FILE_SHARING_ENABLED,
				     NULL);
    bluetooth_enabled = gconf_client_get_bool (client,
    					       FILE_SHARING_BLUETOOTH_ENABLED,
    					       NULL);
    bluetooth_write_enabled = gconf_client_get_bool (client,
						     FILE_SHARING_BLUETOOTH_ALLOW_WRITE,
						     NULL);
    require_pairing_enabled = gconf_client_get_bool (client,
    						     FILE_SHARING_BLUETOOTH_REQUIRE_PAIRING,
    						     NULL);
    bluetooth_obexpush_enabled = gconf_client_get_bool (client,
    							FILE_SHARING_BLUETOOTH_OBEXPUSH_ENABLED,
    							NULL);
    bluetooth_obexpush_notify = gconf_client_get_bool (client,
    						       FILE_SHARING_BLUETOOTH_OBEXPUSH_NOTIFY,
    						       NULL);

    str = gconf_client_get_string (client, FILE_SHARING_REQUIRE_PASSWORD, NULL);
    password_setting = password_setting_from_string (str);
    g_free (str);

    str = gconf_client_get_string (client, FILE_SHARING_BLUETOOTH_OBEXPUSH_ACCEPT_FILES, NULL);
    accept_setting = accept_setting_from_string (str);
    g_free (str);

    check = GTK_WIDGET (gtk_builder_get_object (builder, "enable_check"));
    password_combo = GTK_WIDGET (gtk_builder_get_object (builder, "password_combo"));
    password_entry = GTK_WIDGET (gtk_builder_get_object (builder, "password_entry"));
    bluetooth_check = GTK_WIDGET (gtk_builder_get_object (builder, "enable_bluetooth_check"));
    allow_write_bluetooth_check = GTK_WIDGET (gtk_builder_get_object (builder, "allow_write_bluetooth_check"));
    require_pairing_check = GTK_WIDGET (gtk_builder_get_object (builder, "require_pairing_check"));
    enable_obexpush_check = GTK_WIDGET (gtk_builder_get_object (builder, "enable_obexpush_check"));
    accept_obexpush_combo = GTK_WIDGET (gtk_builder_get_object (builder, "accept_obexpush_combo"));
    notify_received_obexpush_check = GTK_WIDGET (gtk_builder_get_object (builder, "notify_received_obexpush_check"));

    /* Network */
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), enabled);
    gtk_widget_set_sensitive (password_combo, enabled);
    gtk_widget_set_sensitive (password_entry, enabled && password_setting != PASSWORD_NEVER);

    gtk_combo_box_set_active (GTK_COMBO_BOX (password_combo),
			      password_setting);

    /* Bluetooth ObexFTP */
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bluetooth_check), bluetooth_enabled);
    gtk_widget_set_sensitive (allow_write_bluetooth_check, bluetooth_enabled);
    gtk_widget_set_sensitive (require_pairing_check, bluetooth_enabled);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (allow_write_bluetooth_check),
    				  bluetooth_write_enabled);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (require_pairing_check),
    				  require_pairing_enabled);

    /* Bluetooth ObexPush */
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (enable_obexpush_check), bluetooth_obexpush_enabled);
    gtk_widget_set_sensitive (accept_obexpush_combo, bluetooth_obexpush_enabled);
    gtk_widget_set_sensitive (notify_received_obexpush_check, bluetooth_obexpush_enabled);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (notify_received_obexpush_check),
    				  bluetooth_obexpush_notify);

    gtk_combo_box_set_active (GTK_COMBO_BOX (accept_obexpush_combo),
    			      accept_setting);

    g_object_unref (client);
}

static void
file_sharing_enabled_changed (GConfClient* client,
			      guint cnxn_id,
			      GConfEntry *entry,
			      gpointer data)
{
    update_ui ();
}

static void
password_required_changed (GConfClient* client,
			   guint cnxn_id,
			   GConfEntry *entry,
			   gpointer data)
{
    update_ui ();
}

static void
file_sharing_bluetooth_enabled_changed (GConfClient* client,
					guint cnxn_id,
					GConfEntry *entry,
					gpointer data)
{
	update_ui ();
}

static void
file_sharing_bluetooth_allow_write_changed (GConfClient* client,
					    guint cnxn_id,
					    GConfEntry *entry,
					    gpointer data)
{
	update_ui ();
}

static void
file_sharing_bluetooth_require_pairing_changed (GConfClient* client,
						guint cnxn_id,
						GConfEntry *entry,
						gpointer data)
{
	update_ui ();
}

static void
file_sharing_bluetooth_obexpush_enabled_changed (GConfClient* client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 gpointer data)
{
	update_ui ();
}

static void
file_sharing_bluetooth_obexpush_accept_files_changed (GConfClient* client,
						      guint cnxn_id,
						      GConfEntry *entry,
						      gpointer data)
{
	update_ui ();
}

static void
file_sharing_bluetooth_obexpush_notify_changed (GConfClient* client,
						guint cnxn_id,
						GConfEntry *entry,
						gpointer data)
{
	update_ui ();
}

static void
password_combo_changed (GtkComboBox *combo_box)
{
    GConfClient *client;
    guint setting;

    setting = gtk_combo_box_get_active (combo_box);

    client = gconf_client_get_default ();

    gconf_client_set_string (client,
			     FILE_SHARING_REQUIRE_PASSWORD,
			     password_string_from_setting (setting),
			     NULL);
    g_object_unref (client);
}

static void
launch_share (void)
{
	char *argv[2];
	int i;

	i = 0;
	argv[i++] = USER_SHARE_PROGRAM;
	argv[i++] = NULL;

	if (!g_spawn_async (NULL,
			    argv,
			    NULL,
			    0, /* G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL */
			    NULL,
			    NULL,
			    NULL,
			    NULL)) {
		g_warning ("Unable to start gnome-user-share program");
	}
}

static void
enable_bluetooth_check_toggled (GtkWidget *check)
{
	GConfClient *client;
	gboolean enabled;

	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));

	client = gconf_client_get_default ();

	gconf_client_set_bool (client,
			       FILE_SHARING_BLUETOOTH_ENABLED,
			       enabled,
			       NULL);

	g_object_unref (client);

	if (enabled != FALSE)
		launch_share ();
}

static void
enable_check_toggled (GtkWidget *check)
{
	GConfClient *client;
	gboolean enabled;

	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));

	client = gconf_client_get_default ();

	gconf_client_set_bool (client,
			       FILE_SHARING_ENABLED,
			       enabled,
			       NULL);

	g_object_unref (client);

	if (enabled != FALSE)
		launch_share ();
}

static void
password_entry_changed (GtkEditable *editable)
{
	g_object_set_data (G_OBJECT (editable),
			   "user_edited", GINT_TO_POINTER (1));
	flush_password ();
}

static void
bluetooth_allow_write_check_toggled (GtkWidget *check)
{
	GConfClient *client;
	gboolean enabled;

	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));

	client = gconf_client_get_default ();

	gconf_client_set_bool (client,
			       FILE_SHARING_BLUETOOTH_ALLOW_WRITE,
			       enabled,
			       NULL);

	g_object_unref (client);
}

static void
bluetooth_require_pairing_check_toggled (GtkWidget *check)
{
	GConfClient *client;
	gboolean enabled;

	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));

	client = gconf_client_get_default ();

	gconf_client_set_bool (client,
			       FILE_SHARING_BLUETOOTH_REQUIRE_PAIRING,
			       enabled,
			       NULL);

	g_object_unref (client);
}

static void
enable_obexpush_check_toggled (GtkWidget *check)
{
	GConfClient *client;
	gboolean enabled;

	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));

	client = gconf_client_get_default ();

	gconf_client_set_bool (client,
			       FILE_SHARING_BLUETOOTH_OBEXPUSH_ENABLED,
			       enabled,
			       NULL);

	g_object_unref (client);

	if (enabled != FALSE)
		launch_share ();
}

static void
accept_obexpush_combo_changed (GtkComboBox *combo_box)
{
    GConfClient *client;
    guint setting;

    setting = gtk_combo_box_get_active (combo_box);

    client = gconf_client_get_default ();

    gconf_client_set_string (client,
			     FILE_SHARING_BLUETOOTH_OBEXPUSH_ACCEPT_FILES,
			     accept_string_from_setting (setting),
			     NULL);
    g_object_unref (client);
}

static void
notify_received_obexpush_check_toggled (GtkWidget *check)
{
	GConfClient *client;
	gboolean enabled;

	enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));

	client = gconf_client_get_default ();

	gconf_client_set_bool (client,
			       FILE_SHARING_BLUETOOTH_OBEXPUSH_NOTIFY,
			       enabled,
			       NULL);

	g_object_unref (client);
}

static GtkWidget *
error_dialog (const char *title,
	      const char *reason,
	      GtkWindow *parent)
{
	GtkWidget *error_dialog;

	if (reason == NULL)
		reason = _("No reason");

	error_dialog =
		gtk_message_dialog_new (parent,
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_OK,
					"%s", title);
	gtk_message_dialog_format_secondary_text
		(GTK_MESSAGE_DIALOG (error_dialog), "%s", reason);

	gtk_window_set_title (GTK_WINDOW (error_dialog), ""); /* as per HIG */
	gtk_container_set_border_width (GTK_CONTAINER (error_dialog), 5); 
	gtk_dialog_set_default_response (GTK_DIALOG (error_dialog),
					 GTK_RESPONSE_OK);
	gtk_window_set_modal (GTK_WINDOW (error_dialog), TRUE);

	return error_dialog;
}

static void
help_button_clicked (GtkButton *button, GtkWidget *window)
{
	GError *error = NULL;

	if (gtk_show_uri (gtk_widget_get_screen (window), "ghelp:gnome-user-share", gtk_get_current_event_time (), &error) == FALSE) {
		GtkWidget *dialog;

		dialog = error_dialog (_("Could not display the help contents."), error->message, GTK_WINDOW (window));
		g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK
				  (gtk_widget_destroy), error_dialog);
		gtk_window_present (GTK_WINDOW (dialog));

		g_error_free (error);
	}
}

int
main (int argc, char *argv[])
{
    GError *error = NULL;
    GConfClient *client;
    GtkWidget *check;
    GtkWidget *password_combo;
    GtkWidget *password_entry;
    GtkWidget *bluetooth_check;
    GtkWidget *bluetooth_allow_write_check;
    GtkWidget *require_pairing_check;
    GtkWidget *enable_obexpush_check;
    GtkWidget *accept_obexpush_combo;
    GtkWidget *notify_received_obexpush_check;
    GtkWidget *window;
    GtkListStore *store;
    GtkCellRenderer *cell;
    GtkTreeIter iter;
    
    bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    
    gtk_init (&argc, &argv);

    builder = gtk_builder_new ();
    gtk_builder_add_from_file (builder, DATADIR"file-share-properties.ui", &error);

    if (error) {
      GtkWidget *dialog;

      dialog = error_dialog (_("Could not build interface."), error->message, NULL);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      g_error_free (error);
      return 1;
    }

    window = GTK_WIDGET (gtk_builder_get_object (builder, "user_share_dialog"));
    g_signal_connect (G_OBJECT (window), "delete_event",
		      G_CALLBACK (gtk_main_quit), NULL);

    client = gconf_client_get_default ();
    gconf_client_add_dir (client,
			  FILE_SHARING_DIR,
			  GCONF_CLIENT_PRELOAD_RECURSIVE,
			  NULL);

    check = GTK_WIDGET (gtk_builder_get_object (builder, "enable_check"));
    password_combo = GTK_WIDGET (gtk_builder_get_object (builder, "password_combo"));
    password_entry = GTK_WIDGET (gtk_builder_get_object (builder, "password_entry"));
    bluetooth_check = GTK_WIDGET (gtk_builder_get_object (builder, "enable_bluetooth_check"));
    bluetooth_allow_write_check = GTK_WIDGET (gtk_builder_get_object (builder, "allow_write_bluetooth_check"));
    require_pairing_check = GTK_WIDGET (gtk_builder_get_object (builder, "require_pairing_check"));
    enable_obexpush_check = GTK_WIDGET (gtk_builder_get_object (builder, "enable_obexpush_check"));
    accept_obexpush_combo = GTK_WIDGET (gtk_builder_get_object (builder, "accept_obexpush_combo"));
    notify_received_obexpush_check = GTK_WIDGET (gtk_builder_get_object (builder, "notify_received_obexpush_check"));

    store = gtk_list_store_new (1, G_TYPE_STRING);
    gtk_combo_box_set_model (GTK_COMBO_BOX (password_combo),
			     GTK_TREE_MODEL (store));
    cell = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (password_combo), cell, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (password_combo), cell,
				    "text", 0,
				    NULL);

    /* Keep in same order as enum */
    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter, 0,
			_("Never"), -1);
    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter, 0,
			_("When writing files"), -1);
    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter, 0,
			_("Always"), -1);
    g_object_unref (store);

    /* We can't read the password from the text, just set it to something */
    gtk_entry_set_text (GTK_ENTRY (password_entry), "none");
    g_object_set_data (G_OBJECT (password_entry),
		       "user_edited", GINT_TO_POINTER (0));
    g_signal_connect (password_entry,
		      "changed", G_CALLBACK (password_entry_changed), NULL);

    /* Accept files combo */
    store = gtk_list_store_new (1, G_TYPE_STRING);
    gtk_combo_box_set_model (GTK_COMBO_BOX (accept_obexpush_combo),
			     GTK_TREE_MODEL (store));
    cell = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (accept_obexpush_combo), cell, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (accept_obexpush_combo), cell,
				    "text", 0,
				    NULL);

    /* Keep in same order as enum */
    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter, 0,
			_("Always"), -1);
    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter, 0,
			_("Only for Bonded devices"), -1);
    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter, 0,
			_("Only for Bonded and Trusted devices"), -1);
    //FIXME implement
#if 0
    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter, 0,
    			_("Ask"), -1);
#endif
    g_object_unref (store);

    update_ui ();

    g_signal_connect (check,
		      "toggled", G_CALLBACK (enable_check_toggled), NULL);
    g_signal_connect (password_combo,
		      "changed", G_CALLBACK (password_combo_changed), NULL);
    g_signal_connect (bluetooth_check,
    		      "toggled", G_CALLBACK (enable_bluetooth_check_toggled), NULL);
    g_signal_connect (bluetooth_allow_write_check,
    		      "toggled", G_CALLBACK (bluetooth_allow_write_check_toggled), NULL);
    g_signal_connect (require_pairing_check,
    		      "toggled", G_CALLBACK (bluetooth_require_pairing_check_toggled), NULL);
    g_signal_connect (enable_obexpush_check,
    		      "toggled", G_CALLBACK (enable_obexpush_check_toggled), NULL);
    g_signal_connect (accept_obexpush_combo,
    		      "changed", G_CALLBACK (accept_obexpush_combo_changed), NULL);
    g_signal_connect (notify_received_obexpush_check,
    		      "toggled", G_CALLBACK (notify_received_obexpush_check_toggled), NULL);

    g_signal_connect (GTK_WIDGET (gtk_builder_get_object (builder, "close_button")),
		      "clicked", G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect (GTK_WIDGET (gtk_builder_get_object (builder, "help_button")),
		      "clicked", G_CALLBACK (help_button_clicked),
		      gtk_builder_get_object (builder, "user_share_dialog"));

    gconf_client_notify_add (client,
			     FILE_SHARING_ENABLED,
			     file_sharing_enabled_changed,
			     NULL,
			     NULL,
			     NULL);
    gconf_client_notify_add (client,
			     FILE_SHARING_REQUIRE_PASSWORD,
			     password_required_changed,
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

    gtk_widget_show (GTK_WIDGET (gtk_builder_get_object (builder, "user_share_dialog")));
    
    gtk_main ();

    return 0;
}
