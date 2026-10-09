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
            var page = Page(UiText.Get("基础设置")); var panel = new Panel { Dock = DockStyle.Fill, AutoScroll = true };
            scope.Items.AddRange(new object[] { UiText.Get("全局默认"), UiText.Get("Windows 设置") }); scope.SelectedIndex = 0;
            scope.SelectedIndexChanged += delegate { if (loading || UiText.Applying) return; if (generalDirty && !ConfirmDiscard(UiText.Get("切换配置层"))) { loading = true; scope.SelectedIndex = currentScope; loading = false; return; } currentScope = scope.SelectedIndex; LoadGeneral(); };
            var top = Flow(); top.Controls.Add(new Label { Text = UiText.Get("编辑配置层"), AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); top.Controls.Add(scope);
            top.Controls.Add(Button(UiText.Get("重新读取"), delegate { Reload(true); })); top.Controls.Add(Button(UiText.Get("保存此层"), delegate { SaveLayer(general, currentScope == 0 ? "default" : "windows", ""); }));
            panel.Controls.Add(effective); panel.Controls.Add(general); panel.Controls.Add(Note(UiText.Get("Windows 设置是 MYIME 在 Windows 上供所有应用使用的通用设置，可在此页选择；其中未单独设置的项使用全局默认。应用配置中的“同 Windows 设置”使用这一层。跨应用保留只恢复原输入位置；位置失效时丢弃草稿。"))); panel.Controls.Add(top); panel.Controls.Add(UiText.Selector(delegate { if (!selfCheck) PresentationPublication.Publish(dataRoot, applicationDirectory); })); page.Name = "general"; page.Controls.Add(panel);
        }
        void ThemePage()
        {
            var page = Page(UiText.Get("候选主题")); var top = Flow(); top.Controls.Add(themeList);
            themeList.SelectedIndexChanged += delegate { var selected = themeList.SelectedItem as ThemeChoice; if (selected != null && !selfCheck) themePreview.ShowTheme(selected.Id); };
            top.Controls.Add(Button(UiText.Get("应用为全局默认"), delegate { var selected = themeList.SelectedItem as ThemeChoice; if (selected == null || !PermitOtherDrafts("theme")) return; var request = Json.Request("config.apply_theme", configPath); request["expected"] = Revision(); request["theme"] = selected.Id; Run(() => Persist(request), AcceptSave); }));
            top.Controls.Add(Button(UiText.Get("皮肤编辑器"), delegate { var selected = themeList.SelectedItem as ThemeChoice; var editor = new ThemeEditorForm(applicationDirectory, dataRoot, selected == null ? "default" : selected.Id); editor.ThemeSaved += delegate { RefreshThemes(); }; editor.Show(this); }));
            top.Controls.Add(Button(UiText.Get("我的主题目录"), delegate { OpenFolder(Path.Combine(dataRoot, "themes"), true); })); top.Controls.Add(Button(UiText.Get("刷新主题列表"), delegate { RefreshThemes(); }));
            page.Controls.Add(themePreview); page.Controls.Add(top);
        }
        void ProfilePage()
        {
            var page = Page(UiText.Get("应用配置")); var split = new SplitContainer { Dock = DockStyle.Fill, Size = new Size(820, 500), SplitterDistance = 220, FixedPanel = FixedPanel.Panel1 };
            profile.SetContext("profile");
            split.Panel1.Controls.Add(profileList); var right = new Panel { Dock = DockStyle.Fill, AutoScroll = true }; var top = Flow();
            top.Controls.Add(new Label { Text = UiText.Get("EXE 文件名"), AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); top.Controls.Add(profileExe);
            top.Controls.Add(Button(UiText.Get("新建"), delegate { if (profileDirty && !ConfirmDiscard(UiText.Get("新建应用配置"))) return; loading = true; profileList.ClearSelected(); currentExe = ""; profileExe.Text = ""; profile.LoadLayer(new Dictionary<string, object>()); loading = false; profileDirty = false; profileExe.Focus(); }));
            top.Controls.Add(Button(UiText.Get("从 EXE 选择"), delegate { using (var dialog = new OpenFileDialog { Filter = UiText.Get("Windows 应用 (*.exe)|*.exe"), CheckFileExists = true }) if (dialog.ShowDialog(this) == DialogResult.OK) profileExe.Text = Path.GetFileName(dialog.FileName); }));
            top.Controls.Add(Button(UiText.Get("保存应用覆盖"), delegate { string exe = profileExe.Text.Trim(); if (!ValidExecutable(exe)) { Error(UiText.Get("只接受 EXE 文件名，例如 Game.exe，不接受完整路径。")); return; } if (currentExe.Length != 0 && !String.Equals(currentExe, exe, StringComparison.OrdinalIgnoreCase)) { Error(UiText.Get("请点击新建后建立另一个 EXE 配置。")); return; } SaveLayer(profile, "profile", exe); }));
            top.Controls.Add(Button(UiText.Get("清空已知覆盖"), delegate { profile.LoadLayer(new Dictionary<string, object>()); profileDirty = true; Status(UiText.Get("已在界面恢复继承；保存后生效，未知扩展配置保留。")); }));
            profileList.SelectedIndexChanged += delegate { if (loading) return; string exe = profileList.SelectedItem as string ?? ""; if (profileDirty && !ConfirmDiscard(UiText.Get("选择另一个应用"))) { loading = true; profileList.SelectedItem = currentExe; loading = false; return; } LoadProfile(exe); };
            var windowsSettings = Button(UiText.Get("打开 Windows 设置"), delegate { tabs.SelectedIndex = 0; scope.SelectedIndex = 1; });
            top.Controls.Add(windowsSettings);
            right.Controls.Add(profile); right.Controls.Add(Note(UiText.Get("只按 EXE 文件名匹配，大小写不敏感。“同 Windows 设置”指 MYIME 基础设置中的 Windows 通用配置；其中未单独设置的项使用全局默认。"))); right.Controls.Add(top); split.Panel2.Controls.Add(right); page.Controls.Add(split);
        }
        void AdvancedPage()
        {
            var page = Page(UiText.Get("高级配置")); var top = Flow(); top.Controls.Add(Button(UiText.Get("重新读取 TOML"), delegate { Reload(true); }));
            top.Controls.Add(Button(UiText.Get("保存 TOML"), delegate { if (!PermitOtherDrafts("raw")) return; var request = Json.Request("config.write", configPath); request["expected"] = Revision(); request["text"] = raw.Text; Run(() => Persist(request), AcceptSave); }));
            top.Controls.Add(Button(UiText.Get("数据目录"), delegate { OpenFolder(dataRoot, false); })); top.Controls.Add(Button(UiText.Get("诊断日志"), delegate { OpenFolder(Path.Combine(dataRoot, "logs"), false); })); top.Controls.Add(Button(UiText.Get("安装文档"), delegate { OpenFolder(Path.Combine(applicationDirectory, "docs"), false); }));
            page.Controls.Add(raw); page.Controls.Add(Note(UiText.Get("原始产品 TOML：用于界面未覆盖的扩展项。保存前由 Rust 验证；外部修改触发冲突并拒绝覆盖。路径：") + configPath)); page.Controls.Add(top);
        }
        void PatchPage()
        {
            var page = Page(UiText.Get("Rime 配置")); var top = Flow(); patchFile.Text = "default.custom.yaml"; top.Controls.Add(patchFile); top.Controls.Add(Button(UiText.Get("读取 Patch"), delegate { ReadPatch(); }));
            top.Controls.Add(Button(UiText.Get("保存 Patch"), delegate { SavePatch(); })); top.Controls.Add(Button(UiText.Get("部署并发布"), delegate { DeployData(null, false); })); top.Controls.Add(Button(UiText.Get("启用最近部署方案"), delegate { EnableDeployedSchema(); }));
            top.Controls.Add(Button(UiText.Get("恢复上次部署"), delegate { RollbackData(); })); top.Controls.Add(Button(UiText.Get("恢复基础数据"), delegate { DeployData(null, true); })); top.Controls.Add(Button(UiText.Get("Patch 目录"), delegate { OpenFolder(Path.Combine(dataRoot, "rime", "patches"), true); }));
            patchFile.TextChanged += delegate { if (!loading) patchRevision = null; };
            page.Controls.Add(patch); page.Controls.Add(Note(UiText.Get("仅编辑自己的 *.custom.yaml。部署成功才发布，失败保留旧数据。恢复 / 回退前会检查配置引用的方案；恢复基础前先在基础设置的 Windows 设置中将 Schema 设为 pinyin_simp 并保存，再清理应用配置中不在基础数据内的 Schema 覆盖。不会自动改配置，不删除词库槽。"))); page.Controls.Add(top);
        }
        void ImportPage()
        {
            var page = Page(UiText.Get("词库导入")); var top = Flow(); importFormat.Items.AddRange(new object[] { "txt", "csv" }); importFormat.SelectedIndex = 0; top.Controls.Add(importPath); top.Controls.Add(importFormat);
            top.Controls.Add(Button(UiText.Get("选择文件"), delegate { using (var dialog = new OpenFileDialog { Filter = UiText.Get("词表 (*.txt;*.csv)|*.txt;*.csv|所有文件|*.*"), CheckFileExists = true }) if (dialog.ShowDialog(this) == DialogResult.OK) { importPath.Text = dialog.FileName; importFormat.SelectedItem = Path.GetExtension(dialog.FileName).Equals(".csv", StringComparison.OrdinalIgnoreCase) ? "csv" : "txt"; importRevision = null; importRows.Rows.Clear(); } }));
            top.Controls.Add(Button(UiText.Get("解析预览"), delegate { PreviewImport(); })); var output = Flow(); output.Controls.Add(new Label { Text = UiText.Get("包 ID"), AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); output.Controls.Add(importId); output.Controls.Add(new Label { Text = UiText.Get("名称"), AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); output.Controls.Add(importName);
            output.Controls.Add(Button(UiText.Get("生成数据包"), delegate { WriteImport(); })); output.Controls.Add(Button(UiText.Get("词库包目录"), delegate { OpenFolder(Path.Combine(dataRoot, "dictionaries"), true); }));
            output.Controls.Add(Button(UiText.Get("部署指定词库包"), delegate { using (var dialog = new FolderBrowserDialog { Description = UiText.Get("选择含 manifest.json 和 *.dict.yaml 的词库包目录"), SelectedPath = Path.Combine(dataRoot, "dictionaries") }) if (dialog.ShowDialog(this) == DialogResult.OK) DeployData(dialog.SelectedPath, false); }));
            output.Controls.Add(Button(UiText.Get("启用最近部署方案"), delegate { EnableDeployedSchema(); })); importFormat.SelectedIndexChanged += delegate { importRevision = null; }; foreach (string title in new string[] { UiText.Get("词语"), UiText.Get("编码"), UiText.Get("频率"), UiText.Get("来源") }) importRows.Columns.Add(title, title);
            page.Controls.Add(importRows); page.Controls.Add(importSummary); page.Controls.Add(Note(UiText.Get("TXT/CSV → 中间模型 → Rime 数据词库包。不直接修改 userdb，不自动启用。缺少编码的词条报告并跳过，不猜测拼音。"))); page.Controls.Add(output); page.Controls.Add(top);
        }
    }
}
