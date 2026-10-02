# MYIME — Windows Rime frontend MVP

MYIME 是一个以 librime 为输入引擎的 Windows TSF 前端。C++ 负责 Windows/COM/文本编辑和候选窗口，Rust 负责输入状态、产品配置、AppProfile 及 Rime 安全封装。按键路径全部在应用进程内，无 socket、pipe、HTTP 或设置 GUI 依赖。

当前是开发版。2026-10-03 已实现路线图阶段 3 的**独立 C# 设置、Rust 配置 Bridge 和离线 Rime 部署**，并完成阶段 4 的 TXT/CSV 导入及数据词库包切片。多个显式部署包可进入稳定的 `myime_global` 全局方案，各包方案保留给 AppProfile。自动回归结果持续记录在 [测试记录](docs/testing.md)；**未自动安装或替换正在使用的 DLL，真实应用仍需安装后验收**。Windows 搜索能提交但无候选栏的问题仍待目标日志定位，尚未解决。参见 [设置程序](windows/settings/README.md)、[导入说明](docs/importing.md)、[主题使用](docs/themes.md)、[架构](docs/architecture.md) 和 [路线图](docs/roadmap.md)。

## 已实现

- x64 TSF COM DLL、注册/卸载入口、键盘 sink、焦点与 context 生命周期。
- Windows Host → C ABI v1 → 安全 Rust Core → InputProvider → RimeProvider → librime 官方版本化 C API；RimeProvider 是唯一生产输入实现，没有重新实现输入算法。
- 官方 `pinyin_simp` schema/dictionary、Rime session、用户词库、preedit、候选分页/选词、commit。
- TSF edit session 中的 composition/文本提交、UTF-8 caret 到 UTF-16 转换。
- 不夺焦点的候选窗口：跟随 TSF caret，键盘/鼠标选词、上一页/下一页；横竖布局、字体/颜色/间距、圆角/柔和阴影、高对比度系统颜色。
- TOML 数据主题包：雾灰、素白、墨夜、灰色横排；用户包覆盖/撤销、损坏回退到内嵌雾灰；同一绘制代码的独立预览。
- 独立 C# / WinForms 设置程序：基础设置、候选主题、应用配置、高级配置、Rime 配置、词库导入六页。主题页可以选择、编辑 TOML、建立用户模板并启动原生预览；不覆盖安装主题。
- TOML 产品配置与 EXE AppProfile；输入配置变化先完成新 Provider 的配置和初始快照，成功后替换旧实例，失败保留旧实例及有效配置；组合/待确认提交期间禁止变更输入配置，仅主题变化复用 Provider 并保留状态。
- Rust 配置 Bridge：结构化编辑保留未知 TOML 和注释，原文编辑支持修复损坏配置；mtime + SHA-256 修订检查、冲突拒绝、同目录备份和发布。原生 Rime 仅编辑用户自己的 `*.custom.yaml`，保持第三方 schema 边界。
- TXT/CSV 经真实 Importer 和 Converter 生成全新数据包，包含 manifest、字典、metadata、聚合字典与自有 schema；严格引号/编码/范围校验、重复合并、缺码报告和 SHA-256 包校验，不猜测拼音、不直接修改 userdb。
- Windows 离线代际部署：隔离目录构建和预检全部方案后发布，新启动的进程使用新一代；保留原代，支持回退和恢复基础。全局 `myime_global` 包含所有显式部署包，单包 `myime_<id>` 供用户明确选择；发布、回退和恢复会拒绝悬空配置引用。
- 搜索候选元数据诊断：显示协商、Show、上下文/HWND、caret 布局 HRESULT、实际可见性和隐藏原因，去重并限量，不记录正文。
- UI-less 当前页候选快照、受限应用独立数据目录、空闲状态标点交给 Rime。
- 嵌入 DLL 的 MY 品牌图标、“中 / A”模式指示及右键“设置”入口。普通桌面安装配套设置程序后可打开；受限环境或程序缺失时置灰。左键切换暂未实现。
- 兼容性测试应用、测试专用 Provider；InputProvider/InputProviderFactory、Importer/Converter 和产品 ConfigBridge 已有实际实现。SyncProvider、DictionaryProvider、CompatibilityProvider 仍是源码级扩展合约，没有外部 DLL 插件加载。

