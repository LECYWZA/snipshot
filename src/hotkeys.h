#pragma once

#include <QString>
#include "settings.h"

// 全局快捷键：
// - Linux/Wayland：应用无法注册全局热键，唯一可行方式是写 GNOME 自定义快捷键
//   （gsettings media-keys custom-keybindings），绑定命令行参数来唤醒常驻进程。
// - Windows：RegisterHotKey（后续实现）。
namespace Hotkeys {
bool installGnome(const Settings& st);   // 注册 5 个快捷键到 GNOME
QString executablePath();                 // 当前程序路径
}
