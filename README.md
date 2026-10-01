# APK 图标读取器（apkIconShower）

让 Windows 资源管理器 / 桌面直接显示 **APK 自带的启动图标**，并把 .apk 的类型名改成 **安卓应用安装包**；
双击 APK 时弹出 Qt 界面让你**选择用什么软件打开**，也可以自己**添加需要打开的软件**。

## 组成部分

| 组件 | 说明 |
| --- | --- |
| `src/apkcore` | 无 Qt 的核心库：miniz 只读 ZIP、AndroidManifest.xml（二进制 AXML）解析、resources.arsc 资源表解析、WIC 图片解码 → HICON |
| `ApkIconShowerShell.dll` | 进程内 shell 扩展（`IPersistFile` + `IExtractIconW`），按文件返回 APK 内置图标（含缓存） |
| `ApkIconShower.exe` | 静态 Qt6 单文件界面：打开方式选择、添加/移除应用、注册/取消文件关联、刷新图标缓存 |

## 使用

1. 运行 `dist\app\ApkIconShower.exe`。
2. 点 **注册 / 更新文件关联**（会请求一次管理员授权——图标处理器必须写入 HKLM 才会被资源管理器加载）。
3. 之后资源管理器与桌面上的 .apk 就会显示应用自带图标，类型名为「安卓应用安装包」。
4. 双击某个 APK → 弹出「选择打开方式」窗口，先显示该 APK 的应用信息（图标/名称/包名/大小/图标来源），
   再由你选择两种打开方式之一（`%1` 会替换为 APK 路径，未写则自动附加）：
   - **仅一次打开** —— 立刻用所选应用打开，**不记住**；下次双击仍然弹窗询问。
   - **默认用此应用打开** —— 立刻打开并**记住**；以后双击 APK **直接启动该应用、不再弹窗**。
   - **清除默认** —— 恢复成每次都询问。
   - **添加应用…** 选择要用来打开 APK 的 exe（例如模拟器 `dnplayer.exe`）；**移除** 删掉不要的。
   - **设为系统默认打开方式…** 打开系统「打开方式」对话框，勾选「始终使用此应用」即可把本程序设为
     Windows 层面的 .apk 默认程序（这一步决定双击 .apk 时会不会调用本程序）。
5. 单独运行 `ApkIconShower.exe`（不带 APK）会打开**同一个窗口但隐藏应用信息区**，用来管理应用列表与文件关联。
6. 想恢复原状：点 **取消关联**（会还原被本程序移走的 `FileExts\.apk` 备份）。

自定义应用列表保存在 `%APPDATA%\ApkIconShower\apps.json`。

## 深色模式

- 默认**自动跟随系统**：读取 `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`
  （**0 = 深色**，1 = 浅色），并同步把标题栏切成深色（`DWMWA_USE_IMMERSIVE_DARK_MODE`）。
- **实时跟随**：监听 `QStyleHints::colorSchemeChanged`，另加 3 秒一次的低频轮询兜底（只读一个注册表值），
  因此在 Windows 设置里切换主题后，窗口几秒内自动变色。
- 也可强制指定：`ApkIconShower.exe --theme dark` / `--theme light` / `--theme auto`（默认 auto）。
- 自动化测试钩子：环境变量 `APKICONSHOWER_PERSONALIZE_KEY` 可把“系统主题”指向另一个注册表键，
  便于在**不改动真实系统设置**的前提下验证深色/浅色两种外观。


- 处理器调用日志：在 `%TEMP%` 建空文件 `apkIconShower_debug.on`，之后每次调用会写
  `%TEMP%\apkIconShower_shellex.log`（记录 Load / GetIconLocation / Extract 收到的路径）；删掉标记文件即关闭。