## 构建环境

Windows 10/11 x64；Visual Studio 2022 的 **使用 C++ 的桌面开发**（MSVC x64、Windows SDK）；Rust 1.89 或更新的 `stable-x86_64-pc-windows-msvc`；Git；.NET Framework 4.8。设置程序使用系统自带的 Framework x64 C# 编译器，无需 NuGet 或 .NET SDK。CMake 和 librime 由脚本下载到项目目录，不修改系统 PATH。

本机工具链为 Rust 1.98.1、MSVC 19.44、SDK 10.0.26100.0、CMake 4.4.3、librime 1.17.0；历史 MVP 构建过 Debug/Release，本轮以 Release 为交付配置。第三方二进制 hash 与数据 commit 固定在 `dependencies.lock.json`；Rust 依赖固定在 `Cargo.lock`。

在项目根目录的 PowerShell 中执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare-data.ps1 -Configuration Release
```

构建脚本会自动导入 VS 开发环境，并构建 Host/Core、独立 Rust 维护工具、Windows 数据部署工具和 C# 设置程序，无需永久修改 PATH。依赖下载需要 GitHub/crates.io 网络连接。构建本身不执行测试。`prepare-data.ps1` 部署官方数据到 `runtime`，再将只读共享数据放进 `build/Release/data/shared`；它不接触其他输入法的用户词库。

运行测试是独立、显式的操作：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -Configuration Release
```

该脚本包括构建、安全 Core/配置/导入合约测试、维护工具 JSON 协议、设置程序 fixture 自检、真实 librime/C ABI、COM 生命周期、真实 TSF 文档和兼容性程序初始化。本轮已获授权执行，实际结果与尚待验收的范围见 [测试记录](docs/testing.md)。TSF probe 自动开启当前进程的诊断日志，不安装系统 profile；需要交互式 Windows 用户会话。安全 Core 和维护工具可不链接 librime 独立测试。

## 打包、安装和卸载

**开发机推荐：双击项目根目录 `Install-MYIME.cmd`，选择 `1`。** 安装助手依次构建 Release、部署数据、打包，成功后才弹出 UAC 请求安装权限。选择 `2` 只安装已有包，选择 `3` 卸载；不会自动运行测试。助手尚未运行验收，不是独立分发的 MSI/EXE 安装包。

升级无需事先手动卸载：新版放入 `%ProgramFiles%/MYIME/versions/<版本目录>` 后更新注册，不覆盖旧 DLL；注册失败会尝试恢复旧版注册并报告结果。完成后注销再登录，避免系统仍使用旧版。词库、配置和旧版文件保留；日志在 `%TEMP%/MYIME-Setup`。构建失败不会撤销当前输入法。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -Configuration Release
```

产物位于 `out/MYIME-Release`。这是本地开发包；不自动上传二进制或发布 GitHub Release。

在 **64 位管理员 PowerShell** 中，从项目根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1 -Configuration Release
```

安装会将二进制复制到 `%ProgramFiles%/MYIME`，注册 COM、简体中文 TSF profile、keyboard/UI-less/immersive/system tray 类别。使用 Windows 语言设置添加/选择 `MYIME Rime`，或通过 Win+Space 切换。必要时重启目标应用或重新登录。不会更改其他输入法或默认输入法。

目标机器需具备 Microsoft Visual C++ x64 运行库；开发机安装上述 VS 组件时已具备。MVP 安装脚本暂不自动分发运行库或签名二进制。

