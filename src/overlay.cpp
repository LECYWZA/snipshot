#include <QWindow>
#include "overlay.h"
#include "settings.h"
#include "screenutil.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QInputDialog>
#include <QLineEdit>
#include <QClipboard>
#include <QApplication>
#include <QDebug>
#include <QtMath>
#include <cmath>
#include <QTimer>
#include <QMenu>
#include <QFileDialog>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QUrl>

static const QColor kMask(0, 0, 0, 130);
static const QColor kAccent(0, 160, 255);
static const QColor kBtnBg(40, 40, 40, 235);
static const QColor kBtnSel(0, 120, 215);
static const QStringList kToolNames{QStringLiteral("矩形"), QStringLiteral("椭圆"), QStringLiteral("箭头"),
                                   QStringLiteral("画笔"), QStringLiteral("马赛克"), QStringLiteral("文字")};
static const QStringList kActionNames{QStringLiteral("保存"), QStringLiteral("复制"), QStringLiteral("贴图"),
                                     QStringLiteral("翻译"), QStringLiteral("OCR"), QStringLiteral("撤销")};

Overlay::Overlay(const QImage& fullImage, QScreen* screen, Mode mode, QWidget* parent)
    : QWidget(parent), m_mode(mode) {
    m_desk = logicalScreenGeometry(screen);
    m_winRect = m_desk;
    m_origin = m_desk.topLeft();

    const QRect desk = virtualDesktopRect();
    const double sx = double(fullImage.width()) / qMax(1, desk.width());
    const double sy = double(fullImage.height()) / qMax(1, desk.height());
    const QRect src(qRound((m_desk.x() - desk.x()) * sx),
                    qRound((m_desk.y() - desk.y()) * sy),
                    qRound(m_desk.width() * sx),
                    qRound(m_desk.height() * sy));
    m_base = fullImage.copy(src);

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    winId();
    if (windowHandle())
        windowHandle()->setScreen(screen);
    setGeometry(m_desk);
    showFullScreen();
    QTimer::singleShot(0, this, &Overlay::syncGeometry);
    QTimer::singleShot(150, this, &Overlay::syncGeometry);
    qInfo() << "Overlay 屏幕:" << (screen ? screen->name() : QStringLiteral("?"))
            << m_desk << "slice:" << m_base.size();
}

Overlay::Overlay(const QImage& screenshot, Mode mode, QWidget* parent)
    : QWidget(parent), m_base(screenshot), m_mode(mode) {
    m_winRect = virtualDesktopRect();
    m_desk = m_winRect;
    m_origin = m_winRect.topLeft();

    Qt::WindowFlags f = Qt::Popup | Qt::FramelessWindowHint;
    setWindowFlags(f);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setGeometry(m_winRect);
    show();
    raise();
    setFocus();
    QTimer::singleShot(0, this, &Overlay::syncGeometry);
    QTimer::singleShot(120, this, &Overlay::syncGeometry);
    qInfo() << "Overlay 全桌面:" << m_winRect << "逻辑尺寸, 截图:" << m_base.size();
}

Overlay::Overlay(const QImage& image, const QRect& logicalGeometry, bool editMode, QWidget* parent)
    : QWidget(parent), m_base(image), m_mode(Snip), m_edit(editMode), m_winRect(logicalGeometry) {
    if (m_winRect.isEmpty()) {
        const double d = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
        m_winRect = QRect(QPoint(0, 0), QSize(qRound(image.width() / d), qRound(image.height() / d)));
    }
    m_desk = m_winRect;
    m_origin = m_winRect.topLeft();
    m_hasSel = true;
    m_sel = QRect(QPoint(0, 0), m_winRect.size());
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setGeometry(m_winRect);
    show();
    raise();
    activateWindow();
    setFocus();
    QTimer::singleShot(0, this, &Overlay::syncGeometry);
    QTimer::singleShot(120, this, &Overlay::syncGeometry);
    qInfo() << "Overlay 编辑窗:" << m_winRect << "图片:" << m_base.size();
}

Overlay::~Overlay() {
    releaseKeyboard();
}

bool Overlay::hasSelection() const {
    return m_hasSel && selectedRect().width() > 2 && selectedRect().height() > 2;
}

