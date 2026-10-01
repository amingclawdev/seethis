# SeeThis

SeeThis is a macOS menu bar tool: hold **Option+A**, mark one or more areas, and share a local reference or a marked image with your assistant.

SeeThis 是 macOS 菜单栏工具：按住 **Option+A**，用鼠标圈出一个或多个区域，再把本机引用或带标记图像交给你的助手。默认 **Option+D** 可连续删除 mark；已修改快捷键时，以 Inspector 或设置中显示的配置为准。

**[下载 v0.1.0-beta.1](https://github.com/amingclawdev/seethis/releases/tag/v0.1.0-beta.1)** · Apple Silicon · macOS 14.2+ · Developer ID 签名和 Apple 公证 · [MIT](LICENSE)

## Demo

[![Watch the SeeThis demo in landscape](docs/demo/SeeThis-landscape-v3-thumbnail.jpg)](docs/demo/SeeThis-landscape-v3-public.mp4)

[Landscape demo (16:9, 76 seconds)](docs/demo/SeeThis-landscape-v3-public.mp4) · [Portrait alternative (3:4)](docs/demo/SeeThis-portrait-v3-public.mp4)

The demo is edited for clarity. Private reference links, chat details, browser chrome, and personal Finder or desktop content are redacted. It illustrates an earlier recorded workflow; the current Inspector uses a reference dropdown with name and time. The footage does not certify the release build or another Mac. Narration and media attribution are recorded in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md).

## 开始使用

1. [下载、校验、安装与权限](docs/INSTALL.md)。
2. [圈选、Inspector、复制与删除](docs/USAGE.md)。
3. [Google Chrome 页面标记与 Automation](docs/CHROME.md)。
4. [恢复、更新、卸载与反馈](docs/TROUBLESHOOTING.md)。

**立即复制的是 JSON URL；图像稍后就绪。** Inspector 的 **Copy marked image** 显式复制带 mark 的完整所选显示器图像。网页聊天或远程助手需要实际图片附件；`127.0.0.1` URL 只能由同一台 Mac 上的工具读取。

圈选区域不是隐私裁剪边界：引用保存所选显示器的完整画面。截图前关闭私人窗口和通知，分享前检查整幅图像。见 [隐私](PRIVACY.md) 和 [安全报告](SECURITY.md)。

## 构建与发布

使用 C++20、Objective-C++、AppKit 和 ScreenCaptureKit；普通使用无需 Docker、Node、Aming Claw 或 Judgment Brain。Node 18+ 仅用于开发测试。构建步骤见 [CONTRIBUTING](CONTRIBUTING.md)。

此 prerelease 保留已接受的运行时，公开源码的文档、许可和 CI 单独整理；公开 tag 与原始构建、签名、公证和附件摘要的关系见 [RELEASE](docs/RELEASE.md)。用户已在本机接受当前版本；完整跨系统、另一台 Mac、首次权限和 Finder 拖放安装矩阵尚未完成。Intel、自动更新与 App Store 分发不在此 beta 范围内。
