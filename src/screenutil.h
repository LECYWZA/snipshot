#pragma once

#include <QGuiApplication>
#include <QScreen>
#include <QRect>
#include <QPoint>
#include <QtMath>

// 逻辑屏幕几何。
// xcb（Xwayland）下 Qt 会给出 DPR=2，但 screen.geometry().topLeft() 仍是
// X11 物理坐标（例如右屏 x=5120），而 width/height 已经是逻辑值。
// 这里把原点也换算成逻辑坐标，保证虚拟桌面并集正确（两屏 5120x1440，
// 而不是错误的 7680x1440）。
inline QRect logicalScreenGeometry(QScreen* s) {
    QRect g = s->geometry();
    if (QGuiApplication::platformName().startsWith(QStringLiteral("xcb"))) {
        const qreal d = s->devicePixelRatio();
        if (d > 1.0)
            g.moveTopLeft(QPoint(qRound(g.x() / d), qRound(g.y() / d)));
    }
    return g;
}

inline QRect virtualDesktopRect() {
    QRect r;
    bool first = true;
    const auto screens = QGuiApplication::screens();
    for (QScreen* s : screens) {
        const QRect g = logicalScreenGeometry(s);
        if (first) {
            r = g;
            first = false;
        } else {
            r = r.united(g);
        }
    }
    if (first) {
        QScreen* p = QGuiApplication::primaryScreen();
        r = p ? p->geometry() : QRect(0, 0, 800, 600);
    }
    return r;
}
