# 候选主题数据包（2026-10-02）

本轮实现路线图阶段 2：TOML 主题包、产品配置选择、Windows 候选绘制和独立预览。Rust 负责解析/校验数据，Windows 负责绘制、布局、DPI 和资源；Rime 的候选内容、顺序和逻辑分页不变。没有主题 DLL、脚本或输入算法插件。

## 直接使用

已打包后可运行 `out/MYIME-Release/myime-theme-preview.exe`，无需安装输入法。选择主题，临时调整字号/横竖排；点击“重新加载”读取修改后的主题文件。预览使用与 Host 完全相同的 CandidateWindow 和示例候选；不创建 Rime session、不改产品配置或用户词库。它是阶段 2 的原生预览工具，独立 C# 设置程序仍属于阶段 3。

在 `%LOCALAPPDATA%/MYIME/config.toml` 中添加或修改下列字段；已有 `[default.ui]` 时只改 theme 值，不重复添加表：

```toml
[default.ui]
theme = "dark"
```

| id | 显示名 | 布局 |
|---|---|---|
| default | 雾灰 | 灰色竖排，也是内嵌回退样式 |
| light | 素白 | 浅色竖排 |
| dark | 墨夜 | 深色竖排 |
| ribbon | 灰色横排 | 按顺序横排，超过可用宽度时换行 |

恢复默认用 `theme = "default"`，或移除产品配置中该字段；如 platform/AppProfile 也配置了 theme，仍按已有分层规则覆盖。可通过 `[platform.windows.ui]` 和 `[profiles.overrides.ui]` 显式选主题，不猜测应用偏好。产品配置在目标应用启动时读取，修改后重启该应用；已经运行的搜索/Shell 可能需要用户保存工作后注销再登录。没有输入路径文件监视或自动跨进程刷新。

## 自定义包与撤销

目录为 `%LOCALAPPDATA%/MYIME/themes/<id>/theme.toml`。受限应用使用其权限允许的自身 MYIME 数据根，沿用词库/配置隔离原则。先查用户包，再查安装版本的 `themes/<id>/theme.toml`。同 id 用户包可覆盖内置包；删除用户包后恢复内置包。建议使用新的 id，避免无法分辨覆盖来源。

示例：`themes/my-gray/theme.toml`：

```toml
format_version = 1
id = "my-gray"
name = "我的灰色"
version = "1.0.0"
description = "大字号、紧凑竖排"

[font]
family = "Microsoft YaHei UI"
size = 20
weight = 400

[layout]
orientation = "vertical"
padding_x = 10
padding_y = 8
spacing = 3
border_radius = 6

[colors]
background = "#EEEEF0"
text = "#242429"
highlight = "#D4D4DB"
highlight_text = "#17171C"
```

产品配置选 `theme = "my-gray"` 后重启目标应用。撤销时先恢复默认选择，再删除这个主题目录；不会部署 Rime、改 YAML 或删除用户词库。只更换 ui 配置时 Core 复用现有 Provider，保留组合与待确认 commit；输入配置变化仍受原有组合/commit 限制。

包缺失、格式不兼容、内容损坏、目录 id 与 manifest 不一致或路径越出目录时，回退到内嵌雾灰主题，记录 Theme fallback 原因。即使安装目录的默认包损坏，仍有回退。主题选择必须是小写 ASCII 字母、数字、`-`、`_`，长度 1..64，不能用绝对/相对路径；非法产品配置按现有配置错误处理。主题文件最多 64 KiB。读入后只用内存快照，不在每次按键读取文件。

## 格式和字段

`format_version=1`、id、name、version 必填，description 可选。未知字段不影响当前读取；当前不写回用户文件，因此也不会覆盖扩展元数据。外观项未声明时继承仓库 `themes/default/theme.toml`；同一个文件也是 Rust 内嵌回退来源，不另维护一套 C++ 默认值。

| 表 | 字段与约束 |
|---|---|
| font | family（已安装字体名，非字体文件路径）；size 10..48 DIP；weight 100..900 |
| layout | orientation=vertical/horizontal；padding_x/y 0..64；candidate_padding_x 0..64、candidate_padding_y 0..32；spacing 0..32 |
| layout | border_width 0..4；border_radius 0..32；min_width 80..1200；max_width 80..1600 且不小于 min_width；caret_gap 0..32 |
| colors | background、text、muted、label、highlight、highlight_text、border；均为 #RRGGBB，无外部图像资源 |
| shadow | enabled 布尔；size 0..24 DIP；opacity 0..160，0 不显示 |
| display | preedit、comments、labels、page_controls 均为布尔 |

尺寸是 DIP（96 DIP/inch），颜色在 C ABI 中是 0xRRGGBB；不是 Windows COLORREF。Host 做转换。横排换行、长文本省略和极高候选页的滚轮视口只影响显示；不会删掉/排序候选或把显示行数当作 Rime page size。鼠标命中仍返回当前 Rime 页的原始局部 index，翻页继续交给 Core/Rime。隐藏窗口不接受旧鼠标操作。

## Windows 行为与边界

保留原有 Begin/Update/End UI Element 协商。应用要求自绘时不显示 Host 窗口或阴影；主题仅影响 Host 自绘部分，不要求应用采用我们的颜色/布局。窗口和阴影不夺焦点、没有常驻托盘进程；失焦/结束组合同时隐藏。

字体按 [GetDpiForWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdpiforwindow) 的 DPI 缩放，处理 [WM_DPICHANGED](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged) 重排。IME 继承宿主 DPI 上下文，不改变宿主进程/线程的 DPI 设置，以保持 TSF caret 坐标一致。独立预览通过自己的 manifest 使用 PerMonitorV2。

圆角使用窗口 region；柔和阴影使用小型、透明、不接受输入的 layered HWND，通过 [UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow) 更新。阴影是可选效果，创建或绘制失败只禁用效果，不阻断输入；高对比度模式使用系统颜色并隐藏阴影。没有引入 DirectX/DWM 圆角策略依赖或第三方 UI 库。

字体与双缓冲位图按生命周期释放，内容/样式/可用空间未变化时复用布局，不重新绘制或重算阴影。当前还没有完整 UI Automation provider、主题 ZIP 导入、外部图片/字体打包、配置热更新或 C# 编辑器；这些应通过真实需要扩展，不在输入路径加入 GUI/IPC。

## 代码和测试

Rust `theme.rs` 是数据包规则的权威来源；`theme_ffi.rs` 只负责线程、参数、输出及生命周期。`include/myime/theme.h` 是独立 presentation ABI 1，使用 opaque handle 和固定 C 输出；借用字符串在 Host/预览中复制后立即释放句柄。Core ABI v1 保留原结构，新增只读 myime_ui_theme_id 导出；升级必须使用配套 Host/Core。

`scripts/test.ps1 -Configuration Release` 包含 20 项 Rust 测试、扩充的真实 Rime/C ABI、theme probe、preview --self-check 及既有 Windows 回归。主题 probe 使用自己创建的临时目录，覆盖用户包优先/撤销、损坏/缺失回退、ABI 大小及线程、横竖排点击原 index、重复内容不绘制、40 次换主题 GDI 资源、100 候选的视口和隐藏后旧点击。示例渲染 PNG 在 `build/Release/test-artifacts/theme-*.png`，不是用户输入截图，不提交到 Git。

当前自动回归和示例渲染通过；真实 Notepad/Edge、Windows 搜索、多显示器不同 DPI、高对比度及实际鼠标路由仍需安装后验收。主题实现不等于搜索候选隐藏原因已经修复。
