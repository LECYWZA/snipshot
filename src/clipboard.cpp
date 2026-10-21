#include "clipboard.h"
#include "settings.h"

#include <QApplication>
#include <QClipboard>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QBuffer>
#include <QTimer>
#include <QDebug>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QIcon>
#include <QPixmap>
#include <QKeyEvent>
#include <QFileSystemWatcher>
#include <QJsonParseError>
#include <QMimeData>

// ---------------- 历史窗口 ----------------

class ClipboardWindow : public QDialog {
    Q_OBJECT
public:
    explicit ClipboardWindow(ClipboardManager* mgr, QWidget* parent = nullptr)
        : QDialog(parent), m_mgr(mgr) {
        setWindowTitle(QStringLiteral("SnipShot 剪贴板历史"));
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
        resize(620, 680);

        auto* top = new QHBoxLayout;
        m_search = new QLineEdit;
        m_search->setPlaceholderText(QStringLiteral("搜索文本…"));
        top->addWidget(m_search, 1);
        m_status = new QLabel;
        m_status->setStyleSheet("QLabel { color:#888; }");
        top->addWidget(m_status);

        m_list = new QListWidget;
        m_list->setIconSize(QSize(72, 48));
        m_list->setWordWrap(true);

        auto* btns = new QHBoxLayout;
        m_copy = new QPushButton(QStringLiteral("复制"));
        m_pin = new QPushButton(QStringLiteral("固定 / 取消固定"));
        m_del = new QPushButton(QStringLiteral("删除"));
        m_clear = new QPushButton(QStringLiteral("清空未固定"));
        auto* close = new QPushButton(QStringLiteral("关闭"));
        btns->addWidget(m_copy);
        btns->addWidget(m_pin);
        btns->addWidget(m_del);
        btns->addWidget(m_clear);
        btns->addStretch();
        btns->addWidget(close);

        auto* lay = new QVBoxLayout(this);
        lay->addLayout(top);
        lay->addWidget(m_list, 1);
        lay->addLayout(btns);

        connect(m_search, &QLineEdit::textChanged, this, [this]() { refresh(); });
        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { doCopy(); });
        connect(m_copy, &QPushButton::clicked, this, [this]() { doCopy(); });
        connect(m_pin, &QPushButton::clicked, this, [this]() {
            if (auto* it = m_list->currentItem())
                m_mgr->togglePinned(it->data(Qt::UserRole).toInt());
        });
        connect(m_del, &QPushButton::clicked, this, [this]() {
            if (auto* it = m_list->currentItem())
                m_mgr->removeItem(it->data(Qt::UserRole).toInt());
        });
        connect(m_clear, &QPushButton::clicked, this, [this]() { m_mgr->clearUnpinned(); });
        connect(close, &QPushButton::clicked, this, &QWidget::close);
        connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() { updateButtons(); });
        refresh();
        m_search->setFocus();
    }

    void refresh() {
        const QString q = m_search->text().trimmed();
        m_list->clear();
        const auto& items = m_mgr->items();
        int shown = 0, pinned = 0;
        for (int i = 0; i < items.size(); ++i) {
            const ClipItem& c = items[i];
            if (c.pinned)
                ++pinned;
            if (!q.isEmpty()) {
                const QString hay = c.kind == ClipItem::Text ? c.text : QStringLiteral("图片");
                if (!hay.contains(q, Qt::CaseInsensitive))
                    continue;
            }
            QString label;
            if (c.kind == ClipItem::Text)
                label = c.text.simplified().left(140);
            else
                label = QStringLiteral("[图片] ") + QFileInfo(c.imagePath).fileName();
            if (c.pinned)
                label.prepend(QStringLiteral("★ "));
            label += QStringLiteral("    ") + c.time.toString("MM-dd HH:mm");
            auto* it = new QListWidgetItem(label);
            it->setData(Qt::UserRole, i);
            if (c.kind == ClipItem::Image) {
                QPixmap pm(c.imagePath);
                if (!pm.isNull())
                    it->setIcon(QIcon(pm.scaled(72, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            } else {
                it->setIcon(QIcon::fromTheme("text-x-generic"));
            }
            m_list->addItem(it);
            ++shown;
        }
        m_status->setText(QString("共 %1 · 固定 %2 · 显示 %3").arg(items.size()).arg(pinned).arg(shown));
        updateButtons();
    }

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            close();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (m_list->hasFocus()) {
                doCopy();
                return;
            }
        }
        QDialog::keyPressEvent(e);
    }

private:
    void doCopy() {
        auto* it = m_list->currentItem();
        if (!it)
            return;
        m_mgr->copyItem(it->data(Qt::UserRole).toInt());
        close();
    }
    void updateButtons() {
        const bool has = m_list->currentItem() != nullptr;
        m_copy->setEnabled(has);
        m_pin->setEnabled(has);
        m_del->setEnabled(has);
    }

    ClipboardManager* m_mgr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_copy = nullptr;
    QPushButton* m_pin = nullptr;
    QPushButton* m_del = nullptr;
    QPushButton* m_clear = nullptr;
};

