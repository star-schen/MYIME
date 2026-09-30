# 测试状态与验收

2026-09-30 用户明确授权执行测试后，当前 Provider 重构与搜索候选诊断已完成 **Release 编译和完整自动回归**，发现的问题已修正并重跑通过。未安装或替换正在使用的 DLL，未操作真实 Notepad/Edge、Windows 搜索、系统托盘或游戏。以后是否运行测试仍按协作约定与当次授权决定。

## 本轮实际结果（2026-09-30）

| 范围 | 结果及证据 |
|---|---|
| 不链接 librime 的安全层 | `--no-default-features --lib --target-dir target/core-contract-tests`，11 项 Core + 3 项配置测试全部通过；独立 target 目录不覆盖产品 DLL |
| Release 工程与生产测试 | Rust/MSVC/CMake 编译成功；默认 rime feature 下同样 14 项测试通过，doctest 0 项 |
| C ABI / 真实 librime | nihao → 7 候选 → 你好；分页、commit 确认、非法选词、Profile 原子替换、跨线程拒绝、多个 session 与重新初始化全部通过 |
| COM / 候选 / 模式接口 | factory/生命周期、快照/过期操作、图标资源/菜单/通知全部通过 |
| 真实 TSF 文档 | 普通、`--app-ui`、`--reject` 三种模式全部通过；试探回调不改状态，中文提交、只读、取消和应用结束/拒绝 composition 符合断言 |
| 候选显示协商 | 同一会话 Begin 1 次、Update 10 次、提交前 End 0 次；pbShow=1 时 HWND 可见，pbShow=0 时隐藏 |
| 启用诊断的 TSF 路径 | probe 已自动设置 MYIME_DIAGNOSTICS=1；日志含 Begin/pbShow、context、owner、caret HRESULT、实际 visible、End 及 suppressed，未发现输入正文 |
| 兼容程序 | `--self-check` 初始化通过；未执行真实输入法对比 |

