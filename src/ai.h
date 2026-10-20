#pragma once

#include <QString>
#include <QImage>
#include <QJsonObject>

// OpenAI 兼容接口调用：识图（OCR 文字提取）与翻译共用一套 API
class Ai : public QObject {
    Q_OBJECT
public:
    explicit Ai(QObject* parent = nullptr);

    // mode: "ocr" 只提取文字；"translate" 提取并翻译（中文→英文，其他→中文）
    void recognize(const QImage& img, const QString& mode);

signals:
    void done(const QString& text);
    void error(const QString& message);

private:
    QJsonObject baseRequest(const QImage& img, const QString& prompt) const;
};
