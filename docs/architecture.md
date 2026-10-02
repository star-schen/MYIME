# 架构与官方约束

## 输入路径与职责

`Windows TSF → WindowsInputAdapter → myime/core.h (C ABI v1) → core::Core → dyn InputProvider → RimeProvider → rime_get_api() → 原路返回`。

输入不跨进程。C++ bridge 位于 `crates/core/native`，只做官方 API 的版本检查、结构体初始化/释放和字段读取；不含输入算法或候选策略。Rust 使用 opaque Rime 输出句柄，避免手工复制不稳定的函数表布局；unsafe 限制在 Rime/FFI、主题 ABI 和必要的 Windows 文件发布系统边界，配置/导入/词条规则保持安全 Rust。未来替换 bridge 为自动生成绑定不影响 Host ABI。

Windows 决定 HWND、绘制、字体、caret 屏幕坐标、键盘虚拟键转换和文本 API；Rust/Rime 决定候选顺序、内容、分页及 commit。候选窗口只逐项显示快照，不排序、不过滤。独立 C# 设置、Rust 维护 CLI、Windows 离线部署工具已经实现；它们通过有界 JSON 标准输入/输出完成用户明确发起的外围操作。输入法每次按键不调用这些进程；SyncProvider 仍是未来接口，扩展 trait 不会隐式启动后台线程。

## 首轮源码级可替换架构（2026-09-30，自动回归通过）

- `core.rs` 是安全 Rust 编排层，持有 `Box<dyn InputProvider>` 和工厂。它解析/暂存配置、应用 Profile、检查组合/待确认 commit、检查页内选词边界；process/select/clear/state/ack_commit 均通过 Provider。
- `extensions.rs` 的 InputProvider 和 InputProviderFactory 已被生产路径使用。`state()` 返回 Provider 自有快照，Core 不维护另一份状态；工厂必须返回已经应用配置、完成首次状态读取的空闲实例。每次按键复用当前 Provider，不加载插件、不调用工厂。
- `rime.rs` 的 RimeFactory/RimeProvider 是唯一生产实现；Session 和 Runtime 私有。适配器负责进程级 mutex、native session、状态复制和 runtime 引用计数；unsafe 仍仅在 Rime/FFI 边界。每个 Provider 拥有一个 session，process/select/clear 的 native 操作和快照读取在同一次锁持有期间完成。
- `ffi.rs` 仅保留 opaque handle、创建线程检查、参数转换、panic/错误转换和 C 输出 view。Rust trait/struct 不跨 C ABI；`include/myime/core.h` 保持 ABI v1 和原结构布局，后续新增兼容的主题查询、元数据和离线部署导出，Host/Core 必须配套更新。ProcessError 保留 native 操作失败前已经吞键的事实，FFI 转成 eaten 输出，避免同一按键又送给应用。

Profile 替换顺序为：求 effective config → 若输入配置未变则复用实例 → 拒绝组合/待确认 commit 期间的输入配置变更 → 通过官方配置 API 确认部署后的 schema id 与 engine/processors → 工厂创建临时 session → 选择 schema/应用 options → 读取初始快照 → Core 确认新状态空闲 → 替换 Provider 和有效配置。ui 是展示配置；只更换主题更新有效配置，不重建 Provider、不改组合/待确认 commit。不能仅凭 select_schema/schema_open 返回成功判定方案可用：librime 对不存在的方案也可能返回成功或空配置。schema、option 名或快照读取失败只销毁临时资源，旧实例及其有效配置保留；后续可重试。加载产品文件只暂存新文档，解析/读文件失败不覆盖文档，成功加载也不清除最后有效配置。首次 myime_create 使用传入 schema；首次 apply_profile 才应用产品默认/Profile，与原有 Host 时序一致。enabled 仍由现有 Host 用于输入资格判断。

RimeFactory 在锁内初始化/创建；临时 Session 声明在锁 guard 之后，失败或 Rust unwind 时先销毁 session，再由 guard 在 session 数为零时 finalize。成功才增加计数。RimeProvider::Drop 在锁内销毁其 session、减少计数；最后一个 Provider 释放后 finalize。Profile 准备期间旧、新 session 短暂共存，不会中途 finalize。正常操作拒绝 poisoned mutex；Drop 取得其内部 guard 做资源清理，不继续输入。部署也由适配器持锁，只允许无 live session 的离线路径，不在按键或 DllMain 中发生。

安全 Core 的 trait object 不带 Send/Sync，RimeProvider 也明确不允许线程迁移；native ABI 继续检查创建线程。输出 UTF-8 借用生命周期、全局 candidate.index 与页内 selected/select 的区别保持原契约。commit 仅在 ack_commit 或显式 clear 取消时移除；状态读取不再次消费原生 commit。

