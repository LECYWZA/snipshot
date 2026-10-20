#pragma once

#include <QObject>
#include <QImage>

// 抓屏：Linux 下一律走 xdg-desktop-portal。
// 即使本程序跑在 Xwayland(xcb) 上，也不能用 grabWindow：Xwayland 的 root
// window 只包含 X11 客户窗口，截不到 Wayland 原生窗口，会黑屏。
class Capture : public QObject {
    Q_OBJECT
public:
    explicit Capture(QObject* parent = nullptr);

    // 异步抓取整个虚拟桌面，成功发 captured(QImage)
    void captureScreen();

signals:
    void captured(const QImage& img);
    void failed(const QString& error);

private slots:
    void onPortalResponse(uint code, const QVariantMap& results);

private:
    bool tryExtensionShot();
    void captureX11();
    QString m_requestPath;
};
