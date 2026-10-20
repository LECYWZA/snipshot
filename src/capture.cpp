#include "capture.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QFile>
#include <QUrl>
#include <QDebug>
#include <QFileInfo>
#include <QDBusMessage>

Capture::Capture(QObject* parent) : QObject(parent) {}

bool Capture::tryExtensionShot() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.snipshot.Shot", "/org/snipshot/Shot", "org.snipshot.Shot", "Take");
    msg << false;
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 4000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;
    const QString path = reply.arguments().value(0).toString();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;
    QImage img(path);
    if (img.isNull())
        return false;
    qInfo() << "扩展截图:" << img.size() << path;
    emit captured(img);
    return true;
}

void Capture::captureScreen() {
    // 优先 GNOME Shell 扩展（无授权框/无闪光）；没有扩展再走 portal
    if (tryExtensionShot())
        return;

    // Linux 下统一走 xdg-desktop-portal。
    // 即使本程序跑在 Xwayland(xcb) 上，也不能用 grabWindow：Xwayland 的
    // root window 里只有 X11 客户窗口，截不到 Wayland 原生窗口，会黑屏。
    // portal 由 GNOME Shell 侧完成截图，返回整张虚拟桌面（本机为 7680x2160）。
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(QStringLiteral("D-Bus session 不可用"));
        return;
    }

    const QString token = "snipshot_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QString senderPart = bus.baseService();
    if (senderPart.startsWith(':'))
        senderPart.remove(0, 1);
    senderPart.replace('.', '_');
    m_requestPath = "/org/freedesktop/portal/desktop/request/" + senderPart + "/" + token;

    bus.connect("org.freedesktop.portal.Desktop",
                m_requestPath,
                "org.freedesktop.portal.Request",
                "Response",
                this, SLOT(onPortalResponse(uint, QVariantMap)));

    QVariantMap opts;
    opts.insert("handle_token", token);
    opts.insert("interactive", false);
    opts.insert("modal", true);
    // 告诉 portal 已信任截图权限，避免每次都停在授权/预览框
    opts.insert("permission_store_checked", true);

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Screenshot",
        "Screenshot");
    msg << QString() << opts;

    QDBusPendingCall call = bus.asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            qWarning() << "portal Screenshot 调用失败:" << reply.error().message();
            // X11 原生会话下的兜底；Xwayland 下通常会黑屏，但聊胜于无
            captureX11();
            w->deleteLater();
            return;
        }
        const QString handle = reply.value().path();
        qInfo() << "portal 返回 handle:" << handle;
        if (handle != m_requestPath) {
            // 预测路径与实际 handle 不一致时，改用实际 handle 监听 Response
            QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_requestPath,
                "org.freedesktop.portal.Request", "Response", this, SLOT(onPortalResponse(uint, QVariantMap)));
            m_requestPath = handle;
            QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", m_requestPath,
                "org.freedesktop.portal.Request", "Response", this, SLOT(onPortalResponse(uint, QVariantMap)));
        }
        w->deleteLater();
    });
}

void Capture::onPortalResponse(uint code, const QVariantMap& results) {
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop",
                                             m_requestPath,
                                             "org.freedesktop.portal.Request",
                                             "Response",
                                             this, SLOT(onPortalResponse(uint, QVariantMap)));
    if (code != 0) {
        emit failed(QString("截图被拒绝或取消 (code=%1)").arg(code));
        return;
    }
    const QString uri = results.value("uri").toString();
    if (uri.isEmpty()) {
        emit failed(QStringLiteral("portal 未返回截图"));
        return;
    }
    QImage img(QUrl(uri).toLocalFile());
    if (img.isNull()) {
        emit failed("无法读取截图文件: " + uri);
        return;
    }
    qInfo() << "portal 截图尺寸:" << img.size() << "文件:" << uri;
    emit captured(img);
}

void Capture::captureX11() {
    QScreen* scr = QGuiApplication::primaryScreen();
    if (!scr) {
        emit failed(QStringLiteral("找不到屏幕"));
        return;
    }
    QPixmap pm = scr->grabWindow(0);
    if (pm.isNull()) {
        emit failed(QStringLiteral("grabWindow 失败"));
        return;
    }
    qInfo() << "X11 截图尺寸:" << pm.size();
    emit captured(pm.toImage());
}
