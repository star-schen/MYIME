using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal sealed partial class MainForm
    {
        void GeneralPage()
        {
            var page = Page("基础设置"); var panel = new Panel { Dock = DockStyle.Fill, AutoScroll = true };
            scope.Items.AddRange(new object[] { "全局默认", "Windows 平台覆盖" }); scope.SelectedIndex = 0;
            scope.SelectedIndexChanged += delegate { if (loading) return; if (generalDirty && !ConfirmDiscard("切换配置层")) { loading = true; scope.SelectedIndex = currentScope; loading = false; return; } currentScope = scope.SelectedIndex; LoadGeneral(); };
            var top = Flow(); top.Controls.Add(new Label { Text = "编辑配置层", AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); top.Controls.Add(scope);
            top.Controls.Add(Button("重新读取", delegate { Reload(true); })); top.Controls.Add(Button("保存此层", delegate { SaveLayer(general, currentScope == 0 ? "default" : "windows", ""); }));
            panel.Controls.Add(effective); panel.Controls.Add(general); panel.Controls.Add(Note("配置按全局默认 → Windows → 应用配置继承。保存后请重启目标应用，设置不自动替换正在使用的 DLL。")); panel.Controls.Add(top); page.Controls.Add(panel);
        }
        void ThemePage()
        {
            var page = Page("候选主题"); var top = Flow(); top.Controls.Add(themeList);
            top.Controls.Add(Button("应用为全局默认", delegate { if (themeList.SelectedItem == null || !PermitOtherDrafts("theme")) return; var request = Update("default", "", new Dictionary<string, object> { { "theme", themeList.SelectedItem.ToString() } }); Run(() => tool.Send(request), AcceptSave); }));
            top.Controls.Add(Button("打开实时预览", delegate { LaunchExecutable("myime-theme-preview.exe", "--data-root " + ToolClient.Quote(dataRoot) + " --theme " + ToolClient.Quote(themeList.SelectedItem as string ?? "default")); if (themeDirty) Status("预览读取已保存的主题；编辑器中的未保存内容尚未应用。"); }));
            top.Controls.Add(Button("我的主题目录", delegate { OpenFolder(Path.Combine(dataRoot, "themes"), true); })); top.Controls.Add(Button("刷新主题列表", delegate { RefreshThemes(); }));
            var editor = Flow(); editor.Controls.Add(new Label { Text = "编辑 / 新建 ID", AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); editor.Controls.Add(themeId); editor.Controls.Add(new Label { Text = "新模板名称", AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); editor.Controls.Add(themeName);
            editor.Controls.Add(Button("读取选中主题", delegate { ReadTheme(); })); editor.Controls.Add(Button("新建灰色模板", delegate { NewTheme(); })); editor.Controls.Add(Button("保存我的主题", delegate { SaveTheme(); }));
            page.Controls.Add(themeText); page.Controls.Add(Note("内置：default 雾灰 / light 素白 / dark 墨夜 / ribbon 横排。修改主题 TOML 可调整字体、字号、颜色、间距、圆角和阴影；读取内置主题后保存到用户目录，不覆盖安装文件。新建时 metadata.id 必须与目录 ID 一致。保存后刷新预览，目标应用重启后生效。")); page.Controls.Add(editor); page.Controls.Add(top);
        }
        void ProfilePage()
        {
            var page = Page("应用配置"); var split = new SplitContainer { Dock = DockStyle.Fill, Size = new Size(820, 500), SplitterDistance = 220, FixedPanel = FixedPanel.Panel1 };
            split.Panel1.Controls.Add(profileList); var right = new Panel { Dock = DockStyle.Fill, AutoScroll = true }; var top = Flow();
            top.Controls.Add(new Label { Text = "EXE 文件名", AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); top.Controls.Add(profileExe);
            top.Controls.Add(Button("新建", delegate { if (profileDirty && !ConfirmDiscard("新建应用配置")) return; loading = true; profileList.ClearSelected(); currentExe = ""; profileExe.Text = ""; profile.LoadLayer(new Dictionary<string, object>()); loading = false; profileDirty = false; profileExe.Focus(); }));
            top.Controls.Add(Button("从 EXE 选择", delegate { using (var dialog = new OpenFileDialog { Filter = "Windows 应用 (*.exe)|*.exe", CheckFileExists = true }) if (dialog.ShowDialog(this) == DialogResult.OK) profileExe.Text = Path.GetFileName(dialog.FileName); }));
            top.Controls.Add(Button("保存应用覆盖", delegate { string exe = profileExe.Text.Trim(); if (!ValidExecutable(exe)) { Error("只接受 EXE 文件名，例如 Game.exe，不接受完整路径。"); return; } if (currentExe.Length != 0 && !String.Equals(currentExe, exe, StringComparison.OrdinalIgnoreCase)) { Error("请点击新建后建立另一个 EXE 配置。"); return; } SaveLayer(profile, "profile", exe); }));
            top.Controls.Add(Button("清空已知覆盖", delegate { profile.LoadLayer(new Dictionary<string, object>()); profileDirty = true; Status("已在界面恢复继承；保存后生效，未知扩展配置保留。"); }));
            profileList.SelectedIndexChanged += delegate { if (loading) return; string exe = profileList.SelectedItem as string ?? ""; if (profileDirty && !ConfirmDiscard("选择另一个应用")) { loading = true; profileList.SelectedItem = currentExe; loading = false; return; } LoadProfile(exe); };
            right.Controls.Add(profile); right.Controls.Add(Note("只按 EXE 文件名匹配，大小写不敏感。未设置的项继承上层；不会自动猜测游戏兼容性或专业词库。")); right.Controls.Add(top); split.Panel2.Controls.Add(right); page.Controls.Add(split);
        }
        void AdvancedPage()
        {
            var page = Page("高级配置"); var top = Flow(); top.Controls.Add(Button("重新读取 TOML", delegate { Reload(true); }));
            top.Controls.Add(Button("保存 TOML", delegate { if (!PermitOtherDrafts("raw")) return; var request = Json.Request("config.write", configPath); request["expected"] = Revision(); request["text"] = raw.Text; Run(() => tool.Send(request), AcceptSave); }));
            top.Controls.Add(Button("数据目录", delegate { OpenFolder(dataRoot, false); })); top.Controls.Add(Button("诊断日志", delegate { OpenFolder(Path.Combine(dataRoot, "logs"), false); })); top.Controls.Add(Button("安装文档", delegate { OpenFolder(Path.Combine(applicationDirectory, "docs"), false); }));
            page.Controls.Add(raw); page.Controls.Add(Note("原始产品 TOML：用于界面未覆盖的扩展项。保存前由 Rust 验证；外部修改触发冲突并拒绝覆盖。路径：" + configPath)); page.Controls.Add(top);
        }
        void PatchPage()
        {
            var page = Page("Rime 配置"); var top = Flow(); patchFile.Text = "default.custom.yaml"; top.Controls.Add(patchFile); top.Controls.Add(Button("读取 Patch", delegate { ReadPatch(); }));
            top.Controls.Add(Button("保存 Patch", delegate { SavePatch(); })); top.Controls.Add(Button("部署并发布", delegate { DeployData(null, false); })); top.Controls.Add(Button("启用最近部署方案", delegate { EnableDeployedSchema(); }));
            top.Controls.Add(Button("恢复上次部署", delegate { RollbackData(); })); top.Controls.Add(Button("恢复基础数据", delegate { DeployData(null, true); })); top.Controls.Add(Button("Patch 目录", delegate { OpenFolder(Path.Combine(dataRoot, "rime", "patches"), true); }));
            patchFile.TextChanged += delegate { if (!loading) patchRevision = null; };
            page.Controls.Add(patch); page.Controls.Add(Note("仅编辑自己的 *.custom.yaml。部署成功才发布，失败保留旧数据。恢复 / 回退前会检查配置引用的方案；恢复基础前先在基础设置的 Windows 平台覆盖中将 Schema 设为 pinyin_simp 并保存，再清理应用配置中不在基础数据内的 Schema 覆盖。不会自动改配置，不删除词库槽。")); page.Controls.Add(top);
        }
        void ImportPage()
        {
            var page = Page("词库导入"); var top = Flow(); importFormat.Items.AddRange(new object[] { "txt", "csv" }); importFormat.SelectedIndex = 0; top.Controls.Add(importPath); top.Controls.Add(importFormat);
            top.Controls.Add(Button("选择文件", delegate { using (var dialog = new OpenFileDialog { Filter = "词表 (*.txt;*.csv)|*.txt;*.csv|所有文件|*.*", CheckFileExists = true }) if (dialog.ShowDialog(this) == DialogResult.OK) { importPath.Text = dialog.FileName; importFormat.SelectedItem = Path.GetExtension(dialog.FileName).Equals(".csv", StringComparison.OrdinalIgnoreCase) ? "csv" : "txt"; importRevision = null; importRows.Rows.Clear(); } }));
            top.Controls.Add(Button("解析预览", delegate { PreviewImport(); })); var output = Flow(); output.Controls.Add(new Label { Text = "包 ID", AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); output.Controls.Add(importId); output.Controls.Add(new Label { Text = "名称", AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); output.Controls.Add(importName);
            output.Controls.Add(Button("生成数据包", delegate { WriteImport(); })); output.Controls.Add(Button("词库包目录", delegate { OpenFolder(Path.Combine(dataRoot, "dictionaries"), true); }));
            output.Controls.Add(Button("部署指定词库包", delegate { using (var dialog = new FolderBrowserDialog { Description = "选择含 manifest.json 和 *.dict.yaml 的词库包目录", SelectedPath = Path.Combine(dataRoot, "dictionaries") }) if (dialog.ShowDialog(this) == DialogResult.OK) DeployData(dialog.SelectedPath, false); }));
            output.Controls.Add(Button("启用最近部署方案", delegate { EnableDeployedSchema(); })); importFormat.SelectedIndexChanged += delegate { importRevision = null; }; foreach (string title in new string[] { "词语", "编码", "频率", "来源" }) importRows.Columns.Add(title, title);
            page.Controls.Add(importRows); page.Controls.Add(importSummary); page.Controls.Add(Note("TXT/CSV → 中间模型 → Rime 数据词库包。不直接修改 userdb，不自动启用。缺少编码的词条报告并跳过，不猜测拼音。")); page.Controls.Add(output); page.Controls.Add(top);
        }
    }
}
