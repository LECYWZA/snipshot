#include <QWindow>
#include "pin.h"
#include "ai.h"
#include "overlay.h"
#include "settings.h"
#include "screenutil.h"

#include <QPainter>
#include <QMouseEvent>
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

// ---------------- AI 结果贴图：固定贴出，完成后可编辑文本 ----------------

TextPinWindow::TextPinWindow(const QImage& img, const QRect& logicalRect, const QString& mode,
                             QWidget* parent)
    : QWidget(parent), m_img(img), m_mode(mode) {
    double d = img.devicePixelRatio();
    if (d <= 0)
        d = 1.0;
    QSize logical(qRound(img.width() / d), qRound(img.height() / d));
    if (logical.width() < 380) logical.setWidth(380);
    if (logical.height() < 220) logical.setHeight(220);
    m_logical = logical;
    m_border = Settings::load().pinBorder;

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(mode == QStringLiteral("translate") ? QStringLiteral("翻译")
                                                      : QStringLiteral("文字提取"));
    setFixedSize(m_logical);
    if (logicalRect.isValid())
        move(logicalRect.topLeft());

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    auto* header = new QWidget;
    header->setStyleSheet("QWidget { background: rgba(0,0,0,140); }");
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(4, 2, 4, 2);
    auto* title = new QLabel(mode == QStringLiteral("translate") ? QStringLiteral("翻译")
                                                                : QStringLiteral("文字提取"));
    title->setStyleSheet("QLabel { color:#9fd8ff; font-weight:bold; }");
    auto* copyBtn = new QPushButton(QStringLiteral("复制"));
    auto* closeBtn = new QPushButton(QStringLiteral("×"));
    copyBtn->setFixedWidth(52);
    closeBtn->setFixedWidth(30);
    hl->addWidget(title);
    hl->addStretch();
    hl->addWidget(copyBtn);
    hl->addWidget(closeBtn);
    // 标题区拖动
    title->installEventFilter(this);
    lay->addWidget(header);

    m_edit = new QTextEdit;
    m_edit->setReadOnly(true);
    m_edit->setPlaceholderText(mode == QStringLiteral("translate")
                                   ? QStringLiteral("翻译中…")
                                   : QStringLiteral("识别中…"));
    m_edit->setStyleSheet(
        "QTextEdit { background: rgba(0,0,0,150); color:#f2f2f2; border:1px solid #00a0ff; "
        "selection-background-color:#0078d7; }");
    m_edit->installEventFilter(this);
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
    m_edit->setReadOnly(false);
    m_edit->setPlainText(text);
    m_edit->moveCursor(QTextCursor::End);
    m_edit->setFocus();
    raise();
    activateWindow();
}

void TextPinWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawImage(rect(), m_img);
    p.fillRect(rect(), m_border ? QColor(0, 0, 0, 70) : QColor(0, 0, 0, 50));
    if (m_border) {
        p.setPen(QPen(QColor(0, 160, 255), 2));
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}

void TextPinWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool TextPinWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == m_edit && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }
    if (o == m_edit || (o && o->inherits("QLabel"))) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (o != m_edit && me->button() == Qt::LeftButton && windowHandle()) {
                windowHandle()->startSystemMove();
                return true;
            }
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
