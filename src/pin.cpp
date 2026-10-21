#include <QWindow>
#include "pin.h"
#include "ai.h"
#include "overlay.h"
#include "settings.h"
#include "screenutil.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QUrl>
#include <QtMath>

class DragLabel : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && window() && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
            return;
        }
        QLabel::mousePressEvent(e);
    }
};

static QString saveDefault(const QImage& img) {
    Settings st = Settings::load();
    QDir().mkpath(st.saveDir);
    const QString fn = st.saveDir + "/Snip_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
    return img.save(fn) ? fn : QString();
}

// 贴图/编辑结果：置顶文字窗，定位到原区域
static void showAiResult(const QString& title, const QImage& img, const QString& mode, const QRect& at) {
    auto* ai = new Ai;
    auto* win = new QWidget;
    win->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setWindowTitle(title);
    win->setStyleSheet("QWidget { background:#1e1e1e; }");

    auto* edit = new QTextEdit;
    edit->setReadOnly(true);
    edit->setPlaceholderText(QStringLiteral("识别中…"));
    edit->setStyleSheet("QTextEdit { background:#1e1e1e; color:#f0f0f0; border:1px solid #00a0ff; }");

    auto* titleLabel = new DragLabel(title);
    titleLabel->setStyleSheet("QLabel { color:#9fd8ff; font-weight:bold; padding:2px; }");
    auto* copyBtn = new QPushButton(QStringLiteral("复制"));
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    auto* bar = new QHBoxLayout;
    bar->addWidget(titleLabel);
    bar->addStretch();
    bar->addWidget(copyBtn);
    bar->addWidget(closeBtn);

    auto* lay = new QVBoxLayout(win);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->addWidget(edit);
    lay->addLayout(bar);

    QObject::connect(copyBtn, &QPushButton::clicked, win, [edit]() {
        QApplication::clipboard()->setText(edit->toPlainText());
    });
    QObject::connect(closeBtn, &QPushButton::clicked, win, &QWidget::close);

    QRect g = at;
    const QRect desk = virtualDesktopRect();
    if (g.isValid()) {
        if (g.width() < 320) {
            g.setWidth(320);
            g.moveLeft(qMax(desk.left(), at.center().x() - 160));
        }
        if (g.height() < 180) {
            g.setHeight(180);
            g.moveTop(qMax(desk.top(), at.center().y() - 90));
        }
        if (g.right() > desk.right())
            g.moveRight(desk.right());
        if (g.bottom() > desk.bottom())
            g.moveBottom(desk.bottom());
        if (g.left() < desk.left())
            g.moveLeft(desk.left());
        if (g.top() < desk.top())
            g.moveTop(desk.top());
        win->setGeometry(g);
    } else {
        win->resize(520, 360);
    }
    win->show();
    win->raise();

    QObject::connect(ai, &Ai::done, win, [edit, win, mode](const QString& text) {
        edit->setPlainText(text);
        if (mode == "ocr")
            QApplication::clipboard()->setText(text);
        win->raise();
    });
    QObject::connect(ai, &Ai::error, win, [win](const QString& e) {
        QMessageBox::warning(win, "SnipShot", QStringLiteral("AI 调用失败：") + e);
        win->close();
    });
    QObject::connect(ai, &Ai::done, ai, &QObject::deleteLater);
    QObject::connect(ai, &Ai::error, ai, &QObject::deleteLater);
    ai->recognize(img, mode);
}

PinWindow::PinWindow(const QImage& img, const QPoint& pos, QWidget* parent)
    : QWidget(parent), m_img(img) {
    double d = img.devicePixelRatio();
    if (d <= 0)
        d = 1.0;
    const QSize logical(qRound(img.width() / d), qRound(img.height() / d));
    m_baseLogical = logical;
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(logical);
    m_border = Settings::load().pinBorder;
    move(pos);
    show();
}

void PinWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.drawImage(rect(), m_img);
    if (m_border) {
        painter.setPen(QPen(QColor(0, 160, 255), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void PinWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (windowHandle())
            windowHandle()->startSystemMove();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* e) {
    Q_UNUSED(e);
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent*) {
    close();
}

void PinWindow::wheelEvent(QWheelEvent* e) {
    const int d = e->angleDelta().y();
    if (d == 0) {
        QWidget::wheelEvent(e);
        return;
    }
    m_zoom = qBound(0.2, m_zoom * (d > 0 ? 1.1 : 0.9), 8.0);
    setFixedSize(qMax(20, qRound(m_baseLogical.width() * m_zoom)),
                 qMax(20, qRound(m_baseLogical.height() * m_zoom)));
    update();
    e->accept();
}

void PinWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

PinManager* PinManager::instance() {
    static PinManager* s = new PinManager;
    return s;
}

void PinManager::addPin(const QImage& img) {
    if (img.isNull())
        return;
    double d = img.devicePixelRatio();
    if (d <= 0)
        d = 1.0;
    const QSize logical(qRound(img.width() / d), qRound(img.height() / d));
    const QRect desk = virtualDesktopRect();
    const QPoint pos(desk.center().x() - logical.width() / 2,
                     desk.center().y() - logical.height() / 2);
    addPin(img, pos);
}

void PinManager::addPin(const QImage& img, const QPoint& pos) {
    if (img.isNull())
        return;
    auto* w = new PinWindow(img, pos);
    m_pins.append(w);
    connect(w, &QObject::destroyed, this, [this, w]() { m_pins.removeAll(w); });
}

void PinManager::toggleAll() {
    if (m_pins.isEmpty())
        return;
    bool anyVisible = false;
    for (QWidget* w : m_pins)
        if (w->isVisible())
            anyVisible = true;
    for (QWidget* w : m_pins) {
        if (anyVisible)
            w->hide();
        else
            w->show();
    }
}

void PinManager::closeAll() {
    for (QWidget* w : m_pins)
        w->close();
}

// ---------- 贴图窗口右键菜单 ----------

void PinWindow::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu;
    QAction* borderAct = menu.addAction(QStringLiteral("显示边框"));
    borderAct->setCheckable(true);
    borderAct->setChecked(m_border);
    connect(borderAct, &QAction::toggled, this, [this](bool on) {
        m_border = on;
        update();
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("复制"), this, [this]() {
        QApplication::clipboard()->setImage(m_img);
    });
    menu.addAction(QStringLiteral("另存为…"), this, [this]() {
        Settings st = Settings::load();
        QDir().mkpath(st.saveDir);
        const QString fn = QFileDialog::getSaveFileName(
            this, QStringLiteral("另存为"),
            st.saveDir + "/Snip_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png",
            QStringLiteral("PNG (*.png)"));
        if (!fn.isEmpty() && m_img.save(fn))
            m_savedPath = fn;
    });
    menu.addAction(QStringLiteral("打开图片所在路径"), this, [this]() {
        if (m_savedPath.isEmpty())
            m_savedPath = saveDefault(m_img);
        if (!m_savedPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_savedPath).absolutePath()));
    });
    menu.addAction(QStringLiteral("绘制…"), this, [this]() {
        auto* ov = new Overlay(m_img, QRect(pos(), size()), true);
        QObject::connect(ov, &Overlay::saveRequested, this, [](const QImage& im, const QRect&) {
            saveDefault(im);
        });
        QObject::connect(ov, &Overlay::copyRequested, this, [](const QImage& im, const QRect&) {
            QApplication::clipboard()->setImage(im);
        });
        QObject::connect(ov, &Overlay::pinRequested, this, [](const QImage& im, const QRect& r) {
            PinManager::instance()->addPin(im, r.topLeft());
        });
        QObject::connect(ov, &Overlay::translateRequested, this, [ov](const QImage& im, const QRect& r) {
            showAiResult(QStringLiteral("翻译"), im, "translate", r);
            ov->close();
        });
        QObject::connect(ov, &Overlay::ocrRequested, this, [ov](const QImage& im, const QRect& r) {
            showAiResult(QStringLiteral("文字提取"), im, "ocr", r);
            ov->close();
        });
    });
    menu.addAction(QStringLiteral("文字提取"), this, [this]() {
        showAiResult(QStringLiteral("文字提取"), m_img, "ocr", QRect(pos(), size()));
    });
    menu.addAction(QStringLiteral("翻译"), this, [this]() {
        showAiResult(QStringLiteral("翻译"), m_img, "translate", QRect(pos(), size()));
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("关闭"), this, &QWidget::close);
    menu.exec(e->globalPos());
}

// ---------------- AI 结果贴图：像贴图一样显示选区原图 + 原位文字 ----------------

TextPinWindow::TextPinWindow(const QImage& img, const QRect& logicalRect, const QString& mode,
                             QWidget* parent)
    : QWidget(parent), m_img(img), m_mode(mode) {
    double d = img.devicePixelRatio();
    if (d <= 0)
        d = 1.0;
    QSize logical(qRound(img.width() / d), qRound(img.height() / d));
    // 以选区实际大小为准，不再撑成 380×220 的大黑框
    logical.setWidth(qMax(140, logical.width()));
    logical.setHeight(qMax(56, logical.height()));
    m_baseLogical = logical;
    m_baseFont = qBound(11, logical.height() / 8, 22);
    m_border = Settings::load().pinBorder;

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(mode == QStringLiteral("translate") ? QStringLiteral("翻译")
                                                      : QStringLiteral("文字提取"));
    setFixedSize(logical);
    if (logicalRect.isValid())
        move(logicalRect.topLeft());

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 细窄工具条：只做拖动和复制/关闭，背景半透明，下面是原图
    auto* header = new QWidget;
    header->setFixedHeight(18);
    header->setStyleSheet("QWidget { background: rgba(0,0,0,70); }");
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(4, 0, 2, 0);
    hl->setSpacing(2);
    m_title = new DragLabel(mode == QStringLiteral("translate") ? QStringLiteral("翻译")
                                                               : QStringLiteral("文字提取"));
    m_title->setStyleSheet("QLabel { color:#dff4ff; font-size:10px; padding-left:2px; }");
    auto* copyBtn = new QPushButton(QStringLiteral("复制"));
    copyBtn->setFlat(true);
    copyBtn->setFixedSize(34, 16);
    copyBtn->setStyleSheet("QPushButton { background: rgba(255,255,255,26); border:none; "
                           "color:#fff; font-size:10px; } "
                           "QPushButton:hover { background: rgba(255,255,255,70); }");
    auto* closeBtn = new QPushButton(QStringLiteral("×"));
    closeBtn->setFlat(true);
    closeBtn->setFixedSize(16, 16);
    closeBtn->setStyleSheet("QPushButton { background: rgba(255,255,255,26); border:none; "
                            "color:#fff; font-size:12px; } "
                            "QPushButton:hover { background: rgba(255,80,80,160); }");
    hl->addWidget(m_title);
    hl->addStretch();
    hl->addWidget(copyBtn);
    hl->addWidget(closeBtn);
    header->installEventFilter(this);
    lay->addWidget(header);

    m_edit = new QTextEdit;
    m_edit->setReadOnly(true);
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setPlaceholderText(mode == QStringLiteral("translate")
                                   ? QStringLiteral("翻译中…")
                                   : QStringLiteral("识别中…"));
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_edit->setStyleSheet(
        "QTextEdit { background: rgba(0,0,0,95); color:#f4f4f4; border:none; padding:4px; "
        "selection-background-color:#0078d7; }");
    m_edit->setFont(QFont(QStringLiteral("Sans"), m_baseFont));
    m_edit->installEventFilter(this);
    m_edit->viewport()->installEventFilter(this);
    lay->addWidget(m_edit, 1);

    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_edit->toPlainText());
    });
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

    m_ai = new Ai(this);
    connect(m_ai, &Ai::done, this, [this](const QString& text) {
        setResultText(text);
        if (m_mode == QStringLiteral("ocr"))
            QApplication::clipboard()->setText(text);
    });
    connect(m_ai, &Ai::error, this, [this](const QString& e) {
        setResultText(QStringLiteral("识别失败：") + e);
    });
    show();
    activateWindow();
    m_ai->recognize(m_img, m_mode);
}

