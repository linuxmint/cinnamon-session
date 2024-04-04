#!/usr/bin/python3

import signal
import argparse
import gettext
import setproctitle
import os
import sys
import locale
from enum import IntEnum

import config

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('XApp', '1.0')
from gi.repository import GLib, Gio, Gtk, Gdk, XApp

signal.signal(signal.SIGINT, signal.SIG_DFL)

# i18n
locale.bindtextdomain(config.PACKAGE, config.LOCALE_DIR)
gettext.bindtextdomain(config.PACKAGE, config.LOCALE_DIR)
gettext.textdomain(config.PACKAGE)
_ = gettext.gettext

setproctitle.setproctitle("cinnamon-session-quit")

# Make Action values match their ResponseCode counterparts, so we can use
# them as no-prompt default responses.
class Action(IntEnum):
    LOGOUT = 6
    SHUTDOWN = 7
    RESTART = 3

class ResponseCode(IntEnum):
    SUSPEND = 1
    HIBERNATE = 2
    RESTART = 3
    SWITCH_USER = 4
    CANCEL = 5
    LOGOUT = 6
    SHUTDOWN = 7
    CONTINUE = 8
    NONE = 9

class LogoutParams(IntEnum):
    NORMAL = 0,
    NO_PROMPT = 1,
    FORCE = 2

