/* kgx-terminal.c
 *
 * Copyright 2019 Zander Brown
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:kgx-terminal
 * @title: KgxTerminal
 * @short_description: The terminal
 * 
 * The main terminal widget with various features added such as a context
 * menu (via #GtkPopover) and link detection
 */

#include <glib/gi18n.h>
#include <vte/vte.h>
#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>

#include "rgba.h"

#include "kgx-config.h"
#include "kgx-terminal.h"

/*       Regex adapted from TerminalWidget.vala in Pantheon Terminal       */

#define USERCHARS "-[:alnum:]"
#define USERCHARS_CLASS "[" USERCHARS "]"
#define PASSCHARS_CLASS "[-[:alnum:]\\Q,?;.:/!%$^*&~\"#'\\E]"
#define HOSTCHARS_CLASS "[-[:alnum:]]"
#define HOST HOSTCHARS_CLASS "+(\\." HOSTCHARS_CLASS "+)*"
#define PORT "(?:\\:[[:digit:]]{1,5})?"
#define PATHCHARS_CLASS "[-[:alnum:]\\Q_$.+!*,;:@&=?/~#%\\E]"
#define PATHTERM_CLASS "[^\\Q]'.}>) \t\r\n,\"\\E]"
#define SCHEME "(?:news:|telnet:|nntp:|file:\\/|https?:|ftps?:|sftp:|webcal:\n" \
               "|irc:|sftp:|ldaps?:|nfs:|smb:|rsync:|"                          \
               "ssh:|rlogin:|telnet:|git:\n"                                    \
               "|git\\+ssh:|bzr:|bzr\\+ssh:|svn:|svn\\"                         \
               "+ssh:|hg:|mailto:|magnet:)"
#define USERPASS USERCHARS_CLASS "+(?:" PASSCHARS_CLASS "+)?"
#define URLPATH "(?:(/" PATHCHARS_CLASS "+(?:[(]" PATHCHARS_CLASS "*[)])*" PATHCHARS_CLASS "*)*" PATHTERM_CLASS ")?"

static const gchar* links[KGX_TERMINAL_N_LINK_REGEX] = {
  SCHEME "//(?:" USERPASS "\\@)?" HOST PORT URLPATH,
  "(?:www|ftp)" HOSTCHARS_CLASS "*\\." HOST PORT URLPATH,
  "(?:callto:|h323:|sip:)" USERCHARS_CLASS "[" USERCHARS ".]*(?:" PORT "/[a-z0-9]+)?\\@" HOST,
  "(?:mailto:)?" USERCHARS_CLASS "[" USERCHARS ".]*\\@" HOSTCHARS_CLASS "+\\." HOST,
  "(?:news:|man:|info:)[[:alnum:]\\Q^_{|}~!\"#$%&'()*+,./;:=?`\\E]+"
};

/*       Regex adapted from TerminalWidget.vala in Pantheon Terminal       */

G_DEFINE_TYPE (KgxTerminal, kgx_terminal, VTE_TYPE_TERMINAL)

enum {
  PROP_0,
  PROP_THEME,
  PROP_OPAQUE,
  LAST_PROP
};

static GParamSpec *pspecs[LAST_PROP] = { NULL, };


