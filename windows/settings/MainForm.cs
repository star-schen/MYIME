using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal sealed partial class MainForm : Form
    {
        readonly string applicationDirectory, dataRoot, configPath;
        readonly ToolClient tool;
        readonly bool selfCheck;
        readonly ModuleHost tabs = new ModuleHost();
        readonly Label status = new Label { AutoSize = true, Dock = DockStyle.Fill, Padding = new Padding(12, 8, 12, 8) };
        readonly ComboBox scope = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 220 };
        readonly LayerEditor general = new LayerEditor(), profile = new LayerEditor();
        readonly Label effective = new Label { AutoSize = true, Padding = new Padding(12), Dock = DockStyle.Top };
        readonly ListBox profileList = new ListBox { Dock = DockStyle.Fill, IntegralHeight = false, HorizontalScrollbar = true };
        readonly TextBox profileExe = new TextBox { Width = 250 };
        readonly ComboBox themeList = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 260 };
        readonly ThemeImage themePreview;
        readonly TextBox raw = TextArea(), patch = TextArea();
        readonly ComboBox patchFile = new ComboBox { DropDownStyle = ComboBoxStyle.DropDown, Width = 260 };
        readonly TextBox importPath = new TextBox { Width = 430, ReadOnly = true }, importId = new TextBox { Width = 180, Text = "my_words" }, importName = new TextBox { Width = 220, Text = UiText.Get("我的词库") };
        readonly ComboBox importFormat = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 90 };
        readonly DataGridView importRows = new DataGridView { Dock = DockStyle.Fill, ReadOnly = true, AllowUserToAddRows = false, AllowUserToDeleteRows = false, RowHeadersVisible = false, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill, ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize };
        readonly Label importSummary = new Label { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(8) };
        Dictionary<string, object> snapshot = new Dictionary<string, object>(), patchRevision, importRevision;
        bool loading, busy, generalDirty, profileDirty, rawDirty, patchDirty;
        string currentExe = "";
        int currentScope;
        internal MainForm(string directory, string root, bool testing)
        {
            applicationDirectory = Path.GetFullPath(directory); dataRoot = Path.GetFullPath(root); configPath = Path.Combine(dataRoot, "config.toml"); tool = new ToolClient(applicationDirectory); selfCheck = testing;
            themePreview = new ThemeImage(applicationDirectory, dataRoot);
            Text = UiText.Get("MYIME 设置"); Icon = Icon.ExtractAssociatedIcon(System.Reflection.Assembly.GetExecutingAssembly().Location); Font = new Font("Microsoft YaHei UI", 9F); AutoScaleMode = AutoScaleMode.Dpi; ClientSize = new Size(870, 670); MinimumSize = new Size(760, 580); StartPosition = FormStartPosition.CenterScreen;
            var frame = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 3, ColumnCount = 1 };
            frame.RowStyles.Add(new RowStyle(SizeType.AutoSize)); frame.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); frame.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            var heading = new Label { Dock = DockStyle.Fill, AutoSize = true, Padding = new Padding(15, 12, 15, 12), Font = new Font(Font.FontFamily, 13F, FontStyle.Bold), Text = UiText.Get("MYIME    ·    设置与数据管理") };
            frame.Controls.Add(heading, 0, 0); frame.Controls.Add(tabs, 0, 1); frame.Controls.Add(status, 0, 2); Controls.Add(frame);
            GeneralPage(); ThemePage(); ProfilePage(); LearningPage(); LearningMaintenancePage(); AdvancedPage(); PatchPage(); ImportPage();
            general.Edited += delegate { if (!loading && !UiText.Applying) generalDirty = true; }; profile.Edited += delegate { if (!loading && !UiText.Applying) profileDirty = true; };
            profileExe.TextChanged += delegate { if (!loading) profileDirty = true; }; raw.TextChanged += delegate { if (!loading) rawDirty = true; }; patch.TextChanged += delegate { if (!loading) patchDirty = true; };
            FormClosing += delegate(object sender, FormClosingEventArgs args) { if (busy) { args.Cancel = true; Status(UiText.Get("正在处理，请等待完成。"), true); } else if (!selfCheck && Dirty() && !ConfirmDiscard(UiText.Get("关闭设置"))) args.Cancel = true; };
            Shown += delegate { if (!selfCheck) Reload(false); }; RefreshPatchFiles(); Status(UiText.Get("设置读取中。输入热路径不依赖本程序。"));
            GridLayout.Apply(importRows); UiText.Apply(this);
            EventHandler languageChanged = delegate { if (snapshot.Count != 0) RefreshEffectiveSummary(); };
            UiText.Changed += languageChanged; Disposed += delegate { UiText.Changed -= languageChanged; };
        }
        protected override bool ShowWithoutActivation { get { return selfCheck; } }
        protected override CreateParams CreateParams { get { var value = base.CreateParams; if (selfCheck) value.ExStyle |= 0x08000000 | 0x00000080; return value; } }
        static TextBox TextArea() { return new TextBox { Dock = DockStyle.Fill, Multiline = true, AcceptsReturn = true, AcceptsTab = true, ScrollBars = ScrollBars.Both, WordWrap = false, Font = new Font("Consolas", 10F), MaxLength = 1024 * 1024 }; }
        static Button Button(string title, EventHandler action) { var button = new Button { Text = title, AutoSize = true, Padding = new Padding(5, 2, 5, 2), Margin = new Padding(5) }; button.Click += action; return button; }
        static FlowLayoutPanel Flow() { return new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(7), WrapContents = true }; }
        Panel Page(string title) { return tabs.AddPage(title); }
        internal void OpenModule(string name) { for (int index = 0; index < tabs.TabPages.Count; index++) if (tabs.TabPages[index].Name == name) { tabs.SelectedIndex = index; break; } }
        static Label Note(string text)
        {
            var label = new Label { Text = text, Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(12), ForeColor = Color.DimGray };
            label.ParentChanged += delegate { if (label.Parent != null) { label.MaximumSize = new Size(Math.Max(100, label.Parent.ClientSize.Width), 0); label.Parent.SizeChanged += delegate { label.MaximumSize = new Size(Math.Max(100, label.Parent.ClientSize.Width), 0); }; } }; return label;
        }
        bool Dirty() { return generalDirty || profileDirty || rawDirty || patchDirty || learningDirty; }
        bool ConfirmDiscard(string action) { return selfCheck || MessageBox.Show(this, UiText.Format("{0}会放弃未保存的编辑，是否继续？", action), UiText.Get("未保存的更改"), MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) == DialogResult.Yes; }
        bool PermitOtherDrafts(string owner) { return !((owner != "general" && generalDirty) || (owner != "profile" && profileDirty) || (owner != "raw" && rawDirty) || (owner != "learning" && learningDirty)) || ConfirmDiscard(UiText.Get("保存并重新读取配置（其他模块也有未保存更改）")); }
        void Status(string text, bool warning = false) { status.Text = text; status.ForeColor = warning ? Color.DarkRed : Color.DimGray; }
        void Error(string text) { Status(text, true); if (!selfCheck) MessageBox.Show(this, text, UiText.Get("MYIME 设置"), MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        void Run(Func<Dictionary<string, object>> operation, Action<Dictionary<string, object>> completed)
        {
            if (busy) return; busy = true; tabs.Enabled = false; UseWaitCursor = true; Status(UiText.Get("正在处理，请稍候…"));
            Task.Factory.StartNew(operation).ContinueWith(task => { if (IsDisposed) return; BeginInvoke((Action)delegate {
                busy = false; tabs.Enabled = true; UseWaitCursor = false;
                if (task.IsFaulted) { Error(task.Exception.GetBaseException().Message); return; }
                var result = task.Result;
                if (!Json.Flag(result, "ok")) { string message = Json.Text(result, "error", UiText.Get("操作未完成。")); if (Json.Text(result, "kind") == "conflict") message = UiText.Get("文件已被其他程序或手动修改，本次保存未覆盖。当前编辑仍保留；请先复制需要保留的内容，再重新读取。\r\n") + message; Error(message); return; }
                try { completed(result); } catch (Exception error) { Error(UiText.Get("工具操作已结束，但刷新界面失败，请重新读取：") + error.Message); }
            }); });
        }
    }
}