测试专用工厂/Provider 仅存在于 `#[cfg(test)] core/tests.rs`。默认 `rime` feature 启用生产 Rime 和 C ABI；`--no-default-features --lib` 可以只编译并测试安全 Core/配置/导入，无 native bridge 编译、librime 链接或 Rime session。独立 `myime-tool` 也使用该路径。这是合约测试入口，不是产品的第二种输入算法。实际编译、测试覆盖和真实应用验收分开记录在 [testing.md](testing.md)。

## ABI 与 ownership

`include/myime/core.h` 是 C++/Rust 边界的权威定义。`MyimeCore*` 是 opaque handle；字符串为 UTF-8 的 pointer+length，不能当作 NUL 字符串读取。输入路径使用 C-compatible 整数/指针，不跨语言暴露 C++ class 或 Rust 内存布局。

句柄仅在创建线程使用，所有输出 view 借用至该句柄下一次 mutation/destroy。Host 必须先复制需要长期保留的数据。`myime_destroy` 释放句柄；调用两次或传入任意无效地址属于 ABI 调用方错误，Rust 的空指针检查不能防御所有非法 native 指针。

RimeProvider 用进程级 mutex 串行访问 librime；每个 Host activation 拥有 Provider/session，聚焦 context 改变时清空旧 composition。最后一个 session 销毁才 finalize Rime，且不会在 DllMain 中初始化或部署引擎。snapshot 输出仅复制当前逻辑页，v1 仍有字符串分配，尚未做延迟基准或零分配优化。

`InputState` 包含 preedit、UTF-8 byte caret、候选及其全局 index、页内 selected、page/page_size/last_page、commit、schema、常见 options 与 active。FFI 暴露常见 option 位；任意 Rime bool option 可通过产品配置设置。候选未知总数不伪造。commit 在成功写入 Windows 文档后才 `ack_commit`；确认前禁止处理下一按键，以防静默丢失或重复提交。

## 配置与 AppProfile

权威来源为 Rust Config：内置默认 → `[default]` → `[platform.windows]` → `[[profiles]].overrides` → Runtime table。表递归合并，数组/标量替换，未配置项继承。未知产品字段保存在 effective table，但只有 README 列出的字段实际执行。原生 Rime YAML 不做自动往返序列化：编辑和保存用户自己的 custom patch 原文，第三方 schema 保持原样。

EXE 由 Windows 进程路径提取 basename，在 focus/context 变化时交给 Core 匹配；目前大小写不敏感匹配覆盖 ASCII EXE 名。无默认游戏特例、应用词库分类或自动专业词库。未来 matcher 可增加 window class/title/package/custom，但本轮只实现 executable。

`config_bridge.rs` 提供实际 ProductConfigBridge 和维护工具共用规则：读取 mtime + SHA-256 修订，结构化编辑通过 `toml_edit` 保留未知字段和注释；高级原文编辑先验证再保存。保存取得 MYIME 写入锁、重查修订，写同目录临时文件和备份后发布；外部修改触发冲突，不静默覆盖。未知/损坏原文可以读取供用户修复。主题和 custom YAML 保存也使用该修订契约，原生 YAML 验证后保存原文。MYIME 的写锁不能约束任意外部编辑器，最终发布仍存在外部编辑竞争的系统边界，不能宣称通用文件 CAS。

C# / WinForms 六页界面只负责交互、草稿和目录入口；产品配置规则、继承和安全写入由 Rust CLI 维护。主题外观通过 TOML 编辑页调整，再启动原生预览读取已保存文件。基础设置可编辑 Default 和 Windows 层，AppProfile 页只按 EXE 匹配；“启用最近部署方案”写 Windows 层并核对有效结果，保留 Profile 的明确覆盖。配置/主题保存后由用户重启目标应用，不在按键路径监视文件或用 IPC 刷新状态。持续文件监视、自动双向同步尚未实现。

原生 schema 页大小属于部署配置，当前初始 `default.custom.yaml` 给出 7。用户在 Rime 配置页保存自己的 patch，再明确部署；不能用候选窗口的可见行数覆盖引擎页大小。CLI 只编辑 `*.custom.yaml`，不将 UI 控件值随意覆盖第三方 schema。

## 离线词库与代际部署（2026-10-03）

`importer.rs` 的生产导入实际经过 `Box<dyn Importer>` 和 Converter：TXT/严格 CSV → word/code/frequency/source → 检查与去重 → manifest + dictionary + metadata 数据包。UTF-8/BOM、记录范围、CSV 引号/实际行号和控制字符均有边界；缺码词保留报告和 metadata，不自行生成拼音。源文件预览带修订，变化后拒绝写包。输出只创建全新包目录，不打开 Rime 数据库。详细格式见 [importing.md](importing.md)。

数据包自带 MYIME 自有派生 schema 和聚合字典；本轮基于官方 `pinyin_simp`，输入码必须是基础字典的完整拼音。Rust 是文件命名、reserved ID、生成模板和 manifest/hash 的权威来源。`package.validate` 校验目录/文件类型、路径、大小/hash、词条/统计，再对比生成模板；不能通过重算 hash 引入任意 schema 指令。Windows 工具复制完整包到隔离 workspace，调用 Rust 校验副本后交给官方 Rime 编译。

