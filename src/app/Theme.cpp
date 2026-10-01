#include "Theme.h"

#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>
#include <QWidget>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

namespace {

Theme::Mode g_mode = Theme::Mode::Auto;

struct Colors {
    const char *bg, *card, *border, *text, *textDim, *hint, *title;
    const char *primary, *primaryHover, *primaryText, *primaryDisabled, *primaryDisabledText;
    const char *listBg, *listSel, *listSelText, *btnHover, *disabledText, *disabledBg;
    const char *iconBg, *ok, *err, *info, *link;
};

const Colors kLight = {
    "#f5f6f8", "#ffffff", "#e2e6ec", "#1f2430", "#5a6472", "#7a8494", "#1f2430",
    "#1a73e8", "#1567d3", "#ffffff", "#a9c6f0", "#f0f5ff",
    "#ffffff", "#e3f0ff", "#10406e", "#f0f4fa", "#a9b2bf", "#f2f3f5",
    "#eef1f5", "#2e7d32", "#c62828", "#10406e", "#1a73e8",
};

const Colors kDark = {
    "#1b1d21", "#26292e", "#3a3f47", "#e6e8ec", "#a9b0bb", "#8b93a0", "#f0f2f5",
    "#3b82f6", "#5591f7", "#ffffff", "#3d5f8f", "#dfe8f5",
    "#26292e", "#2c3a4f", "#cfe3ff", "#31353c", "#6b7280", "#2a2d33",
    "#32363d", "#66bb6a", "#ef5350", "#8ab4f8", "#6aa9ff",
};

const char *kTemplate = R"QSS(
QWidget#root { background: %bg%; }
QLabel#title { font-size: 20px; font-weight: 600; color: %title%; }
QLabel#detail { color: %textDim%; font-size: 12px; }
QLabel#hint { color: %hint%; font-size: 12px; }
QLabel#sectionTitle { font-size: 14px; font-weight: 600; color: %title%; }
QLabel#status { color: %ok%; font-size: 12px; }
QLabel#statusError { color: %err%; font-size: 12px; }
QLabel#assoc { color: %textDim%; font-size: 12px; }
QLabel#defaultInfo { color: %info%; font-size: 12px; }
QLabel#apkIcon { background: %iconBg%; border-radius: 12px; }
QFrame#card { background: %card%; border: 1px solid %border%; border-radius: 10px; }
QListWidget { background: %listBg%; border: 1px solid %border%; border-radius: 8px; padding: 4px; font-size: 13px; color: %text%; }
QListWidget::item { padding: 6px; border-radius: 6px; }
QListWidget::item:selected { background: %listSel%; color: %listSelText%; }
QPushButton { background: %card%; border: 1px solid %border%; border-radius: 7px; padding: 7px 14px; font-size: 13px; color: %text%; }
QPushButton:hover { background: %btnHover%; }
QPushButton:disabled { color: %disabledText%; background: %disabledBg%; }
QPushButton#primary { background: %primary%; border: 1px solid %primary%; color: %primaryText%; font-weight: 600; }
QPushButton#primary:hover { background: %primaryHover%; }
QPushButton#primary:disabled { background: %primaryDisabled%; border-color: %primaryDisabled%; color: %primaryDisabledText%; }
)QSS";

} // namespace

namespace Theme {

void setMode(Mode m) { g_mode = m; }
Mode mode() { return g_mode; }

bool modeFromString(const QString &s, Mode *out)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("auto")) { if (out) *out = Mode::Auto; return true; }
    if (v == QLatin1String("light")) { if (out) *out = Mode::Light; return true; }
    if (v == QLatin1String("dark")) { if (out) *out = Mode::Dark; return true; }
    return false;
}

bool systemPrefersDark()
{
#ifdef Q_OS_WIN
    // 应用主题：AppsUseLightTheme = 0 表示深色
    // APKICONSHOWER_PERSONALIZE_KEY 可把读取位置指向别的键，供自动化测试使用（不改动真实系统设置）
    QString key = qEnvironmentVariable("APKICONSHOWER_PERSONALIZE_KEY");
    if (key.isEmpty())
        key = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
    QSettings personalize(key, QSettings::NativeFormat);
    const QVariant v = personalize.value(QStringLiteral("AppsUseLightTheme"));
    if (v.isValid()) return v.toInt() == 0;
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) return true;
#endif
    return false;
}

bool effectiveDark()
{
    switch (g_mode) {
    case Mode::Light: return false;
    case Mode::Dark: return true;
    case Mode::Auto: break;
    }
    return systemPrefersDark();
}

QString styleSheet(bool dark)
{
    const Colors &c = dark ? kDark : kLight;
    QString qss = QString::fromUtf8(kTemplate);
    const struct { const char *token; const char *value; } kVars[] = {
        {"%bg%", c.bg}, {"%card%", c.card}, {"%border%", c.border}, {"%text%", c.text},
        {"%textDim%", c.textDim}, {"%hint%", c.hint}, {"%title%", c.title},
        {"%primary%", c.primary}, {"%primaryHover%", c.primaryHover}, {"%primaryText%", c.primaryText},
        {"%primaryDisabled%", c.primaryDisabled}, {"%primaryDisabledText%", c.primaryDisabledText},
        {"%listBg%", c.listBg}, {"%listSel%", c.listSel}, {"%listSelText%", c.listSelText},
        {"%btnHover%", c.btnHover}, {"%disabledText%", c.disabledText}, {"%disabledBg%", c.disabledBg},
        {"%iconBg%", c.iconBg}, {"%ok%", c.ok}, {"%err%", c.err}, {"%info%", c.info},
    };
    for (const auto &v : kVars) qss.replace(QLatin1String(v.token), QLatin1String(v.value));
    return qss;
}

void applyDarkTitleBar(QWidget *w, bool dark)
{
#ifdef Q_OS_WIN
    if (!w) return;
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) return;
    const BOOL value = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE（Win10 2004+）；19 为更早的 1809/1903
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value))))
        DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
#else
    Q_UNUSED(w);
    Q_UNUSED(dark);
#endif
}

void apply(QWidget *w, bool dark)
{
    if (!w) return;
    const Colors &c = dark ? kDark : kLight;

    QPalette pal = w->palette();
    pal.setColor(QPalette::Window, QColor(QString::fromLatin1(c.bg)));
    pal.setColor(QPalette::Base, QColor(QString::fromLatin1(c.listBg)));
    pal.setColor(QPalette::AlternateBase, QColor(QString::fromLatin1(c.card)));
    pal.setColor(QPalette::Text, QColor(QString::fromLatin1(c.text)));
    pal.setColor(QPalette::WindowText, QColor(QString::fromLatin1(c.text)));
    pal.setColor(QPalette::Button, QColor(QString::fromLatin1(c.card)));
    pal.setColor(QPalette::ButtonText, QColor(QString::fromLatin1(c.text)));
    pal.setColor(QPalette::Highlight, QColor(QString::fromLatin1(c.listSel)));
    pal.setColor(QPalette::HighlightedText, QColor(QString::fromLatin1(c.listSelText)));
    w->setPalette(pal);

    w->setStyleSheet(styleSheet(dark));
    applyDarkTitleBar(w, dark);
}

} // namespace Theme
