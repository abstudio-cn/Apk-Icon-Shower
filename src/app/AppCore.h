#pragma once
// AppCore — 应用清单存储、已知模拟器探测与启动
#include <QIcon>
#include <QString>
#include <QVector>

struct AppEntry {
    QString name;
    QString path;      // 可执行文件路径；特殊值 shell:openas 表示系统“打开方式”对话框
    QString args;      // 额外参数（%1 会替换为 APK 路径，未包含 %1 时自动追加）
    bool detected = false;
};

namespace AppCore {

// 持久化偏好：默认用于打开 APK 的应用（用户选择“默认用此应用打开”时写入）
struct Prefs {
    QString defaultAppPath;
    QString defaultAppName;
};

QString dataDir();
QVector<AppEntry> loadApps();
bool saveApps(const QVector<AppEntry> &apps);

Prefs loadPrefs();
bool savePrefs(const Prefs &prefs);

// 探测本机常见安卓模拟器/工具
QVector<AppEntry> detectKnownApps();

// 合并：用户清单 + 探测结果（去重）
QVector<AppEntry> mergedApps();

QIcon iconForPath(const QString &path, int size = 32);

// 启动指定应用打开 APK
bool launchApp(const AppEntry &app, const QString &apkPath, QString *error);

// 系统“打开方式”对话框
void showSystemOpenWith(const QString &apkPath);

QString formatSize(qint64 bytes);

// 以管理员身份重新启动本程序（等待其结束）。args 为命令行参数。
bool runElevated(const QString &args, QString *error);

// 当前进程是否已提权
bool isElevated();

} // namespace AppCore
