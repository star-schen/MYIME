# 架构与边界（2026-10-05）

这是本版源码的职责说明。本轮未编译、未运行测试或实际输入验收；历史验证另见 [testing.md](testing.md)。

## 输入、平台与 ABI

调用链：WindowsInputAdapter → 公共 C ABI → Rust Core → InputProvider / RimeProvider → 官方版本化 Rime C API。普通按键不启动外围进程，不经过 socket、pipe、HTTP 或设置 GUI。Windows 负责键盘转换、TSF/COM、caret、文本 API 和绘制；Rime 负责拼音、组词、候选排序、分页和学习算法。Core 增加的 Caps 字面大写是产品输入策略，不另写拼音算法。

`include/myime/core.h` 是公共契约，opaque Core handle 只能由创建线程访问。Rust 借用字符串有效期与句柄操作规则明确，Host 复制后使用；不跨 DLL 传递内部 C++ 对象。原有 ABI v1 结构保留，新策略、共享学习和维护通过附加导出及新结构提供，Host/Core/rime.dll 必须配套升级。

Native bridge 只做版本检查、结构初始化/释放、字段读取和存储 C 接口转发。Rust 不手工重建 librime 函数表布局。unsafe 限制在 Rime/FFI、主题 ABI 与必要的 Windows 文件操作边界；配置、主题和导入规则使用安全 Rust。

Rime 调用由进程级 mutex 串行；各 context 有独立 Provider/session。最后一个 session 销毁才 finalize。DllMain 只记录模块句柄，不做 I/O、COM、Rime 初始化、部署或工作线程启动。

## 配置与更新

产品 TOML 与 Rime YAML 分离：Default → Windows → AppProfile。产品 AppProfile 当前按 EXE basename（ASCII 大小写不敏感）匹配；没有默认游戏特例或按 EXE 猜测字典。学习分组独立使用完整 EXE 路径映射。

Provider 替换先解析有效配置、检查已部署 schema 的身份及 engine/processors，再准备新实例、应用 options、读取空闲初始快照，成功后替换。不能仅凭 select_schema/schema_open 成功认定方案存在。准备失败保留旧实例；组合/待确认 commit 期间延后替换。

展示和 input policy 可在组合期间更新，不销毁活动 Provider。设置发布必要的已知产品字段与主题快照到新的 p-代目录，用原子 selector 切换并广播；Host 在创建线程的普通消息/一秒定时器处理更新。定时器维护闲置 context、应用暂存配置并检查展示代，不在每次按键读取配置或做 IPC。外部源文件直接修改仍需在设置中重新读取/保存发布，不提供完整双向同步。

配置 Bridge 保留注释和未知字段，以修订/hash、同目录备份和原子发布防冲突。只读展示投影不发布 API 凭据、工具状态或词库正文。新部署的原生数据仍独立选择数据代，不在运行中输入路径切换。

## TSF 文本与焦点

TSF 写入必须在 read/write edit session 中，OnTestKey 不处理引擎/正文。键盘使用同步锁，鼠标选词使用携带 context/generation 的异步锁，过时请求丢弃；定位使用只读异步锁，同时检查请求与 session HRESULT。[RequestEditSession](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession)

每个 context 保存 canonical identity、document identity、Core、当前 composition、预期文本和安全空锚点，状态包含 Active / Detaching / AwaitingEndEdit / Suspended / Idle / Invalid。跨应用 retain/cancel/commit 始终操作原 context。retain 先清理原 composition，在自身 OnEndEdit 完成后才恢复；其他编辑改变原位置时失效，不重放按键或搬草稿到新应用。闲置记录有数量和五分钟期限。

只有枚举仍属于 MYIME 且文本与预期一致的 composition 才可删改。清理 edit 对象只持有 context/composition/预期文本，不持有已释放 Core。独立 composition observer 无服务 back-reference，Windows 延迟持有回调也不能延长 Core 生命周期。Deactivate 先断开 observer 再释放会话与租约；DLL 引用计数仍包含未释放的 COM 回调。

StartComposition 可能成功却返回空指针，必须处理；文本写入失败暂停当前 context，不伪造 commit 成功。已准备的 commit 在成功写入后明确 ack；失败并不等于能回滚外部应用的所有编辑。[StartComposition](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextcomposition-startcomposition)

只读/禁用 context 与 password input scope 不送入学习引擎，恢复草稿也先经过 password 检查。Ctrl/Alt/Win 组合键交回应用。Caps 策略在 Core，Windows 更新系统开关，只有已吃下的键才转发对应 release。

## 候选与皮肤

