# 仓库开发约定

开始任务时先阅读 [llms.txt](llms.txt) 定位项目资料，再按任务阅读下列源码入口与相关测试。
本文件保存开发约定，`llms.txt` 保存精简文档索引，详细接口与行为说明保留在 `docs/`。

- 默认只在主工作区的 `main` 分支修改代码、提交和同步。
- 除非用户明确要求子分支，否则不要创建或切换到开发子分支，也不要创建用于开发的隔离 worktree。
- 已有子分支或 worktree 不作为后续开发的默认工作区。
- 同步代码时将已验证的提交推送到 `origin/main`。

## 项目定位与边界

- Axisly 是面向原生 UI 的跨平台 C++20/STL 静态布局库，支持混合 Block、Flexbox、Grid 布局树。
- 宿主提供控件、内容测量和绘制，库接收类型化样式并计算几何；CSS 字符串解析、级联、DOM 和文字塑形不在核心范围内。
- CMake 包名为 `axisly`，链接目标为 `axisly::axisly`，公共头文件位于 `include/axisly/`，布局接口位于 `axisly::css`。
- 布局语义以锁定的 Taffy 0.14.0 为兼容参照；与浏览器规范的差异见 [规范对照](docs/css-conformance.md)，参考测试通过不等于完整 CSS 规范通过。
- 核心只依赖 C++ 标准库及平台线程支持。Rust/Cargo 仅用于可选的 Taffy 参考测试和比较基准；SDL3、GeeyoouUI 等依赖只属于可选示例。

## 源码与任务入口

| 任务 | 优先阅读 |
| --- | --- |
| 公共接口、样式与测量 | [include/axisly/](include/axisly/) 定义公共 API，[src/css_tree.cpp](src/css_tree.cpp) 管理布局树；先读 [布局指南](docs/css-layout.md) 理解调用契约 |
| Block、Flexbox、Grid 算法 | [src/css/layout_engine.cpp](src/css/layout_engine.cpp) 是内部引擎入口；具体算法从同目录的 [block_layout.cpp](src/css/block_layout.cpp)、[flex_layout.cpp](src/css/flex_layout.cpp)、[grid_layout.cpp](src/css/grid_layout.cpp) 进入 |
| 缓存失效、宿主已有树接入 | [src/css/layout_cache.cpp](src/css/layout_cache.cpp)、[src/css/layout_source.cpp](src/css/layout_source.cpp)；宿主接口见 [css_source.hpp](include/axisly/css_source.hpp) |
| 内存复用、结果发布、批量计算 | [执行资源说明](docs/css-execution-resources.md)，实现见 [src/css_memory.cpp](src/css_memory.cpp)、[src/css_batch.cpp](src/css_batch.cpp) 和 [src/css/layout_fragment.cpp](src/css/layout_fragment.cpp) |
| 核心行为与回归测试 | [tests/css/](tests/css/) 保存契约测试，[tests/CMakeLists.txt](tests/CMakeLists.txt) 注册可筛选的 CTest 用例 |
| Taffy 差分与浏览器差异 | [tests/reference/](tests/reference/) 保存参考驱动、输入和属性矩阵；[tests/spec/](tests/spec/) 保存浏览器重放输入，运行方法见 [测试指南](docs/testing.md) |
| 宿主集成与示例 | [examples/css_grid.cpp](examples/css_grid.cpp) 是控制台示例；[examples/playground/](examples/playground/) 和 [examples/geeyoou/](examples/geeyoou/) 是可选 GUI 示例，使用方式见 [示例指南](docs/examples.md) |
| 性能与分配优化 | [benchmarks/](benchmarks/) 保存基准；先读 [比较基准说明](benchmarks/comparison/README.md) 确认输入、输出模式、几何检查与计时口径 |
| 构建、安装与打包 | [CMakeLists.txt](CMakeLists.txt)、[CMakePresets.json](CMakePresets.json)、[cmake/](cmake/)、[scripts/](scripts/)；[tests/consumer/](tests/consumer/) 验证安装包与源码消费，CI 入口是 [.github/workflows/ci.yml](.github/workflows/ci.yml) |

