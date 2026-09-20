# MYIME — Windows Rime frontend MVP

MYIME 是一个以 librime 为输入引擎的 Windows TSF 前端。C++ 负责 Windows/COM/文本编辑和候选窗口，Rust 负责输入状态、产品配置、AppProfile 及 Rime 安全封装。按键路径全部在应用进程内，无 socket、pipe、HTTP 或设置 GUI 依赖。

当前是 **可构建、可部署数据、已通过组件及 TSF 文档测试的开发版**，不是已完成桌面应用验收的发布版。尚未在本轮执行系统注册、Notepad/Edge 手工输入或游戏测试。先阅读下方限制，再安装。

## 已实现

- x64 TSF COM DLL、注册/卸载入口、键盘 sink、焦点与 context 生命周期。
- C++ → C ABI → Rust → librime 官方版本化 C API；没有重新实现输入算法。
- 官方 `pinyin_simp` schema/dictionary、Rime session、用户词库、preedit、候选分页/选词、commit。
- TSF edit session 中的 composition/文本提交、UTF-8 caret 到 UTF-16 转换。
- 不夺焦点的竖排候选窗口：跟随 TSF caret，键盘/鼠标选词、上一页/下一页。
- TOML 产品配置与 EXE AppProfile，基本诊断日志。
- 兼容性测试应用，以及 Importer、SyncProvider、DictionaryProvider、InputProvider、Converter、ConfigBridge、CompatibilityProvider 接口。

## 构建环境

Windows 10/11 x64；Visual Studio 2022 的 **使用 C++ 的桌面开发**（MSVC x64、Windows SDK）；Rust `stable-x86_64-pc-windows-msvc`；Git。CMake 和 librime 由脚本下载到项目目录，不修改系统 PATH。

已在本机使用 Rust 1.98.1、MSVC 19.44、SDK 10.0.26100.0、CMake 4.4.3、librime 1.17.0 完成 Debug/Release 构建。第三方二进制 hash 与数据 commit 固定在 `dependencies.lock.json`；Rust 依赖固定在 `Cargo.lock`。

在项目根目录的 PowerShell 中执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare-data.ps1 -Configuration Release
```

构建脚本会自动导入 VS 开发环境，因此无需永久修改 PATH。依赖下载需要 GitHub/crates.io 网络连接。构建本身不执行测试。`prepare-data.ps1` 部署官方数据到 `runtime`，再将只读共享数据放进 `build/Release/data/shared`；它不接触其他输入法的用户词库。

运行测试是独立、显式的操作：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -Configuration Release
```

该脚本包括构建、3 个配置测试、真实 librime 测试、COM 生命周期测试、真实 TSF 文档测试和兼容性程序初始化。TSF 测试需要交互式 Windows 用户会话；受限沙箱中可能无法访问 COM 或用户目录。完整覆盖范围见 [测试记录](docs/testing.md)。

## 打包、安装和卸载

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -Configuration Release
```

产物位于 `out/MYIME-Release`。这是本地开发包；不自动上传二进制或发布 GitHub Release。

在 **64 位管理员 PowerShell** 中，从项目根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1 -Configuration Release
```

安装会将二进制复制到 `%ProgramFiles%/MYIME`，注册 COM、简体中文 TSF profile 和 keyboard category。使用 Windows 语言设置添加/选择 `MYIME Rime`，或通过 Win+Space 切换。必要时重启目标应用或重新登录。不会更改其他输入法或默认输入法。

目标机器需具备 Microsoft Visual C++ x64 运行库；开发机安装上述 VS 组件时已具备。MVP 安装脚本暂不自动分发运行库或签名二进制。

当前执行会话没有管理员令牌，因此本轮未运行安装脚本。系统注册、真实系统键盘路由、Notepad/Edge 中的表现都仍待安装后验收。Debug DLL 依赖开发工具运行库，普通使用请安装 Release。

