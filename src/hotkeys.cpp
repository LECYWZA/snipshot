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

static QString normalizeBinding(QString b) {
    // GNOME/GTK accelerator 里 Alt+Q 的 Q 是独立 keyval，和物理 Alt+q 不匹配。
    // 单个字母统一转小写；F1/Print 等保持原样。
    const int pos = b.lastIndexOf('+');
    if (pos >= 0 && pos + 1 < b.size()) {
        const QString key = b.mid(pos + 1);
        if (key.size() == 1 && key[0].isLetter())
            b = b.left(pos + 1) + key.toLower();
    }
    return b;
}

static QString upperVariant(const QString& b) {
    const int pos = b.lastIndexOf('+');
    if (pos < 0 || pos + 1 >= b.size())
        return QString();
    QString key = b.mid(pos + 1);
    if (key.size() != 1 || !key[0].isLower())
        return QString();
    return b.left(pos + 1) + key.toUpper();
}

static bool gsettingsSet(const QString& schema, const QString& key, const QString& value) {
    // startDetached：不阻塞 UI 主线程（之前注册时会卡住，贴图悬停显示忙光标）
    return QProcess::startDetached("gsettings", {"set", schema, key, value});
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
        // 同时注册小写和 Shift/CapsLock 下的字母变体，避免实际按键 keysym 不匹配
        QStringList bindings{normalizeBinding(e.binding)};
        const QString upper = upperVariant(normalizeBinding(e.binding));
        if (!upper.isEmpty() && upper != bindings.first())
            bindings << upper;
        for (int vi = 0; vi < bindings.size(); ++vi) {
            const QString name = vi == 0 ? e.name : e.name + "-upper";
            const QString path = QString(kCustomPrefix) + name + "/";
            const QString schema = QString("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:") + path;
            ok = gsettingsSet(schema, "name", name) && ok;
            ok = gsettingsSet(schema, "command", e.cmd) && ok;
            ok = gsettingsSet(schema, "binding", bindings[vi]) && ok;
            if (!paths.contains(path))
                paths << path;
        }
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
