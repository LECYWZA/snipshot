#include "hotkeys.h"

#include <QCoreApplication>
#include <algorithm>
#include <QProcess>
#include <QRegularExpression>
#include <QDebug>

static const char* kMediaKeysSchema = "org.gnome.settings-daemon.plugins.media-keys";
static const char* kCustomPrefix = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/";

QString Hotkeys::executablePath() {
    return QCoreApplication::applicationFilePath();
}

static QString normalizeBinding(const QString& input) {
    QString b = input.trimmed();
    if (b.isEmpty())
        return b;
    // 已经是 <Alt>q 形式：只需把单字母 key 转小写
    if (b.contains('<') || b.contains('>')) {
        if (b.back().isLetter() && b.back().isUpper())
            b[b.size() - 1] = b.back().toLower();
        return b;
    }
    const QStringList parts = b.split('+', Qt::SkipEmptyParts);
    if (parts.size() <= 1) {
        if (b.size() == 1 && b[0].isLetter())
            b = b.toLower();
        return b;
    }
    QString out;
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString m = parts[i].trimmed().toLower();
        if (m == QStringLiteral("alt"))
            out += QStringLiteral("<Alt>");
        else if (m == QStringLiteral("ctrl") || m == QStringLiteral("control"))
            out += QStringLiteral("<Ctrl>");
        else if (m == QStringLiteral("shift"))
            out += QStringLiteral("<Shift>");
        else if (m == QStringLiteral("super") || m == QStringLiteral("win") || m == QStringLiteral("meta"))
            out += QStringLiteral("<Super>");
        else
            out += QStringLiteral("<") + parts[i].trimmed() + QStringLiteral(">");
    }
    QString key = parts.last().trimmed();
    if (key.size() == 1 && key[0].isLetter())
        key = key.toLower();
    return out + key;
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

    // 清掉上一版遗留的 -upper 条目，避免它们抢占/报 grab 失败
    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [](const QString& p) { return p.contains(QStringLiteral("-upper/")); }),
                paths.end());

    bool ok = true;
    for (const Entry& e : entries) {
        const QString path = QString(kCustomPrefix) + e.name + "/";
        const QString schema = QString("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:") + path;
        ok = gsettingsSet(schema, "name", e.name) && ok;
        ok = gsettingsSet(schema, "command", e.cmd) && ok;
        ok = gsettingsSet(schema, "binding", normalizeBinding(e.binding)) && ok;
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
