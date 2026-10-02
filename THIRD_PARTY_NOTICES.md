# Dependency and media notices

SeeThis source and project-created icon artwork are released under [MIT](LICENSE), Copyright (c) 2026 SeeThis contributors. Third-party components and recorded third-party interfaces retain their respective rights.

## Runtime and build dependencies

| Component | Use and distribution | Primary license reference |
| --- | --- | --- |
| Apple AppKit, ApplicationServices, Carbon, ScreenCaptureKit and related frameworks | macOS-supplied system frameworks, dynamically linked; no Apple SDK source bundled | Apple platform and SDK terms |
| C++ standard library, Threads, libSystem, libobjc | macOS/toolchain system runtime | System/toolchain terms |
| zlib | PNG compression; dynamically linked to macOS `/usr/lib/libz.1.dylib`, current link version 1.2.12 | [Upstream zlib 1.2.12 copyright and license](https://github.com/madler/zlib/blob/v1.2.12/README#L77-L104) |
| CMake/CTest 3.25+ | Development tools, not bundled in SeeThis | [BSD 3-clause](https://cmake.org/licensing/) |
| Ninja | Development build runner, not bundled | [Apache 2.0](https://github.com/ninja-build/ninja/blob/master/COPYING) |
| Apple clang, Xcode/Command Line Tools | Compiler and SDK, not bundled | Apple/toolchain terms |
| Node.js 18+ | Development-only viewer JavaScript tests, not a runtime dependency | [Node distribution notices](https://github.com/nodejs/node/blob/main/LICENSE) |
| Python 3, Bash and macOS packaging commands | Development/packaging only | Respective tooling terms |

No vendored third-party source or package manager runtime dependency is included. The release executable links only system paths. `otool -L` reports zlib current version 1.2.12 and libc++ current version 2100.43.0; these are link compatibility/version readbacks, not a claim that an upstream distribution is bundled. Final build tool versions are in [RELEASE](docs/RELEASE.md). System zlib originates with Jean-loup Gailly and Mark Adler; SeeThis does not claim authorship or redistribute altered zlib source.

## Project icon

`assets/app-icon/SeeThis.svg`, `SeeThis.png` and `SeeThis.icns` are project-created geometric artwork. The editable geometry, colors and raster/icon generation are defined in `scripts/generate_app_icon.swift` using Apple drawing APIs; no downloaded icon or stock asset input is used. The bundled ICNS SHA-256 is `6c10f576d1935b63e2778dfe05476c2e31a00d96016546e74b3ea2b0083d397e`. This source and artwork are included in the approved MIT publication.

## Demo media

The project owner explicitly authorized public reuse of their original v3 demonstration videos without added privacy masks. The MP4 files below are byte-identical copies of the original landscape and portrait exports, including their English subtitles, scene order, animated zoom and narration. The thumbnails are frames extracted at 0.5 seconds from those exports. The recorded narration voice is Microsoft Andrew Multilingual Neural. Recorded app interfaces, browser UI and webpage imagery retain their respective ownership; this notice does not claim ownership of those interfaces or endorsement by their vendors.

The public files are:

| File | SHA-256 |
| --- | --- |
| `docs/demo/SeeThis-landscape-v3-public.mp4` | `e91003bda6ff3f92bb3877a5503089e1ff18665a0f5c31ee25fa890e45449e9a` |
| `docs/demo/SeeThis-landscape-v3-thumbnail.jpg` | `958ce9b0f8d8fb9785e95b6e3c5f816becd77ed7f3bfd95636d9a47777ed8c5e` |
| `docs/demo/SeeThis-portrait-v3-public.mp4` | `91a4e08cdb889415f1b7634701b2fe0781ff611f7c0b13cde9aea2f5348ed043` |
| `docs/demo/SeeThis-portrait-v3-thumbnail.jpg` | `ca4b72cd5686d5961f0ac95d8e967c127eb7d2c34f1d3db6db334892e1226aed` |

The recording shows an earlier Inspector workflow; current behavior is documented in [USAGE](docs/USAGE.md). Editing projects, longer source recordings and private review records are excluded from the repository.
