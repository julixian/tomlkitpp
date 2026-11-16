# AI Instructions

## `mcpp` 构建与索引验证注意事项

- **复用本机工具链缓存**：在 Windows PowerShell 调用 `mcpp` 前，确保设置 `$env:MCPP_HOME = "$env:USERPROFILE\.mcpp"`，直接复用本机已安装的 `llvm@23.1.3` 工具链，避免在空缓存目录下触发静默拉取而卡在 `Planning` 阶段。
- **`mcpp-index` 发包验证**：向 `mcpplibs/mcpp-index` 更新 `julixian.tomlkitpp` 时，不要在 `mcpp-index` 仓库子目录中直接运行不带 `-p <member>` 的裸 `mcpp build` / `mcpp test`（会触发根工作区 `.mcpp` 将本地索引目录递归拷贝进自身导致死循环）。本地冒烟测试应仅将 `index.toml` 与 `pkgs/j/julixian.tomlkitpp.lua` 复制到不含 `.mcpp` / `.git` 的纯净临时目录，并在仓库外的独立临时消费工程中通过 `[indices]` 指向该目录验证。
- **`mcpp` 防卡死**：若 `mcpp` 命令在后台长时间停留在 `Planning`（如超过 90 秒无构建进展），应及时检查日志并终止进程排查原因，不要无限等待。
