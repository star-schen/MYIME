# Windows 搜索候选诊断

## 2026-10-09 共享路径与初始化源码修复（未构建、安装或测试）

用户授权继续实施。安装器现在发布受保护的共享存储基目录，Host 保留有效用户 SID 和逐阶段原生错误信息；共享学习及共享设置不可用时保留基础输入，并提供简繁中文状态提示。Core、librime 包装器和对应 ABI 同步修改；查询、提交及正常回滚也纳入不可用状态处理。

本轮只改源码并准备同步仓库。未编译、安装、启动新采集或复现，不能宣称开始菜单已经修复。旧日志仍不足以确定唯一失败 API，损坏的工作区或未部署字典仍需按实际日志修复。配套构建要求和验证边界见 [源码修复说明](diagnostics/windows-search-2026-10-08/source-fix-2026-10-09.md)。

## 2026-10-08 已构建安装后的搜索初始化失败（历史诊断）

用户已重启并手动复现：第一次失效，第二次在搜索框内重新选择 MYIME 后实际仍是微软拼音。最新日志明确捕获 SearchApp PID 24876 的 MYIME 两次初始化失败，hr=0x80004005；Core / Rime DLL 加载和用户目录租用成功，失败位于共用工作区路径 / 全局学习目录身份解析范围，发生在创建 Rime 会话和候选 UI 之前。当前通用异常日志丢失具体原因，尚未确认精确失败点，不能把它认定为旧候选遮挡问题。

构建安装已完成，用户真实搜索复现未通过；本机日志采集已停止，用户要求不启动新测试、改在其他机器继续。完整结论、版本信息、代码入口、原始跟踪和解码记录见 [跨机器交接](diagnostics/windows-search-2026-10-08/README.md)。下文保留历史状态，旧的“尚未构建安装”不代表当前状态。
## 2026-10-05 源码处理与验收状态

2026-10-04 用户两轮手动复现确认输入/空格提交正常，但候选不可见。详细日志来自 SearchApp：pbShow=1、caret 非空且未裁剪、期望窗口 owner=0x303A6，而候选窗口读回 owner=0；另有主题加载 os error 5。输入结束后的 cloaked=2 快照不能作为输入期间被 Shell 隐藏的证明。

