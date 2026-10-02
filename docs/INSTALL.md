# 安装 SeeThis beta

面向 Apple Silicon（M 系列）Mac，macOS **14.2 或更新版本**。安装后只有菜单栏项，没有主窗口属于正常情况。

## 下载与校验

1. 打开 [v0.1.0-beta.1 Release](https://github.com/amingclawdev/seethis/releases/tag/v0.1.0-beta.1)。下载 **SeeThis-0.1.0-beta.1-macos-arm64-release.dmg** 和 **SHA256SUMS**；也可选择 **SeeThis-0.1.0-beta.1-macos-arm64-release.zip**。
2. 在 Terminal 进入下载目录，对下载的附件核对摘要：

```sh
cd ~/Downloads
shasum -a 256 SeeThis-0.1.0-beta.1-macos-arm64-release.dmg
# ZIP 替代包：
shasum -a 256 SeeThis-0.1.0-beta.1-macos-arm64-release.zip
```

3. 将结果与同一 Release 的 SHA256SUMS 比较。下载了两份附件时，也可运行 `shasum -a 256 -c SHA256SUMS`，两项都应为 **OK**。只下载其中一份时，另一项缺失不代表已下载文件损坏；逐项核对自己下载的文件。摘要不同就重新下载，暂停安装。

## 放入 Applications 并打开

1. 双击 DMG。挂载卷中有 **SeeThis.app** 和指向 Applications 的快捷入口；用 Finder 将 app 拖入 Applications，再推出磁盘。ZIP 方式：解压后把 **SeeThis.app** 放入 Applications。
2. 从 Applications 手动打开 SeeThis。如果 macOS 询问是否打开互联网下载的 app，核对名称与来源后按系统提示打开。
3. 此版本以 **Developer ID Application: YING ZHANG (GLHUR8CC29)** 签名，App 和 DMG 已公证并 stapled。若系统报告 damaged、恶意软件或身份异常，检查下载来源和摘要并[反馈](TROUBLESHOOTING.md#反馈)，不要关闭系统安全保护。
4. 展开 SeeThis 菜单，用 **Location:** 核对运行路径，避免同时打开下载目录、旧包和 Applications 中的多份 app。

DMG 布局、签名、公证和挂载后 app 内容已经技术校验；此最终 DMG 的实际 Finder 拖放安装尚未复测。遇到拷贝错误可记录错误并尝试经过同样摘要校验的 ZIP 方式。

## Input Monitoring 与 Screen Recording

1. 在 **System Settings > Privacy & Security > Input Monitoring** 为这份 SeeThis 允许输入监听。菜单的 **Open Input Monitoring Settings…** 可打开权限页；允许后按 **Retry Input Monitoring**。若显示 `restart_required`，选 **Quit SeeThis**，再从确切 Applications 路径手动重开。
2. 在 **Screen Recording**（系统版本可能显示 Screen & System Audio Recording）允许这份 app，按系统要求重开。菜单的 **Open Screen Recording Settings…** 可打开页面。
3. 在菜单分别确认 Input Monitoring、Screen Recording、Capture shortcut 和 Delete shortcut ready。仅有一个权限就绪不足以证明可以截图。
4. 在安全测试窗口试一次[圈选](USAGE.md)，检查 Inspector 的图像和复制结果。系统 **Quit & Reopen** 后仍异常时，以 **Location:** 核对实际副本并手动重开。

Chrome 页面身份另需 [Automation](CHROME.md)。不需要 Accessibility 权限或浏览器扩展。权限可能因系统政策、签名变化或多副本而需要重新允许；不能承诺自动延续。首次下载、首次授权和更多 macOS 版本仍需要实际验证，见 [release 状态](RELEASE.md)。
