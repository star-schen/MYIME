# MYIME 分阶段路线（2026-10-03）

共同约束：Windows Host 负责平台适配；官方 librime/schema/dictionary 负责输入算法。按键全程进程内、不使用 IPC；产品 TOML 与原生 Rime YAML 分离并保留用户 patch。当前扩展 trait 是源码级替换接口，没有稳定二进制插件 ABI。各阶段实施前明确输入/输出、权威数据源、验收和失败恢复，按实际需要组织职责，不为未来功能过度拆分 crate。

截至 2026-10-03，阶段 1/2 已实施，阶段 3 已有独立设置、配置 Bridge 和代际部署实现；阶段 4 已落地 TXT/CSV 与数据词库包切片。自动测试、真实应用验收和未完成能力分开记录在 [testing.md](testing.md)。多个学习槽的一致性尚未完成，因此不能据静态词库包功能宣称同步已经实现。Windows 搜索无候选依旧待目标日志。

| 阶段 | 交付范围 | 验收/交接 |
|---|---|---|
| 1 Provider 接入 | 已实现 Core 的 `Box<dyn InputProvider>`、唯一生产 RimeProvider、配置工厂、原子 Profile 替换、安全状态/commit 编排、薄 C ABI；测试专用 Provider | 避免未部署 schema 被错误接受；保留 ABI v1 原结构；编译/合约/真实 Rime 与 TSF 结果见 testing.md，真实桌面验收独立进行 |
| 2 候选主题数据包 | 已实现 TOML manifest/版本/字体/颜色/间距、横竖布局、圆角/阴影、高对比度颜色、用户包覆盖/撤销和独立预览；本轮设置页增加用户主题 TOML 编辑和预览入口 | 候选顺序/分页保持 Rime 语义；回退/点击/资源/预览回归记录见 testing.md；真实多屏/DPI/高对比度仍待验收，见 themes.md |
| 3 配置 Bridge 与独立 C# 设置 | 已实现六页 C# 设置、Rust mtime + SHA-256 修订/备份/冲突、未知项/注释保护、产品与 custom YAML 原文编辑；Windows 隔离 Rime 数据代构建、预检、发布、回退和基础恢复 | GUI 及维护 IPC 仅用于外围操作；“启用”写 Windows 层并核对有效结果；恢复/回退拒绝悬空 schema，保留旧代/slots；实际测试结果见 testing.md，未自动安装 |
| 4 本机学习一致性、词库数据包与导入 | **切片已实现：**真实 TXT/CSV Importer → 中间模型 → Converter → 带 hash 的新数据包；官方 Rime 派生方案和稳定 `myime_global` 聚合全部显式部署包，每包方案留给 AppProfile。**待完成：**持久 slots 的学习协调/原生导出与 merge、专有格式及更多 Converter | 不猜编码、不按 EXE 启用词库、不覆盖 live LevelDB；包验证与代际发布有失败边界；下一步先补可复现的学习协调，再进入远端同步 |
| 5 WebDAV | SyncProvider 传输 Rime 导出的同步对象，ETag/If-Match 表达并发冲突；凭据独立安全存储 | 依赖阶段 4 的本机一致性；离线重试、远端冲突、账号边界和导出数据验收；不传输 live userdb、不进入按键路径 |
| 6 分立专项 | 外部代码插件、其他平台、游戏支持分别立项 | 不在同一功能中捆绑；外部代码先确定 ABI/版本/信任/失败隔离，其他平台以输入协议适配，游戏专项最后按实际证据制定兼容策略 |

## 阶段 1 的真实扩展点

生产调用顺序及模块责任见 [architecture.md](architecture.md)。当前可在 Rust 源码中实现 InputProvider/InputProviderFactory，再由创建路径选用工厂；C Host 只看到 opaque handle 与 C-compatible view。TXT/CSV Importer、Normalize Converter 和产品 ConfigBridge 已有实际实现；主题/词库优先数据包。DictionaryProvider、SyncProvider、CompatibilityProvider 仍是后续扩展合约，没有动态 DLL 插件发现、复杂注册框架、外部脚本输入引擎或第二个生产算法。

测试 Provider 仅在 cfg(test) 使用，不对用户提供切换开关。关闭默认 rime feature 可独立测试安全 Core/配置/导入，维护 CLI 也使用该路径；不构成无 Rime 的产品输入 DLL。本轮已获授权执行构建和自动回归，结果以 [testing.md](testing.md) 为准；安装与真实桌面验收待用户操作。

## 下一轮优先顺序

1. 先用配套安装包验收六页设置、配置继承/冲突、主题编辑/预览、连续部署多个词库包与恢复流程；发现具体问题后按失败证据修复，不一次新增全部远期接口。
2. 继续阶段 4：为多个持久学习槽制定原生 Rime 导出/merge 的离线协调、占用处理和失败恢复；保持 `myime_global` 的稳定学习身份，不能通过复制 userdb 达成同步。
3. 学习协调可靠后接 WebDAV：凭据、ETag/If-Match、异步重试与冲突，不放入输入路径。
4. 外部代码插件、其他平台和游戏专项分别立项；设置体验和主题可视编辑按实际使用反馈扩展，持续监视/双向配置同步也需要独立规格。

部署与设置是可分别完成的操作。启用新部署方案写 Windows 配置层，AppProfile 的单包覆盖保留。回退或恢复基础前，把将失效的 Default/Windows/Profile schema 改为目标数据代已有方案；恢复基础使用 `pinyin_simp`。预检不通过时旧 selector 保留，不自动删学习数据、安装或替换 DLL。操作说明见 README 和 [设置程序](../windows/settings/README.md)。

## Windows 搜索单独诊断

本轮保留候选 Begin/Update/End 与中/A 图标，只加入无正文、限量的 pbShow/Show/context/HWND/caret HRESULT/实际可见性/隐藏原因日志；没有目标日志，修复待定位。完整 ITfFnSearchCandidateProvider/ITfIntegratableCandidateListUIElement 搜索建议集成另立任务，不用缺少完整接口来解释未经定位的候选栏问题。步骤见 [search-candidates.md](search-candidates.md)。

## 阶段 6 的分立范围

外部代码插件需要独立设计稳定边界、兼容版本、生命周期与故障策略，不能把现有 Rust trait 描述为可跨语言加载的稳定插件 ABI。其他平台分别实现其输入协议、文本事务和目录协调，复用安全 Core/官方 Rime。

游戏专项放到最后。本轮未执行真实游戏启动和输入验收，不推断白名单、CEF、网络或显示模式是根因。后续按授权收集具体应用、输入协议、加载模块、权限、崩溃报告和对照输入法结果，再决定是否需要 legacy IMM32 或其他适配；策略必须来自实测证据，不按 EXE 名硬编码词库/兼容行为，不修改游戏或其他输入法。
