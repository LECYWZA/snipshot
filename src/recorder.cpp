#include "recorder.h"
#include "settings.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QVariantMap>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>

ScreenRecorder::ScreenRecorder(QObject* parent) : QObject(parent) {}

QString ScreenRecorder::newFilePath() {
    Settings st = Settings::load();
    QString dir = st.recordDir;
    if (dir.isEmpty())
        dir = Settings::defaultRecordDir();
    QDir().mkpath(dir);
    return dir + "/SnipShot_" +
           QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".webm";
}

bool ScreenRecorder::start() {
    if (m_recording)
        return true;
    const Settings st = Settings::load();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit error(QStringLiteral("D-Bus 会话不可用，无法录屏"));
        return false;
    }
    const QString file = newFilePath();
    QVariantMap opts;
    opts.insert("framerate", qBound(5, st.recordFps, 60));
    opts.insert("draw-cursor", st.recordCursor);

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.gnome.Shell.Screencast",
        "/org/gnome/Shell/Screencast",
        "org.gnome.Shell.Screencast",
        "Screencast");
    msg << file << opts;
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        emit error(QStringLiteral("录屏启动失败: ") + reply.errorMessage());
        return false;
    }
    const QList<QVariant> args = reply.arguments();
    bool ok = false;
    QString used;
    if (args.size() >= 1)
        ok = args[0].toBool();
    if (args.size() >= 2)
        used = args[1].toString();
    if (!ok) {
        emit error(QStringLiteral("GNOME Shell 拒绝了录屏请求"));
        return false;
    }
    m_recording = true;
    m_file = used.isEmpty() ? file : used;
    qInfo() << "录屏开始:" << m_file;
    emit started(m_file);
    return true;
}

void ScreenRecorder::stop() {
    if (!m_recording)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.gnome.Shell.Screencast",
        "/org/gnome/Shell/Screencast",
        "org.gnome.Shell.Screencast",
        "StopScreencast");
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning() << "StopScreencast:" << reply.errorMessage();
    m_recording = false;
    m_lastFile = m_file;
    qInfo() << "录屏结束:" << m_lastFile;
    emit finished(m_lastFile);
}
