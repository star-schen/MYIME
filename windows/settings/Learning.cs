using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal sealed class GroupChoice
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public override string ToString() { return Name; }
    }
    internal sealed partial class MainForm
    {
        readonly CheckBox habitsEnabled = new CheckBox { Text = UiText.Get("启用按应用分组的输入习惯"), AutoSize = true };
        readonly CheckBox aiEnabled = new CheckBox { Text = UiText.Get("启用手动 AI 分组工具"), AutoSize = true };
        readonly ListBox groups = new ListBox { Dock = DockStyle.Fill };
        readonly DataGridView applications = new DataGridView { Dock = DockStyle.Fill, AllowUserToAddRows = false, AllowUserToDeleteRows = false, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill, RowHeadersVisible = false, SelectionMode = DataGridViewSelectionMode.FullRowSelect };
        readonly List<GroupChoice> groupChoices = new List<GroupChoice>();
        bool learningDirty;
        void LearningPage()
        {
            var page = Page(UiText.Get("学习与分组")); page.Name = "learning";
            GridLayout.Apply(applications);
            applications.Columns.Add(new DataGridViewTextBoxColumn { Name = "name", HeaderText = UiText.Get("应用名称"), FillWeight = 25 });
            applications.Columns.Add(new DataGridViewTextBoxColumn { Name = "path", HeaderText = UiText.Get("完整路径"), ReadOnly = true, FillWeight = 50 });
            applications.Columns.Add(new DataGridViewComboBoxColumn { Name = "group", HeaderText = UiText.Get("分组"), DisplayMember = "Name", ValueMember = "Id", FillWeight = 25 });
            var split = new SplitContainer { Dock = DockStyle.Fill, Size = new System.Drawing.Size(700, 450), SplitterDistance = 155 };
            split.Panel1.Controls.Add(groups); split.Panel2.Controls.Add(applications);
            var actions = Flow(); actions.Controls.Add(habitsEnabled); actions.Controls.Add(aiEnabled);
            actions.Controls.Add(Button(UiText.Get("保存学习设置"), delegate { SaveLearning(); }));
            actions.Controls.Add(Button(UiText.Get("新建组"), delegate { AddGroup(); })); actions.Controls.Add(Button(UiText.Get("删除选中组"), delegate { RemoveGroup(); }));
            actions.Controls.Add(Button(UiText.Get("添加应用"), delegate { using (var dialog = new OpenFileDialog { Filter = UiText.Get("应用 (*.exe)|*.exe"), Multiselect = true, CheckFileExists = true }) if (dialog.ShowDialog(this) == DialogResult.OK) foreach (string path in dialog.FileNames) AddApplication(path); }));
            actions.Controls.Add(Button(UiText.Get("读取当前运行的应用"), delegate { ReadRunningApplications(); }));
            actions.Controls.Add(Button(UiText.Get("移除所选应用"), delegate { foreach (DataGridViewRow row in applications.SelectedRows) applications.Rows.Remove(row); learningDirty = true; }));
            var ai = Button(UiText.Get("AI 分组建议…"), delegate { OpenAiGrouping(); }); actions.Controls.Add(ai); ai.Enabled = false;
            aiEnabled.CheckedChanged += delegate { ai.Enabled = aiEnabled.Checked; if (!loading) learningDirty = true; };
            habitsEnabled.CheckedChanged += delegate { if (!loading) learningDirty = true; };
            applications.CellValueChanged += delegate { if (!loading) learningDirty = true; };
            applications.CurrentCellDirtyStateChanged += delegate { if (applications.IsCurrentCellDirty) applications.CommitEdit(DataGridViewDataErrorContexts.Commit); };
            GridLayout.Stack(page, split, actions, Note(UiText.Get("默认即时全局学习，应用和分组标签保留，排序忽略标签。打开输入习惯后才按组读取学习数据。未分组应用为日常；右键切换作用于当前应用本次运行，重开后按已保存的分组；当前组合结束后使用新组。AI 仅在本页手动打开，只发送你在建议窗口确认的应用名称和目录。")));
        }
        void LoadLearning(Dictionary<string, object> value)
        {
            loading = true; var learning = Json.Map(Json.Get(value, "learning")); habitsEnabled.Checked = Json.Flag(learning, "habits_enabled"); aiEnabled.Checked = Json.Flag(learning, "ai_enabled");
            groupChoices.Clear(); var entries = Json.Get(learning, "groups") as IEnumerable;
            if (entries != null) foreach (object item in entries) { var group = Json.Map(item); groupChoices.Add(new GroupChoice { Id = Json.Text(group, "id"), Name = Json.Text(group, "name") }); }
            if (groupChoices.Count == 0) groupChoices.AddRange(new GroupChoice[] { new GroupChoice { Id = "daily", Name = UiText.Get("日常") }, new GroupChoice { Id = "work", Name = UiText.Get("工作") } });
            applications.Rows.Clear(); RefreshGroups(); entries = Json.Get(learning, "applications") as IEnumerable;
            if (entries != null) foreach (object item in entries) { var app = Json.Map(item); applications.Rows.Add(Json.Text(app, "name"), Json.Text(app, "path"), Json.Text(app, "group", "daily")); }
            learningDirty = false; loading = false;
        }
        void RefreshGroups() { groups.Items.Clear(); foreach (var group in groupChoices) groups.Items.Add(group); ((DataGridViewComboBoxColumn)applications.Columns["group"]).DataSource = groupChoices.ToList(); }
        void AddGroup()
        {
            if (groupChoices.Count >= 32) { Error(UiText.Get("最多 32 个组。")); return; }
            using (var dialog = new Form { Text = UiText.Get("新建学习分组"), ClientSize = new System.Drawing.Size(360, 160), StartPosition = FormStartPosition.CenterParent, FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false }) {
                var id = new TextBox { Left = 100, Top = 15, Width = 235 }; var name = new TextBox { Left = 100, Top = 55, Width = 235 };
                dialog.Controls.Add(new Label { Left = 15, Top = 18, Text = UiText.Get("组标识"), AutoSize = true }); dialog.Controls.Add(id); dialog.Controls.Add(new Label { Left = 15, Top = 58, Text = UiText.Get("显示名称"), AutoSize = true }); dialog.Controls.Add(name);
                var ok = new Button { Left = 175, Top = 110, Text = UiText.Get("创建"), DialogResult = DialogResult.OK }; dialog.Controls.Add(ok); dialog.AcceptButton = ok;
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
                string groupId = id.Text.Trim(), title = name.Text.Trim(); if (!ValidId(groupId) || groupId == "global" || groupChoices.Any(g => g.Id == groupId) || title.Length == 0 || title.Length > 80) { Error(UiText.Get("组标识需唯一，使用小写字母、数字、下划线或短横线；请填写名称。global 保留给全局视图。")); return; }
                groupChoices.Add(new GroupChoice { Id = groupId, Name = title }); RefreshGroups(); learningDirty = true;
            }
        }
        void RemoveGroup()
        {
            var group = groups.SelectedItem as GroupChoice; if (group == null) return;
            if (group.Id == "daily" || group.Id == "work") { Status(UiText.Get("日常和工作是默认组，可增加自定义组。"), true); return; }
            foreach (DataGridViewRow row in applications.Rows) if (Convert.ToString(row.Cells["group"].Value) == group.Id) row.Cells["group"].Value = "daily";
            groupChoices.Remove(group); RefreshGroups(); learningDirty = true;
        }
        void AddApplication(string path)
        {
            path = Path.GetFullPath(path); foreach (DataGridViewRow row in applications.Rows) if (String.Equals(Convert.ToString(row.Cells["path"].Value), path, StringComparison.OrdinalIgnoreCase)) return;
            if (applications.Rows.Count >= 512) { Status(UiText.Get("应用映射最多 512 项。"), true); return; }
            applications.Rows.Add(Path.GetFileNameWithoutExtension(path), path, "daily"); learningDirty = true;
        }
        void ReadRunningApplications()
        {
            Run(()=>{
                var paths=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach(var process in Process.GetProcesses())using(process){try{string path=process.MainModule.FileName;if(path.EndsWith(".exe",StringComparison.OrdinalIgnoreCase) && paths.Count<512)paths.Add(path);}catch(System.ComponentModel.Win32Exception){}catch(InvalidOperationException){}catch(System.Security.SecurityException){}}
                return new Dictionary<string,object>{{"ok",true},{"paths",paths.ToArray()}};
            },result=>{foreach(string path in (string[])Json.Get(result,"paths"))AddApplication(path);Status(UiText.Get("已读取可访问的应用路径，尚未保存分组。"));});
        }
        List<Dictionary<string, object>> ApplicationRows() { applications.EndEdit(); return applications.Rows.Cast<DataGridViewRow>().Select(row => new Dictionary<string, object> { { "name", Convert.ToString(row.Cells["name"].Value) }, { "path", Convert.ToString(row.Cells["path"].Value) }, { "group", Convert.ToString(row.Cells["group"].Value) } }).ToList(); }
        void SaveLearning()
        {
            if (!PermitOtherDrafts("learning")) return; var request = Json.Request("learning.update", configPath); request["expected"] = Revision();
            var wireGroups=groupChoices.Select(group=>new Dictionary<string,object>{{"id",group.Id},{"name",group.Name}}).ToList();
            request["learning"] = new Dictionary<string, object> { { "habits_enabled", habitsEnabled.Checked }, { "ai_enabled", aiEnabled.Checked }, { "groups", wireGroups }, { "applications", ApplicationRows() } }; Run(() => Persist(request), AcceptSave);
        }
        void OpenAiGrouping()
        {
            if (!aiEnabled.Checked) return;
            using (var dialog = new AiGroupingForm(dataRoot, groupChoices, ApplicationRows())) { if (dialog.ShowDialog(this) != DialogResult.OK) return; foreach (var proposal in dialog.Accepted) if (proposal.Index >= 0 && proposal.Index < applications.Rows.Count) applications.Rows[proposal.Index].Cells["group"].Value = proposal.Group; learningDirty = true; Status(UiText.Get("已采用确认的建议；点击保存学习设置后生效。")); }
        }
    }
}
