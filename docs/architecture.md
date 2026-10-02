# 架构与官方约束

## 输入路径与职责

`Windows TSF → WindowsInputAdapter → myime/core.h (C ABI v1) → core::Core → dyn InputProvider → RimeProvider → rime_get_api() → 原路返回`。

输入不跨进程。C++ bridge 位于 `crates/core/native`，只做官方 API 的版本检查、结构体初始化/释放和字段读取；不含输入算法或候选策略。Rust 使用 opaque Rime 输出句柄，避免手工复制不稳定的函数表布局，unsafe 集中在 `ffi.rs` 和 `rime.rs`。未来替换 bridge 为自动生成绑定不影响 Host ABI。

Windows 决定 HWND、绘制、字体、caret 屏幕坐标、键盘虚拟键转换和文本 API；Rust/Rime 决定候选顺序、内容、分页及 commit。候选窗口只逐项显示快照，不排序、不过滤。设置 GUI、同步、导入等外围进程暂未创建，扩展 trait 不会隐式启动线程或 IPC。

## 首轮源码级可替换架构（2026-09-30，自动回归通过）

- `core.rs` 是安全 Rust 编排层，持有 `Box<dyn InputProvider>` 和工厂。它解析/暂存配置、应用 Profile、检查组合/待确认 commit、检查页内选词边界；process/select/clear/state/ack_commit 均通过 Provider。
- `extensions.rs` 的 InputProvider 和 InputProviderFactory 已被生产路径使用。`state()` 返回 Provider 自有快照，Core 不维护另一份状态；工厂必须返回已经应用配置、完成首次状态读取的空闲实例。每次按键复用当前 Provider，不加载插件、不调用工厂。
- `rime.rs` 的 RimeFactory/RimeProvider 是唯一生产实现；Session 和 Runtime 私有。适配器负责进程级 mutex、native session、状态复制和 runtime 引用计数；unsafe 仍仅在 Rime/FFI 边界。每个 Provider 拥有一个 session，process/select/clear 的 native 操作和快照读取在同一次锁持有期间完成。
- `ffi.rs` 仅保留 opaque handle、创建线程检查、参数转换、panic/错误转换和 C 输出 view。Rust trait/struct 不跨 C ABI，`include/myime/core.h` 的导出、版本及结构布局不变。ProcessError 保留 native 操作失败前已经吞键的事实，FFI 转成原有 eaten 输出，避免同一按键又送给应用。

Profile 替换顺序为：求 effective config → 若输入配置未变则复用实例 → 拒绝组合/待确认 commit 期间的输入配置变更 → 通过官方配置 API 确认部署后的 schema id 与 engine/processors → 工厂创建临时 session → 选择 schema/应用 options → 读取初始快照 → Core 确认新状态空闲 → 替换 Provider 和有效配置。ui 是展示配置；只更换主题更新有效配置，不重建 Provider、不改组合/待确认 commit。不能仅凭 select_schema/schema_open 返回成功判定方案可用：librime 对不存在的方案也可能返回成功或空配置。schema、option 名或快照读取失败只销毁临时资源，旧实例及其有效配置保留；后续可重试。加载产品文件只暂存新文档，解析/读文件失败不覆盖文档，成功加载也不清除最后有效配置。首次 myime_create 使用传入 schema；首次 apply_profile 才应用产品默认/Profile，与原有 Host 时序一致。enabled 仍由现有 Host 用于输入资格判断。

RimeFactory 在锁内初始化/创建；临时 Session 声明在锁 guard 之后，失败或 Rust unwind 时先销毁 session，再由 guard 在 session 数为零时 finalize。成功才增加计数。RimeProvider::Drop 在锁内销毁其 session、减少计数；最后一个 Provider 释放后 finalize。Profile 准备期间旧、新 session 短暂共存，不会中途 finalize。正常操作拒绝 poisoned mutex；Drop 取得其内部 guard 做资源清理，不继续输入。部署也由适配器持锁，只允许无 live session 的离线路径，不在按键或 DllMain 中发生。

安全 Core 的 trait object 不带 Send/Sync，RimeProvider 也明确不允许线程迁移；native ABI 继续检查创建线程。输出 UTF-8 借用生命周期、全局 candidate.index 与页内 selected/select 的区别保持原契约。commit 仅在 ack_commit 或显式 clear 取消时移除；状态读取不再次消费原生 commit。

测试专用工厂/Provider 仅存在于 `#[cfg(test)] core/tests.rs`。默认 `rime` feature 启用生产 Rime 和 C ABI；`--no-default-features --lib` 可以只编译并测试安全 Core/配置，无 native bridge 编译、librime 链接或 Rime session。这是合约测试入口，不是产品的第二种输入算法。两种 feature 下的 14 项测试及真实 librime/C ABI/TSF 回归本轮均已通过，命令和实际覆盖见 [testing.md](testing.md)。

## ABI 与 ownership

`include/myime/core.h` 是 C++/Rust 边界的权威定义。`MyimeCore*` 是 opaque handle；字符串为 UTF-8 的 pointer+length，不能当作 NUL 字符串读取。输入路径使用 C-compatible 整数/指针，不跨语言暴露 C++ class 或 Rust 内存布局。

