#pragma once
// Theme — 深色/浅色外观
//   默认 auto：读取系统设置（Windows: HKCU\...\Personalize\AppsUseLightTheme）
//   可用命令行 --theme dark|light|auto 覆盖
#include <QString>

class QWidget;

namespace Theme {

enum class Mode { Auto, Light, Dark };

void setMode(Mode m);
Mode mode();
bool modeFromString(const QString &s, Mode *out);

// 系统当前是否为深色模式
bool systemPrefersDark();

// 结合 --theme 覆盖后的最终取值
bool effectiveDark();

QString styleSheet(bool dark);
void apply(QWidget *w, bool dark);
void applyDarkTitleBar(QWidget *w, bool dark);

} // namespace Theme