static void
kgx_terminal_set_theme (KgxTerminal *self,
                        KgxTheme     theme,
                        gboolean     opaque)
{
  GdkRGBA fg;
  GdkRGBA bg = (GdkRGBA) { 0.1, 0.1, 0.1, 0.96};

  // Workings of GDK_RGBA prevent this being static
  GdkRGBA palette[16] = {
    GDK_RGBA ("241f31"), // Black
    GDK_RGBA ("c01c28"), // Red
    GDK_RGBA ("2ec27e"), // Green
    GDK_RGBA ("f5c211"), // Yellow
    GDK_RGBA ("1e78e4"), // Blue
    GDK_RGBA ("9841bb"), // Magenta
    GDK_RGBA ("0ab9dc"), // Cyan
    GDK_RGBA ("c0bfbc"), // White
    GDK_RGBA ("5e5c64"), // Bright Black
    GDK_RGBA ("ed333b"), // Bright Red
    GDK_RGBA ("57e389"), // Bright Green
    GDK_RGBA ("f8e45c"), // Bright Yellow
    GDK_RGBA ("51a1ff"), // Bright Blue
    GDK_RGBA ("c061cb"), // Bright Magenta
    GDK_RGBA ("4fd2fd"), // Bright Cyan
    GDK_RGBA ("f6f5f4"), // Bright White
  };

  if (self->theme == theme && self->opaque == opaque) {
    return;
  }

  if (opaque) {
    bg.alpha = 1.0;
  }

  switch (theme) {
    case KGX_THEME_HACKER:
      fg = (GdkRGBA) { 0.1, 1.0, 0.1, 1.0};
      break;
    case KGX_THEME_NIGHT:
    default:
      fg = (GdkRGBA) { 1.0, 1.0, 1.0, 1.0};
      break;
  }

  vte_terminal_set_colors (VTE_TERMINAL (self), &fg, &bg, palette, 16);

  if (self->theme != theme) {
    self->theme = theme;
    g_object_notify_by_pspec (G_OBJECT (self), pspecs[PROP_THEME]);
  }
  
  if (self->opaque != opaque) {
    self->opaque = opaque;
    g_object_notify_by_pspec (G_OBJECT (self), pspecs[PROP_OPAQUE]);
  }
}

static void
kgx_terminal_set_property (GObject      *object,
                           guint         property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  KgxTerminal *self = KGX_TERMINAL (object);

  switch (property_id) {
    case PROP_THEME:
      kgx_terminal_set_theme (self, g_value_get_enum (value), self->opaque);
      break;
    case PROP_OPAQUE:
      kgx_terminal_set_theme (self, self->theme, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
kgx_terminal_get_property (GObject    *object,
                           guint       property_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  KgxTerminal *self = KGX_TERMINAL (object);

  switch (property_id) {
    case PROP_THEME:
      g_value_set_enum (value, self->theme);
      break;
    case PROP_OPAQUE:
      g_value_set_boolean (value, self->opaque);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
context_menu (GtkWidget *widget,
              int        x,
              int        y,
              GdkEvent  *event)
{
  KgxTerminal    *self = KGX_TERMINAL (widget);
  GAction        *act;
  GtkWidget      *menu;
  GtkApplication *app;
  GMenu          *model;
  GdkRectangle    rect = {x, y, 1, 1};
  gboolean        value;
  const char     *match;
  int             match_id;

  match = vte_terminal_match_check_event (VTE_TERMINAL (self),
                                          event,
                                          &match_id);

  self->current_url = NULL;
  for (int i = 0; i < KGX_TERMINAL_N_LINK_REGEX; i++) {
    if (self->match_id[i] == match_id) {
      self->current_url = match;
      break;
    }
  }
  value = self->current_url != NULL;

  act = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "open-link");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), value);
  act = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "copy-link");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), value);

  app = GTK_APPLICATION (g_application_get_default ());
  model = gtk_application_get_menu_by_id (app, "context-menu");

  menu = gtk_popover_new_from_model (widget, G_MENU_MODEL (model));
  gtk_popover_set_pointing_to (GTK_POPOVER (menu), &rect);
  gtk_popover_popup (GTK_POPOVER (menu));
}

static gboolean
kgx_terminal_popup_menu (GtkWidget *self)
{
  context_menu (self, 1, 1, NULL);
  return TRUE;
}

static gboolean
kgx_terminal_button_press_event (GtkWidget *self, GdkEventButton *event)
{
  if (gdk_event_triggers_context_menu ((GdkEvent *) event) &&
      event->type == GDK_BUTTON_PRESS)
    {
      context_menu (self, event->x, event->y, (GdkEvent *) event);
      return TRUE;
    }

  return GTK_WIDGET_CLASS (kgx_terminal_parent_class)->button_press_event (self, event);
}

