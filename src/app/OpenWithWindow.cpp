#include "OpenWithWindow.h"

#include "Theme.h"

#include "ApkInfo.h"
#include "AppIdentity.h"
#include "AssocRegistry.h"
#include "WicIcon.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QGuiApplication>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>
#include <QVBoxLayout>

#include <vector>

namespace {

QImage imageFromBytes(const std::vector<uint8_t> &data, int size)
{
    if (data.empty()) return QImage();
    std::vector<uint8_t> bgra;
    int w = 0, h = 0;
    if (apk::decodeImageToBgra(data.data(), data.size(), size, &bgra, &w, &h)) {
        QImage img(bgra.data(), w, h, QImage::Format_ARGB32_Premultiplied);
        return img.copy();
    }
    QImage fallback = QImage::fromData(data.data(), (int)data.size());
    if (!fallback.isNull() && size > 0)
        fallback = fallback.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return fallback;
}

} // namespace

OpenWithWindow::OpenWithWindow(const QString &apkPath, QWidget *parent) : QWidget(parent), apkPath_(apkPath)
{
    const bool hasApk = !apkPath_.isEmpty();
    setWindowTitle(hasApk ? QStringLiteral("APK 图标读取器 — 选择打开方式")
                          : QStringLiteral("APK 图标读取器 — 设置"));
    setMinimumSize(680, hasApk ? 560 : 520);
    setObjectName("root");

    apps_ = AppCore::mergedApps();
    buildUi();

    // 注意：必须在 buildUi() 之后套主题（applyTheme 会刷新图标预览，依赖已创建的控件）
    applyTheme(Theme::effectiveDark());

    // 系统主题变化时自动跟随：
    //   Qt 6.5+ 提供 colorSchemeChanged 信号；Windows 上不保证每次都触发，
    //   因此再加一个低频轮询兜底（只读一个注册表值，开销可忽略）。
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &OpenWithWindow::onSystemThemeChanged);
#endif
    themeTimer_ = new QTimer(this);
    themeTimer_->setInterval(3000);
    connect(themeTimer_, &QTimer::timeout, this, &OpenWithWindow::onSystemThemeChanged);
    themeTimer_->start();

    if (hasApk) {
        // 双击 APK：只显示应用信息 + 选择打开方式，隐藏文件关联区
        loadApkInfo(apkPath_);
        assocCard_->setVisible(false);
    } else {
        // 单独启动：保持窗口用于管理，但不显示应用信息
        headerCard_->setVisible(false);
    }
    reloadAppList(0);
    updateStatus();
}

void OpenWithWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ---------- 顶部：APK 信息（单独启动时隐藏） ----------
    headerCard_ = new QFrame(this);
    headerCard_->setObjectName("card");
    auto *hl = new QHBoxLayout(headerCard_);
    hl->setContentsMargins(16, 16, 16, 16);
    hl->setSpacing(16);

    iconLabel_ = new QLabel(headerCard_);
    iconLabel_->setFixedSize(96, 96);
    iconLabel_->setAlignment(Qt::AlignCenter);
    iconLabel_->setObjectName("apkIcon");
    hl->addWidget(iconLabel_);

    auto *textCol = new QVBoxLayout();
    textCol->setSpacing(4);
    titleLabel_ = new QLabel(QStringLiteral("未选择 APK 文件"), headerCard_);
    titleLabel_->setObjectName("title");
    detailLabel_ = new QLabel(headerCard_);
    detailLabel_->setObjectName("detail");
    detailLabel_->setWordWrap(true);
    textCol->addWidget(titleLabel_);
    textCol->addWidget(detailLabel_);
    textCol->addStretch(1);
    hl->addLayout(textCol, 1);
    root->addWidget(headerCard_);

    // ---------- 打开方式 ----------
    auto *openCard = new QFrame(this);
    openCard->setObjectName("card");
    auto *ol = new QVBoxLayout(openCard);
    ol->setContentsMargins(16, 14, 16, 14);
    ol->setSpacing(10);

    auto *openTitle = new QLabel(QStringLiteral("选择打开方式"), openCard);
    openTitle->setObjectName("sectionTitle");
    ol->addWidget(openTitle);

    appList_ = new QListWidget(openCard);
    appList_->setIconSize(QSize(24, 24));
    appList_->setMinimumHeight(150);
    connect(appList_, &QListWidget::itemDoubleClicked, this, &OpenWithWindow::onItemDoubleClicked);
    ol->addWidget(appList_, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    onceButton_ = new QPushButton(QStringLiteral("仅一次打开"), openCard);
    onceButton_->setObjectName("primary");
    connect(onceButton_, &QPushButton::clicked, this, &OpenWithWindow::onOpenOnceClicked);
    defaultButton_ = new QPushButton(QStringLiteral("默认用此应用打开"), openCard);
    connect(defaultButton_, &QPushButton::clicked, this, &OpenWithWindow::onOpenDefaultClicked);
    auto *addBtn = new QPushButton(QStringLiteral("添加应用…"), openCard);
    connect(addBtn, &QPushButton::clicked, this, &OpenWithWindow::onAddAppClicked);
    auto *removeBtn = new QPushButton(QStringLiteral("移除"), openCard);
    connect(removeBtn, &QPushButton::clicked, this, &OpenWithWindow::onRemoveAppClicked);
    browseButton_ = new QPushButton(QStringLiteral("选择 APK…"), openCard);
    connect(browseButton_, &QPushButton::clicked, this, &OpenWithWindow::onBrowseApkClicked);
    btnRow->addWidget(onceButton_);
    btnRow->addWidget(defaultButton_);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(browseButton_);
    ol->addLayout(btnRow);

    auto *defRow = new QHBoxLayout();
    defRow->setSpacing(8);
    defaultLabel_ = new QLabel(openCard);
    defaultLabel_->setObjectName("defaultInfo");
    defaultLabel_->setWordWrap(true);
    clearDefaultButton_ = new QPushButton(QStringLiteral("清除默认"), openCard);
    connect(clearDefaultButton_, &QPushButton::clicked, this, &OpenWithWindow::onClearDefaultClicked);
    defRow->addWidget(defaultLabel_, 1);
    defRow->addWidget(clearDefaultButton_);
    ol->addLayout(defRow);

    auto *hint = new QLabel(
        QStringLiteral("“默认用此应用打开”会记住选择：以后双击 APK 直接启动该应用，不再弹出本窗口；"
                       "“仅一次打开”则下次仍会询问。“清除默认”可恢复每次都询问。"),
        openCard);
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    ol->addWidget(hint);
    root->addWidget(openCard, 1);

    // ---------- 文件关联 ----------
    assocCard_ = new QFrame(this);
    auto *assocCard = assocCard_;
    assocCard->setObjectName("card");
    auto *al = new QVBoxLayout(assocCard);
    al->setContentsMargins(16, 14, 16, 14);
    al->setSpacing(8);

    auto *assocTitle = new QLabel(QStringLiteral("文件关联（资源管理器 / 桌面显示 APK 图标）"), assocCard);
    assocTitle->setObjectName("sectionTitle");
    al->addWidget(assocTitle);

    assocLabel_ = new QLabel(assocCard);
    assocLabel_->setObjectName("assoc");
    assocLabel_->setWordWrap(true);
    al->addWidget(assocLabel_);

    auto *assocRow = new QHBoxLayout();
    assocRow->setSpacing(8);
    auto *regBtn = new QPushButton(QStringLiteral("注册 / 更新文件关联"), assocCard);
    connect(regBtn, &QPushButton::clicked, this, &OpenWithWindow::onRegisterClicked);
    auto *unregBtn = new QPushButton(QStringLiteral("取消关联"), assocCard);
    connect(unregBtn, &QPushButton::clicked, this, &OpenWithWindow::onUnregisterClicked);
    auto *defBtn = new QPushButton(QStringLiteral("设为系统默认打开方式…"), assocCard);
    connect(defBtn, &QPushButton::clicked, this, &OpenWithWindow::onSetDefaultClicked);
    auto *icoBtn = new QPushButton(QStringLiteral("刷新图标缓存"), assocCard);
    connect(icoBtn, &QPushButton::clicked, this, &OpenWithWindow::onRefreshIconsClicked);
    assocRow->addWidget(regBtn);
    assocRow->addWidget(defBtn);
    assocRow->addWidget(icoBtn);
    assocRow->addStretch(1);
    assocRow->addWidget(unregBtn);
    al->addLayout(assocRow);
    root->addWidget(assocCard);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("status");
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);
}

