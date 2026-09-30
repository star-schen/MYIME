# 候选窗口和模式图标修复（2026-09-30）

## 当前首轮状态

以下修复与已运行结果属于此前 `d662451` 基线。用户最新反馈候选闪烁已不存在、模式图标正常；开始菜单跳转 Windows 搜索后候选栏不显示，但 nihao + 空格可以提交“你好”。游戏已经能启动，输入限制的白名单判断尚未核实。

当前首轮在保留该候选会话生命周期和中/A 图标行为的基础上补充无正文诊断，没有目标进程日志，不宣称搜索已修复。本轮未编译、未运行测试、未安装、未操作界面。诊断字段与最小采集步骤见 [search-candidates.md](search-candidates.md)，源码级 Provider 改动见 [architecture.md](architecture.md)。

## 已定位的代码问题

此前每次引擎状态更新都会 End/Begin 候选 UI Element，容易让应用自绘候选反复消失重建；现在同一会话只更新内容，失焦、提交、取消才结束。窗口通过内存缓冲绘制，内容和位置不变时不重绘，也不重复发出显示事件。临时 TF_E_NOLAYOUT 保留同一上下文最后的位置并更新内容，等布局通知后重新定位。

此前把 UIELEMENTENABLEDONLY 误当成“禁止显示 Host 窗口”，即使应用返回 pbShow=true 仍隐藏候选。现在完全遵守显示协商，IsShown 返回 HWND 实际可见性。这修复了影响搜索环境的一条明确路径，但不代表已完成 Windows Search 实测或完整搜索建议集成。

模式项补上 BTN_MENU、初始及重新获得键盘焦点时的通知；Show 仅在状态真正变化时通知，避免重复通知。系统 InitMenu 与右键菜单均提供一个置灰“设置”；右键使用当前上下文所属窗口，不再创建临时不可见菜单宿主。Core 的中英文和全角状态发布到 TSF conversion compartment，保留其他标志，避免延用上个输入法的显示模式。左键仍不切换，不创建独立托盘程序。

## 参考与边界

参考 [小狼毫 CandidateList](https://github.com/rime/weasel/blob/master/WeaselTSF/CandidateList.cpp)、[LanguageBar](https://github.com/rime/weasel/blob/master/WeaselTSF/LanguageBar.cpp) 的协议接入方式，并依据 [微软 UI-less 协议](https://learn.microsoft.com/en-us/windows/win32/tsf/uiless-mode-overview) 独立实现，未复制其代码。百度目录只作只读文件清单查看，未运行或反编译百度输入法，不能据此推断内部机制。

所有改动在 Windows Host 和测试工具中；Core C ABI、Rime 算法和进程内热路径保持原有结构。候选对象持有自有字符串，旧选择在引擎状态变化后失效，停用后的对象不能继续提交。模式 compartment 目前为状态发布，不声明双向控制能力。

## 测试与升级

旧基线曾按当时授权执行 `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -Configuration Release`，构建和全部回归通过，详细范围见 [测试记录](testing.md)。旧测试中真实候选 HWND 的显示协商已覆盖，但未在 Windows 搜索中操作、未观察系统托盘切换、未测试游戏；当前首轮尚未重跑。

用户可运行仓库根目录 `Install-MYIME.cmd`，选择构建并安装新版；安装器按独立版本目录升级，无需先手动卸载。此轮没有自动调用安装器。安装后按提示重新登录，以使仍加载旧 DLL 的进程退出。

重点复测：从 Windows 英文切入 MYIME 后“中”图标能否持续显示；从百度/小狼毫切入后是否仍消失；右键是否出现置灰“设置”；Notepad 连续输入是否闪烁；开始菜单跳转到搜索后候选是否出现。左键没有动作是本轮约定。

诊断仍用 `MYIME_DIAGNOSTICS=1`，普通应用日志为 `%LOCALAPPDATA%\MYIME\logs\host-<pid>.log`。新增候选由谁绘制、模式图标显示/隐藏、模式发布和菜单回调记录，不记录输入正文。反馈时同时提供目标进程名和安装目录，防止把旧 DLL 行为当作新版。