static void
kgx_terminal_class_init (KgxTerminalClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = kgx_terminal_set_property;
  object_class->get_property = kgx_terminal_get_property;

  widget_class->popup_menu = kgx_terminal_popup_menu;
  widget_class->button_press_event = kgx_terminal_button_press_event;

  /**
   * KgxTerminal:theme:
   * 
   * The palette to use, one of the values of #KgxTheme
   * 
   * Officially only "night" exists, "hacker" is just a little fun
   * 
   * Bound to #KgxApplication:theme on the #KgxApplication
   * 
   * Stability: Private
   */
  pspecs[PROP_THEME] =
    g_param_spec_enum ("theme", "Theme", "Terminal theme",
                       KGX_TYPE_THEME, KGX_THEME_NIGHT,
                       G_PARAM_READWRITE);

  /**
   * KgxTerminal:opaque:
   * 
   * Whether to disable transparency
   * 
   * Bound to #GtkWindow:is-maximized on the #KgxWindow
   * 
   * Stability: Private
   */
  pspecs[PROP_OPAQUE] =
    g_param_spec_boolean ("opaque", "Opaque", "Terminal opaqueness",
                          FALSE,
                          G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, pspecs);
}

static void
open_link_activated (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       data)
{
  GError *error = NULL;
  KgxTerminal *self = KGX_TERMINAL (data);
  guint32 timestamp;

  timestamp = GDK_CURRENT_TIME;

  gtk_show_uri_on_window (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))),
                          self->current_url,
                          timestamp,
                          &error);

  if (error) {
    g_warning ("Failed to open link %s", error->message);
  }
}

static void
copy_link_activated (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       data)
{
  GtkClipboard *cb;
  KgxTerminal *self = KGX_TERMINAL (data);

  cb = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  gtk_clipboard_set_text (cb, self->current_url, -1);
}

static void
copy_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       data)
{
  vte_terminal_copy_clipboard_format (VTE_TERMINAL (data), VTE_FORMAT_TEXT);
}

struct Paste {
  VteTerminal *dest;
  char        *text;
};

static void
paste_response (GtkDialog    *dlg,
                int           response,
                struct Paste *paste)
{
  if (dlg && GTK_IS_DIALOG (dlg)) {
    gtk_widget_destroy (GTK_WIDGET (dlg));
  }

  if (response == GTK_RESPONSE_ACCEPT) {
    vte_terminal_feed_child (paste->dest, paste->text, -1);
  }

  g_free (paste->text);
  g_free (paste);
}

static void
got_text (GtkClipboard *clipboard,
          const gchar  *text,
          gpointer      data)
{
  g_autofree char *striped = g_strchug (g_strdup (text));
  struct Paste    *paste = g_new (struct Paste, 1);

  paste->dest = VTE_TERMINAL (data);
  paste->text = g_strdup (text);

  if (g_strstr_len (striped, -1, "sudo") != NULL &&
      g_strstr_len (striped, -1, "\n") != NULL) {
    GtkWidget *accept = NULL;
    GtkWidget *dlg = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (data))),
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_QUESTION,
                                             GTK_BUTTONS_NONE,
                                             _("You are pasting a command that runs as an administrator"));
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dlg),
                                              // TRANSLATORS: %s is the command being pasted
                                              _("Make sure you know what the command does:\n%s"),
                                              text);

    g_signal_connect (dlg, "response", G_CALLBACK (paste_response), paste);
    gtk_dialog_add_button (GTK_DIALOG (dlg),
                           _("_Cancel"),
                           GTK_RESPONSE_DELETE_EVENT);
    accept = gtk_dialog_add_button (GTK_DIALOG (dlg),
                                    _("_Paste"),
                                    GTK_RESPONSE_ACCEPT);
    gtk_style_context_add_class (gtk_widget_get_style_context (accept),
                                 GTK_STYLE_CLASS_DESTRUCTIVE_ACTION);
    gtk_widget_show (dlg);
  } else {
    paste_response (NULL, GTK_RESPONSE_ACCEPT, paste);
  }
}

static void
paste_activated (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       data)
{
  GtkClipboard *cb;

  cb = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

  gtk_clipboard_request_text (cb, got_text, data);
}

static void
select_all_activated (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       data)
{
  vte_terminal_select_all (VTE_TERMINAL (data));
}

