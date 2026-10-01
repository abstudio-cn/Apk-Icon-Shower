#include "AppCore.h"

#include "AssocRegistry.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QPixmap>
#include <QImage>

namespace AppCore {

namespace {

QString appsFile()
{
    return dataDir() + "/apps.json";
}

void addIfExists(QVector<AppEntry> *out, const QString &name, const QString &path, const QString &args = QString())
{
    if (path.isEmpty()) return;
    if (!QFileInfo::exists(path)) return;
    for (const AppEntry &e : *out)
        if (QString::compare(e.path, path, Qt::CaseInsensitive) == 0) return;
    AppEntry a;
    a.name = name;
    a.path = path;
    a.args = args;
    a.detected = true;
    out->append(a);
}

} // namespace

QString dataDir()
{
    // 固定为 %APPDATA%\ApkIconShower，避免 org/app 同名导致路径重复
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString dir = base + QStringLiteral("/ApkIconShower");
    QDir().mkpath(dir);
    // 迁移旧版（org/app 同名会形成双层目录）
    const QString legacy = base + QStringLiteral("/ApkIconShower/ApkIconShower/apps.json");
    if (QFileInfo::exists(legacy) && !QFileInfo::exists(dir + QStringLiteral("/apps.json")))
        QFile::copy(legacy, dir + QStringLiteral("/apps.json"));
    return dir;
}

QVector<AppEntry> loadApps()
{
    QVector<AppEntry> result;
    QFile f(appsFile());
    if (!f.open(QIODevice::ReadOnly)) return result;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return result;
    const QJsonArray arr = doc.object().value("apps").toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        AppEntry a;
        a.name = o.value("name").toString();
        a.path = o.value("path").toString();
        a.args = o.value("args").toString();
        a.detected = false;
        if (!a.path.isEmpty()) result.append(a);
    }
    return result;
}

