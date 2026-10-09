# MYIME — Windows Rime 前端

MYIME 使用 C++ 适配 Windows TSF、文本编辑和候选窗口，Rust 管理产品配置与输入状态，librime 负责拼音、候选排序、组词和原生学习规则。输入调用在应用进程内完成，不依赖设置窗口、网络或常驻维护程序。

2026-10-05 源码版加入皮肤编辑器、图片预览、设置即时发布、三种跨应用组词策略、Caps Lock 策略、共享学习与手动分组/维护。**本轮按用户授权完成本机 Release 构建；输入、界面和性能验收尚未执行，既往测试结果不代表这版通过。** 用户完成整个版本的验收，步骤见 [本版验收](docs/feature-acceptance.md)。

## 使用入口

安装配套新版后，右键“中 / A”模式图标打开设置；也可直接运行安装目录的 `myime-settings.exe`。设置采用左侧模块树、右侧内容区域，模块增加时左侧滚动。

- **基础设置**：全局默认 / Windows 覆盖、输入方案、Rime options、跨应用组词与 Caps Lock 策略。空白 / 继承删除当前层覆盖。
- **候选主题**：下拉显示皮肤的 `name`，选择后自动更新窗口内图片。应用为全局默认、打开皮肤编辑器、目录与刷新按钮集中在此。
- **皮肤编辑器**：新建、另存、参数编辑和 TOML 原文编辑；未保存草稿也可预览。全部创建和编辑操作在编辑器中，安装主题通过用户覆盖修改。
- **应用配置**：按 EXE 文件名匹配已有产品 AppProfile；不猜测游戏行为或专业词库。
- **学习与分组**：默认全局学习，保留应用和组标签；启用习惯后按组使用。完整 EXE 路径映射、工作/日常与自定义组、默认关闭的手动 AI 建议。
- **词库维护**：手动合并关闭的旧词库槽、在线备份共享学习、被动存储整理。关闭设置后没有常驻整理进程。
- **高级配置 / Rime 配置 / 词库导入**：保留产品 TOML、用户 `*.custom.yaml`、TXT/CSV 预览、生成与显式部署流程。

皮肤保存和“应用为全局默认”会发布新展示配置并通知运行中的 Host，另有约一秒检查作为补充。应用为全局默认同时清除 Windows 层的主题覆盖，显式 AppProfile 主题保留。输入方案变化在当前组合结束后处理；新部署的 Rime 数据代仍需重开目标应用。详见 [主题](docs/themes.md)、[输入策略](docs/input-policies.md)、[学习与分组](docs/learning.md)。

## 构建与安装

需要 Windows 10/11 x64、Visual Studio 2022“使用 C++ 的桌面开发”及 Windows SDK、Rust 1.89 或更新的 MSVC 工具链、Git 和 .NET Framework 4.8。学习存储使用系统 `winsqlite3.dll`，运行时要求 SQLite 3.24 或更新。设置使用系统 C# 编译器，无需 NuGet、.NET SDK 或本地 AI 模型。

源码必须完整，尤其是 `themes/default/theme.toml`；`include_str!` 的缺文件错误表示源文件缺失，不能据此判断 Rust 未安装。

开发机可双击项目根目录 `Install-MYIME.cmd` 并选择 `1`：构建 Release → 部署基础数据 → 打包 → 成功后请求 UAC 安装。选择 `2` 只安装已有包，选择 `3` 卸载；助手不自动运行测试。源码升级后优先选 `1`；选 `2` 必须已有配套完整包，缺少 `myime-learning-tool.exe` 时应先完成构建与打包。

分步准备：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare-data.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -Configuration Release
```

首次构建会从固定的官方 librime 源码和固定 vcpkg 版本编译依赖，缓存位于忽略的 `.deps`。它比原先下载现成 SDK 更耗时、占用更多开发磁盘；后续相同存储源码使用缓存。构建明确关闭 librime 测试，不运行测试脚本。缺少锁定提交或依赖下载缓存时需要网络；已缓存的锁定提交不重复 fetch。SDK 固定版本、静态依赖和原生数据提交写在 `dependencies.lock.json`，Rust 依赖由 `Cargo.lock` 固定。

依赖下载完成但第一次 checkout 前中止时，重新选 `1` 可复用只有 `.git`、尚无索引的空缓存并完成固定提交的 checkout。已有依赖目录包含真实本地修改，或无索引却包含其他文件时仍会中止，保留文件供人工处理；不要删除词库或用强制重置绕过保护。GitHub 登录用于源码上传，不是安装输入法的步骤。

Boost.Regex 依赖通过其 CMake 配置导入，兼容本版 Boost 1.88 的头文件实现，不要求旧式 Regex `.lib`。Windows SDK 26100 在 Windows 10 目标下缺少的 SQLite 文件名类型声明由存储适配层补齐。librime 配置和编译的完整日志保存在 `.deps/rime-shared/build-<构建标识>/configure.log` 与 `compile.log`，失败提示会给出对应路径。

产生的本地开发包在 `out/MYIME-Release`。安装采用独立 `%ProgramFiles%/MYIME/versions/<版本>`，不覆盖已加载 DLL；失败尝试恢复旧版注册。**首次采用新共享目录后需注销再登录**，让新增的用户数据组进入应用令牌，再验收开始菜单。安装不关闭应用、不自动注销，不更改其他输入法或默认输入法。普通使用需要 Microsoft Visual C++ x64 运行库。

管理员手动入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/uninstall.ps1
```

