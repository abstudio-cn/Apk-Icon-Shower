#pragma once
// OpenWithWindow — 主窗口
//   带 APK 启动：显示应用信息 + 选择打开方式（仅一次 / 默认此应用）
//   单独启动   ：显示同一窗口，但不显示应用信息，可用于管理应用与文件关联
#include <QWidget>
#include <QVector>

#include "AppCore.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QFrame;
class QTimer;

class OpenWithWindow : public QWidget {
    Q_OBJECT

public:
    explicit OpenWithWindow(const QString &apkPath, QWidget *parent = nullptr);

private slots:
    void onOpenOnceClicked();
    void onOpenDefaultClicked();
    void onAddAppClicked();
    void onRemoveAppClicked();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onBrowseApkClicked();
    void onClearDefaultClicked();
    void onRegisterClicked();
    void onUnregisterClicked();
    void onSetDefaultClicked();
    void onRefreshIconsClicked();

private:
    void buildUi();
    void loadApkInfo(const QString &path);
    void reloadAppList(int selectRow = 0);
    void updateStatus();
    void refreshApkPreview();
    void setStatus(const QString &text, bool error = false);
    void applyTheme(bool dark);
    void onSystemThemeChanged();
    bool currentEntry(AppEntry *out) const;
    void doOpen(bool saveAsDefault);

    QString apkPath_;
    QVector<AppEntry> apps_;

    QFrame *headerCard_ = nullptr;
    QFrame *assocCard_ = nullptr;   // 文件关联区：仅在单独启动（设置模式）显示
    QLabel *iconLabel_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *detailLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *assocLabel_ = nullptr;
    QLabel *defaultLabel_ = nullptr;
    QListWidget *appList_ = nullptr;
    QPushButton *onceButton_ = nullptr;
    QPushButton *defaultButton_ = nullptr;
    QPushButton *clearDefaultButton_ = nullptr;
    QPushButton *browseButton_ = nullptr;
    QTimer *themeTimer_ = nullptr;
    bool dark_ = false;

    // APK 信息
    QString apkLabel_;
    QString apkPackage_;
    QString apkVersion_;
    QString apkIconEntry_;
    QString apkIconSource_;
    int apkIconW_ = 0;
    int apkIconH_ = 0;
    qint64 apkFileSize_ = 0;
};
