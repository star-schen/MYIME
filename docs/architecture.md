# 架构与官方约束

## 输入路径与职责

`Windows TSF → WindowsInputAdapter → myime/core.h → Rust Core → rime_get_api() → 原路返回`。

输入不跨进程。C++ bridge 位于 `crates/core/native`，只做官方 API 的版本检查、结构体初始化/释放和字段读取；不含输入算法或候选策略。Rust 使用 opaque Rime 输出句柄，避免手工复制不稳定的函数表布局，unsafe 集中在 `ffi.rs` 和 `rime.rs`。未来替换 bridge 为自动生成绑定不影响 Host ABI。

Windows 决定 HWND、绘制、字体、caret 屏幕坐标、键盘虚拟键转换和文本 API；Rust/Rime 决定候选顺序、内容、分页及 commit。候选窗口只逐项显示快照，不排序、不过滤。设置 GUI、同步、导入等外围进程暂未创建，扩展 trait 不会隐式启动线程或 IPC。

## ABI 与 ownership

`include/myime/core.h` 是 C++/Rust 边界的权威定义。`MyimeCore*` 是 opaque handle；字符串为 UTF-8 的 pointer+length，不能当作 NUL 字符串读取。输入路径使用 C-compatible 整数/指针，不跨语言暴露 C++ class 或 Rust 内存布局。

句柄仅在创建线程使用，所有输出 view 借用至该句柄下一次 mutation/destroy。Host 必须先复制需要长期保留的数据。`myime_destroy` 释放句柄；调用两次或传入任意无效地址属于 ABI 调用方错误，Rust 的空指针检查不能防御所有非法 native 指针。

Core 用进程级 mutex 串行访问 librime；每个 Host activation 拥有 session，聚焦 context 改变时清空旧 composition。最后一个 session 销毁才 finalize Rime，且不会在 DllMain 中初始化或部署引擎。snapshot 输出仅复制当前逻辑页，v1 仍有字符串分配，尚未做延迟基准或零分配优化。

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

application-rendered UI 必须通过 `ITfUIElementMgr`、`ITfCandidateListUIElement` 和 `BeginUIElement` 的 `pbShow` 协商。仅实现一个候选 HWND 不等于支持此模式。因此 MVP 不注册 UI-less capability，并拒绝 `TF_TMAE_UIELEMENTENABLEDONLY`；测试程序可观察其他实现这一协议的输入法。未来完整 UI Element adapter 必须向应用报告 Rime 的真实页边界，不能根据游戏固定 9 行推断 engine page_size。[官方 UI-less 说明](https://learn.microsoft.com/en-us/windows/win32/tsf/uiless-mode-overview)

## 系统接入和错误处理

COM server 使用 Apartment 线程模型，DLL 引用计数包括 factory、service、edit session 和 composition observer。DllMain 只保存模块句柄。通过绝对路径与 DLL_LOAD_DIR/DEFAULT_DIRS 安全加载 Core 及其依赖，避免依赖工作目录。安装到 Program Files；注册同时处理 COM、TSF profile 和 keyboard category。[官方注册说明](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)

键盘转换使用 Windows ToUnicodeEx 的不修改 dead-key 状态选项，v1 主要覆盖 ASCII schema 输入和编辑键。只读/禁用 context 和 password input scope 不送入学习引擎；Ctrl/Alt/Win 组合键交回应用。错误写 OutputDebugString，不记录正文。FFI 将 Rust unwind 转换成错误；非法 native 地址、进程级 OOM/abort 并不在可恢复范围内。

文本写入失败时暂停当前 context，避免继续破坏已知状态；切换焦点重置后恢复。该策略不能把失败文档事务神奇回滚成原状态，相关故障注入与实际应用恢复仍要继续完善。

## 后续边界

原生 librime 调用参考 [rime_api.h](https://github.com/rime/librime/blob/1.17.0/src/rime_api.h)。扩展 trait 是源代码级接口，不是稳定二进制插件 ABI。词库包优先 manifest+data+metadata；Importer 只输出 word/code/frequency/source 中间模型；SyncProvider 传输 Rime 导出的数据并通过 ETag/If-Match 表达冲突，均不进入按键路径。

下一轮先完成管理员安装与 Notepad/Edge 验收，再补 IME UI Element/IMM32 适配、跨进程用户词库 merge、显示属性/DPI、语言栏和热路径性能测量。游戏策略只能来自实际比较测试，不能根据 EXE 名硬编码猜测。