QRect Overlay::selectedRect() const {
    return m_sel.normalized();
}

QRect Overlay::selectionGlobalRect() const {
    return selectedRect().translated(m_origin);
}

QRect Overlay::sourceRectFor(const QRect& sel) const {
    if (m_edit) {
        const double sx = double(m_base.width()) / qMax(1, width());
        const double sy = double(m_base.height()) / qMax(1, height());
        return QRect(qRound(sel.x() * sx), qRound(sel.y() * sy),
                     qRound(sel.width() * sx), qRound(sel.height() * sy));
    }
    const double sx = double(m_base.width()) / qMax(1, m_desk.width());
    const double sy = double(m_base.height()) / qMax(1, m_desk.height());
    const QRect global = sel.translated(m_origin);
    return QRect(qRound((global.x() - m_desk.x()) * sx), qRound((global.y() - m_desk.y()) * sy),
                 qRound(global.width() * sx), qRound(global.height() * sy));
}

QImage Overlay::currentResult() const {
    if (!hasSelection())
        return QImage();
    const QRect sel = selectedRect();
    QImage out;
    if (m_edit) {
        out = m_base;
    } else {
        out = m_base.copy(sourceRectFor(sel));
    }
    if (out.isNull())
        return out;
    const double sx = double(out.width()) / qMax(1, sel.width());
    const double sy = double(out.height()) / qMax(1, sel.height());
    QPainter p(&out);
    p.scale(sx, sy);
    p.translate(-sel.topLeft());
    drawAnnotations(p);
    p.end();
    out.setDevicePixelRatio(sx);
    return out;
}

void Overlay::setProcessing(bool on) {
    m_processing = on;
    update();
}

void Overlay::startSelection(QPoint p) {
    m_selecting = true;
    m_start = p;
    m_sel = QRect(p, p);
    m_hasSel = false;
    m_anns.clear();
    m_drawing = false;
    update();
}

void Overlay::updateSelection(QPoint p) {
    if (!m_selecting)
        return;
    m_sel = QRect(m_start, p).normalized();
    update();
}

void Overlay::endSelection() {
    m_selecting = false;
    if (selectedRect().width() > 4 && selectedRect().height() > 4) {
        m_hasSel = true;
        m_tool = Rect;
        if (m_mode == Translate || m_mode == Ocr) {
            m_processing = true;
            update();
            const QImage img = currentResult();
            const QRect r = selectionGlobalRect();
            if (m_mode == Translate)
                emit translateRequested(img, r);
            else
                emit ocrRequested(img, r);
            return;
        }
    } else {
        m_hasSel = false;
    }
    update();
    emitCurrentIfSnip();
}

void Overlay::emitCurrentIfSnip() {
    if (m_edit || m_mode != Snip || !hasSelection())
        return;
    emit selectionChanged(currentResult(), selectionGlobalRect());
}

void Overlay::startAnnotation(QPoint p) {
    if (!hasSelection())
        return;
    if (m_tool == Text) {
        bool ok = false;
        const QString t = QInputDialog::getText(this, QStringLiteral("文字标注"),
                                               QStringLiteral("输入文字："),
                                               QLineEdit::Normal, QString(), &ok);
        if (ok && !t.isEmpty()) {
            Ann a;
            a.tool = Text;
            a.rect = QRect(p, QSize(10, 10));
            a.text = t;
            a.color = QColor(255, 60, 60);
            a.width = 0;
            m_anns.append(a);
            update();
            emitCurrentIfSnip();
        }
        return;
    }
    Ann a;
    a.tool = m_tool;
    a.a = p;
    a.b = p;
    a.rect = QRect(p, p);
    a.color = QColor(255, 60, 60);
    a.width = 3;
    if (m_tool == Pen)
        a.path.append(p);
    m_anns.append(a);
    m_drawing = true;
    update();
}

void Overlay::updateAnnotation(QPoint p) {
    if (!m_drawing || m_anns.isEmpty())
        return;
    Ann& a = m_anns.last();
    if (a.tool == Pen) {
        a.path.append(p);
        a.b = p;
    } else {
        a.b = p;
        a.rect = QRect(a.a, p).normalized();
    }
    update();
}