本版源码将有效顶层 owner 传入 CreateWindowExW，后续设置核对 LastError 与 GW_OWNER；首帧同步绘制。主题根据已打开文件的最终 NT 路径验证包含关系，设置将主题与必要配置发布到受限应用可读的 MYIME 展示目录。相关方法符合微软 [IME owned-window 要求](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#owned-window)；它们不构成已修好的界面证据。

**这版没有构建、安装或复现。** 用户需用最新源码构建/打包并安装，首次共享数据组生效后注销再登录，再确认候选实际可见、位置、皮肤与提交。请勿用旧 out 包代替本版；完整步骤见 [feature-acceptance.md](feature-acceptance.md)。只有真实验收或新日志才能决定是否还需要处理遮挡、绘制或 Shell 行为。

## 2026-09-30 历史记录与通用采集方法

状态：诊断版 **Release 编译及自动 TSF 回归已通过，待搜索目标进程实际日志定位**。独立 TSF probe 启用诊断，已记录 pbShow=1/0 与 visible=1/0、context/owner/caret、End 和 suppressed；不等于 Windows 搜索已修复。本轮未安装、未操作真实搜索、未注销或启动游戏。用户反馈此前版本搜索无候选栏，但 nihao + 空格可提交“你好”；闪烁已消失、图标正常。

## 本轮内容与边界

静态阅读范围为 CandidateElement、publish_candidates、position、CandidateWindow 和 Diagnostics。保留 Begin/Update/End 候选会话、pbShow/Show 显示协商、临时 TF_E_NOLAYOUT 保留同一上下文位置、实际 HWND 的 IsShown 和中/A 图标。本轮没有确定 Windows 搜索的代码根因，也没有针对搜索修改显示策略。

不根据“能提交但无候选”推断缺少某个接口，不硬编码目标 EXE 名、不强制自绘、不忽略 pbShow、不扩大 AppContainer 私人目录 ACL。ITfFnSearchCandidateProvider/ITfIntegratableCandidateListUIElement 的完整搜索建议集成另立任务，当前只是当前 Rime 页的基本 UI Element。

## 新增元数据

| 记录 | 可以区分的路径 |
|---|---|
| Candidate BeginUIElement id / pbShow / context / HRESULT | 应用是否允许 Host 自绘、Begin 是否失败 |
| Candidate Show request=true/false | 应用后续显式显示/隐藏请求 |
| Candidate context old/new | 开始菜单/搜索之间的 TSF context 变化 |
| Candidate owner context/view/view_hwnd/resolved/previous/fallback / HRESULT | GetWnd 结果、GetFocus 回退及实际 Host owner 变化；在 Host 需要定位时采集 |
| Candidate caret layout clipped/empty / HRESULT | GetTextExt 成功、TF_E_NOLAYOUT、其他失败、裁剪或空矩形；不记录正文或 caret 文本位置 |
| GetActiveView / GetSelection unavailable / HRESULT | 尚未拿到布局或选择区，保留原有窗口的分支 |
| Candidate visibility reason/context/hwnd/owner/requested/visible / HRESULT | 请求显示和 HWND 实际可见性是否一致、每个隐藏分支及保留窗口分支 |
| Candidate UpdateUIElement / EndUIElement / HRESULT | 同一会话更新和结束、发布失败阶段 |

visibility reason 包括 host-display-not-requested、Show-false、no-owner-hwnd、layout-failure、caret-clipped、caret-empty、no-layout-retain-same-context、window-create-failure、layout-ready-update、focus-reset、composition-inactive、application-ended-composition、document-or-ui-failure 和 IsShown-query 等。requested 表示当前元素的显示意向；元素结束后为 false，不等于 HWND 必然隐藏。visible 读取 IsWindowVisible，不能证明没有被别的窗口遮挡或实际像素正确。

候选详细诊断只在 `MYIME_DIAGNOSTICS=1` 时启用。七个元数据通道各自限制每秒 12 条，连续相同消息/HRESULT 五秒内抑制；下次发出事件的 suppressed 计数说明期间省略了多少条，不是完整逐回调轨迹。原有文件约 1 MiB 循环截断，不可写则只输出调试消息。所有新日志都不包含按键、preedit、候选或 commit 正文；上下文指针、HWND 和模块路径仅用于身份/阶段关联。

## 用户最小采集步骤

1. 先确认源码提交和配套构建产物，构建/打包成功后再安装。新版可在 Install-MYIME.cmd 选 1 准备；选 2 仅适用于已确认最新的完整 out/MYIME-Release 包。不要只复制 DLL；采用独立版本目录。记下目录和提交号，首次新版安装后注销再登录，避免旧 DLL 与未更新的数据组令牌。
2. Windows 搜索/开始菜单可能已经运行，单独在新 PowerShell 设置 `$env:MYIME_DIAGNOSTICS='1'` 不会让已有系统进程继承。先记下用户环境变量原值，再由用户自行设置下列变量；如需让系统进程继承，由用户自行注销并重新登录。本轮实施不会自动注销、强杀系统进程或修改其他输入法。

   ```powershell
   [Environment]::GetEnvironmentVariable('MYIME_DIAGNOSTICS','User')
   [Environment]::SetEnvironmentVariable('MYIME_DIAGNOSTICS','1','User')
   ```

3. 只复现一次：进入开始菜单，选择 MYIME，跳转到搜索，输入约定的 nihao 并空格提交；记录本地时间、Windows 版本、候选是否出现、提交是否成功。可在 Notepad 做一次相同输入作对照；不要在采集时输入真实敏感正文。
4. 普通身份日志在 `%LOCALAPPDATA%\MYIME\logs\host-<PID>.log`；AppContainer 使用系统返回的自身本地目录下 `MYIME\logs`，不能假定也在桌面目录。查看激活记录中的 PID、模块路径及数据根，确认来自实际目标。若没找到文件或环境变量未继承，用已有 DebugView/VS 调试器读取目标 PID 的 `MYIME:` OutputDebugString，附上实际加载的 myime_host/myime_core 目录。不要为取得日志放宽私人目录 ACL。
5. 反馈从激活/上下文切换、Begin 协商、布局到提交/End 的短日志片段及复现时间。若 suppressed 非零，可隔几秒重复一次短复现，保留省略计数；缺少某一行不应单独解释为没有回调。安装目录、日志路径可能带用户名，分享前可遮去用户名，保留 PID/HRESULT/显示字段。
6. 采集完恢复原环境变量。此前未设置的用户可执行以下命令清除；原来已有值则恢复原值。已经运行的进程保留旧环境，下一次正常启动才采用恢复值。

   ```powershell
   [Environment]::SetEnvironmentVariable('MYIME_DIAGNOSTICS',$null,'User')
   ```

## 定位时需要回答

先看 pbShow/Show 与 visible，再看 context/owner 和 caret HRESULT。如果应用要求自绘，则继续定位应用绘制链路；如果允许 Host 显示，则用明确的隐藏原因/创建结果定位。若日志显示 Host visible=true，仍需用户提供是否遮挡、焦点所属及多屏信息，不能仅凭 IsWindowVisible 宣称搜索正常。没有实际日志前保持“诊断已加入，搜索修复待定位”。

