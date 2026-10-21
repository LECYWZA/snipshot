#pragma once

#include <QObject>
#include <QImage>
#include <QPoint>
#include <QList>
#include <QWidget>
#include <QString>
#include <QSize>

class QTextEdit;
class QLabel;
class Ai;

// 无边框置顶贴图窗（Snipaste 的"贴图"）
class PinWindow : public QWidget {
    Q_OBJECT
public:
    PinWindow(const QImage& img, const QPoint& pos, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    QImage m_img;
    QSize m_baseLogical;
    qreal m_zoom = 1.0;
    QPoint m_dragOffset;
    bool m_dragging = false;
    QString m_savedPath;
    bool m_border = true;
};

// AI 结果贴图：先固定贴出原图并显示“翻译中/识别中”，完成后变成可编辑文本域
class TextPinWindow : public QWidget {
    Q_OBJECT
public:
    TextPinWindow(const QImage& img, const QRect& logicalRect, const QString& mode,
                  QWidget* parent = nullptr);
    ~TextPinWindow() override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void setResultText(const QString& text);
    void applyZoom();

    QImage m_img;
    QSize m_baseLogical;
    qreal m_zoom = 1.0;
    int m_baseFont = 14;
    QLabel* m_title = nullptr;
    QString m_mode;
    bool m_border = true;
    bool m_done = false;
    QTextEdit* m_edit = nullptr;
    Ai* m_ai = nullptr;
};

// 管理所有贴图窗：F3 一键隐藏/显示
class PinManager : public QObject {
    Q_OBJECT
public:
    static PinManager* instance();
    void addPin(const QImage& img);                   // 居中贴出
    void addPin(const QImage& img, const QPoint& pos); // 在逻辑坐标 pos 贴出
    void registerPin(QWidget* w);                     // 托管已有贴图窗（如文本结果贴图）
    void toggleAll();
    void closeAll();
    int count() const { return m_pins.size(); }

private:
    QList<QWidget*> m_pins;
};