QRect Overlay::barRect(bool tools) const {
    const int bw = 52, bh = 28, gap = 4;
    const int n = tools ? kToolNames.size() : kActionNames.size();
    const int w = n * (bw + gap) + gap;
    const int h = bh + gap * 2;
    const QRect sel = m_edit ? rect() : selectedRect();
    int x = sel.right() - w + 1;
    int y;
    if (tools) {
        y = sel.top() - h - gap;
        if (y < 0)
            y = sel.top() + gap;
    } else {
        y = sel.bottom() + gap;
        if (y + h > height())
            y = sel.bottom() - h - gap;
    }
    if (x < 0)
        x = 0;
    if (x + w > width())
        x = width() - w;
    if (y < 0)
        y = 0;
    if (y + h > height())
        y = height() - h;
    return QRect(x, y, w, h);
}

int Overlay::hitBar(QPoint p, bool tools) const {
    const QRect tb = barRect(tools);
    if (!tb.contains(p))
        return -1;
    const int bw = 52, bh = 28, gap = 4;
    int x = tb.left() + gap;
    for (int i = 0; i < (tools ? kToolNames.size() : kActionNames.size()); ++i) {
        const QRect r(x, tb.top() + gap, bw, bh);
        if (r.contains(p))
            return i;
        x += bw + gap;
    }
    return -1;
}

void Overlay::drawBar(QPainter& p, bool tools) {
    const QRect tb = barRect(tools);
    p.fillRect(tb, kBtnBg);
    p.setPen(QPen(QColor(90, 90, 90), 1));
    p.drawRect(tb);
    const int bw = 52, bh = 28, gap = 4;
    int x = tb.left() + gap;
    const QStringList& names = tools ? kToolNames : kActionNames;
    for (int i = 0; i < names.size(); ++i) {
        const QRect r(x, tb.top() + gap, bw, bh);
        const bool active = tools && i == int(m_tool);
        p.fillRect(r, active ? kBtnSel : QColor(60, 60, 60));
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, names[i]);
        x += bw + gap;
    }
}

