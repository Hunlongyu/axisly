# 示例与宿主接入

所有示例都由宿主提供内容尺寸并消费 Axisly 的几何结果。
图形示例是可选开发目标，其 UI 和渲染依赖不会进入生产库。

## 控制台示例

开启 `AXISLY_BUILD_EXAMPLES`，或使用顶层 `debug`／`release` 预设后，可运行：

| 源码 | 构建目标 | 内容 |
| --- | --- | --- |
| [css_grid.cpp](../examples/css_grid.cpp) | `axisly_api_example` | CSS 固定／fr／minmax 轨道、测量、跨度、对齐、嵌套、缓存与取整 |

程序位于相应构建目录，Windows 文件名带 `.exe`。Windows 双击启动并独占控制台时，
输出保留到按回车关闭；已有终端或重定向运行时直接退出。
CSS 混合树的最小例子见 [README](../README.md#快速开始)。

## SDL3 Playground

```powershell
./scripts/build-msvc.ps1 -Preset playground-release
./build/playground-release/axisly_playground.exe
```

其他平台使用 `cmake --preset playground-release`，再构建并运行对应程序。
该预设下载锁定的 SDL3、SDL_ttf 和 FreeType。使用现有开发包时，可只开启
`AXISLY_BUILD_PLAYGROUND=ON`，通过 `CMAKE_PREFIX_PATH` 提供 SDL3 与 SDL_ttf。
版本和校验值见 [示例 CMake 配置](../examples/playground/CMakeLists.txt)。

十个场景展示 CSS 固定、fr、auto、跨度、对齐、minmax 与同树嵌套布局：

- `1..0`、左右键或侧栏点击：切换场景。
- `G`／`B`／`M`：切换网格线、条目边框和数值标注。
- 鼠标悬停或上下键：查看条目 ID、跨度、单元格与最终矩形。
- `[`／`]`：调整画布宽度；拖动窗口边缘可观察重排。
- `R`：恢复尺寸；`Esc`：退出。

`AXISLY_FONT` 可指定本机 `.ttf`／`.otf` 字体。示例从宿主加载字体，不打包个人字体文件。

## GeeyoouUI Showcase（Windows）

```powershell
./scripts/build-msvc.ps1 -Preset geeyoou-release
./build/geeyoou-release/bin/axisly_geeyoou_showcase.exe
```

首次构建需要 Python 3.10+、Git 和网络，下载锁定的 GeeyoouUI、Blend2D 与 AsmJit。
19 个页面包含表单、表格、布局演示、HMI、主题、弹窗与 3D 视图。
Axisly 分配页面和容器矩形；控件内部、虚拟化表格、工艺图与 3D 场景保留自身算法。
适配器和测量实现位于 [examples/geeyoou](../examples/geeyoou/)，第三方声明见
[NOTICE](../examples/geeyoou/NOTICE.md)。

## 接入自己的 UI

1. 保持 `css::NodeId` 与宿主控件的映射，或通过 `LayoutSource` 借用宿主树。
2. 用宿主文字或控件测量接口处理布局请求。
3. 内容和样式变化后标脏或更新对应节点。
4. 计算成功后应用全部所需矩形，处理坐标转换、裁剪和滚动。

同一控件的外部矩形应只有一个布局管理者。线程绑定的控件和字体资源应在允许的线程测量。
统一接口、测量和借用宿主树的约定见 [布局指南](css-layout.md)。