static void
show_in_files_activated (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       data)
{
  GDBusProxy      *proxy;
  GVariant        *retval;
  GVariantBuilder *builder;
  GError          *error = NULL;
  KgxTerminal     *term = KGX_TERMINAL (data);
  const gchar     *uri = NULL;
  const gchar     *method;

  uri = vte_terminal_get_current_file_uri (VTE_TERMINAL (term));
  method = "ShowItems";

  if (uri == NULL) {
    uri = vte_terminal_get_current_directory_uri (VTE_TERMINAL (term));
    method = "ShowFolders";
  }

  if (uri == NULL) {
    g_warning ("win.show-in-files: no file");
    return;
  }

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.FileManager1",
                                         "/org/freedesktop/FileManager1",
                                         "org.freedesktop.FileManager1",
                                         NULL, &error);

  if (!proxy) {
    g_warning ("win.show-in-files: D-Bus connect failed %s", error->message);
    return;
  }

  builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
  g_variant_builder_add (builder, "s", uri);

  retval = g_dbus_proxy_call_sync (proxy,
                                   method,
                                   g_variant_new ("(ass)",
                                                  builder,
                                                  ""),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1, NULL, &error);

  g_variant_builder_unref (builder);
  g_object_unref (proxy);

  if (!retval) {
    g_warning ("win.show-in-files: D-Bus call failed %s", error->message);
    return;
  }

  g_variant_unref (retval);
}

static GActionEntry term_entries[] =
{
  { "open-link", open_link_activated, NULL, NULL, NULL },
  { "copy-link", copy_link_activated, NULL, NULL, NULL },
  { "copy", copy_activated, NULL, NULL, NULL },
  { "paste", paste_activated, NULL, NULL, NULL },
  { "select-all", select_all_activated, NULL, NULL, NULL },
  { "show-in-files", show_in_files_activated, NULL, NULL, NULL },
};

static void
long_pressed (GtkGestureLongPress *gesture,
              gdouble              x,
              gdouble              y,
              KgxTerminal         *self)
{
  context_menu (GTK_WIDGET (self), (int) x, (int) y, NULL);
}

static void
selection_changed (KgxTerminal *self)
{
  GAction *act;

  act = g_action_map_lookup_action (self->actions, "copy");

  g_simple_action_set_enabled (G_SIMPLE_ACTION (act),
                               vte_terminal_get_has_selection (VTE_TERMINAL (self)));
}

static void
location_changed (KgxTerminal *self)
{
  GAction *act;
  gboolean value;

  act = g_action_map_lookup_action (self->actions, "show-in-files");

  value = vte_terminal_get_current_file_uri (VTE_TERMINAL (self)) ||
            vte_terminal_get_current_directory_uri (VTE_TERMINAL (self));

  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), value);
}

static void
kgx_terminal_init (KgxTerminal *self)
{
  GAction *act;
  GtkGesture *gesture;

  self->theme = KGX_THEME_NIGHT;
  self->opaque = FALSE;

  self->actions = G_ACTION_MAP (g_simple_action_group_new ());
  g_action_map_add_action_entries (self->actions,
                                   term_entries,
                                   G_N_ELEMENTS (term_entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "term",
                                  G_ACTION_GROUP (self->actions));

  gesture = gtk_gesture_long_press_new (GTK_WIDGET (self));
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (gesture), TRUE);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture),
                                              GTK_PHASE_TARGET);
  g_signal_connect (gesture, "pressed", G_CALLBACK (long_pressed), self);

  act = g_action_map_lookup_action (self->actions, "open-link");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);
  act = g_action_map_lookup_action (self->actions, "copy-link");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);
  act = g_action_map_lookup_action (self->actions, "copy");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);
  act = g_action_map_lookup_action (self->actions, "show-in-files");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);

  vte_terminal_search_set_wrap_around (VTE_TERMINAL (self), TRUE);

  g_signal_connect (self, "selection-changed",
                    G_CALLBACK (selection_changed), NULL);
  g_signal_connect (self, "current-directory-uri-changed",
                    G_CALLBACK (location_changed), NULL);
  g_signal_connect (self, "current-file-uri-changed",
                    G_CALLBACK (location_changed), NULL);

  for (int i = 0; i < KGX_TERMINAL_N_LINK_REGEX; i++) {
    g_autoptr (VteRegex) regex = NULL;
    g_autoptr (GError) error = NULL;

    regex = vte_regex_new_for_match (links[i], -1, PCRE2_MULTILINE, &error);

    if (error) {
      g_warning ("link regex failed: %s", error->message);
      continue;
    }

    self->match_id[i] = vte_terminal_match_add_regex (VTE_TERMINAL (self),
                                                      regex,
                                                      0);

    vte_terminal_match_set_cursor_name (VTE_TERMINAL (self),
                                        self->match_id[i],
                                        "pointer");
  }
}
