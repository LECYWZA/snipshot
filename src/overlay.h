#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>
#include <QPoint>
#include <QList>
#include <QColor>
#include <QFont>
#include <QString>

class QScreen;

// 全屏遮罩 / 标注编辑器。
// - Snip/Translate/Ocr：覆盖整个虚拟桌面，显示定格截图，框选后可标注。
// - Translate/Ocr：框选松开后自动翻译/OCR。
// - Edit：固定在某个贴图位置的小窗口，直接对已有贴图继续绘制。
class Overlay : public QWidget {
    Q_OBJECT
public:
    enum Mode { Snip, Translate, Ocr };

    // 每块屏一个全屏遮罩（Wayland 下唯一可靠的方式）
    explicit Overlay(const QImage& fullImage, QScreen* screen, Mode mode, QWidget* parent = nullptr);
    // 全虚拟桌面遮罩模式（调试/兜底）
    explicit Overlay(const QImage& screenshot, Mode mode, QWidget* parent = nullptr);
    // 贴图编辑模式（窗口大小/位置固定，图片铺满窗口）
    Overlay(const QImage& image, const QRect& logicalGeometry, bool editMode,
            QWidget* parent = nullptr);
    ~Overlay() override;

    bool hasSelection() const;
    bool isEdit() const { return m_edit; }
    Mode mode() const { return m_mode; }
    QRect selectionGlobalRect() const;
    QImage baseImage() const { return m_base; }
    QImage currentResult() const;   // 含标注，用于贴图/复制/保存
    void setProcessing(bool on);

signals:
    void saveRequested(const QImage& result, const QRect& logicalRect);
    void copyRequested(const QImage& result, const QRect& logicalRect);
    void pinRequested(const QImage& result, const QRect& logicalRect);
    void translateRequested(const QImage& result, const QRect& logicalRect);
    void ocrRequested(const QImage& result, const QRect& logicalRect);
    void selectionChanged(const QImage& result, const QRect& logicalRect);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void syncGeometry();

private:
    enum Tool { Rect = 0, Ellipse, Arrow, Pen, Mosaic, Text };
    struct Ann {
        Tool tool = Rect;
        QRect rect;
        QPoint a, b;
        QList<QPoint> path;
        QColor color;
        int width = 3;
        QString text;
    };

    void startSelection(QPoint p);
    void updateSelection(QPoint p);
    void endSelection();
    void startAnnotation(QPoint p);
    void updateAnnotation(QPoint p);

    QRect selectedRect() const;
    QRect sourceRectFor(const QRect& sel) const;

    QRect barRect(bool tools) const;
    int hitBar(QPoint p, bool tools) const;
    void drawBar(QPainter& p, bool tools);
    void handleAction(int idx);
    void showContextMenu(const QPoint& globalPos);

    void drawAnnotations(QPainter& p) const;
    void emitCurrentIfSnip();

    QImage m_base;
    Mode m_mode = Snip;
    bool m_edit = false;
    QRect m_desk;           // 截图对应的逻辑桌面区域
    QRect m_winRect;        // 逻辑窗口区域
    QPoint m_origin;        // 逻辑全局原点（窗口实际位置）

    QRect m_sel;            // 窗口内选区
    bool m_selecting = false;
    bool m_hasSel = false;
    QPoint m_start;

    Tool m_tool = Rect;
    QList<Ann> m_anns;
    bool m_drawing = false;
    bool m_processing = false;
};
