# 仓库开发约定

- 默认只在主工作区的 `main` 分支修改代码、提交和同步。
- 除非用户明确要求子分支，否则不要创建或切换到开发子分支，也不要创建用于开发的隔离 worktree。
- 已有子分支或 worktree 不作为后续开发的默认工作区。
- 同步代码时将已验证的提交推送到 `origin/main`。

## 提交规范

- 使用中文 Conventional Commits，格式为 `type(scope): 摘要` 或 `type: 摘要`。
- 标题不超过 50 字符，末尾不加句号；复杂变更在空行后补充中文说明。
- 存在破坏性变更时，正文以 `BREAKING CHANGE` 开头，说明影响和迁移方法。

## 构建与发布

- Windows 本地预检：`./scripts/build-msvc.ps1 -Preset release`，脚本通过含 `-prerelease` 的 `vswhere` 发现 MSVC。
- 首次发布已验证的本机入口：`C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat`，不固定 MSVC 版本目录。
- 版本以根目录 `CMakeLists.txt` 的 `project(axisly VERSION ...)` 为准，发布时同步中英文 README 和带日期的 `CHANGELOG.md` 条目。
- 发布源码包；先使目标提交的完整 CI 通过，再推送对应的单个 `vX.Y.Z` tag。不得覆盖既有 tag 或已公开资产。
- `.github/workflows/ci.yml` 在标签构建全部通过后，验证版本、打包源码、验证包内构建和安装后消费，再创建 GitHub Release。
- `scripts/package-release.py --tag vX.Y.Z --output <空目录>` 可预检当前提交；正式发布加 `--require-tag` 校验标签指向。
- 发布后下载同一次 Actions 的 `asset-report.json` 和 Release 资产，逐项核对文件名、大小和 SHA-256。
- 核心是 C++20/STL 库；Rust 仅用于可选的 Taffy 参考测试和比较基准，不是普通构建或使用依赖。
