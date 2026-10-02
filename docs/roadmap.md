# MYIME 分阶段路线（2026-09-30）

共同约束：Windows Host 负责平台适配；官方 librime/schema/dictionary 负责输入算法。按键全程进程内、不使用 IPC；产品 TOML 与原生 Rime YAML 分离并保留用户 patch。当前扩展 trait 是源码级替换接口，没有稳定二进制插件 ABI。各阶段实施前明确输入/输出、权威数据源、验收和失败恢复，按实际需要组织职责，不为未来功能过度拆分 crate。

截至 2026-10-02，阶段 1 与阶段 2 已实施并完成自动回归；Windows 搜索仍待目标日志。阶段 3 及以后是后续计划。

| 阶段 | 交付范围 | 验收/交接 |
|---|---|---|
| 1 Provider 接入 | Core 实际持有 `Box<dyn InputProvider>`；唯一生产 RimeProvider、配置工厂、原子 Profile 替换、安全 Core 状态/commit 编排、薄 C ABI；测试专用 Provider | Release 编译、14 项 Rust 测试、真实 Rime/C ABI 与 TSF 回归已通过；修复未部署 schema 的错误接受；ABI v1 保持不变，真实桌面验收待安装 |
| 2 候选主题数据包 | 已实现 TOML manifest/版本/字体/颜色/间距、横竖布局、圆角/阴影、高对比度颜色、用户包覆盖/撤销和同绘制代码的独立预览 | Release 编译、20 项 Rust 测试、主题 ABI/点击/回退/资源/预览及既有 TSF 回归通过；候选顺序/分页保持 Rime 语义，真实多屏/DPI/高对比度仍待验收；见 themes.md |
| 3 配置 Bridge 与独立 C# 设置 | 将 ConfigBridge 的 revision/mtime/hash、外部修改冲突与 patch 保留规则落地；独立 C# 设置进程编辑产品配置、按约定交接原生部署 | 设置进程不进入按键路径；未知 YAML/用户 patch 保留；冲突显示和部署失败可恢复；中/A 左键与菜单动作另行确定 |
| 4 本机学习一致性、词库数据包与导入 | 基于 Rime sync export/merge 协调各持久 slot 的学习；词库 manifest+data+metadata；TXT/CSV → ImportedWord → Converter → 离线部署 | 不复制覆盖 live LevelDB；先实现可复现的本机 merge/冲突规则，再接词库包和导入；来源、编码、频次、失败回退明确 |
| 5 WebDAV | SyncProvider 传输 Rime 导出的同步对象，ETag/If-Match 表达并发冲突；凭据独立安全存储 | 依赖阶段 4 的本机一致性；离线重试、远端冲突、账号边界和导出数据验收；不传输 live userdb、不进入按键路径 |
| 6 分立专项 | 外部代码插件、其他平台、游戏支持分别立项 | 不在同一功能中捆绑；外部代码先确定 ABI/版本/信任/失败隔离，其他平台以输入协议适配，游戏专项最后按实际证据制定兼容策略 |

## 阶段 1 的真实扩展点

生产调用顺序及模块责任见 [architecture.md](architecture.md)。当前可在 Rust 源码中实现 InputProvider/InputProviderFactory，再由创建路径选用工厂；C Host 仍只看到 opaque handle 与 C-compatible view。没有动态 DLL 插件发现、复杂注册框架、外部脚本输入引擎或第二个生产算法。Importer、DictionaryProvider、Converter、ConfigBridge、SyncProvider、CompatibilityProvider 仍是接口定义，没有导入/同步/设置产品实现。

测试 Provider 仅在 cfg(test) 使用，不对用户提供切换开关。关闭默认 rime feature 的入口只用于安全 Core/配置测试，不构成无 Rime 的产品 DLL。当前 Release 编译和完整自动回归已按用户授权完成；安装与真实桌面验收待用户操作，见 [testing.md](testing.md)。

## Windows 搜索单独诊断

本轮保留候选 Begin/Update/End 与中/A 图标，只加入无正文、限量的 pbShow/Show/context/HWND/caret HRESULT/实际可见性/隐藏原因日志；没有目标日志，修复待定位。完整 ITfFnSearchCandidateProvider/ITfIntegratableCandidateListUIElement 搜索建议集成另立任务，不用缺少完整接口来解释未经定位的候选栏问题。步骤见 [search-candidates.md](search-candidates.md)。

## 阶段 6 的分立范围

外部代码插件需要独立设计稳定边界、兼容版本、生命周期与故障策略，不能把现有 Rust trait 描述为可跨语言加载的稳定插件 ABI。其他平台分别实现其输入协议、文本事务和目录协调，复用安全 Core/官方 Rime。

游戏专项放到最后。用户已反馈游戏可以正常启动，认为输入限制与白名单有关；这是待核实线索。以后按授权收集具体应用、输入协议、加载模块、全屏/权限和对照输入法结果，再决定是否需要 legacy IMM32 或其他适配，不按 EXE 名硬编码白名单/词库策略、不修改游戏或其他输入法。当前不宣称游戏端到端输入已通过。