## 修改时保留的契约

- 公共行为调整同步相关头文件、实现、契约测试和对应文档；新增源文件或测试时更新所属 `CMakeLists.txt`。
- `LayoutTree::mark_dirty` 只失效当前节点和祖先，整棵子树使用 `mark_subtree_dirty`；宿主树拓扑变化、删除或复用 ID 后使用 `LayoutSession::invalidate()`。
- 测量回调返回内容盒尺寸；结果坐标、借用引用、异常恢复与线程约定按 [布局指南](docs/css-layout.md) 和 [执行资源说明](docs/css-execution-resources.md) 处理。
- 算法优化先保持几何、缓存失效和异常恢复契约，再比较性能；Taffy 兼容性与浏览器规范验证分别记录。
- 构建产物、下载的依赖和生成的参考结果留在 `build/`，修改源文件或生成器，不直接修补生成产物。

## 提交规范

- 使用中文 Conventional Commits，格式为 `type(scope): 摘要` 或 `type: 摘要`。
- 标题不超过 50 字符，末尾不加句号；复杂变更在空行后补充中文说明。
- 存在破坏性变更时，正文以 `BREAKING CHANGE` 开头，说明影响和迁移方法。

## 构建与验证

- 以下命令在仓库根目录执行。需要 C++20 编译器和 CMake 3.25+；项目预设使用 Ninja。
- Windows 日常预检：`./scripts/build-msvc.ps1 -Preset release`，脚本通过含 `-prerelease` 的 `vswhere` 发现 MSVC，初始化 x64 环境并依次配置、构建、运行 CTest。
- 首次发布已验证的本机入口：`C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat`，不固定 MSVC 版本目录。
- 调试构建使用 `./scripts/build-msvc.ps1 -Preset debug`。C++ 变更运行核心构建和测试，布局语义变更按影响范围补充参考测试：`./scripts/build-msvc.ps1 -Preset reference-release`，另需 Python 3.10+ 和 Rust/Cargo。
- Linux、macOS 或已初始化编译器环境的 Windows 终端使用：

  ```text
  cmake --preset release
  cmake --build --preset release
  ctest --preset release
  ```

- 预设产物位于 `build/<preset>/`。安装与消费验证见 [构建指南](docs/building.md)，Sanitizer、覆盖率及参考测试筛选见 [测试指南](docs/testing.md)。
- 纯文档修改核对路径、链接、命令与当前实现，并运行 `git diff --check`；无需为此重跑 C++ 构建。交付时明确本次实际验证范围。

## 发布

- 版本以根目录 `CMakeLists.txt` 的 `project(axisly VERSION ...)` 为准，发布时同步中英文 README 和带日期的 `CHANGELOG.md` 条目。
- 发布源码包；先使目标提交的完整 CI 通过，再推送对应的单个 `vX.Y.Z` tag。不得覆盖既有 tag 或已公开资产。
- `.github/workflows/ci.yml` 在标签构建全部通过后，验证版本、打包源码、验证包内构建和安装后消费，再创建 GitHub Release。
- `scripts/package-release.py --tag vX.Y.Z --output <空目录>` 可预检当前提交；正式发布加 `--require-tag` 校验标签指向。
- 发布后下载同一次 Actions 的 `asset-report.json` 和 Release 资产，逐项核对文件名、大小和 SHA-256。

## 文档维护

- `README.md` 保持英文，`README.zh-CN.md` 保留完整中文及双向语言链接；开发指引沿用中文。
- 路径、构建命令或接口边界变化时同步本文件和 `llms.txt` 的相关入口。索引只保留摘要与链接，不复制完整 API 文档、测试统计或性能结果。