// ---------------- 管理器 ----------------

ClipboardManager::ClipboardManager(QObject* parent) : QObject(parent) {}

ClipboardManager::~ClipboardManager() {
    stop();
}

QString ClipboardManager::storeDir() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + "/clipboard";
}

void ClipboardManager::start() {
    const Settings st = Settings::load();
    if (!st.clipboardEnabled) {
        stop();
        return;
    }
    if (!m_running) {
        QDir().mkpath(storeDir());
        load();
        m_running = true;
        if (extensionInstalled()) {
            startRegistrySync();
        } else {
            startWatchers();
        }
    }
    emit changed();
}

void ClipboardManager::stop() {
    stopWatchers();
    stopRegistrySync();
    m_running = false;
    if (m_window)
        m_window->close();
}

void ClipboardManager::reloadSettings() {
    const Settings st = Settings::load();
    if (!st.clipboardEnabled) {
        stop();
        return;
    }
    if (!m_running) {
        start();
    } else if (m_poll) {
        syncRegistry();
    } else {
        startWatchers();
    }
    enforceLimit();
    emit changed();
}

void ClipboardManager::startWatchers() {
    stopWatchers();
    const QString wl = QStandardPaths::findExecutable("wl-paste");
    if (wl.isEmpty()) {
        // 没有 wl-clipboard 时退化为前台 QClipboard 监听（仅本应用有焦点时可靠）
        connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
            const QMimeData* mime = QApplication::clipboard()->mimeData();
            if (!mime)
                return;
            if (mime->hasImage()) {
                const QImage img = qvariant_cast<QImage>(mime->imageData());
                if (!img.isNull())
                    addImage(img);
            } else if (mime->hasText()) {
                addText(mime->text());
            }
        }, Qt::UniqueConnection);
        emit error(QStringLiteral("未找到 wl-paste，剪贴板只在 SnipShot 有焦点时记录"));
        return;
    }

    const QString app = QCoreApplication::applicationFilePath();
    auto makeScript = [app](const QString& type) {
        return QString(
            "f=$(mktemp) || exit 0\n"
            "cat > \"$f\"\n"
            "\"%1\" --clip-file=%2:\"$f\" >/dev/null 2>&1\n")
            .arg(app, type);
    };

    m_textWatch = new QProcess(this);
    m_textWatch->setProgram(wl);
    m_textWatch->setArguments({"--watch", "sh", "-c", makeScript("text")});
    m_textWatch->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_textWatch, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_textWatch)
            emit error(QStringLiteral("wl-paste 文本监听失败: ") + m_textWatch->errorString());
    });
    m_textWatch->start();

    m_imageWatch = new QProcess(this);
    m_imageWatch->setProgram(wl);
    m_imageWatch->setArguments({"--type", "image/png", "--watch", "sh", "-c", makeScript("image")});
    m_imageWatch->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_imageWatch, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_imageWatch)
            emit error(QStringLiteral("wl-paste 图片监听失败: ") + m_imageWatch->errorString());
    });
    m_imageWatch->start();
}

void ClipboardManager::stopWatchers() {
    if (m_textWatch) {
        m_textWatch->disconnect(this);
        m_textWatch->kill();
        m_textWatch->deleteLater();
        m_textWatch = nullptr;
    }
    if (m_imageWatch) {
        m_imageWatch->disconnect(this);
        m_imageWatch->kill();
        m_imageWatch->deleteLater();
        m_imageWatch = nullptr;
    }
    disconnect(QApplication::clipboard(), nullptr, this, nullptr);
}

QString ClipboardManager::extensionCacheDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
           "/clipboard-indicator@tudmotu.com";
}

QString ClipboardManager::extensionRegistryPath() const {
    return extensionCacheDir() + "/registry.txt";
}

bool ClipboardManager::extensionInstalled() const {
    const QString uuid = QStringLiteral("clipboard-indicator@tudmotu.com");
    return QFileInfo::exists(QDir::homePath() + "/.local/share/gnome-shell/extensions/" + uuid) ||
           QFileInfo::exists("/usr/share/gnome-shell/extensions/" + uuid);
}

