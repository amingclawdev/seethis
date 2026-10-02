# Contributing

SeeThis uses C++20, Objective-C++, AppKit and ScreenCaptureKit. The current beta targets Apple Silicon and macOS 14.2 or later.

## Build and test

Install CMake 3.25+, Ninja, an Apple-supported Xcode or Command Line Tools installation, and Node 18+ for the JavaScript viewer tests. Check that `xcrun --find clang++`, `xcrun --sdk macosx --show-sdk-path` and `ninja --version` work.

From the source root, use a separate output directory:

```sh
cmake -S . -B /tmp/seethis-release-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.2
cmake --build /tmp/seethis-release-build --parallel 3
ctest --test-dir /tmp/seethis-release-build --output-on-failure
bash tests/package_macos_test.sh
```

A source archive can be configured, built and tested without Git metadata. `scripts/package_macos.sh package` requires a clean, exact Git commit. It builds a new package from source; it does not accept a prebuilt app as evidence of a new source build. See [release provenance](docs/RELEASE.md).

Automated tests use disposable fixtures. Do not reset TCC or change real user data or system settings to run tests. State-machine and mock tests do not establish real Gatekeeper, TCC, Chrome or paste behavior.

## Submit a change

Open a focused issue or pull request on [GitHub](https://github.com/amingclawdev/seethis). Describe the behavior change, relevant verification and known limits. Do not submit private screenshots, read-access capability URLs, keys, personal absolute paths, build caches or local configuration. Report security issues through the [private channel](SECURITY.md).

This project is released under [MIT](LICENSE), Copyright (c) 2026 SeeThis contributors. Contributors must have the right to provide their code or media and identify any third-party licenses. See [dependency and media notices](THIRD_PARTY_NOTICES.md).