bool saveApps(const QVector<AppEntry> &apps)
{
    QJsonArray arr;
    for (const AppEntry &a : apps) {
        if (a.detected) continue;   // 探测到的应用不落盘
        QJsonObject o;
        o.insert("name", a.name);
        o.insert("path", a.path);
        o.insert("args", a.args);
        arr.append(o);
    }
    QJsonObject root;
    root.insert("apps", arr);
    QFile f(appsFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

Prefs loadPrefs()
{
    Prefs p;
    QFile f(appsFile());
    if (!f.open(QIODevice::ReadOnly)) return p;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return p;
    p.defaultAppPath = doc.object().value("defaultAppPath").toString();
    p.defaultAppName = doc.object().value("defaultAppName").toString();
    return p;
}

bool savePrefs(const Prefs &prefs)
{
    QJsonObject root;
    QFile f(appsFile());
    if (f.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }
    if (prefs.defaultAppPath.isEmpty()) {
        root.remove("defaultAppPath");
        root.remove("defaultAppName");
    } else {
        root.insert("defaultAppPath", prefs.defaultAppPath);
        root.insert("defaultAppName", prefs.defaultAppName);
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

QVector<AppEntry> detectKnownApps()
{
    QVector<AppEntry> out;
    const QString home = QDir::homePath();
    const QString pf = qEnvironmentVariable("ProgramFiles", "C:/Program Files");
    const QString pf86 = qEnvironmentVariable("ProgramFiles(x86)", "C:/Program Files (x86)");
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    // 雷电模拟器
    const QStringList ld = {
        "C:/LDPlayer/LDPlayer9/dnplayer.exe",
        "C:/LDPlayer/LDPlayer64/dnplayer.exe",
        "C:/Program Files/LDPlayer/LDPlayer9/dnplayer.exe",
        docs + "/leidian9/dnplayer.exe",
        home + "/Documents/leidian9/dnplayer.exe",
        "D:/LDPlayer/LDPlayer9/dnplayer.exe",
    };
    for (const QString &p : ld) addIfExists(&out, QStringLiteral("雷电模拟器"), QDir::fromNativeSeparators(p));

    // 夜神模拟器
    const QStringList nox = {
        pf86 + "/Nox/bin/Nox.exe", pf + "/Nox/bin/Nox.exe", "D:/Nox/bin/Nox.exe", "C:/Nox/bin/Nox.exe",
    };
    for (const QString &p : nox) addIfExists(&out, QStringLiteral("夜神模拟器"), p);

    // 蓝叠
    const QStringList bs = {
        pf + "/BlueStacks_nxt/HD-Player.exe",
        pf86 + "/BlueStacks_nxt/HD-Player.exe",
        local + "/BlueStacks_nxt/HD-Player.exe",
    };
    for (const QString &p : bs) addIfExists(&out, QStringLiteral("BlueStacks"), p);

    // MuMu
    const QStringList mumu = {
        pf + "/Netease/MuMuPlayer-12.0/shell/MuMuPlayer.exe",
        pf86 + "/Netease/MuMuPlayer-12.0/shell/MuMuPlayer.exe",
        pf + "/MuMuPlayer-12.0/shell/MuMuPlayer.exe",
    };
    for (const QString &p : mumu) addIfExists(&out, QStringLiteral("MuMu 模拟器"), p);

    // 7-Zip（解包查看）
    const QStringList z7 = {pf + "/7-Zip/7zFM.exe", pf86 + "/7-Zip/7zFM.exe"};
    for (const QString &p : z7) addIfExists(&out, QStringLiteral("7-Zip 解包"), p);

    // 系统“打开方式”对话框
    AppEntry sys;
    sys.name = QStringLiteral("Windows 打开方式…");
    sys.path = QStringLiteral("shell:openas");
    sys.detected = true;
    out.append(sys);

    return out;
}

QVector<AppEntry> mergedApps()
{
    QVector<AppEntry> result = loadApps();
    for (const AppEntry &d : detectKnownApps()) {
        bool dup = false;
        for (const AppEntry &e : result)
            if (QString::compare(e.path, d.path, Qt::CaseInsensitive) == 0) dup = true;
        if (!dup) result.append(d);
    }
    return result;
}

QIcon iconForPath(const QString &path, int size)
{
    if (path.isEmpty()) return QIcon();
    if (path.startsWith(QLatin1String("shell:"))) return QIcon::fromTheme(QStringLiteral("system-run"));

    SHFILEINFOW sfi;
    memset(&sfi, 0, sizeof(sfi));
    const QString native = QDir::toNativeSeparators(path);
    const UINT flags = SHGFI_ICON | (size >= 32 ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    if (!SHGetFileInfoW(reinterpret_cast<const wchar_t *>(native.utf16()), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                        flags))
        return QIcon();
    if (!sfi.hIcon) return QIcon();
    QImage img = QImage::fromHICON(sfi.hIcon);
    DestroyIcon(sfi.hIcon);
    if (img.isNull()) return QIcon();
    if (size > 0 && (img.width() != size))
        img = img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return QIcon(QPixmap::fromImage(img));
}

bool launchApp(const AppEntry &app, const QString &apkPath, QString *error)
{
    if (app.path.startsWith(QLatin1String("shell:"))) {
        showSystemOpenWith(apkPath);
        return true;
    }
    if (!QFileInfo::exists(app.path)) {
        if (error) *error = QStringLiteral("应用不存在：%1").arg(app.path);
        return false;
    }

    QString args = app.args;
    if (args.contains(QLatin1String("%1")))
        args.replace(QLatin1String("%1"), QStringLiteral("\"%1\"").arg(apkPath));
    else {
        if (!args.isEmpty()) args += QLatin1Char(' ');
        args += QStringLiteral("\"%1\"").arg(apkPath);
    }

    const QString nativeExe = QDir::toNativeSeparators(app.path);
    const QString nativeArgs = QDir::toNativeSeparators(args);
    const HINSTANCE r = ShellExecuteW(nullptr, L"open", reinterpret_cast<const wchar_t *>(nativeExe.utf16()),
                                      reinterpret_cast<const wchar_t *>(nativeArgs.utf16()), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32) {
        if (error) *error = QStringLiteral("启动失败（ShellExecute 返回 %1）").arg(reinterpret_cast<INT_PTR>(r));
        return false;
    }
    return true;
}

void showSystemOpenWith(const QString &apkPath)
{
    const QString native = QDir::toNativeSeparators(apkPath);
    const std::wstring cmd = L"rundll32.exe shell32.dll,OpenAs_RunDLL \"" + native.toStdWString() + L"\"";
    std::wstring mutableCmd = cmd;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    if (CreateProcessW(nullptr, &mutableCmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

bool runElevated(const QString &args, QString *error)
{
    const QString exe = QDir::toNativeSeparators(QApplication::applicationFilePath());
    std::wstring params = args.toStdWString();

    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = reinterpret_cast<const wchar_t *>(exe.utf16());
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        const DWORD e = GetLastError();
        if (error)
            *error = (e == ERROR_CANCELLED) ? QStringLiteral("已取消管理员授权（UAC）")
                                            : QStringLiteral("提权启动失败（错误码 %1）").arg(e);
        return false;
    }
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 60000);
        DWORD code = 1;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        if (code != 0) {
            if (error) *error = QStringLiteral("管理员模式下注册失败（退出码 %1）").arg(code);
            return false;
        }
    }
    return true;
}

bool isElevated()
{
    return apk::isProcessElevated();
}

QString formatSize(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
}

} // namespace AppCore