依照协作约定，本次未运行安装脚本。系统注册、真实系统键盘路由、Notepad/Edge/开始菜单中的表现都仍待安装后验收。Debug DLL 依赖开发工具运行库，普通使用请安装 Release。

卸载（管理员 PowerShell）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/uninstall.ps1
```

卸载按当前注册路径撤销输入法，兼容旧版固定目录和新版版本目录，保留二进制和用户词库。升级可直接使用安装助手；脚本不会强杀应用、自动注销或强行覆盖已加载 DLL。

## 第一轮使用

在 Notepad / Edge 普通文本框中选择 MYIME 后输入 `nihao`，用空格/数字键选择候选；PageUp/PageDown 或候选窗口底部翻页；Esc 取消。Shift 的中英文切换由 Rime schema 的 `ascii_composer` 决定。Ctrl/Alt/Win 组合键让应用处理，尚不支持所有 schema 快捷键。

安装配套新版后，可右键模式图标打开“设置”，或运行 `out/MYIME-Release/myime-settings.exe`。六页提供全局默认/Windows 覆盖、EXE AppProfile、主题与高级原文编辑，以及 Rime patch 和导入。设置程序只在用户保存/生成/部署时执行对应写入；保存后重启目标应用。开发包也可用 `--data-root <绝对路径>` 打开独立配置目录。

配置示例：首次配置可将 `config/product.example.toml` 复制到 `%LOCALAPPDATA%/MYIME/config.toml`；已有配置通过设置或原文编辑修改，不用示例覆盖。实际可配置项为 `enabled`、`schema`、`options.<Rime option>`、`ui.theme`。schema 必须已部署；不根据应用名称猜测词库或兼容策略。Windows 的“启用最近部署方案”写入 `[platform.windows]` 并核对返回有效方案，明确的 AppProfile 覆盖仍优先，不将写 Default 误称为已经启用 Windows 方案。

用户数据位于 `%LOCALAPPDATA%/MYIME/rime/slots/<n>`。每个应用进程租用一个持久槽位，该进程的所有 TSF 线程共享它；目录不会自动删除。**并发进程的学习词频目前不自动合并**，后续用 Rime sync export/merge 处理，不能直接覆盖正在使用的 userdb。

原生 Rime schema/dictionary 保留兼容。设置程序将用户 patch 保存在 `%LOCALAPPDATA%/MYIME/rime/patches/*.custom.yaml`，保存与部署是两个明确操作。部署工具创建 `rime/workspaces/<generation>`，复制基础数据、此前显式部署的包和用户 patch，调用官方 Rime 部署并预检，成功才发布 `rime/active-workspace.txt`。运行中的进程继续使用已有数据代和词库租约，不在按键路径部署或切换。

导入页先预览 UTF-8 TXT/CSV，再生成新数据包；缺少编码的词条仅进入 metadata 和报告，不自动编码。显式部署词库包后，`myime_global` 汇总所有已部署包，`myime_<id>` 保留供 AppProfile 指定；未部署的包不会被自动扫描启用。详情见 [导入说明](docs/importing.md)。跨学习槽协调和 userdb 合并尚未实现。

回退和恢复基础不会自动改配置或删学习数据。目标代没有当前配置引用的 schema 时会拒绝发布：先把全局默认、Windows 覆盖及 AppProfile 中即将失效的方案改为目标代已有方案；恢复基础时使用 `pinyin_simp`，保存后再点“恢复基础数据”。不要通过删除词库槽来处理失败。外部修改检测发生在读取/保存时；自动文件监视和双向同步仍属后续。

## 兼容性测试程序

主题预览：在设置“候选主题”页打开实时预览，或运行 `out/MYIME-Release/myime-theme-preview.exe`，选择主题或临时修改预览字号/布局；不需要安装、不改变当前输入配置。外观 TOML 编辑保存后刷新预览；设置页“应用为全局默认”修改 Default，Windows/AppProfile 的主题覆盖仍优先。完整主题制作/撤销说明见 [themes.md](docs/themes.md)。

```powershell
build/Release/myime-compat.exe
build/Release/myime-compat.exe --uiless
```

包含普通 Windows EDIT、自绘 TSF 文档、composition 拒绝开关、TSF UI Element 候选观察，以及 7/9 个显示槽位。日志显示输入法报告的候选索引；MYIME 当前采用局部单页快照，page offset 为 0，真实翻页需对照候选内容。显示槽位数不会修改 Rime page size；不记录键入正文到文件。先设置测试选项，再聚焦文本区输入。

`--uiless` 用于比较实现 UI-less 协议的输入法。UI Element 当前页适配的独立 TSF 回归包含应用允许/禁止 Host 自绘，结果见测试记录；不等于已通过开始菜单或游戏测试。IMM32 仅在测试程序中观察消息，Host 尚无 legacy IMM32 adapter。

## 调试与目录

VS 附加到使用输入法的应用进程，加载 `build/Debug/myime_host.pdb`，观察 OutputDebugString 的 `MYIME:` 日志。设置目标进程环境变量 `MYIME_DIAGNOSTICS=1` 可启用 `%LOCALAPPDATA%/MYIME/logs/host-<PID>.log`；受限应用使用系统返回的自身本地目录下的 `MYIME/logs`。目标进程需重新启动并继承环境变量。每文件约 1 MiB 循环截断，不可写时仅调试输出；新增候选诊断只有 opt-in 时启用，连续相同消息五秒内抑制、每通道每秒至多 12 条。Rime 自身 ERROR 日志仍使用其默认位置，通常为临时目录。不记录按键/正文。Windows 搜索最小采集步骤见 [搜索候选诊断](docs/search-candidates.md)。

```text
crates/core/          Rust 状态、配置 Bridge、Importer、数据包规则、FFI、Rime 安全封装
crates/tools/         独立 Rust JSON 维护工具；不需要 native Rime
crates/core/native/   官方 rime_get_api 的机械转发桥接
include/myime/       唯一公共 C ABI 契约
windows/host/        TSF、COM、候选窗口、Windows 生命周期与用户目录租约
windows/compat/      兼容性测试程序与 ITextStoreACP 文档
windows/theme_preview/ 独立原生候选主题预览
windows/settings/     独立 C# / WinForms 六页设置程序
windows/data_tool/    离线 Rime 部署、代际预检/发布/回退/恢复
themes/              manifest 和外观数据，无 DLL 或脚本
tools/probe/         C ABI、COM、TSF 自动测试入口
scripts/             依赖、构建、部署、测试、打包、安装/卸载
docs/                架构约束、测试范围与后续工作
```

## 当前边界

仅 x64；尚无 x86/ARM64、IMM32 adapter、完整搜索建议集成、完整 UIA provider 或 WebDAV。独立设置、主题编辑与原生预览、TXT/CSV 和数据词库包已实现；复杂多屏 DPI/高对比度和真实输入仍待验收。百度/搜狗/QQ 等专有格式导入、自动编码、跨学习槽协调/userdb 合并和外部代码插件尚未实现。UI-less 只导出当前 Rime 页，不能跨页随机访问或接受应用重新分页；受限应用的学习和配置独立，普通桌面设置不会绕过其目录权限。受保护/沙箱应用、外部选择区移动及异常 text store 行为仍需实际应用测试。不声明安全桌面支持。

StartComposition 被应用拒绝时，当前 Windows 会保留一个独立的 composition 回调对象；它不持有服务/Core，DLL 会按 COM 引用计数保持映射直到外部引用释放。文档写入失败后暂停该 context 的输入，等待焦点切换恢复；不伪造成功 commit。详情见 [架构说明](docs/architecture.md)。

源码沿用仓库 MPL-2.0。第三方依赖及数据的许可独立，见 [THIRD_PARTY.md](THIRD_PARTY.md)。
