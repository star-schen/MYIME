# 低功耗皮肤组成与编辑（2026-10-06）

本版把新建、另存和编辑操作集中到皮肤编辑器。候选主题页只负责选择、应用、打开编辑器和目录，页面下方使用窗口内图片预览。**本轮没有启动界面或验证绘制结果。**

## 一个皮肤包含什么

皮肤是 `theme.toml` 数据包，不包含 DLL、脚本或输入算法。显示名称取 `name`，`id` 只用于目录和配置引用。

| 部分 | 决定的外观 |
|---|---|
| 信息 | 显示名称、版本、说明；id 对应主题目录 |
| 字体 | 已安装的字体名称、字号、粗细 |
| 布局 | 横排/竖排、窗口及候选项内边距、间距、宽度、边框、圆角、距光标距离 |
| 全局背景层 | 整个窗口的实心背景 |
| 输入栏卡片 | 独立的背景层、边框、圆角，以及输入文字和光标的字符层 |
| 候选栏卡片 | 独立的背景层、边框、圆角、选中背景，以及正文、注释、编号、选中文字的字符层 |
| 阴影 | 是否显示、扩散大小、透明程度 |
| 显示内容 | 是否显示组词、注释、候选编号、翻页控件 |

皮肤不改变 Rime 候选顺序、组词、逻辑页数或学习；横排换行只改变显示。当前没有背景图片、字体文件打包或 ZIP 导入。

本阶段采用低功耗实心绘制：不增加颜色透明通道、桌面获取、背景/文字滤镜或效果层。继续使用 GDI 与现有缓冲，仅在展示内容、主题或尺寸变化时重画。原有缓存窗口阴影保留，可关闭；它不是新增的皮肤透明层。没有新增常驻进程。

## 选择和编辑

运行 `myime-settings.exe`，进入“候选主题”。下拉显示名称，选择后自动刷新图片，无需点预览按钮。内置皮肤为雾灰 default、素白 light、墨夜 dark、灰色横排 ribbon。

“皮肤编辑器”在独立普通窗口内提供：

1. 读取现有皮肤。
2. 新建默认样式或另存当前草稿，填写独立 id 和显示名。
3. 通过属性面板调整上述各部分；也可切换 TOML 原文编辑。
   选中参数后，下方注释栏显示该参数的中文名称和说明，随简繁界面语言切换。字段路径和 TOML 键保持不变。
4. 查看草稿图片。无效草稿保留编辑内容，并提示校验错误。
5. 保存到用户主题目录，Rust 检查字段范围、目录 id 和文件修订。发生外部修改冲突时拒绝覆盖。

图片由原生候选绘制代码离屏输出，再嵌入设置窗口；不创建置顶候选 HWND、不创建 Rime session、不改学习记录。草稿刷新会合并连续编辑请求，旧结果不覆盖新草稿。普通运行 `myime-theme-preview.exe` 也会打开皮肤编辑器；它的 `--render` 入口仅供设置程序生成图片。

内置主题保存为用户覆盖，不修改安装文件。新主题在点击保存前不会建立主题文件。

## 应用与即时刷新

“应用为全局默认”保存 `[default.ui].theme` 并清除 Windows 层的主题覆盖。显式 AppProfile 主题继续优先；要让这些应用也跟随默认，请在应用配置中选择“同 Windows 设置”。这里指 MYIME 基础设置中的 Windows 通用配置，其中未设置的项使用全局默认。

设置保存后发布新配置代和主题快照，再广播通知已运行 Host。Host 另有约一秒检查补充，重新读取主题并更新当前候选窗口；按键仍使用内存中的样式。正在组词时只更新展示和策略，输入方案替换延后到组合结束。若发布失败，界面明确提示“已保存，但尚未即时发布”；不要将这个提示当作运行中应用已经更新。

首次新版安装需注销再登录，使共享目录的数据组权限生效；这是初始化权限要求，后续正常切换皮肤不以重启应用作为生效步骤。新部署 Rime 数据代仍按原有独立流程在重开应用后使用。

## 目录、继承与撤销

编辑源在 `%LOCALAPPDATA%/MYIME/themes/<id>/theme.toml`。先读用户包，再读安装主题；用户覆盖缺失后恢复安装包。必要的主题由设置发布到该 Windows 用户的 ProgramData 展示目录，受限应用只读，不在输入路径复制私人文件。

