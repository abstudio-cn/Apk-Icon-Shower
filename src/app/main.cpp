// main.cpp — APK 图标读取器入口
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QString>
#include <QStringList>

#include <windows.h>
#include <shellapi.h>

#include "AppCore.h"
#include "AppIdentity.h"
#include "AssocRegistry.h"
#include "OpenWithWindow.h"
#include "Theme.h"

namespace {

QStringList parseArgs()
{
    QStringList out;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return out;
    for (int i = 1; i < argc; ++i) out << QString::fromWCharArray(argv[i]);
    LocalFree(argv);
    return out;
}

bool registerFromThisExe(QString *message)
{
    const QString exe = QDir::toNativeSeparators(QApplication::applicationFilePath());
    const QString dir = QFileInfo(exe).absolutePath();
    const QString dll = dir + QStringLiteral("/") + QString::fromWCharArray(apk::shellDllName());
    if (!QFileInfo::exists(QDir::toNativeSeparators(dll))) {
        if (message) *message = QStringLiteral("未找到图标处理器：%1").arg(dll);
        return false;
    }
    apk::AssocPaths paths;
    paths.exePath = exe.toStdWString();
    paths.dllPath = QDir::toNativeSeparators(dll).toStdWString();
    std::wstring err;
    if (!apk::registerAssociations(paths, &err)) {
        if (message) *message = QString::fromStdWString(err);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ApkIconShower"));
    QApplication::setOrganizationName(QStringLiteral("ApkIconShower"));
    QApplication::setApplicationDisplayName(QString::fromWCharArray(apk::appDisplayName()));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));

    const QStringList args = parseArgs();
    QString apkPath;
    QString mode;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a.compare(QStringLiteral("--register"), Qt::CaseInsensitive) == 0) {
            mode = QStringLiteral("register");
        } else if (a.compare(QStringLiteral("--unregister"), Qt::CaseInsensitive) == 0) {
            mode = QStringLiteral("unregister");
        } else if (a.startsWith(QStringLiteral("--theme="), Qt::CaseInsensitive)) {
            Theme::Mode m;
            if (Theme::modeFromString(a.mid(8), &m)) Theme::setMode(m);
        } else if (a.compare(QStringLiteral("--theme"), Qt::CaseInsensitive) == 0) {
            if (i + 1 < args.size()) {
                Theme::Mode m;
                if (Theme::modeFromString(args[i + 1], &m)) { Theme::setMode(m); ++i; }
            }
        } else if (a.startsWith(QStringLiteral("--"))) {
            continue;
        } else {
            apkPath = QDir::fromNativeSeparators(a);
        }
    }

    if (mode == QStringLiteral("register")) {
        QString message;
        return registerFromThisExe(&message) ? 0 : 1;
    }
    if (mode == QStringLiteral("unregister")) {
        std::wstring err;
        apk::unregisterAssociations(&err);
        apk::refreshIconCache();
        return 0;
    }

    // 双击 APK 启动：若用户此前选过“默认用此应用打开”，直接启动该应用，不弹窗。
    // 应用已不存在或启动失败时，回落到窗口让用户重新选择。
    if (!apkPath.isEmpty()) {
        const AppCore::Prefs prefs = AppCore::loadPrefs();
        if (!prefs.defaultAppPath.isEmpty()) {
            AppEntry target;
            target.name = prefs.defaultAppName;
            target.path = prefs.defaultAppPath;
            for (const AppEntry &a : AppCore::mergedApps()) {
                if (QString::compare(a.path, prefs.defaultAppPath, Qt::CaseInsensitive) == 0) {
                    target = a;
                    break;
                }
            }
            if (QFileInfo::exists(target.path)) {
                QString err;
                if (AppCore::launchApp(target, apkPath, &err)) return 0;
            }
        }
    }

    OpenWithWindow window(apkPath);
    window.show();
    return app.exec();
}