void OpenWithWindow::loadApkInfo(const QString &path)
{
    apkPath_ = path;
    headerCard_->setVisible(true);
    apkLabel_.clear();
    apkPackage_.clear();
    apkVersion_.clear();
    apkIconEntry_.clear();
    apkIconSource_.clear();
    apkIconW_ = apkIconH_ = 0;
    apkFileSize_ = 0;

    const QFileInfo fi(path);
    if (!fi.exists()) {
        titleLabel_->setText(QStringLiteral("文件不存在"));
        detailLabel_->setText(path);
        iconLabel_->setPixmap(QPixmap());
        return;
    }
    apkFileSize_ = fi.size();

    apk::ApkInfo info;
    std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    apk::readApkInfo(wpath, &info);

    apkLabel_ = QString::fromStdString(info.label);
    apkPackage_ = QString::fromStdString(info.packageName);
    apkVersion_ = QString::fromStdString(info.versionName);
    apkIconEntry_ = QString::fromStdString(info.iconEntry);
    apkIconSource_ = QString::fromStdString(info.source);
    apkIconW_ = info.iconWidth;
    apkIconH_ = info.iconHeight;

    if (apkLabel_.isEmpty()) apkLabel_ = fi.completeBaseName();
    titleLabel_->setText(apkLabel_);

    QStringList details;
    if (!apkPackage_.isEmpty()) details << QStringLiteral("包名：%1").arg(apkPackage_);
    if (!apkVersion_.isEmpty()) details << QStringLiteral("版本：%1").arg(apkVersion_);
    details << QStringLiteral("大小：%1").arg(AppCore::formatSize(apkFileSize_));
    if (!apkIconEntry_.isEmpty())
        details << QStringLiteral("图标：%1（%2×%3，%4）")
                       .arg(apkIconEntry_)
                       .arg(apkIconW_)
                       .arg(apkIconH_)
                       .arg(apkIconSource_);
    else
        details << QStringLiteral("未从 APK 中解析到图标");
    details << QStringLiteral("文件：%1").arg(path);
    detailLabel_->setText(details.join(QStringLiteral("\n")));

    refreshApkPreview();
}

void OpenWithWindow::refreshApkPreview()
{
    if (!iconLabel_ || apkPath_.isEmpty()) {
        iconLabel_->setPixmap(QPixmap());
        return;
    }
    apk::ApkInfo info;
    std::wstring wpath = QDir::toNativeSeparators(apkPath_).toStdWString();
    apk::readApkInfo(wpath, &info);
    const QImage img = imageFromBytes(info.iconData, 96);
    if (!img.isNull()) {
        iconLabel_->setPixmap(QPixmap::fromImage(img));
        return;
    }
    const QIcon ic = AppCore::iconForPath(apkPath_, 96);
    iconLabel_->setPixmap(ic.pixmap(96, 96));
}

void OpenWithWindow::reloadAppList(int selectRow)
{
    appList_->clear();
    for (const AppEntry &a : apps_) {
        auto *item = new QListWidgetItem(AppCore::iconForPath(a.path, 24), a.name);
        QString sub = a.path;
        if (a.detected) sub += QStringLiteral("  （自动检测）");
        if (a.path.startsWith(QLatin1String("shell:"))) sub = QStringLiteral("调用 Windows 自带的“打开方式”对话框");
        item->setToolTip(sub);
        item->setData(Qt::UserRole, a.path);
        appList_->addItem(item);
    }
    if (selectRow >= 0 && selectRow < appList_->count()) appList_->setCurrentRow(selectRow);
}

bool OpenWithWindow::currentEntry(AppEntry *out) const
{
    QListWidgetItem *item = appList_->currentItem();
    if (!item) return false;
    const int row = appList_->row(item);
    if (row < 0 || row >= apps_.size()) return false;
    if (out) *out = apps_[row];
    return true;
}

void OpenWithWindow::updateStatus()
{
    const bool registered = apk::isAssociationRegistered();
    const QString adminNote = AppCore::isElevated() ? QStringLiteral("（管理员）") : QStringLiteral("（标准权限）");
    if (registered) {
        assocLabel_->setText(QStringLiteral("当前状态：已注册 %1。APK 类型名显示为“%2”，并在资源管理器 / 桌面中显示 APK 自带图标。")
                                 .arg(adminNote, QString::fromWCharArray(apk::friendlyTypeName())));
    } else {
        assocLabel_->setText(QStringLiteral("当前状态：未注册 %1。点击“注册 / 更新文件关联”后，APK 会在资源管理器 / 桌面中显示图标，"
                                            "类型名变为“%2”，并出现在“打开方式”列表中（注册时会请求一次管理员授权）。")
                                 .arg(adminNote, QString::fromWCharArray(apk::friendlyTypeName())));
    }

    const AppCore::Prefs prefs = AppCore::loadPrefs();
    if (prefs.defaultAppPath.isEmpty()) {
        defaultLabel_->setText(QStringLiteral("默认打开方式：未设置（每次都询问）"));
        clearDefaultButton_->setEnabled(false);
    } else {
        QString name = prefs.defaultAppName;
        if (name.isEmpty()) name = QFileInfo(prefs.defaultAppPath).completeBaseName();
        defaultLabel_->setText(QStringLiteral("默认打开方式：%1（双击 APK 直接启动，不再询问）").arg(name));
        clearDefaultButton_->setEnabled(true);
    }

    AppEntry sel;
    const bool hasSel = currentEntry(&sel);
    onceButton_->setEnabled(!apkPath_.isEmpty() && hasSel);
    defaultButton_->setEnabled(!apkPath_.isEmpty() && hasSel && !sel.path.startsWith(QLatin1String("shell:")));
    browseButton_->setEnabled(true);
}

