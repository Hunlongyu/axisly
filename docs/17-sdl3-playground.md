# M9：SDL3 图形演示

版本 `0.0.10`。十个场景和交互使用已实现的公开 API，核心布局算法未因演示而改变。

## 构建与运行

Windows 推荐：

```powershell
./scripts/build-msvc.ps1 -Preset playground-release
./build/playground-release/axisly_playground.exe
```

脚本通过 `vswhere -prerelease` 找到当前 MSVC x64 与 Visual Studio CMake/Ninja。
Debug 替换为 `playground-debug`。普通 `debug` / `release` 预设不启用图形依赖。
其他平台可按 README 运行 CMake 预设。

独立启用 `AXISLY_BUILD_PLAYGROUND=ON` 默认使用 `find_package` 查找 SDL3 3.4+、
SDL_ttf 3.2+；`AXISLY_FETCH_SDL3=ON` 才下载下表中的源码，校验 SHA-256 后构建。
这个选项同时管理演示的图形和字体依赖。

| 依赖 | 固定来源 | SHA-256 |
| --- | --- | --- |
| SDL3 | 3.4.16 官方发布归档 | `7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68` |
| SDL_ttf | 3.2.2 官方发布归档 | `63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d` |
| FreeType | SDL_ttf 3.2.2 引用的 `9973564cfa63763a3e4ac67c09147899539b1e07` | `026a05a49d114a1235d2926f4c03a9330e4b1a6efe7c217ec9607904c32907d4` |

下载构建使用静态库，不要求额外 SDL DLL。已有的动态开发包会复制 CMake 声明的运行时 DLL；
系统包若有未声明的动态依赖，需要由该包的运行环境提供。图形目标不会安装到 axisly 核心包中。

本机某套 CMake 的 TLS 根证书配置不完整。遇到证书验证失败时，应修正证书配置或使用
已校验的本地源码，不关闭 TLS 校验。例如 SDL 源码准备好后：

```powershell
./scripts/build-msvc.ps1 -Preset playground-release -SdlSource ./build/deps/SDL3-3.4.16
```

通用 CMake 对应 `-DFETCHCONTENT_SOURCE_DIR_SDL3=<源码绝对路径>`；本地目录仍应与固定归档匹配。

## 字体

已按用户反馈替换像素调试字体，使用 SDL_ttf / FreeType 抗锯齿渲染。
Windows 默认使用 Consolas，与 [VS Code 的 Windows 默认字体定义](https://github.com/microsoft/vscode/blob/main/src/vs/editor/common/config/fontInfo.ts) 一致。
缺少时从本机字体目录寻找 Cascadia、Fira Code、Source Code Pro、Maple Mono。
Linux 支持 DejaVu Sans Mono / Liberation Mono，macOS 支持 Menlo / Monaco。

指定其他本机字体：

```powershell
$env:AXISLY_FONT = "$env:WINDIR/Fonts/consola.ttf"
./build/playground-release/axisly_playground.exe
```

字体文件不会复制进仓库或分发物；找不到字体或显式路径无效时显示错误。
文字按实际输出像素密度栅格化，按实测宽度省略过长文本；标签纹理缓存有上限。
当前界面使用英文短标签。Content 场景仍使用可重复的模拟测量值，未把真实字体度量
引入布局回调；复杂文字 shaping、彩色 emoji 和字体选择 UI 不在本阶段范围内。

## 布局与交互

完整场景和按键表见 [SDL3 演示](05-sdl3-visual-demo.md)。十个场景覆盖固定、两类权重、
混合、Content、跨列、跨行、16 种对齐、Range、嵌套。

宿主只调用 `compute_grid` 与 M7 的 `compute_nested_grid`。轨道尺寸从非跨格条目的
`cell_rect` 提取，不使用内部求解函数，不追加可能改变 Content 尺寸的探测条目。
嵌套 ID 显示完整路径，子条目坐标已转换到根网格空间。
父容器的测量来自适配器多次探测，不伪造单一 preferred 值，检查面板注明来源。

画布按容器裁剪溢出。金色 preferred 轮廓也裁剪到条目内，检查面板保留完整数值。
时间是最近一次宿主场景构建加布局的耗时，不包含绘制，不能当成核心微基准。
测量次数和布局次数直接来自结果。场景、尺寸、画布比例改变后重排，静止时等待事件。

## 构建可靠性修复

本机 CMake 自动检测的 `/showIncludes` 前缀出现中文编码错误，Ninja 曾记录零头文件依赖。
修改共享结构体后，测试目标因此复用了旧对象文件并崩溃；完整重编译后恢复。
单纯换用 Visual Studio CMake 或设置 `VSLANG=1033` 未消除本机问题。

`cmake/DetectMsvcIncludes.cmake` 在顶层 MSVC/Ninja 构建中，用相同的 `/utf-8` 编译方式
探测实际头文件输出，保留原始字节并设置 Ninja 使用的前缀。首次修复时失效旧依赖记录，
随后保留增量构建。作为子目录接入时不修改宿主构建规则。

## 验证范围

- `axisly.playground.scenes`：十场景乘五种容器，检查有限非负几何、重复计算一致性，
  并断言权重比例、跨格间隙、16 种对齐、Content 换行、Range 上下限和嵌套坐标。
- `axisly.playground.runtime`：隐藏 dummy 窗口、software 渲染器，遍历十场景乘三种
  窗口大小（1280×840、960×720、1600×1000），检查直接/循环导航、键盘检查、鼠标命中、
  图层、窄画布溢出、复位、Esc、实际事件循环等待与退出；保存 PNG。
- 原生窗口渲染可单独运行：

```powershell
./build/playground-release/axisly_playground.exe --smoke-test ./build/native-captures
```

CTest 设置 dummy/software 环境；上述直接运行在未设置驱动覆盖时使用本机窗口后端，
仍保持窗口隐藏。截图检查与几何测试是不同证据，不做像素级 golden 比较。
CI 配置不代表本机完成其他平台运行验证；多显示器 DPI 切换仍需人工覆盖。

2026-09-28 本机结果（MSVC x64）：Debug / Release 各 207/207 通过；
Windows + Direct3D 11 隐藏原生窗口的 30 组场景/尺寸和事件检查通过，实际加载
`C:\WINDOWS\Fonts\consola.ttf`（Consolas）。已检查常规、窄窗口、嵌套和溢出截图。
核心独立构建通过；安装接入与子目录接入各 1/1 通过。触碰场景头文件后，
Ninja 正确重新编译场景、图形入口和测试三个使用方，随后两项演示测试通过。
新增 Windows Playground CI 配置尚未在远端运行。

## 第三方声明

演示使用 [SDL3](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16)、
[SDL_ttf](https://github.com/libsdl-org/SDL_ttf/releases/tag/release-3.2.2) 和 FreeType。
Portions of this software are copyright (C) 1996-2023 The FreeType Project
(https://www.freetype.org). All rights reserved.

下载构建会在可执行文件旁生成 `playground-licenses/`，保存 SDL、SDL_ttf 和 FreeType 许可。
分发图形示例时一并保留该目录。核心安装产物不包含这些第三方库或本机字体。
