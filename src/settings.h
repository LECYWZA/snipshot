#pragma once

#include <QString>

// 配置项：OpenAI 兼容 API + 保存目录 + 快捷键
struct Settings {
    QString apiBase  = "http://spark-1:8888/v1";
    QString apiKey;   // 敏感信息不写死进仓库
    QString model    = "deepseek-v4.1-flash";
    QString saveDir;  // 默认 ~/图片/截图
    // 快捷键（仅用于展示/Windows 注册；Linux Wayland 走 GNOME 自定义快捷键）
    QString hkSnip       = "F1";
    QString hkPin        = "F2";
    QString hkTogglePins = "F3";
    QString hkTranslate  = "Alt+Q";
    QString hkOcr        = "Alt+W";
    bool pinBorder       = true;   // 新贴图默认是否显示边框

    // 剪贴板管理器
    bool clipboardEnabled = true;
    int  clipboardMax     = 100;
    QString hkClipboard   = "Alt+V";

    // 屏幕录制
    bool recordingEnabled = true;
    int  recordFps        = 30;
    bool recordCursor     = true;
    QString recordDir;             // 默认 ~/视频/录屏
    QString hkRecord      = "Alt+R";

    static Settings load();
    void save() const;
    static QString defaultSaveDir();
    static QString defaultRecordDir();
};
