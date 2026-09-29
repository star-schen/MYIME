# 兼容性更新（尚未编译、尚未运行测试）

## 游戏启动异常

2026-09-29 的游戏报告多次出现 MSCTF → myime_host → MSVCP140 的相同调用链。
安装 DLL 的静态反汇编将 Host 返回位置对应到目录租约的 `_Mtx_lock` 调用。
Host 使用 14.44 工具链构建，游戏目录有 14.34 运行库；新版 mutex 初始化与旧运行库不兼容是当前有力假设，仍需用户复测确认实际加载路径及修复效果。

目录租约改用 Windows SRWLOCK，不修改游戏运行库、不按游戏 EXE 特判。锁保护引用计数与独占文件句柄，生命周期仍是进程内共享目录、进程之间不同槽位。
初始化失败统一释放部分创建的窗口、Core、目录租约和 TSF 订阅。不捕获 native 访问冲突后继续使用损坏状态。

设置 `MYIME_DIAGNOSTICS=1` 后重启目标应用可启用文件诊断；默认仍提供 OutputDebugString。
日志按进程命名，单文件约 1 MiB 后循环截断。不记录按键、preedit、候选或提交正文。
调试输出由 DebugView/调试器读取，环境变量须由目标进程继承。

系统安装新版运行库不保证能替代游戏私有运行库，本轮不更改游戏文件。
参考：[微软 STL 运行库兼容说明](https://github.com/microsoft/STL/releases)。

## 开始菜单、标点和图标

- UI-less 不再返回 E_NOTIMPL；候选通过 Begin/Update/EndUIElement 协商。应用自绘时不出现 MYIME 候选窗口，普通窗口使用 context view 返回的所属窗口，并发出 IME show/hide/change 事件。
- ABI 只提供当前 Rime 页，TSF 暴露单页快照（本地索引从 0 开始）。PgUp/PgDn 由 Rime 翻页；应用固定显示 9 行不会把引擎的 7 候选页改为 9。未实现完整搜索建议集成或任意跨页查询。
- 可翻译的 ASCII 字符，包括标点，在无 preedit 时也交给 Rime。Host 不自行决定中文符号；ascii_mode/ascii_punct、快捷键和应用输入限制仍生效。
- AppContainer 数据根由有效令牌与 GetAppContainerFolderPath 获取；普通进程使用 FOLDERID_LocalAppData。各自使用 `MYIME/rime/slots` 与 `MYIME/config.toml`，共享只读预部署基础数据，但不合并学习记录。不复制或修改桌面用户词库，不增加 ACL。
- MY 品牌图标及“中 / A”模式图标以 16/20/24/32/40/48 像素、32 位 alpha ICO 嵌入 DLL。SVG 是编辑源稿，`scripts/export-icons.ps1` 只供素材导出，正常构建直接使用已提交 ICO，不执行导出脚本。
- AddLanguageProfile 按官方 API 使用图标序号 0，绑定 DLL 第一组资源 `IDI_MYIME=101`；不是把 SVG 传给 Windows。模式图标使用 GUID_LBI_INPUTMODE，展示 Core 的 ascii_mode。左键无操作，右键仅有置灰“设置”。
- 图标、菜单、TSF COM 对象留在 Windows 层。未来独立 C# GUI 只需实现 OpenSettings 动作；不改变 Core ABI 或输入进程路径。不为一个菜单引入动态插件加载器。

## 用户执行的升级步骤

**新增简化入口：双击项目根目录 `Install-MYIME.cmd`，选 `1` 构建并升级。** 不必先手动卸载；助手构建成功后才申请管理员权限，放入独立版本目录再撤销旧注册、注册新版。失败时尝试恢复旧注册并明确报告。选 `2` 会使用已有包，不能保证包含最新源码；选 `3` 卸载但保留词库与文件。助手的日志在 `%TEMP%/MYIME-Setup`。

助手与升级脚本均未运行测试。安装后请注销并重新登录以释放旧 DLL。下列手动流程仍可使用，但不再是必需步骤。

以下命令未由本轮实施过程执行。先在项目根目录构建新包，确认成功后再卸载旧版本，避免构建失败时失去可用版本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare-data.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1 -Configuration Release
```

保留旧安装包用于回退。切换其他输入法，在 64 位管理员 PowerShell 中执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/uninstall.ps1
```

关闭使用 MYIME 的应用；开始菜单等系统进程可能仍加载旧 DLL，建议注销并重新登录，再在 64 位管理员 PowerShell 的项目目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install.ps1 -Configuration Release
```

类别注册和图标资源都发生了变化，不能只复制 DLL 而省略重新注册。若文件仍被占用，不强杀系统进程、不强制覆盖。回退也先卸载本版、注销释放 DLL，再安装保留的旧包；用户词库保留。不要把游戏中的 DLL 换成 MYIME 或系统版本。

## 用户验收清单（全部待执行）

1. 构建 Release，按需运行 `scripts/test.ps1`。历史测试入口仍存在；新增候选对象回归入口覆盖快照边界、无效重分页和失效后的操作。
2. Notepad/Edge 输入 nihao、空格提交、直接输入逗号句号；检查中英文模式和 ascii_punct 下结果；Ctrl/Alt/Win 快捷键、只读框、密码框不被吞键。重复 OnTest 不应改变引擎或文档。
3. 打开开始菜单，选择 MYIME 输入中文，再切回桌面；记录输入法是否被跳过、是否显示候选、是否能提交。缺少搜索建议集成不应被误写成已通过开始菜单验收。
4. `myime-compat.exe --uiless` 中开启应用自绘，7/9 槽位分别用 PgUp/PgDn 翻页并点击选词。不得出现两套候选窗口，不得把第 8 个候选跳成第 10 个。显示页码为快照本地页 0，真实翻页应以候选内容变化核对。
5. 组合中切换焦点、切换输入法、取消、让应用终止 composition；旧候选操作不得写入新文档，停用/再次激活正常。
6. 选用 MYIME 启动游戏，记录是否仍有 MSVCP140/_Mtx_lock 调用链。若失败，保留新的 `.crash`、对应 monitor 和 MYIME 日志。全屏显示模式、CEF/SSL 是独立线索，不因本轮修复就认为已解决。
7. 不同系统缩放、明暗主题下观察 MY 与中/A 图标；左键不切换，右键仅有置灰“设置”，菜单关闭后可继续输入。
8. 受限应用可输入且其学习写入自身目录，普通应用学习目录不被复制、覆盖或放宽权限；目录不可写时记录失败阶段。多线程激活、窗口关闭、注销、重启后无悬空候选回调。

所有条目必须由实际结果判定。本次仅交付代码和资源，没有端到端通过结论。