class QuitDialog:
    def __init__(self):
        parser = argparse.ArgumentParser(description='cinnamon-session-quit')
        group = parser.add_mutually_exclusive_group(required=True)
        group.add_argument("--logout", dest="mode", action='store_const', const=Action.LOGOUT,
                            help=_("Log out"))
        group.add_argument("--power-off", dest="mode", action='store_const', const=Action.SHUTDOWN,
                            help=_("Power off"))
        group.add_argument("--reboot", dest="mode", action='store_const', const=Action.RESTART,
                            help="Log out")
        parser.add_argument("--force", dest="force", action="store_true",
                            help=_("Ignoring any existing inhibitors"))
        parser.add_argument("--no-prompt", dest="no_prompt", action='store_true',
                            help=_("Don't prompt for user confirmation"))
        parser.add_argument("--sm-owned", action="store_true", help=argparse.SUPPRESS)
        parser.add_argument("--sm-bus-id", dest="bus_id", action="store", help=argparse.SUPPRESS, default=config.DBUS_ADDRESS)
        args = parser.parse_args()

        self.dialog_response = ResponseCode.NONE
        self.inhibited = False

        self.mode = args.mode
        self.default_response = ResponseCode(int(self.mode))

        self.force = args.force
        self.no_prompt = args.no_prompt
        self.sm_owned = args.sm_owned

        if self.sm_owned:
            self.bus_id = args.bus_id
        else:
            self.bus_id = None

        self.proxy = None
        self.signal_handler_id = 0

        self.delay_duration = 0
        self.current_time = 0
        self.timer_id = 0

        GLib.unix_signal_add(GLib.PRIORITY_HIGH, signal.SIGTERM, self.on_terminate, None)

        if not self.sm_owned:
            # Launched by some other process or the user,
            # we kick it to the session-manager and exit.
            self.forward_to_sm()
        else:
            # The session manager launched us, so we show our dialog.
            self.interact()

    def forward_to_sm(self):
        sm_proxy = None

        try:
            sm_proxy = Gio.DBusProxy.new_for_bus_sync(
                Gio.BusType.SESSION,
                Gio.DBusProxyFlags.DO_NOT_AUTO_START,
                None,
                "org.gnome.SessionManager",
                "/org/gnome/SessionManager",
                "org.gnome.SessionManager",
                None
            )

            def async_cb(proxy, res):
                try:
                    proxy.call_finish(res)
                    # self.quit()
                except GLib.Error as e:
                    print("An error occurred forwarding to the session manager: %s" % e.message)

            if self.mode == Action.LOGOUT:
                arg = LogoutParams.NORMAL

                if self.no_prompt:
                    arg |= LogoutParams.NO_PROMPT
                if self.force:
                    arg |= LogoutParams.FORCE

                sm_proxy.call(
                    "Logout",
                    GLib.Variant("(u)", [arg]),
                    Gio.DBusCallFlags.NO_AUTO_START,
                    -1,
                    None,
                    async_cb
                )
            elif self.mode == Action.SHUTDOWN:
                sm_proxy.call(
                    "Shutdown",
                    None,
                    Gio.DBusCallFlags.NO_AUTO_START,
                    -1,
                    None,
                    async_cb
                )
            elif self.mode == Action.RESTART:
                sm_proxy.call(
                    "Reboot",
                    None,
                    Gio.DBusCallFlags.NO_AUTO_START,
                    -1,
                    None,
                    async_cb
                )
        except GLib.Error as e:
            if sm_proxy is None:
                print("Could not forward to org.cinnamon.SessionManager.Manager: %s" % e.message)
                sys.exit(1)

        sys.exit(0)

    def interact(self):
        self.setup_proxy()
        self.run_dialog()

    def setup_proxy(self):
        connection = None

        try:
            connection = Gio.DBusConnection.new_for_address_sync(
                self.bus_id,
                Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT,
                None, None
            )

            self.proxy = Gio.DBusProxy.new_sync(
                connection,
                Gio.DBusProxyFlags.DO_NOT_AUTO_START,
                None,
                None,
                "/org/gnome/SessionManager",
                "org.cinnamon.SessionManager.DialogPrivate",
                None
            )

            self.proxy.connect("g-signal", self.inhibitor_info_received)

        except GLib.Error as e:
            if connection is None:
                print("Could not connect to session dialog server: %s" % e.message)
                sys.exit(1)
            if self.proxy is None:
                print("Could not create proxy to session dialog interface: %s" % e.message)
                sys.exit(1)

    def inhibitor_info_received(self, proxy, sender, signal, params):
        inhibitors = params[0]

        if self.dialog_response == ResponseCode.NONE:
            print("Ignoring inhibitor info, still waiting on initial response from user")
            return

        print("Inhibitor info received (%d inhibitors)" % len(inhibitors))

        if inhibitors:
            self.inhibited = True
            self.show_inhibit_view(inhibitors)
        else:
            if self.inhibited:
                self.handle_response(self.window, ResponseCode.CONTINUE)
                self.finish_up()

    def run_dialog(self):
        self.builder = Gtk.Builder.new_from_file(os.path.join(config.PKG_DATADIR, "cinnamon-session-quit.glade"))
        self.window = self.builder.get_object("window")
        self.window.connect("delete-event", lambda w, e: self.quit(0))

        self.still_running_display = self.builder.get_object("still_running_display")
        self.progress_bar = self.builder.get_object("progress_bar")
        self.dialog_label = self.builder.get_object("dialog_label")
        self.action_icon = self.builder.get_object("action_icon")

        self.button_suspend = self.builder.get_object("button_suspend")
        self.button_hibernate = self.builder.get_object("button_hibernate")
        self.button_restart = self.builder.get_object("button_restart")
        self.button_switchuser = self.builder.get_object("button_switchuser")
        self.button_cancel = self.builder.get_object("button_cancel")
        self.button_logout = self.builder.get_object("button_logout")
        self.button_shutdown = self.builder.get_object("button_shutdown")
        self.button_continue = self.builder.get_object("button_continue")
        self.dynamic_buttons = XApp.VisibilityGroup.new(False, True, [
            self.button_suspend,
            self.button_hibernate,
            self.button_restart,
            self.button_switchuser,
            self.button_logout,
            self.button_shutdown,
            self.button_continue
        ])

        self.view_stack = self.builder.get_object("view_stack")
        self.inhibitor_treeview = self.builder.get_object("inhibitor_treeview")

        can_switch_user, can_stop, can_restart, can_hybrid_sleep, can_suspend, can_hibernate, can_logout = self.get_session_capabilities()

        default_button = None

        if self.mode == Action.LOGOUT:
            self.dialog_label.set_text(_("Log out of this system now?"))
            self.button_switchuser.set_visible(can_switch_user)
            self.button_logout.set_visible(True)
            default_button = self.button_logout
            self.action_icon.set_from_icon_name("system-log-out-symbolic", Gtk.IconSize.DIALOG)
            self.window.set_icon_name("system-log-out")
        elif self.mode == Action.SHUTDOWN:
            self.dialog_label.set_text(_("Shut down this system now?"))
            self.button_suspend.set_visible(can_suspend)
            self.button_hibernate.set_visible(can_hibernate)
            self.button_restart.set_visible(can_restart)
            self.button_shutdown.set_visible(can_stop)
            default_button = self.button_shutdown
            self.action_icon.set_from_icon_name("system-shutdown-symbolic", Gtk.IconSize.DIALOG)
            self.window.set_icon_name("system-shutdown")
        elif self.mode == Action.RESTART:
            if not can_restart:
                print("Restart not available")
                Gtk.main_quit()
                return
            self.dialog_label.set_text(_("Restart this system now?"))
            self.button_restart.set_visible(True)
            default_button = self.button_restart
            self.action_icon.set_from_icon_name("system-reboot-symbolic", Gtk.IconSize.DIALOG)
            self.window.set_icon_name("system-reboot")

        default_button.get_style_context().add_class("destructive-action")

        session_settings = Gio.Settings(schema_id="org.cinnamon.SessionManager")
        self.delay_duration = session_settings.get_int("quit-time-delay")

        if session_settings.get_boolean("quit-delay-toggle"):
            self.progress_bar.show()
            self.progress_bar.set_fraction(1.0)
            self.start_timer()
        else:
            self.progress_bar.hide()

        self.window.set_default_response(self.default_response)
        self.window.connect("response", self.handle_response)

        self.window.set_keep_above(True)
        self.window.stick()
        self.window.present_with_time(0)

    def get_session_capabilities(self):
        try:
            caps = self.proxy.call_sync(
                "GetCapabilities",
                None,
                Gio.DBusCallFlags.NO_AUTO_START,
                -1,
                None
            )
            return caps[0]
        except GLib.Error as e:
            print("Could not retrieve session capabilities: %s" % e.message)

    def start_timer(self):
        if self.timer_id > 0:
            GLib.source_remove(self.timer_id)

        self.current_time = self.delay_duration

        self.update_timer()
        GLib.timeout_add(1000, self.update_timer)

    def update_timer(self):
        if self.current_time == 0:
            self.handle_response(self.window, self.default_response)
            return

        if self.mode == Action.LOGOUT:
            seconds_warning = gettext.ngettext ("You will be automatically logged "
                                                "out in %d second.",
                                                "You will be logged "
                                                "out in %d seconds.",
                                                self.current_time) % self.current_time
        elif self.mode == Action.SHUTDOWN:
            seconds_warning = gettext.ngettext ("This system will be automatically "
                                                "shut down in %d second.",
                                                "This system will be "
                                                "shut down in %d seconds.",
                                                self.current_time) % self.current_time
        elif self.mode == Action.RESTART:
            seconds_warning = gettext.ngettext ("This system will be automatically "
                                                "restarted in %d second.",
                                                "This system will be "
                                                "restarted in %d seconds.",
                                                self.current_time) % self.current_time

        self.progress_bar.set_text(seconds_warning)
        self.progress_bar.set_fraction(self.current_time / self.delay_duration)

        self.current_time -= 1

        return GLib.SOURCE_CONTINUE

    def handle_response(self, dialog, code):
        self.view_stack.set_visible_child_name("busy")

        if self.inhibited:
            if code == ResponseCode.CONTINUE:
                print("Sending ignore inhibitors")
                self.send_command("IgnoreInhibitors")
                self.finish_up()
            elif code in (ResponseCode.CANCEL, Gtk.ResponseType.NONE, Gtk.ResponseType.DELETE_EVENT):
                print("Canceling action during inhibit phase")
                self.send_command("Cancel")
                self.quit()
            return

        self.dialog_response = code

        if code == ResponseCode.SUSPEND:
            self.send_command("Suspend")
        elif code == ResponseCode.RESTART:
            self.send_command("Restart")
        elif code == ResponseCode.HIBERNATE:
            self.send_command("Hibernate")
        elif code == ResponseCode.SWITCH_USER:
            self.send_command("SwitchUser")
        elif code == ResponseCode.LOGOUT:
            self.send_command("Logout")
        elif code == ResponseCode.SHUTDOWN:
            self.send_command("Shutdown")
        elif code in (ResponseCode.CANCEL, Gtk.ResponseType.NONE, Gtk.ResponseType.DELETE_EVENT):
            self.send_command("Cancel")
            self.quit(0)
        else:
            print("Invalid response code: %d" % code)

    def send_command(self, command):
        try:
            self.proxy.call(
                command,
                None,
                Gio.DBusCallFlags.NO_AUTO_START,
                -1,
                None,
                None
            )
        except GLib.Error as e:
            print("Could not send command '%s' to session manager: %s" % (str(command), e.message))

        self.command_sent = True
        # wait for inhibit info

    def show_inhibit_view(self, info):
        self.inhibit_model = Gtk.ListStore(str, str, str)
        self.inhibitor_treeview.set_model(self.inhibit_model)

        for inhibitor in info:
            self.inhibit_model.insert_with_values(-1, [0, 1], [inhibitor[0], inhibitor[2]])

        self.dynamic_buttons.hide()
        self.button_continue.show()

        self.view_stack.set_visible_child_name("inhibit")

    def on_terminate(self, data=None):
        print("Received SIGTERM from cinnamon-session, exiting")
        self.quit(0)

    def finish_up(self):
        if self.timer_id > 0:
            GLib.source_remove(self.timer_id)

        self.view_stack.set_visible_child_name("busy")

        GLib.timeout_add_seconds(5, self.quit)

    def quit(self, code=0):
        self.window.destroy()

        if Gtk.main_level() == 0:
            sys.exit(code)
        else:
            self.ecode = code
            Gtk.main_quit()

if __name__ == "__main__":
    dialog = QuitDialog()
    Gtk.main()
    sys.exit(0)