void OpenWithWindow::setStatus(const QString &text, bool error)
{
    statusLabel_->setObjectName(error ? "statusError" : "status");
    statusLabel_->setStyleSheet(QString());
    statusLabel_->setText(text);
    statusLabel_->style()->unpolish(statusLabel_);
    statusLabel_->style()->polish(statusLabel_);
}

void OpenWithWindow::doOpen(bool saveAsDefault)
{
    AppEntry entry;
    if (!currentEntry(&entry)) {
        setStatus(QStringLiteral("请先选择一个应用。"), true);
        return;
    }
    if (apkPath_.isEmpty()) {
        setStatus(QStringLiteral("请先选择一个 APK 文件。"), true);
        return;
    }
    if (saveAsDefault && entry.path.startsWith(QLatin1String("shell:"))) {
        setStatus(QStringLiteral("“Windows 打开方式”不能设为默认应用，请选择具体的程序。"), true);
        return;
    }

    QString err;
    if (!AppCore::launchApp(entry, apkPath_, &err)) {
        setStatus(err, true);
        return;
    }

    if (saveAsDefault) {
        AppCore::Prefs prefs;
        prefs.defaultAppPath = entry.path;
        prefs.defaultAppName = entry.name;
        AppCore::savePrefs(prefs);
    }

    setStatus(saveAsDefault ? QStringLiteral("已用“%1”打开，并设为默认（下次直接启动）。").arg(entry.name)
                            : QStringLiteral("已用“%1”打开（仅一次，下次仍会询问）。").arg(entry.name));

    // 双击 APK 的场景：启动后关闭窗口
    close();
}

void OpenWithWindow::onOpenOnceClicked() { doOpen(false); }
void OpenWithWindow::onOpenDefaultClicked() { doOpen(true); }
void OpenWithWindow::onItemDoubleClicked(QListWidgetItem *) { doOpen(false); }

void OpenWithWindow::onAddAppClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择要用于打开 APK 的应用程序"), QString(),
                                                      QStringLiteral("可执行文件 (*.exe);;所有文件 (*.*)"));
    if (path.isEmpty()) return;
    const QFileInfo fi(path);
    for (const AppEntry &e : apps_) {
        if (QString::compare(e.path, path, Qt::CaseInsensitive) == 0) {
            setStatus(QStringLiteral("该应用已在列表中。"), true);
            return;
        }
    }
    AppEntry a;
    a.name = fi.completeBaseName();
    a.path = QDir::fromNativeSeparators(path);
    a.detected = false;
    apps_.append(a);
    AppCore::saveApps(apps_);
    reloadAppList(apps_.size() - 1);
    updateStatus();
    setStatus(QStringLiteral("已添加应用：%1").arg(a.name));
}

void OpenWithWindow::onRemoveAppClicked()
{
    AppEntry sel;
    if (!currentEntry(&sel)) return;
    if (sel.detected) {
        setStatus(QStringLiteral("自动检测到的应用无法移除（可在应用的安装位置更改后自动消失）。"), true);
        return;
    }
    const AppCore::Prefs prefs = AppCore::loadPrefs();
    if (QString::compare(prefs.defaultAppPath, sel.path, Qt::CaseInsensitive) == 0) {
        AppCore::Prefs cleared;
        AppCore::savePrefs(cleared);   // 清空默认
    }
    QListWidgetItem *item = appList_->currentItem();
    const int row = appList_->row(item);
    apps_.remove(row);
    AppCore::saveApps(apps_);
    reloadAppList(qMax(0, row - 1));
    updateStatus();
    setStatus(QStringLiteral("已移除应用：%1").arg(sel.name));
}

void OpenWithWindow::onBrowseApkClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 APK 文件"), QString(),
                                                      QStringLiteral("安卓应用安装包 (*.apk);;所有文件 (*.*)"));
    if (path.isEmpty()) return;
    loadApkInfo(QDir::fromNativeSeparators(path));
    updateStatus();
    setStatus(QStringLiteral("已选择：%1").arg(QFileInfo(path).fileName()));
}