void ClipboardManager::startRegistrySync() {
    stopWatchers();
    stopRegistrySync();
    QDir().mkpath(extensionCacheDir());
    m_registryPath = extensionRegistryPath();
    m_watch = new QFileSystemWatcher(this);
    // 目录和文件都监听：registry.txt 可能尚未创建
    if (QFileInfo::exists(m_registryPath))
        m_watch->addPath(m_registryPath);
    if (QFileInfo::exists(extensionCacheDir()))
        m_watch->addPath(extensionCacheDir());
    connect(m_watch, &QFileSystemWatcher::fileChanged, this, [this]() {
        syncRegistry();
        if (QFileInfo::exists(m_registryPath) && !m_watch->files().contains(m_registryPath))
            m_watch->addPath(m_registryPath);
    });
    connect(m_watch, &QFileSystemWatcher::directoryChanged, this, [this]() {
        if (QFileInfo::exists(m_registryPath) && !m_watch->files().contains(m_registryPath))
            m_watch->addPath(m_registryPath);
        syncRegistry();
    });
    m_poll = new QTimer(this);
    m_poll->setInterval(1500);
    connect(m_poll, &QTimer::timeout, this, &ClipboardManager::syncRegistry);
    m_poll->start();
    syncRegistry();
    emit error(QStringLiteral("剪贴板：使用 clipboard-indicator 扩展历史作为后台捕获源"));
}

void ClipboardManager::stopRegistrySync() {
    if (m_poll) {
        m_poll->stop();
        m_poll->deleteLater();
        m_poll = nullptr;
    }
    if (m_watch) {
        m_watch->deleteLater();
        m_watch = nullptr;
    }
}

void ClipboardManager::syncRegistry() {
    if (!m_running)
        return;
    QFile f(m_registryPath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return;
    const QJsonArray arr = doc.array();
    const Settings st = Settings::load();
    const int limit = qMin(arr.size(), qMax(20, st.clipboardMax + 20));
    const int before = m_items.size();
    m_syncing = true;
    // 扩展 registry 为新到旧；只取最新的 limit 条，倒序加入，让最新排最前
    for (int i = limit - 1; i >= 0; --i) {
        const QJsonObject o = arr[i].toObject();
        const QString mime = o.value("mimetype").toString();
        const QString contents = o.value("contents").toString();
        const bool fav = o.value("favorite").toBool();
        if (mime.startsWith("text/")) {
            if (!contents.trimmed().isEmpty())
                addText(contents, fav);
        } else if (mime.startsWith("image/")) {
            if (m_extLoaded.contains(contents))
                continue;
            const QString path = extensionCacheDir() + "/" + contents;
            QImage img(path);
            if (!img.isNull())
                addImage(img, fav);
            m_extLoaded.insert(contents);
        }
    }
    m_syncing = false;
    if (m_items.size() != before) {
        enforceLimit();
        save();
        emit changed();
        refreshWindow();
    }
}

void ClipboardManager::addFile(const QString& type, const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        f.remove();
        return;
    }
    const QByteArray data = f.readAll();
    f.close();
    f.remove();
    if (type == QStringLiteral("image")) {
        const QImage img = QImage::fromData(data, "PNG");
        if (!img.isNull())
            addImage(img);
    } else {
        const QString text = QString::fromUtf8(data);
        if (!text.trimmed().isEmpty())
            addText(text);
    }
}

void ClipboardManager::addText(const QString& text, bool pinned) {
    const QString t = text;
    if (t.trimmed().isEmpty())
        return;
    const QByteArray hash = QCryptographicHash::hash(t.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString h = QString::fromLatin1(hash);
    if (m_hidden.contains(h))
        return;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].hash == h) {
            if (m_syncing)
                return;
            ClipItem it = m_items.takeAt(i);
            it.time = QDateTime::currentDateTime();
            m_items.prepend(it);
            enforceLimit();
            save();
            emit changed();
            refreshWindow();
            return;
        }
    }
    ClipItem it;
    it.kind = ClipItem::Text;
    it.text = t;
    it.time = QDateTime::currentDateTime();
    it.hash = h;
    it.pinned = pinned;
    m_items.prepend(it);
    if (m_syncing)
        return;
    enforceLimit();
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::addImage(const QImage& img, bool pinFlag) {
    if (img.isNull())
        return;
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    const QString h = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (m_hidden.contains(h))
        return;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].hash == h) {
            if (m_syncing)
                return;
            ClipItem it = m_items.takeAt(i);
            it.time = QDateTime::currentDateTime();
            m_items.prepend(it);
            enforceLimit();
            save();
            emit changed();
            refreshWindow();
            return;
        }
    }
    QDir().mkpath(storeDir());
    const QString path = storeDir() + "/" + h + ".png";
    if (!QFile::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return;
        f.write(bytes);
        f.close();
    }
    ClipItem it;
    it.kind = ClipItem::Image;
    it.imagePath = path;
    it.time = QDateTime::currentDateTime();
    it.hash = h;
    it.pinned = pinFlag;
    m_items.prepend(it);
    if (m_syncing)
        return;
    enforceLimit();
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::enforceLimit() {
    const Settings st = Settings::load();
    int max = qMax(10, st.clipboardMax);
    int count = 0;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].pinned)
            continue;
        ++count;
        if (count > max) {
            deleteImageFile(m_items[i]);
            m_items.removeAt(i);
        }
    }
}

