# Windows 搜索候选诊断（2026-09-30）

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

1. 本轮已完成构建/回归及打包，不必重复编译。在当前仓库双击 Install-MYIME.cmd，选 2 安装 out/MYIME-Release；若使用另一份仓库或包需确认提交版本。不要只复制 DLL；采用独立版本目录。记下版本目录和提交号，避免目标仍加载旧 DLL。
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