句柄仅在创建线程使用，所有输出 view 借用至该句柄下一次 mutation/destroy。Host 必须先复制需要长期保留的数据。`myime_destroy` 释放句柄；调用两次或传入任意无效地址属于 ABI 调用方错误，Rust 的空指针检查不能防御所有非法 native 指针。

RimeProvider 用进程级 mutex 串行访问 librime；每个 Host activation 拥有 Provider/session，聚焦 context 改变时清空旧 composition。最后一个 session 销毁才 finalize Rime，且不会在 DllMain 中初始化或部署引擎。snapshot 输出仅复制当前逻辑页，v1 仍有字符串分配，尚未做延迟基准或零分配优化。

`InputState` 包含 preedit、UTF-8 byte caret、候选及其全局 index、页内 selected、page/page_size/last_page、commit、schema、常见 options 与 active。FFI 暴露常见 option 位；任意 Rime bool option 可通过产品配置设置。候选未知总数不伪造。commit 在成功写入 Windows 文档后才 `ack_commit`；确认前禁止处理下一按键，以防静默丢失或重复提交。

## 配置与 AppProfile

权威来源为 Rust Config：内置默认 → `[default]` → `[platform.windows]` → `[[profiles]].overrides` → Runtime table。表递归合并，数组/标量替换，未配置项继承。未知产品字段保存在 effective table，但只有 README 列出的字段实际执行。v1 不对 Rime YAML 做往返序列化，因此不会丢失未知 schema 内容。

EXE 由 Windows 进程路径提取 basename，在 focus/context 变化时交给 Core 匹配；目前大小写不敏感匹配覆盖 ASCII EXE 名。无默认游戏特例、应用词库分类或自动专业词库。未来 matcher 可增加 window class/title/package/custom，但本轮只实现 executable。

原生 schema 页大小属于部署配置，当前产品 `default.custom.yaml` 给出 7；GUI bridge、revision/mtime/hash、外部冲突与双向同步是 `ConfigBridge` 合约，未实现自动写 patch。不能用候选窗口的可见行数覆盖引擎页大小。

## 需要调整的原设想

### 每个应用进程都直接使用同一份 userdb

目标是所有实时调用都进程内，并共享用户学习数据。Rime 的 LevelDB user dictionary 有文件锁；多个应用进程直接打开同一目录会发生冲突。采用 Windows 文件独占租约分配持久 `slots/<n>`，同一进程内多线程复用一个租约。顺序启动的进程可复用空闲槽位；并发槽位的学习历史尚未合并。

这保留 librime 原生 userdb 格式和进程内热路径，但当前不提供跨应用实时一致的学习词频。后续必须基于 Rime 的同步导出/合并能力建立离线协调，不能复制覆盖 live LevelDB。平台只负责目录锁；Core/Rime session API 不依赖 Windows，未来平台可换锁适配。

### 按键回调直接写文本

TSF 要求写操作位于 read/write edit session。`OnTestKeyDown/Up` 不改变 Rime 或文本；实际 `OnKeyDown/Up` 请求同步锁，获得锁后才处理引擎并写文本。鼠标选词请求异步 edit session，捕获 context 和 generation，过时操作被丢弃；布局通知只请求异步 read session。必须同时检查 RequestEditSession 本身与 session 返回值。[官方文档](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession)

### composition 回调直接持有整个服务

实测当前 Windows 在 owner 拒绝 StartComposition 时会保留传入的 composition sink。直接传 service 会延长 Core 所在服务的生命周期。改为独立、无 owner 引用的 sink，并用 context 的 `ITfTextEditSink` 枚举观察 composition 终止。Deactivate 始终释放 Core、窗口和目录租约；若 Windows 仍持有 sink，DllCanUnloadNow 正确返回 S_FALSE，绝不强行卸载。正常接受/取消路径已测试可卸载。这是 Windows 适配层的处理，不影响跨平台模型。

官方说明 StartComposition 可能成功但返回空 composition；必须检测这一结果。文档称 sink 可空，但当前本机测试传空时未能开始 composition，故仍提供独立 sink。[官方接口](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextcomposition-startcomposition)

### 应用自绘候选与 Host 窗口并存