UI Element 保持 Begin/Update/End 协商并遵从 pbShow；应用自绘时不显示 Host popup/阴影。v1 只提供当前 Rime 逻辑页，不实现跨页随机访问或应用重分页；鼠标索引始终为当前页的原始局部 index。[UI-less 模式](https://learn.microsoft.com/en-us/windows/win32/tsf/uiless-mode-overview)

CandidateWindow 在创建时绑定当前 TSF 窗口的顶层 owner，后续 owner 设置检查 LastError 和 GW_OWNER；不夺焦点，首次显示同步绘制。临时 TF_E_NOLAYOUT 只保留同一上下文已有位置，其他失效/失焦隐藏。可见样式不能证明屏幕像素未被遮挡。[IME owned-window 要求](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements#owned-window)

皮肤规则由 Rust theme.rs / theme_editor.rs 管理，default/theme.toml 为唯一默认源。文件 containment 根据已打开对象的最终 NT 路径验证，再读取同一句柄，避开 AppContainer 的 DOS 卷名解析限制。绘制只接收复制后的展示快照，未知/损坏包回退内嵌主题。

原生离屏 render_image 不创建候选 HWND；设置以图片嵌入主页面/完整皮肤编辑器。编辑、新建、另存集中在编辑器，不把运行中候选栏置顶充当编辑预览。DPI、字体、region、可选 layered shadow 与高对比度仍由 Windows 绘制层负责。

## 共享学习与权限

普通部署元数据仍用持久 slots 的独占租约，同一进程 TSF 线程共享租约。官方 LevelDB 单目录写锁不用于跨进程并发学习。固定 librime 1.17.0 源码内接入 SqliteDb，按字典身份存共享学习，全局/分组范围与应用标签明确分离。

源码补丁的匹配失败会停下，原官方 UserDictionary 算法体不改；存储包装调用它更新全局和分组，刷新查询 tick。每个会话有独立存储 handle，事件结束释放读快照和短写事务。并发通过 SQLite WAL 处理，写锁等待有界；学习失败保留输入/commit，诊断不记录词语。undo 以版本和标签计数保护，避免撤销覆盖其他进程的后续学习。

手动旧槽合并用官方 UserDbMerger，原文件保留；在线备份和被动 checkpoint 为独立维护 CLI 操作，按键路径不合并全库。没有常驻整理或 AI 服务。[学习模块说明](learning.md)

安装仅配置 ProgramData/MYIME 下该用户的专属目录与本地数据组：展示只读，学习可写且低完整性。条件 ACE 限定用户组，首次需注销再登录。普通用户私有目录不整体扩大 ACL。AppContainer 自身的部署/日志目录仍按有效令牌与系统接口解析。[AppContainer 模型](https://learn.microsoft.com/en-us/windows/win32/secauthz/implementing-an-appcontainer)

## 外围模块与数据部署

WinForms 负责模块树、草稿和确认；Rust JSON CLI 管理配置/主题/导入规则；Windows data tool 负责原生数据部署和代际发布，learning tool 负责手动学习维护。维护 JSON 有时间和大小上限，均在用户主动操作时启动。AI 只请求用户勾选的应用元数据，默认关闭，确认后保存；key 为当前用户 DPAPI，不进入 Host。

TXT/CSV 经 Importer/Converter 检查、去重、生成 manifest/dictionary/metadata。缺码词保留报告不自动编码；包/hash/模板校验后在隔离 workspace 交给官方 Rime 编译。多个显式包进入稳定 myime_global，每包方案保留供明确 Profile 使用，不扫描未部署包自动启用。

数据发布/回退/恢复先预检全部目标 schema 和产品引用，再原子更新 active-workspace。运行中的租约使用旧代，新激活使用新代；不复制覆盖 live userdb，不自动修改用户方案，不自动删历史数据。[导入说明](importing.md)

COM server 使用 Apartment 模型，按绝对路径安全加载 Core/依赖，安装到 Program Files 的版本目录。[TSF 注册](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)

## 验证与未实现范围

诊断为 opt-in，记录激活、owner、布局 HRESULT、显示协商、隐藏/共享学习错误等元数据；有去重、速率及文件大小限制，不记录按键/正文。Rime 源码构建关闭 glog；进程 OOM/abort、无效 native 地址不在可恢复保证内。

InputProvider/Factory、Importer/Converter、ConfigBridge 有实际实现；SyncProvider 等仍是扩展合约，不是外部 DLL 插件管理器。没有 IMM32 adapter、完整搜索建议/完整 UIA、WebDAV、专有词库解码或 AI 词典分析。游戏/沙箱兼容与性能必须由实际证据确定，不猜测 EXE 策略。

用户验收见 [feature-acceptance.md](feature-acceptance.md)；新版本没有“已通过构建/测试/开始菜单/性能”的结论。