多个显式部署包由 Rust 生成稳定 `myime_global` 聚合字典/schema，按固定包 ID 顺序引用基础字典和全部已部署包；每包 `myime_<id>` 方案也保留，供用户在 AppProfile 显式选用。没有扫描用户词库目录后自动启用未部署包，也没有按 EXE 猜测专业词库。稳定全局字典身份避免每加一个包就换一个默认学习身份；它并不等于跨应用学习槽已经协调。

生成 schema 显式提供原始 `schema/schema_id` 和名称，通过官方 `__include` / `__patch` 复用基础配置、schema 元数据依赖并设置 translator/dictionary。显式 root `__patch` 会禁用自动 custom hook，因此模板把 `myime_<id>.custom:/patch?` 或 `myime_global.custom:/patch?` 加进补丁列表，保留手改 YAML 的入口。已有 schema、用户 patch 和 userdb 不被导入器覆盖。[官方 include/patch 规则](https://github.com/rime/home/wiki/Configuration)

Windows `myime-data-tool` 管理 `%LOCALAPPDATA%/MYIME/rime/workspaces/<generation>`：复制安装基础数据、前代显式包和原文 patch，创建独立编译 user 目录，调用官方部署并逐一预检所有基础、全局及单包方案；检查产品配置的 schema 引用后才更新 `active-workspace.txt`。发布失败保留旧 selector，旧代和失败代不自动删除。选中共享数据路径在进程的租约期间缓存，运行中的输入继续使用原代；新启动/激活使用新代。用户学习仍在持久 slots 中，离线工具不会复制覆盖 live LevelDB。

回退前确认上一代完整且配置引用均存在；恢复基础也先检查 `pinyin_simp` 基础数据，悬空 schema 会拒绝发布。维护工具不替用户改配置：先将即将失效的 Default、Windows 及 Profile schema 改为目标代已有方案，再重试；恢复基础时使用 `pinyin_simp`。普通桌面设置与受限应用数据各自独立；新数据代、设置 GUI 或安装步骤不扩大受限目录权限。

本轮只完成词库包与 TXT/CSV 切片。多个 slots 的学习导出/merge、专有输入法格式、自动编码/字形 Converter 和 WebDAV 尚未实现；不能把导入静态数据包或代际回退描述为用户词库同步。

## 需要调整的原设想

### 派生 schema 只在补丁展开后提供 ID

最初生成模板只通过 `__include` / `__patch` 设置新方案 ID。实际调用官方 SchemaUpdate 时，更新流程会先读取原始 `schema/schema_id`，此时还没有展开配置；缺少原始元数据会拒绝部署。因此模板显式声明原始 schema 元数据，再嵌套复用基础 schema 的依赖及其他配置。后续仍由官方 Rime 编译器处理 include/patch，没有修改 librime 或复制拼音算法。这是共享配置 Bridge 的数据约束，不影响未来平台复用；最终部署测试结果单独记录。

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

模式指示器使用 ITfLangBarItemButton/ITfSource + GUID_LBI_INPUTMODE，读取 Core options 的 ascii_mode，不持有 Core handle。右键“设置”通过单一平台动作启动安装目录的独立 C# 进程；仅普通桌面且配套程序存在时可用，受限环境置灰。左键暂不切换。菜单和启动生命周期属于 Host，跨平台 Core 不引入 HWND/HICON/COM 或 GUI 依赖。SVG 源稿与导出的 ICO 同存平台资源目录，构建嵌入资源，不自动运行素材生成脚本。

文本写入失败时暂停当前 context，避免继续破坏已知状态；切换焦点重置后恢复。该策略不能把失败文档事务神奇回滚成原状态，相关故障注入与实际应用恢复仍要继续完善。

## 后续边界

原生 librime 调用参考 [rime_api.h](https://github.com/rime/librime/blob/1.17.0/src/rime_api.h)。扩展 trait 是源代码级接口，不是稳定二进制插件 ABI。InputProvider/InputProviderFactory 和 Importer/Converter 已接实际流程，产品 ConfigBridge 已实现；DictionaryProvider、CompatibilityProvider、SyncProvider 仍是合约，不构成完整插件管理器。词库和主题优先 manifest+data+metadata，不需要外部 DLL。未来 SyncProvider 传输 Rime 导出的同步对象，通过 ETag/If-Match 表达冲突，不进入按键路径。

本次更新已获授权执行构建和自动回归，实际结果见 [testing.md](testing.md)；安装与 Notepad/Edge/开始菜单的真实交互仍待用户验收。搜索无候选依旧待目标日志，不能由设置/导入功能推断已经解决。后续工作见 [roadmap.md](roadmap.md)；本轮未进行游戏启动/输入验收，游戏专项安排在最后。游戏策略只能来自实际比较测试，不能根据 EXE 名硬编码猜测。