卸载（管理员 PowerShell）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/uninstall.ps1
```

卸载只撤销注册，保留二进制和用户词库。更新前先切换到其他输入法、卸载并关闭使用 MYIME 的应用，再安装新版本；脚本不会强杀应用或强行覆盖已加载 DLL。

## 第一轮使用

在 Notepad / Edge 普通文本框中选择 MYIME 后输入 `nihao`，用空格/数字键选择候选；PageUp/PageDown 或候选窗口底部翻页；Esc 取消。Shift 的中英文切换由 Rime schema 的 `ascii_composer` 决定。Ctrl/Alt/Win 组合键让应用处理，尚不支持所有 schema 快捷键。

配置示例：将 `config/product.example.toml` 复制到 `%LOCALAPPDATA%/MYIME/config.toml`，编辑后重启目标应用。v1 实际可配置项为 `enabled`、`schema`、`options.<Rime option>`。schema 必须已部署；不根据应用名称猜测词库或兼容策略。

用户数据位于 `%LOCALAPPDATA%/MYIME/rime/slots/<n>`。每个应用进程租用一个持久槽位，该进程的所有 TSF 线程共享它；目录不会自动删除。**并发进程的学习词频目前不自动合并**，后续用 Rime sync export/merge 处理，不能直接覆盖正在使用的 userdb。

原生 Rime YAML、schema、dictionary 保持原样。`runtime/user/default.custom.yaml` 是初始产品 patch，仅在不存在时生成；修改后手动重新部署和打包。GUI bridge 的 mtime/hash、冲突提示和双向同步只定义了接口，尚未实现。

## 兼容性测试程序

```powershell
build/Release/myime-compat.exe
build/Release/myime-compat.exe --uiless
```

包含普通 Windows EDIT、自绘 TSF 文档、composition 拒绝开关、TSF UI Element 候选观察，以及 7/9 个显示槽位。日志保留引擎的真实 page offset，显示槽位数不会修改 Rime page size；不记录键入正文到文件。先设置测试选项，再聚焦文本区输入。

`--uiless` 用于比较实现 UI-less 协议的输入法。**当前 MYIME 不宣称支持 application-rendered UI，也不在 UI-less 线程激活**；普通模式使用 MYIME 自己的候选窗口。IMM32 仅在测试程序中观察消息，Host 尚无 legacy IMM32 adapter。因此不承诺《战舰世界》或其他游戏兼容。

## 调试与目录

VS 附加到使用输入法的应用进程，加载 `build/Debug/myime_host.pdb`，观察 OutputDebugString 的 `MYIME:` 日志。设置目标进程环境变量 `MYIME_DIAGNOSTICS=1` 可同时将错误写到 stderr。Rime 只启用 ERROR 级别默认日志，位于系统临时目录；不在常规日志中输出按键/正文。

```text
crates/core/          Rust 状态、配置、FFI、Rime 安全封装、扩展接口
crates/core/native/   官方 rime_get_api 的机械转发桥接
include/myime/       唯一公共 C ABI 契约
windows/host/        TSF、COM、候选窗口、Windows 生命周期与用户目录租约
windows/compat/      兼容性测试程序与 ITextStoreACP 文档
tools/probe/         C ABI、COM、TSF 自动测试入口
scripts/             依赖、构建、部署、测试、打包、安装/卸载
docs/                架构约束、测试范围与后续工作
```

## 当前边界

仅 x64；尚无 x86/ARM64、IMM32 adapter、UI-less candidate element、语言栏按钮、完整 DPI/皮肤系统、部署 GUI、词库导入或 WebDAV 实现。候选窗口有字体/颜色等平台样式结构，但还没有主题加载和预览。极大 page size、复杂多显示器缩放、受保护/沙箱应用、外部选择区移动及异常 text store 行为仍需实际应用测试。

StartComposition 被应用拒绝时，当前 Windows 会保留一个独立的 composition 回调对象；它不持有服务/Core，DLL 会按 COM 引用计数保持映射直到外部引用释放。文档写入失败后暂停该 context 的输入，等待焦点切换恢复；不伪造成功 commit。详情见 [架构说明](docs/architecture.md)。

源码沿用仓库 MPL-2.0。第三方依赖及数据的许可独立，见 [THIRD_PARTY.md](THIRD_PARTY.md)。
