# 开发与贡献

SeeThis 使用 C++20、Objective-C++、AppKit 和 ScreenCaptureKit。Apple Silicon/macOS 14.2+ 是当前 beta 目标。

## 构建与测试

安装 CMake 3.25+、Ninja、Apple 支持的 Xcode/Command Line Tools，以及 Node 18+（仅 JavaScript viewer 测试）。确认 `xcrun --find clang++`、`xcrun --sdk macosx --show-sdk-path` 和 `ninja --version` 可用。

在源码根目录选择独立输出目录：

```sh
cmake -S . -B /tmp/seethis-release-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.2
cmake --build /tmp/seethis-release-build --parallel 3
ctest --test-dir /tmp/seethis-release-build --output-on-failure
bash tests/package_macos_test.sh
```

源码 archive 可直接 configure/build/test，无需 Git 元数据。`scripts/package_macos.sh package` 则要求干净的确切 Git commit；它从源码新构建包，不接收预编译 app 作为新的构建 provenance。见 [RELEASE](docs/RELEASE.md)。

自动测试使用 disposable fixtures。不要重置 TCC、修改真实用户数据或系统设置来运行测试。状态机和 mock 测试不证明 Gatekeeper、TCC、Chrome 或真实粘贴行为。

## 提交

通过 [GitHub](https://github.com/amingclawdev/seethis) 提交聚焦的 issue 或 pull request，说明行为变化、必要验证和已知限制。不要提交私人截图、读取 capability、密钥、个人绝对路径、构建缓存或本机配置。安全问题使用[私下通道](SECURITY.md)。

本项目以 [MIT](LICENSE) 发布，Copyright (c) 2026 SeeThis contributors。提交者应有权提供所提交的代码或素材，并明确任何第三方许可。依赖和媒体说明见 [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md)。
