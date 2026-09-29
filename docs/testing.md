# 测试状态与验收

2026-09-30 用户明确授权本轮编译和测试。当前 Release 构建及 `scripts/test.ps1 -Configuration Release` 全部通过；未安装新版、未启动游戏、未在 Windows 搜索/系统托盘验收。默认测试仍由用户负责，本次授权不改变默认约定。

## 本轮已运行（2026-09-30）

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

## 尚未运行

- 本轮新版安装/卸载和输入法列表可见性。
- Notepad、Edge 普通文本框真实按键、鼠标点击与可视布局验收。
- 微软拼音/百度/Weasel 对比，尤其《战舰世界》。
- UI-less 在真实应用中的显示、开始菜单/搜索、受限应用独立目录；legacy IMM32、x86/ARM64 尚未实现。
- 多显示器 DPI、休眠/注销、进程崩溃恢复、大型候选页、跨进程用户词库合并。

## 安装后手工清单

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