void Overlay::drawAnnotations(QPainter& p) const {
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const Ann& a : m_anns) {
        p.setPen(QPen(a.color, a.width));
        switch (a.tool) {
        case Rect:
            p.setBrush(Qt::NoBrush);
            p.drawRect(a.rect);
            break;
        case Ellipse:
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(a.rect);
            break;
        case Arrow: {
            p.drawLine(a.a, a.b);
            QLineF line(a.a, a.b);
            const double ang = std::atan2(-line.dy(), line.dx());
            const double len = 14;
            const QPointF p1 = a.b - QPointF(std::cos(ang - M_PI / 7) * len, -std::sin(ang - M_PI / 7) * len);
            const QPointF p2 = a.b - QPointF(std::cos(ang + M_PI / 7) * len, -std::sin(ang + M_PI / 7) * len);
            p.drawLine(a.b, p1);
            p.drawLine(a.b, p2);
            break;
        }
        case Pen:
            if (a.path.size() > 1)
                p.drawPolyline(a.path.constData(), a.path.size());
            break;
        case Mosaic: {
            const QRect src = sourceRectFor(a.rect);
            QImage sub = m_base.copy(src);
            if (!sub.isNull()) {
                const QImage small = sub.scaled(qMax(1, sub.width() / 8), qMax(1, sub.height() / 8),
                                               Qt::IgnoreAspectRatio, Qt::FastTransformation);
                p.drawImage(a.rect, small.scaled(sub.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
            }
            break;
        }
        case Text:
            p.setFont(QFont(QStringLiteral("Sans"), 16));
            p.drawText(a.rect, Qt::AlignLeft | Qt::AlignTop, a.text);
            break;
        default:
            break;
        }
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void Overlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (m_edit) {
        p.drawImage(rect(), m_base);
    } else {
        // 窗口可能被 WM 限制在工作区（让出顶栏），按实际窗口原点偏移绘制整张桌面图
        const QRect imgRect(m_desk.topLeft() - m_origin, m_desk.size());
        p.drawImage(imgRect, m_base);
    }

    if (m_edit) {
        drawAnnotations(p);
        drawBar(p, true);
        drawBar(p, false);
        return;
    }

    if (m_hasSel || m_selecting) {
        const QRect sel = selectedRect();
        p.fillRect(rect(), kMask);
        if (sel.width() > 0 && sel.height() > 0)
            p.drawImage(sel, m_base, sourceRectFor(sel));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(kAccent, 1));
        p.drawRect(sel.adjusted(0, 0, -1, -1));
        drawAnnotations(p);

        // 尺寸提示
        const QString dim = QString("%1 × %2").arg(sel.width()).arg(sel.height());
        QFont f = p.font();
        f.setPixelSize(12);
        p.setFont(f);
        QRect r(sel.left(), sel.top() - 22, 120, 20);
        if (r.top() < 0)
            r.moveTop(sel.top() + 4);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, dim);

        if (m_processing) {
            const QString msg = m_mode == Translate ? QStringLiteral("翻译中…")
                                                   : (m_mode == Ocr ? QStringLiteral("识别中…")
                                                                    : QStringLiteral("处理中…"));
            QRect bubble(sel.center() - QPoint(70, 20), QSize(140, 40));
            p.fillRect(bubble, QColor(0, 0, 0, 180));
            p.setPen(Qt::white);
            p.drawText(bubble, Qt::AlignCenter, msg);
        } else if (m_selecting && m_mode != Snip) {
            p.setPen(Qt::white);
            p.drawText(QRect(10, height() - 30, 600, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       m_mode == Translate ? QStringLiteral("松开鼠标立即翻译 • Esc 取消")
                                           : QStringLiteral("松开鼠标立即提取文字 • Esc 取消"));
        } else if (!m_selecting && m_mode == Snip) {
            drawBar(p, true);
            drawBar(p, false);
            p.setPen(Qt::white);
            p.drawText(QRect(10, height() - 30, 400, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("Esc / 右键 取消 • Ctrl+Z 撤销"));
        }
    } else {
        p.fillRect(rect(), kMask);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(16);
        p.setFont(f);
        QString hint = QStringLiteral("按住鼠标拖拽选择区域 • Esc 取消");
        if (m_mode == Translate)
            hint = QStringLiteral("按住鼠标拖拽选择翻译区域 • Esc 取消");
        else if (m_mode == Ocr)
            hint = QStringLiteral("按住鼠标拖拽选择提取文字区域 • Esc 取消");
        p.drawText(rect(), Qt::AlignCenter, hint);
    }
}

void Overlay::mousePressEvent(QMouseEvent* e) {
    qInfo() << "Overlay mousePress" << e->button() << e->position().toPoint();
    if (e->button() == Qt::RightButton) {
        if (!m_edit && !m_hasSel) {
            close();
            return;
        }
        showContextMenu(e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    const QPoint pos = e->position().toPoint();
    const int toolBtn = hitBar(pos, true);
    if (toolBtn >= 0) {
        m_tool = Tool(toolBtn);
        update();
        return;
    }
    const int actionBtn = hitBar(pos, false);
    if (actionBtn >= 0) {
        handleAction(actionBtn);
        return;
    }
    if (m_processing)
        return;
    if (!m_edit && !m_hasSel) {
        startSelection(pos);
        return;
    }
    if (selectedRect().contains(pos)) {
        startAnnotation(pos);
        return;
    }
    if (!m_edit)
        startSelection(pos);
}

void Overlay::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    if (m_selecting) {
        updateSelection(pos);
        return;
    }
    if (m_drawing && (e->buttons() & Qt::LeftButton))
        updateAnnotation(pos);
}

void Overlay::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    if (m_selecting) {
        endSelection();
        return;
    }
    if (m_drawing) {
        m_drawing = false;
        emitCurrentIfSnip();
        update();
    }
}

void Overlay::closeEvent(QCloseEvent* e) {
    releaseKeyboard();
    QWidget::closeEvent(e);
}

void Overlay::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    setFocus();
    QTimer::singleShot(0, this, &Overlay::syncGeometry);
}

void Overlay::syncGeometry() {
    const QRect g = geometry();
    bool changed = false;
    if (m_origin != g.topLeft()) {
        m_origin = g.topLeft();
        changed = true;
    }
    if (m_edit && (m_desk.size() != g.size() || m_desk.topLeft() != g.topLeft())) {
        m_desk = g;
        m_sel = QRect(QPoint(0, 0), g.size());
        changed = true;
    }
    if (changed)
        update();
    qInfo() << "Overlay 实际窗口:" << g << "origin:" << m_origin;
}

void Overlay::keyPressEvent(QKeyEvent* e) {
    qInfo() << "Overlay keyPress" << e->key() << e->text();
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        handleAction(0);
        return;
    }
    if (e->key() == Qt::Key_Z && (e->modifiers() & Qt::ControlModifier)) {
        if (!m_anns.isEmpty()) {
            m_anns.removeLast();
            update();
            emitCurrentIfSnip();
        }
        return;
    }
    if (e->key() == Qt::Key_C && (e->modifiers() & Qt::ControlModifier)) {
        handleAction(1);
        return;
    }
    if (e->key() == Qt::Key_S && (e->modifiers() & Qt::ControlModifier)) {
        handleAction(0);
        return;
    }
    if (e->key() == Qt::Key_F2) {
        if (hasSelection()) {
            const QImage img = currentResult();
            const QRect r = selectionGlobalRect();
            if (!img.isNull())
                emit pinRequested(img, r);
            close();
        }
        return;
    }
    QWidget::keyPressEvent(e);
}