TextPinWindow::~TextPinWindow() = default;

void TextPinWindow::setResultText(const QString& text) {
    m_done = true;
    if (m_title)
        m_title->setText(m_mode == QStringLiteral("translate") ? QStringLiteral("翻译")
                                                               : QStringLiteral("文字提取"));
    m_edit->setReadOnly(false);
    m_edit->setPlainText(text);
    m_edit->moveCursor(QTextCursor::End);
    m_edit->setFocus();
    raise();
    activateWindow();
    update();
}

void TextPinWindow::applyZoom() {
    const QSize sz(qMax(60, qRound(m_baseLogical.width() * m_zoom)),
                   qMax(30, qRound(m_baseLogical.height() * m_zoom)));
    setFixedSize(sz);
    QFont f = m_edit->font();
    f.setPixelSize(qMax(8, qRound(m_baseFont * m_zoom)));
    m_edit->setFont(f);
    update();
}

void TextPinWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // 原图铺满窗口，文字区域半透明叠在上面；不再额外压一层大黑底
    p.drawImage(rect(), m_img);
    if (m_border) {
        p.setPen(QPen(QColor(0, 160, 255), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void TextPinWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

void TextPinWindow::wheelEvent(QWheelEvent* e) {
    const int d = e->angleDelta().y();
    if (d == 0) {
        QWidget::wheelEvent(e);
        return;
    }
    m_zoom = qBound(0.3, m_zoom * (d > 0 ? 1.1 : 0.9), 6.0);
    applyZoom();
    e->accept();
}

bool TextPinWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == m_edit || (m_edit && o == m_edit->viewport())) {
        if (e->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Escape) {
                close();
                return true;
            }
        } else if (e->type() == QEvent::Wheel) {
            auto* we = static_cast<QWheelEvent*>(e);
            if (we->modifiers() & Qt::ControlModifier) {
                const int d = we->angleDelta().y();
                if (d != 0) {
                    m_zoom = qBound(0.3, m_zoom * (d > 0 ? 1.1 : 0.9), 6.0);
                    applyZoom();
                    return true;
                }
            }
        }
    }
    if (e->type() == QEvent::MouseButtonPress && m_title) {
        auto* me = static_cast<QMouseEvent*>(e);
        QWidget* w = qobject_cast<QWidget*>(o);
        if (me->button() == Qt::LeftButton &&
            (w == m_title || w == m_title->parentWidget())) {
            if (windowHandle())
                windowHandle()->startSystemMove();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void TextPinWindow::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    menu.addAction(QStringLiteral("复制全部文字"), this, [this]() {
        QApplication::clipboard()->setText(m_edit->toPlainText());
    });
    menu.addAction(QStringLiteral("全选"), this, [this]() { m_edit->selectAll(); });
    menu.addAction(QStringLiteral("保存文字…"), this, [this]() {
        Settings st = Settings::load();
        QDir().mkpath(st.saveDir);
        const QString fn = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存文字"),
            st.saveDir + "/Snip_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".txt",
            QStringLiteral("文本 (*.txt)"));
        if (!fn.isEmpty()) {
            QFile f(fn);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(m_edit->toPlainText().toUtf8());
        }
    });
    menu.addSeparator();
    QAction* border = menu.addAction(QStringLiteral("显示边框"));
    border->setCheckable(true);
    border->setChecked(m_border);
    connect(border, &QAction::toggled, this, [this](bool on) { m_border = on; update(); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("关闭"), this, &QWidget::close);
    menu.exec(e->globalPos());
}

void PinManager::registerPin(QWidget* w) {
    if (!w)
        return;
    m_pins.append(w);
    connect(w, &QObject::destroyed, this, [this, w]() { m_pins.removeAll(w); });
}