application-rendered UI 通过 `ITfUIElementMgr` 与 `ITfCandidateListUIElementBehavior` 协商；遵守 `BeginUIElement` 的 `pbShow`，包括 UI-less 激活下应用允许 Host 自绘的情况。一次候选会话只 Begin 一次，后续按键和翻页更新同一对象拥有的字符串快照；结束、失焦或停用时先禁用回调再 End。异步选词校验 context 和 generation；SetSelection 后发生状态更新则拒绝旧 Finalize，应用须重新选择。[官方 UI-less 说明](https://learn.microsoft.com/en-us/windows/win32/tsf/uiless-mode-overview)

当前 Core ABI 只有 Rime 当前页，并无整个候选列表。因此每个 UI Element 是单页局部快照：count 等于当前页候选数，page index 为 `[0]`，current page 为 0。不会伪造全局总数或填充不存在的候选。Rime 的真实页码仍用于 Host 窗口显示；PgUp/PgDn 仍交给 Rime，再发布新的页快照。SetPageIndex 只接受这个单页边界，不允许应用的 7/9 行布局改变引擎分页。跨页随机访问、集成搜索建议接口尚未实现，须明确区分基本 UI-less 支持与完整搜索集成。

本轮只补充搜索候选的显示协商/布局/可见性诊断，保留同一会话 Begin/Update/End 以及中/A 图标行为。没有目标日志，不将“能提交但无候选栏”推断为缺少某个搜索接口；不忽略 pbShow、不按进程名特判、不放宽 AppContainer 私人目录 ACL。ITfFnSearchCandidateProvider/ITfIntegratableCandidateListUIElement 的完整集成另立任务；采集和日志字段见 [search-candidates.md](search-candidates.md)。

在应用自绘候选时 SetSelection 记录该快照的待提交项，Finalize 通过 Core 的 page-local select 提交；不会改写 Rime 候选排序或词频。Abort 通过 edit session 清空组合。ITfFunctionProvider 提供经典触摸键盘布局。Host 将 Core 的中英文/全角状态发布到 TSF conversion compartment，保留其余标志；这不是双向模式控制，未注册 INPUTMODECOMPARTMENT 或安全桌面能力。

受限应用的身份由有效令牌确定：AppContainer 使用系统 GetAppContainerFolderPath 返回目录，普通应用使用 FOLDERID_LocalAppData，各自在其下使用 MYIME/rime/slots。只读词库来自 Program Files 的预部署数据。不会扩大用户词库 ACL、复制 live userdb 或在输入路径部署。受限环境的学习与配置独立，尚未合并；目录不可写时激活失败并记录阶段。[系统目录接口](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-getappcontainerfolderpath)

## 系统接入和错误处理

主题包使用安全 Rust theme.rs 解析/校验，theme_ffi.rs 提供独立 presentation ABI 1；Core ABI v1 仅新增只读主题 id 导出，原结构保持不变。候选/主题字段在 Host 中复制为自有快照；CandidateWindow 只接受展示数据，不持有 Engine/Core/Rime 句柄。ThemeCatalog 在激活及选择不同主题的 context/profile 变化时加载，按键重用内存；缺失/损坏主题回退内嵌资产。字体/颜色/窗口 geometry 始终由 Windows Frontend 使用；主题包没有代码入口。独立原生预览使用同一绘制类和示例数据，不创建 Rime session。详见 [主题包与边界](themes.md)。

COM server 使用 Apartment 线程模型，DLL 引用计数包括 factory、service、edit session 和 composition observer。DllMain 只保存模块句柄。通过绝对路径与 DLL_LOAD_DIR/DEFAULT_DIRS 安全加载 Core 及其依赖，避免依赖工作目录。安装到 Program Files；注册同时处理 COM、TSF profile 和 keyboard category。[官方注册说明](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)

键盘转换使用 Windows ToUnicodeEx 的不修改 dead-key 状态选项，v1 主要覆盖 ASCII schema 输入和编辑键。只读/禁用 context 和 password input scope 不送入学习引擎；Ctrl/Alt/Win 组合键交回应用。错误写 OutputDebugString，可选启用身份隔离目录中的限量日志，不记录正文。FFI 将 Rust unwind 转换成错误；非法 native 地址、进程级 OOM/abort 并不在可恢复范围内。

模式指示器使用 ITfLangBarItemButton/ITfSource + GUID_LBI_INPUTMODE，读取 Core options 的 ascii_mode，不持有 Core handle。右键菜单只有置灰设置项，左键暂不切换。未来 GUI 启动器替换 OpenSettings 动作即可；跨平台 Core 不引入 HWND/HICON/COM 或 GUI 依赖。SVG 源稿与导出的 ICO 同存平台资源目录，构建嵌入资源，不自动运行素材生成脚本。

文本写入失败时暂停当前 context，避免继续破坏已知状态；切换焦点重置后恢复。该策略不能把失败文档事务神奇回滚成原状态，相关故障注入与实际应用恢复仍要继续完善。

## 后续边界

原生 librime 调用参考 [rime_api.h](https://github.com/rime/librime/blob/1.17.0/src/rime_api.h)。扩展 trait 是源代码级接口，不是稳定二进制插件 ABI。词库包优先 manifest+data+metadata；Importer 只输出 word/code/frequency/source 中间模型；SyncProvider 传输 Rime 导出的数据并通过 ETag/If-Match 表达冲突，均不进入按键路径。

本次更新已获授权完成 Release 编译和自动回归；安装与 Notepad/Edge/开始菜单的真实交互仍待用户验收。后续分阶段工作见 [roadmap.md](roadmap.md)；游戏专项安排在最后，用户的白名单判断保留为反馈线索，尚未核实根因。游戏策略只能来自实际比较测试，不能根据 EXE 名硬编码猜测。