void ClipboardManager::deleteImageFile(const ClipItem& it) {
    if (it.kind == ClipItem::Image && !it.imagePath.isEmpty()) {
        // 还有别的条目引用同一文件就不删
        for (const ClipItem& o : m_items) {
            if (&o != &it && o.imagePath == it.imagePath)
                return;
        }
        QFile::remove(it.imagePath);
    }
}

void ClipboardManager::load() {
    m_items.clear();
    m_hidden.clear();
    QFile f(storeDir() + "/clipboard.json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    for (const QJsonValue& v : doc.object().value("items").toArray()) {
        const QJsonObject o = v.toObject();
        ClipItem it;
        it.kind = o.value("kind").toString() == "image" ? ClipItem::Image : ClipItem::Text;
        it.text = o.value("text").toString();
        it.imagePath = o.value("image").toString();
        it.hash = o.value("hash").toString();
        it.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
        it.pinned = o.value("pinned").toBool();
        if (it.kind == ClipItem::Image && !QFile::exists(it.imagePath))
            continue;
        m_items.append(it);
    }
    for (const QJsonValue& v : doc.object().value("hidden").toArray())
        m_hidden.insert(v.toString());
    enforceLimit();
}

void ClipboardManager::save() {
    QDir().mkpath(storeDir());
    QJsonArray arr;
    for (const ClipItem& it : m_items) {
        QJsonObject o;
        o["kind"] = it.kind == ClipItem::Image ? "image" : "text";
        o["text"] = it.text;
        o["image"] = it.imagePath;
        o["hash"] = it.hash;
        o["time"] = it.time.toString(Qt::ISODate);
        o["pinned"] = it.pinned;
        arr.append(o);
    }
    QJsonArray hiddenArr;
    for (const QString& h : m_hidden)
        hiddenArr.append(h);
    QJsonObject root;
    root["items"] = arr;
    root["hidden"] = hiddenArr;
    QSaveFile f(storeDir() + "/clipboard.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void ClipboardManager::scheduleSave() {
    if (m_savePending)
        return;
    m_savePending = true;
    QTimer::singleShot(150, this, [this]() {
        m_savePending = false;
        save();
    });
}

void ClipboardManager::openWindow() {
    if (!m_running)
        start();
    if (m_window) {
        m_window->refresh();
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
        return;
    }
    m_window = new ClipboardWindow(this);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void ClipboardManager::refreshWindow() {
    if (m_window)
        m_window->refresh();
}

void ClipboardManager::copyItem(int index) {
    if (index < 0 || index >= m_items.size())
        return;
    const ClipItem& it = m_items[index];
    if (it.kind == ClipItem::Text) {
        QApplication::clipboard()->setText(it.text);
    } else {
        const QImage img(it.imagePath);
        if (!img.isNull())
            QApplication::clipboard()->setImage(img);
    }
    emit changed();
}

void ClipboardManager::togglePinned(int index) {
    if (index < 0 || index >= m_items.size())
        return;
    m_items[index].pinned = !m_items[index].pinned;
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::removeItem(int index) {
    if (index < 0 || index >= m_items.size())
        return;
    m_hidden.insert(m_items[index].hash);
    deleteImageFile(m_items[index]);
    m_items.removeAt(index);
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::clearUnpinned() {
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (!m_items[i].pinned) {
            m_hidden.insert(m_items[i].hash);
            deleteImageFile(m_items[i]);
            m_items.removeAt(i);
        }
    }
    save();
    emit changed();
    refreshWindow();
}

#include "clipboard.moc"