首次完整回归在 C ABI 阶段失败：不存在的 schema 被接受，正常 Provider 被替换，后续候选数为 0。官方 [RimeSelectSchema 实现](https://github.com/rime/librime/blob/1.17.0/src/rime_api_impl.h) 只确认 session/id 参数，不证明配置已部署；[配置组件](https://github.com/rime/librime/blob/1.17.0/src/rime/config/config_component.cc) 也可能为不存在的文件返回空 Config。现在用官方配置 API 检查 schema/schema_id 和非空 engine/processors，再创建/选择 session。失败保留原 Provider；未改变 Host/Core ABI 或原生 YAML。修正后完整脚本退出码为 0。

TSF probe 的拒绝组合分支 DllCanUnloadNow=S_FALSE，服务引用在拒绝前后均为 8；这是既有 Windows sink 保留行为。正常与应用自绘分支均为 S_OK。测试使用独立进程和真实 Windows 文档，手动分发按键；不覆盖系统注册/真实键盘路由、Search 像素或鼠标操作。

本轮 C ABI probe 使用仓库 runtime 测试数据和临时 TOML。TSF probe 沿用 Host 的普通身份数据目录/槽位租约，不删除或重置已有配置、词库；日志在普通 MYIME 日志目录。自动回归的 HWND 可见性结论不能替代实际应用界面验收。

## 首轮新增测试覆盖（已执行）

`crates/core/src/core/tests.rs` 包含 11 项安全 Core 合约测试，测试工厂/Provider 仅在 cfg(test) 中出现，不创建 Rime session：

- 初始 schema/options、输入事件传递、复用唯一 Provider 及唯一状态来源。
- commit 重复读取不消费；确认前按键/选词禁止；ack 幂等；clear 取消组合和待确认 commit；Esc 路径。
- 翻页、真实快照页号/全局编号和页内 select 边界，非法 index 不调用 Provider。
- 创建、配置、首次快照失败和工厂错误返回非空闲实例时释放临时资源；新实例准备完成前旧实例仍活着。
- Profile 失败保留旧实例/状态/有效配置，成功替换才释放旧实例；相同 effective config 不重建；组合/待确认 commit 禁止变更。
- 暂存新配置后的失败与重试；process/select/clear/ack 错误传播；失败前已吞键的事实传回 Host；一个 Core 销毁不释放另一个 Provider 的资源。

`tools/probe/main.cpp` 在现有真实 librime/C ABI 回归上补充了临时产品配置、未部署 schema/含 NUL 的 option 名导致的 Profile 替换失败、组合/commit 期间的 Profile 拒绝、取消待确认 commit、跨线程 destroy 拒绝、创建失败后旧实例继续可用、有 live session 时禁止部署，以及最后一个 session 释放后的失败创建和重新初始化。测试配置只写入临时文件，不使用安装目录的用户配置。

Mock 合约测试不覆盖 librime 原生行为、原生 runtime mutex/引用计数实际运行、Windows COM/TSF 或真实键盘路由。既有候选、模式图标和 TSF probe 保留，真实回归与桌面端到端验收仍必须独立进行。

## 重复运行命令

在已有 Rust/MSVC 工具链的 x64 开发 PowerShell 中，从仓库根目录先运行安全 Core 与配置测试：

```powershell
cargo test -p myime-core --locked --no-default-features --lib --target-dir target/core-contract-tests
```

该模式不编译 native bridge、不链接/加载 librime，不需要准备 Rime 数据；`--lib` 只用于安全层测试。不要用关闭默认 feature 的产物替换产品 DLL：生产构建必须保留默认 `rime` feature 和 C ABI。

在普通 PowerShell 中，现有完整回归入口会自行导入 VS 环境，构建默认生产实现并准备官方数据，然后运行 Rust/C ABI/librime/COM/候选/图标/真实 TSF/兼容程序回归：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -Configuration Release
```

两条命令本轮均已执行并通过。完整脚本不会安装输入法。Release 包位于 out/MYIME-Release，用户可用 Install-MYIME.cmd 的选项 2 升级，再按下文手工清单和 [Windows 搜索最小采集步骤](search-candidates.md) 反馈结果。本轮没有真实桌面、Search 或游戏通过结论。

## 旧基线已运行（2026-09-30，d662451）

- Rust 配置测试 3 项；C ABI/Rime 分页、提交及生命周期；COM factory/卸载。
- 候选对象：页边界、过期选择不能提交、Show/IsShown、停用后回调无效。
- 模式图标：中英文资源可加载、重复 Show 不重复通知、左键不动作、系统菜单只有置灰“设置”。
- 真实 TSF 文档：中文提交、只读、试探回调、取消、应用主动结束/拒绝组合。
- 同一候选会话 10 次更新仅 Begin 1 次，提交前 End 0 次；结束时旧对象已禁止提交。
- UI-less 标志下 pbShow=true/false 分别显示/隐藏真实候选 HWND；测试窗口不激活，不安装系统 profile。
- 兼容程序初始化。拒绝组合时 Windows 保留独立 sink、DLL 暂不能卸载的行为与历史说明一致。

详见 [候选和模式图标修复](ui-regression.md)。这些结果不证明真实系统切换、Shell 托盘和 Windows Search 已正常。

## 历史已运行（2026-09-21 的 MVP）

本机 Windows x64，2026-09-21，Debug/Release：

| 层 | 测试内容 | 结果 |
|---|---|---|
| 工程骨架 | MSVC + Windows SDK、Rust DLL 构建 | 通过 |
| C ABI | C++ 调用 Rust，版本、空 handle、跨线程拒绝 | 通过 |
| Rime | 官方数据部署、nihao → ni hao / 7 候选 → 你好 | 通过 |
| 状态 | Rime 页码/全局 index、无效选词、Esc、commit 确认 | 通过 |
| 生命周期 | 同进程两个 session，一个销毁后另一个可用 | 通过 |
| 配置 | 继承、EXE 匹配、runtime 优先、未知字段、非法/重复配置 | 3 项通过 |
| COM | factory/QI、引用计数、正常 DLL 卸载 | 通过 |
| TSF 文档 | 真实 ITextStoreACP + Windows edit lock/composition，提交“你好” | 通过 |
| TSF 边界 | 多次 OnTest 不改变状态、只读 context、Esc 取消、应用主动终止、拒绝 composition | 通过；拒绝路径有下述 Windows 引用保留 |
| 兼容程序 | 隐藏窗口/控件及真实 TSF 初始化、退出 | 通过 |

TSF probe 的 `ManualDispatch` 只替换键盘 sink 注册，测试自己调用按键回调。原因是没有安装 profile 时，手动激活服务的真实 AdviseKeyEventSink 返回 E_INVALIDARG。文档上下文、range、edit cookie 和 composition 都来自 Windows，**但此测试不能证明系统注册/激活或真实键盘路由成功**。

拒绝 composition 的独立进程测试确认正文保持不变、service 引用不增加；当前 Windows 仍保留独立 sink，DllCanUnloadNow 为 S_FALSE。测试不强行 FreeLibrary，进程退出后由系统回收。正常提交/取消的独立测试要求 S_OK。

## 当前尚未运行

- 本轮新版安装/卸载和输入法列表可见性。
- Notepad、Edge 普通文本框真实按键、鼠标点击与可视布局验收。
- 微软拼音/百度/Weasel 对比，尤其《战舰世界》。
- UI-less 在真实应用中的显示、开始菜单/搜索、受限应用独立目录；legacy IMM32、x86/ARM64 尚未实现。
- 多显示器 DPI、休眠/注销、进程崩溃恢复、大型候选页、跨进程用户词库合并。

用户最新反馈旧基线候选不再闪烁、模式图标正常；游戏能正常启动；开始菜单跳转搜索后没有候选栏，但 nihao + 空格能提交“你好”。这些反馈不是本轮源码的测试结果，也不能证明搜索根因或游戏白名单假设。

## 安装后手工清单

本轮用户最小验收只需：使用已准备的 Release 包升级，保存工作后注销/登录；Notepad 或 Edge 输入 nihao、翻页/选词/标点并切换窗口；重点在开始菜单跳转搜索后观察候选和提交，按搜索文档采集目标日志。系统托盘菜单/位置、真实 caret、多屏和搜索遮挡必须在实际桌面观察。游戏专项暂不纳入本轮。

1. Release 构建、部署数据、打包；在管理员 PowerShell 执行 install.ps1。确认 Win+Space/语言设置可选择 MYIME Rime。
2. Notepad 输入 nihao，检查 composition 只出现一次、caret 位置正确、空格提交“你好”；继续输入检查上一次 commit 不重复。
3. 输入 ni，记录引擎第 1 页与第 2 页的候选、页号；用 PageDown/PageUp 和鼠标翻页选词。
4. Esc、Backspace、左右方向、数字选词；输入中切换窗口/控件/输入法，确认旧 preedit 不写入新控件。
5. Edge 普通输入框复现以上步骤；只读/密码框、Ctrl+C/V、Alt+Tab 等不被拦截。
6. 同时打开两个应用；确认无 userdb lock 错误，记录各自用户数据 slot。退出重启确认目录保留；目前不期待并发 slot 的词频自动一致。
7. myime-compat 普通模式对比 native EDIT 与自绘 TSF 文档，尝试 composition 拒绝；开启 UI-less 模式比较支持这一模式的其他输入法。
8. 先选 7/9 槽位，再聚焦输入。记录实际 page start/next，而非只看屏幕上的第 1 项；槽位切换不是修改 Rime 页大小。
9. 卸载后确认 profile 消失；词库保留。关闭应用后才能替换其加载的 DLL。

报告问题请附 Windows/应用版本、进程位数、schema、AppProfile、重现键序列、候选页边界和 MYIME 错误日志。不要提交真实用户词库、密码或其他敏感正文。
