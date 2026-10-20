import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const IFACE = `<node>
<interface name="org.snipshot.Shot">
  <method name="Take">
    <arg type="b" direction="in" name="includeCursor"/>
    <arg type="s" direction="out" name="path"/>
  </method>
</interface>
</node>`;

export default class SnipShotExtension extends Extension {
    enable() {
        this._nameId = Gio.bus_own_name(Gio.BusType.SESSION, 'org.snipshot.Shot',
            Gio.BusNameOwnerFlags.NONE, null, null);
        this._obj = Gio.DBusExportedObject.wrapJSObject(IFACE, this);
        this._obj.export(Gio.DBus.session, '/org/snipshot/Shot');
    }

    disable() {
        if (this._obj) {
            this._obj.unexport();
            this._obj = null;
        }
        if (this._nameId) {
            Gio.bus_unown_name(this._nameId);
            this._nameId = 0;
        }
    }

    Take(includeCursor) {
        const dir = GLib.build_filenamev([GLib.get_user_cache_dir(), 'snipshot']);
        GLib.mkdir_with_parents(dir, 0o755);
        const path = GLib.build_filenamev([dir, 'shot.png']);
        try {
            Gio.DBus.session.call_sync(
                'org.gnome.Shell.Screenshot',
                '/org/gnome/Shell/Screenshot',
                'org.gnome.Shell.Screenshot',
                'Screenshot',
                new GLib.Variant('(bbs)', [includeCursor, false, path]),
                null, Gio.DBusCallFlags.NONE, -1, null);
        } catch (e) {
            logError(e, 'snipshot: D-Bus screenshot failed');
            return '';
        }
        return path;
    }
}
