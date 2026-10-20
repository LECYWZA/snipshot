#include "hotkeys.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QDebug>

static const char* kMediaKeysSchema = "org.gnome.settings-daemon.plugins.media-keys";
static const char* kCustomPrefix = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/";

QString Hotkeys::executablePath() {
    return QCoreApplication::applicationFilePath();
}

static bool gsettingsSet(const QString& schema, const QString& key, const QString& value) {
    QProcess p;
    p.start("gsettings", {"set", schema, key, value});
    p.waitForFinished(5000);
    if (p.exitCode() != 0)
        qWarning() << "gsettings set 失败:" << schema << key << p.readAllStandardError();
    return p.exitCode() == 0;
}

static QString gsettingsGet(const QString& schema, const QString& key) {
    QProcess p;
    p.start("gsettings", {"get", schema, key});
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

bool Hotkeys::installGnome(const Settings& st) {
    // 现有 custom-keybindings 列表
    const QString cur = gsettingsGet(kMediaKeysSchema, "custom-keybindings");
    QRegularExpression re(QString("(%1[^']+)").arg(kCustomPrefix));
    QStringList paths;
    auto it = re.globalMatch(cur);
    while (it.hasNext())
        paths << it.next().captured(1);

    struct Entry { QString name; QString cmd; QString binding; };
    const QList<Entry> entries{
        {"snipshot-snip",      executablePath() + " --snip",                    st.hkSnip},
        {"snipshot-pin",       executablePath() + " --pin",                     st.hkPin},
        {"snipshot-toggle",    executablePath() + " --toggle-pins",             st.hkTogglePins},
        {"snipshot-translate", executablePath() + " --translate",               st.hkTranslate},
        {"snipshot-ocr",       executablePath() + " --ocr",                     st.hkOcr},
        {"snipshot-clipboard", executablePath() + " --clipboard",               st.hkClipboard},
        {"snipshot-record",    executablePath() + " --record-toggle",           st.hkRecord},
    };

    bool ok = true;
    for (const Entry& e : entries) {
        const QString path = QString(kCustomPrefix) + e.name + "/";
        const QString schema = QString("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:") + path;
        ok = gsettingsSet(schema, "name", e.name) && ok;
        ok = gsettingsSet(schema, "command", e.cmd) && ok;
        ok = gsettingsSet(schema, "binding", e.binding) && ok;
        if (!paths.contains(path))
            paths << path;
    }
    // 更新列表
    QString list = "[";
    for (int i = 0; i < paths.size(); ++i) {
        if (i)
            list += ", ";
        list += "'" + paths[i] + "'";
    }
    list += "]";
    ok = gsettingsSet(kMediaKeysSchema, "custom-keybindings", list) && ok;
    return ok;
}