卸载保留旧版本、用户配置、学习数据和 MYIME 数据组。发布到 GitHub 的是源码，不是已编译安装包；不要用旧包验证新代码。

## 数据与性能

普通用户编辑源位于 `%LOCALAPPDATA%/MYIME/config.toml` 和 `themes/<id>/theme.toml`。设置将必要的已知产品字段和主题发布到 `%ProgramData%/MYIME/users/<Windows SID>/presentation`。共享学习在同一 SID 下的 `learning`。安装只授权 MYIME 自己的目录；配置/主题为受限应用只读，学习目录允许该用户的受限应用读写，不扩大整个用户目录权限。

Rime 部署元数据和兼容数据代仍由每个权限环境自己的 `rime/slots`、`rime/workspaces` 管理。共享学习按 Rime 字典身份分文件，相同字典的应用读取同一库；不同字典/编码身份不强行混合。

共享学习采用 SQLite WAL，成功提交后其他应用下一次查询读取。输入路径不做全库合并、后台分析或 AI 请求；每次事件结束释放数据库读快照，写锁等待上限为 12 ms。遇到锁/存储错误，保留原生输入及 commit，不重放按键，诊断记录学习写入失败。它仍有少量同步读写开销，**本轮未量测延迟，不能保证任何环境都没有变慢**。

旧独立 LevelDB 槽不会自动删除。维护模块使用 Rime 官方合并规则，跳过运行中的槽，原文件保留。大量旧数据合并可能占写锁，建议暂停输入后执行。AI 仅对用户选择的应用名称和目录提出分组建议，确认采用并保存后生效；不读取词库或输入正文，不自动下载模型。

原生 Rime patch、静态词库包和数据代发布仍遵循 [导入说明](docs/importing.md)。数据部署不复制覆盖 live userdb；恢复基础与回退会预检配置引用，不替用户改方案。

## 诊断与实现范围

可选 `MYIME_DIAGNOSTICS=1` 日志记录激活、布局、窗口 owner、显示协商、共享学习错误和隐藏原因，不写按键、候选或提交正文。普通环境在 `%LOCALAPPDATA%/MYIME/logs`；AppContainer 使用其自身本地目录。Rime 源码构建关闭 glog，诊断以 Host 为准。详见 [搜索诊断](docs/search-candidates.md)。

仅 x64；没有 IMM32 adapter、完整搜索建议集成、完整 UIA、外部代码插件、专有输入法格式导入、自动编码或 WebDAV。开始菜单候选框已有针对日志的代码处理，仍待安装后的运行验收，不能宣称问题已彻底解决。复杂 DPI、高对比度、异常 text store、跨应用草稿和共享存储都需实际验证。

```text
crates/core/            Rust 配置、策略、状态、主题、导入规则和 C ABI
crates/core/native/     官方 Rime C API 与存储 C API 的机械桥接
crates/tools/          不链接 Rime 的 JSON 维护工具
native/rime_storage/   固定 librime 源码内的共享用户学习存储
include/myime/         Host/Core 公共 C ABI 契约
windows/host/          TSF、COM、候选绘制、焦点会话与配置更新
windows/settings/      WinForms 模块导航、皮肤编辑器、AI 和手动维护
windows/theme_preview/ 无窗口图片绘制；普通入口打开皮肤编辑器
windows/data_tool/     原生数据部署与数据代发布/回退
windows/learning_tool/ 用户主动启动的学习维护 CLI
windows/compat/        兼容性验收工具
themes/                主题 TOML；默认包也是内嵌默认值的唯一来源
scripts/               依赖、构建、打包、安装及独立测试入口
docs/                  架构、使用说明和分日期验证记录
```

源码为 MPL-2.0，第三方许可独立，见 [THIRD_PARTY.md](THIRD_PARTY.md)。