撤销自定义主题时先切回默认，再删除其用户包；删除同 id 用户覆盖后使用刷新/保存发布目录变化。配置或包缺失、损坏、格式不兼容、目录 id 不符、解析后路径越界时，绘制回退到内嵌雾灰，并记录原因。路径判定针对已打开文件的最终 NT 路径，再读取同一个句柄，避免在 AppContainer 中用 DOS 卷路径规范化失败。

外观字段缺省继承 `themes/default/theme.toml`；该文件也是 Rust 内嵌回退的唯一来源。注释和未知字段在编辑/保存时保留，保存带修订检查及同目录备份。最大文件 64 KiB；id 为 1..64 位小写 ASCII 字母、数字、短横线或下划线。

## 字段约束

`format_version=1`、id、name、version 必填；description 可选。

| 表 | 字段与约束 |
|---|---|
| font | family 为已安装字体名；size 10..48 DIP；weight 100..900 |
| layout | orientation=vertical/horizontal；padding_x/y 0..64；candidate_padding_x 0..64、candidate_padding_y 0..32；spacing 0..32 |
| layout | border_width 0..4；border_radius 0..32；min_width 80..1200；max_width 80..1600 且不小于 min_width；caret_gap 0..32 |
| colors | background、text、muted、label、highlight、highlight_text、border，均为 #RRGGBB |
| background | color：全局实心背景颜色，#RRGGBB |
| input.background | color、border：#RRGGBB；border_width 0..4 DIP；border_radius 0..32 DIP |
| input.characters | color、caret：#RRGGBB，分别为输入文字和光标 |
| candidates.background | color、border、highlight：#RRGGBB；border_width 0..4 DIP；border_radius 0..32 DIP |
| candidates.characters | color、comment、label、selected：#RRGGBB，分别为正文、注释、编号、选中文字 |
| shadow | enabled 布尔；size 0..24 DIP；opacity 0..160 |
| display | preedit、comments、labels、page_controls，均为布尔 |

DIP 按 Windows DPI 缩放，颜色 C ABI 为 0xRRGGBB，由 Host 转为 COLORREF。字体缩放和重排参考微软 [GetDpiForWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdpiforwindow)、[WM_DPICHANGED](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged)。Host 继承宿主 DPI 上下文，不改变宿主设置。

新分层字段都是可选的；旧主题继续有效，不会自动重写用户文件。`background.color` 缺省使用旧 `colors.background`；两张卡片的背景缺省使用已解析的全局背景，字符缺省使用旧颜色角色。卡片边框与圆角缺省为 0，避免给旧主题添加额外边框。编辑器只显示分层颜色入口；旧 `colors` 仍可在 TOML 原文中编辑作为兼容默认。背景效果和八位 RGBA 颜色不在本版接口中。

例如，可在现有主题后添加：

```toml
[background]
color = "#E8E8E8"
[input.background]
color = "#FFFFFF"
border = "#B0B0B0"
border_width = 1
border_radius = 4
[input.characters]
color = "#202020"
caret = "#606060"
[candidates.background]
color = "#F5F5F5"
border = "#B0B0B0"
border_width = 1
border_radius = 4
highlight = "#DCE8F5"
[candidates.characters]
color = "#202020"
comment = "#707070"
label = "#606060"
selected = "#102030"
```

主题文件仍是 `format_version=1`；展示 ABI 升为 2，需要 Core、Host、预览程序和设置工具一起构建、打包升级。本次源码修改未构建、未测试，不表示已安装的软件已经具备新增功能。

候选窗口遵守 Begin/Update/End UI Element 和 `pbShow`：应用要求自己绘制时隐藏 Host 窗口和阴影。圆角使用 region，阴影使用 [UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow)；失败只关闭阴影，高对比度使用系统颜色。绘制窗口在创建时绑定有效 owner，首次显示同步绘制；这些处理仍需开始菜单和实际多屏验收。

规则来源为 Rust `theme.rs` / `theme_editor.rs`；展示 ABI 在 `include/myime/theme.h`，Windows `CandidateWindow` 只接收复制后的展示快照。整体验收见 [feature-acceptance.md](feature-acceptance.md)，历史结果见 [testing.md](testing.md)。
