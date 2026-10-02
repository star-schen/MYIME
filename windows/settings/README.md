# MYIME 独立设置程序

Windows x64、.NET Framework 4.8、C# 5 / WinForms。无需 NuGet 包、.NET SDK 或网络服务。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-settings.ps1 -Configuration Release
build/Release/myime-settings.exe
```

与 `myime-tool.exe`、`myime-data-tool.exe`、`myime-theme-preview.exe` 一起发行。

- 基础设置编辑全局默认或 Windows 覆盖；空白 / 继承删除这一层的覆盖。
- 应用配置只匹配 EXE 文件名。只编辑已知字段，未知扩展项由 Rust 工具保留。
- 候选主题支持选择、原文编辑、内置主题的用户覆盖、新建模板和预览；校验与冲突规则由 Rust 工具处理，安装主题不会被覆盖。
- 高级配置提供产品 TOML 原文编辑；语法损坏时仍可读取原文并修复。
- Rime 配置编辑 `%LOCALAPPDATA%/MYIME/rime/patches/*.custom.yaml`，通过单独的维护工具部署和发布、回退上次发布或恢复基础数据。基础数据和运行中的 userdb 不被设置程序覆盖。
- TXT/CSV 导入先预览，生成全新数据词库包；需要用户明确部署并启用新方案，不猜测编码、不直接修改 userdb。
- “启用最近部署方案”只修改 Windows 配置层并核对返回的有效方案；AppProfile 的明确覆盖保留。恢复基础数据前，在基础设置的 Windows 覆盖中保存 `schema = 'pinyin_simp'`，并清理引用其他方案的 AppProfile；恢复 / 回退预检失败时不发布，也不自动修改配置。
- “新模板名称”只用于新建主题模板；编辑已有主题的名称需修改主题 TOML 的 `name`。
- 数据、日志和文档提供目录快捷入口。

维护操作使用有大小和时间限制的 UTF-8 JSON stdin/stdout；输入法热路径不运行设置进程、不使用此 IPC。配置、Patch 和导入源使用 Rust 维护工具的修订契约。外部修改导致冲突时界面保留未保存内容并提示重新读取，没有静默覆盖。

普通运行不自动创建或修改产品配置。只在用户点击保存、生成词库包、部署或目录按钮时执行对应操作。保存配置后重启目标应用，不自动安装、注销输入法或替换 DLL。

`--data-root <绝对路径>` 用于显式的独立数据目录。系统 DPI aware，基础缩放由 WinForms 处理；混合 DPI 屏幕移动仍需真实机器复测。

`--self-check` 在可执行文件旁创建和清理自己独有的 fixture，不读取普通用户配置；检查真实维护工具的读写、继承、AppProfile、冲突、未知字段保留、YAML、主题模板/保存、导入预览/数据包和异步完成。透明、无激活窗口通过 `DrawToBitmap` 输出六页到 `test-artifacts/settings-*.png`，不进行真实应用或输入法安装验收。
