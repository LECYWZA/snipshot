#include "ai.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QBuffer>
#include <QUrl>
#include "settings.h"

Ai::Ai(QObject* parent) : QObject(parent) {}

QJsonObject Ai::baseRequest(const QImage& img, const QString& prompt) const {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    const QString dataUri = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());

    QJsonObject textPart{{"type", "text"}, {"text", prompt}};
    QJsonObject imagePart{{"type", "image_url"}, {"image_url", QJsonObject{{"url", dataUri}}}};
    QJsonArray content{textPart, imagePart};
    QJsonObject msg{{"role", "user"}, {"content", content}};
    QJsonArray messages{msg};
    return QJsonObject{
        {"model", Settings::load().model},
        {"messages", messages},
        {"temperature", 0.1},
        {"max_tokens", 2000}
    };
}

void Ai::recognize(const QImage& img, const QString& mode) {
    Settings st = Settings::load();
    QString prompt;
    if (mode == "ocr") {
        prompt = QStringLiteral(
            "请提取图片中的全部文字，保持原来的段落与顺序。直接输出文字内容，不要任何解释、不要 markdown 代码块。");
    } else {
        prompt = QStringLiteral(
            "请识别图片中的文字并翻译：原文是中文则翻译成英文，原文是其他语言则翻译成简体中文。"
            "只输出译文，不要输出原文，不要任何解释、不要 markdown 代码块。");
    }

    auto* nam = new QNetworkAccessManager(this);
    QUrl url(st.apiBase + "/chat/completions");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + st.apiKey.toUtf8());
    const QJsonObject body = baseRequest(img, prompt);
    QNetworkReply* reply = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, mode]() {
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QString("API 请求失败: ") + reply->errorString());
        } else {
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            const QJsonObject obj = doc.object();
            const QJsonArray choices = obj.value("choices").toArray();
            if (choices.isEmpty()) {
                emit error("API 返回为空: " + QString::fromUtf8(raw.left(300)));
            } else {
                const QJsonObject m = choices.first().toObject().value("message").toObject();
                QString text = m.value("content").toString();
                if (text.trimmed().isEmpty())
                    text = m.value("reasoning_content").toString();
                emit done(text.trimmed());
            }
        }
        reply->deleteLater();
        nam->deleteLater();
    });
    Q_UNUSED(mode);
}
