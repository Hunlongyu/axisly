# 构建与安装

需要 C++20 编译器和 CMake 3.25+。预设使用 Ninja；也可通过 `-S`／`-B` 选择其他生成器。
生产库只使用 C++ 标准库，由 CMake 配置平台线程支持。

## 构建和测试

Windows 推荐使用脚本，它会通过 `vswhere` 发现 Visual Studio（包括预览版），
初始化 MSVC x64 环境，再配置、构建和运行测试：

```powershell
./scripts/build-msvc.ps1 -Preset release
```

安装 LLVM 后可使用 `-Preset release-clang`，通过 clang-cl／lld-link 和同一套 Windows SDK 构建。
调试构建使用 `-Preset debug`。

Linux、macOS 或已经初始化编译器环境的 Windows 终端：

```text
cmake --preset release
cmake --build --preset release
ctest --preset release
```

不使用 Ninja 时，可用本机默认生成器：

```text
cmake -S . -B build/default -DCMAKE_BUILD_TYPE=Release
cmake --build build/default --config Release
ctest --test-dir build/default -C Release --output-on-failure
```

## 安装与消费

```text
cmake --install build/release --prefix build/install --config Release
```

消费工程将安装位置加入 `CMAKE_PREFIX_PATH`，再链接导出目标：

```cmake
find_package(axisly CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE axisly::axisly)
```

直接嵌入源码时使用 `add_subdirectory(path/to/axisly)` 和相同目标即可。
公共入口是 `<axisly/axisly.hpp>`，也可以单独包含所需头文件。
更换版本或编译配置后，应重新编译库和消费者；当前没有跨版本 ABI 承诺。

## CMake 选项

| 选项 | 默认值 | 用途 |
| --- | --- | --- |
| `AXISLY_BUILD_TESTS` | 顶层项目 ON，子项目 OFF | 核心契约测试 |
| `AXISLY_BUILD_EXAMPLES` | 顶层项目 ON，子项目 OFF | 控制台示例 |
| `AXISLY_BUILD_REFERENCE_TESTS` | OFF | Taffy 差分测试，需要测试开关、Python 3.10+ 和 Rust/Cargo |
| `AXISLY_BUILD_BENCHMARKS` | OFF | 布局计时、分配统计及压力场景 |
| `AXISLY_BUILD_COMPARISON_BENCHMARKS` | OFF | 三方基准，需要 benchmark 开关、Python 和 Rust/Cargo |
| `AXISLY_BUILD_PLAYGROUND` | OFF | SDL3 图形示例 |
| `AXISLY_FETCH_SDL3` | OFF | 为图形示例下载锁定的依赖 |
| `AXISLY_BUILD_GEEYOOU_SHOWCASE` | OFF | Windows GeeyoouUI 示例，下载锁定的依赖 |
| `AXISLY_ENABLE_SANITIZERS` | OFF | 开发构建的 ASan／UBSan |
| `AXISLY_ENABLE_TSAN` | OFF | Linux GCC／Clang 的线程竞争检测，与 ASan 分开构建 |
| `AXISLY_ENABLE_COVERAGE` | OFF | Clang 的行与分支覆盖率，与 Sanitizer 分开构建 |
| `AXISLY_CLANG_PGO` | OFF | Clang 的 `GENERATE`／`USE` 配置 |

普通构建不下载参考实现或 UI 依赖。可选工具的运行方法见
[测试指南](testing.md)、[示例](examples.md)和[性能基准](../benchmarks/comparison/README.md)。
PGO 流程见[执行资源](css-execution-resources.md#可选-clang-pgo)。

## 旧名称迁移

旧的私有开发名称为 `gridly`。头文件、命名空间、CMake 包／目标和构建选项前缀
分别改为 `axisly/`、`axisly::`、`axisly`／`axisly::axisly` 和 `AXISLY_`。
旧名称没有保留公开别名，已有宿主需要更新引用并重新构建。