void OpenWithWindow::onClearDefaultClicked()
{
    AppCore::Prefs cleared;
    AppCore::savePrefs(cleared);
    updateStatus();
    setStatus(QStringLiteral("已清除默认打开方式，下次双击 APK 会重新询问。"));
}

void OpenWithWindow::onRegisterClicked()
{
    if (!AppCore::isElevated()) {
        setStatus(QStringLiteral("正在请求管理员权限以注册图标处理器…"));
        QString err;
        if (!AppCore::runElevated(QStringLiteral("--register"), &err)) {
            setStatus(err, true);
            return;
        }
        updateStatus();
        setStatus(QStringLiteral("文件关联注册成功。若资源管理器未立即刷新，可点击“刷新图标缓存”。"));
        return;
    }

    const QString exe = QDir::toNativeSeparators(QApplication::applicationFilePath());
    const QString dir = QFileInfo(exe).absolutePath();
    const QString dll = dir + QStringLiteral("/") + QString::fromWCharArray(apk::shellDllName());
    if (!QFileInfo::exists(QDir::toNativeSeparators(dll))) {
        setStatus(QStringLiteral("未找到图标处理器：%1，请先完成构建。").arg(dll), true);
        return;
    }
    apk::AssocPaths paths;
    paths.exePath = exe.toStdWString();
    paths.dllPath = QDir::toNativeSeparators(dll).toStdWString();
    std::wstring err;
    if (!apk::registerAssociations(paths, &err)) {
        setStatus(QStringLiteral("注册失败：%1").arg(QString::fromStdWString(err)), true);
        return;
    }
    updateStatus();
    setStatus(QStringLiteral("文件关联注册成功。若资源管理器未立即刷新，可点击“刷新图标缓存”。"));
}

void OpenWithWindow::onUnregisterClicked()
{
    if (!AppCore::isElevated()) {
        QString err;
        if (!AppCore::runElevated(QStringLiteral("--unregister"), &err)) {
            setStatus(err, true);
            return;
        }
    } else {
        std::wstring err;
        apk::unregisterAssociations(&err);
        apk::refreshIconCache();
    }
    updateStatus();
    setStatus(QStringLiteral("已取消文件关联。"));
}

void OpenWithWindow::onSetDefaultClicked()
{
    if (!AppCore::isElevated()) {
        QString err;
        if (!AppCore::runElevated(QStringLiteral("--register"), &err)) {
            setStatus(err, true);
            return;
        }
    } else {
        const QString exe = QDir::toNativeSeparators(QApplication::applicationFilePath());
        const QString dir = QFileInfo(exe).absolutePath();
        const QString dll = dir + QStringLiteral("/") + QString::fromWCharArray(apk::shellDllName());
        apk::AssocPaths paths;
        paths.exePath = exe.toStdWString();
        paths.dllPath = QDir::toNativeSeparators(dll).toStdWString();
        std::wstring err;
        if (!apk::registerAssociations(paths, &err)) {
            setStatus(QStringLiteral("注册失败：%1").arg(QString::fromStdWString(err)), true);
            return;
        }
    }
    updateStatus();
    QString sample = apkPath_;
    if (sample.isEmpty()) {
        const QString dir = QDir::toNativeSeparators(QFileInfo(QApplication::applicationFilePath()).absolutePath());
        sample = dir;   // 无 APK 时由用户自己在对话框里选
    }
    if (!apkPath_.isEmpty()) {
        AppCore::showSystemOpenWith(apkPath_);
        setStatus(QStringLiteral("已打开系统“打开方式”对话框，请选择“%1”并勾选“始终使用此应用”。")
                      .arg(QString::fromWCharArray(apk::appDisplayName())));
    } else {
        setStatus(QStringLiteral("已注册。要设为系统默认，请右键任意 APK → 打开方式 → 选择“%1”并勾选“始终”。")
                      .arg(QString::fromWCharArray(apk::appDisplayName())));
    }
}

void OpenWithWindow::onRefreshIconsClicked()
{
    apk::refreshIconCache();
    setStatus(QStringLiteral("已通知系统刷新图标缓存。"));
}

void OpenWithWindow::applyTheme(bool dark)
{
    dark_ = dark;
    Theme::apply(this, dark);
    // 图标占位底色由样式表控制，这里只刷新已显示的图标
    if (iconLabel_ && !apkPath_.isEmpty()) refreshApkPreview();
}

void OpenWithWindow::onSystemThemeChanged()
{
    if (Theme::mode() != Theme::Mode::Auto) return;   // 命令行强制指定时不跟随系统
    const bool dark = Theme::systemPrefersDark();
    if (dark != dark_) applyTheme(dark);
}