void Overlay::handleAction(int idx) {
    if (idx == 5) {  // 撤销
        if (!m_anns.isEmpty()) {
            m_anns.removeLast();
            update();
            emitCurrentIfSnip();
        }
        return;
    }
    if (!hasSelection())
        return;
    const QImage img = currentResult();
    if (img.isNull())
        return;
    const QRect r = selectionGlobalRect();
    switch (idx) {
    case 0:
        emit saveRequested(img, r);
        close();
        break;
    case 1:
        emit copyRequested(img, r);
        close();
        break;
    case 2:
        emit pinRequested(img, r);
        close();
        break;
    case 3:
        setProcessing(true);
        emit translateRequested(img, r);
        break;
    case 4:
        setProcessing(true);
        emit ocrRequested(img, r);
        break;
    default:
        break;
    }
}

void Overlay::showContextMenu(const QPoint& globalPos) {
    QMenu menu(this);
    if (hasSelection()) {
        const QImage img = currentResult();
        const QRect r = selectionGlobalRect();
        menu.addAction(QStringLiteral("复制"), this, [this, img, r]() {
            emit copyRequested(img, r);
            close();
        });
        menu.addAction(QStringLiteral("保存到默认目录"), this, [this, img, r]() {
            emit saveRequested(img, r);
            close();
        });
        menu.addAction(QStringLiteral("另存为…"), this, [this, img]() {
            Settings st = Settings::load();
            QDir().mkpath(st.saveDir);
            const QString fn = QFileDialog::getSaveFileName(
                this, QStringLiteral("另存为"),
                st.saveDir + "/Snip_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png",
                QStringLiteral("PNG (*.png)"));
            if (!fn.isEmpty())
                img.save(fn);
            close();
        });
        menu.addAction(QStringLiteral("打开图片所在路径"), this, [this, img]() {
            Settings st = Settings::load();
            QDir().mkpath(st.saveDir);
            const QString fn = st.saveDir + "/Snip_" +
                               QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
            if (img.save(fn))
                QDesktopServices::openUrl(QUrl::fromLocalFile(st.saveDir));
            close();
        });
        menu.addAction(QStringLiteral("贴图"), this, [this, img, r]() {
            emit pinRequested(img, r);
            close();
        });
        menu.addAction(QStringLiteral("翻译"), this, [this, img, r]() {
            setProcessing(true);
            emit translateRequested(img, r);
        });
        menu.addAction(QStringLiteral("文字提取"), this, [this, img, r]() {
            setProcessing(true);
            emit ocrRequested(img, r);
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("撤销"), this, [this]() {
            if (!m_anns.isEmpty()) {
                m_anns.removeLast();
                update();
                emitCurrentIfSnip();
            }
        });
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("取消"), this, &QWidget::close);
    menu.exec(globalPos);
}
