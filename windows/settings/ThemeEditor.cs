using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal sealed class ThemeEditorForm : Form
    {
        readonly ToolClient tool;
        readonly string directory, root;
        readonly ComboBox themes = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 210 };
        readonly TextBox source = new TextBox { Dock = DockStyle.Fill, Multiline = true, AcceptsTab = true, AcceptsReturn = true, ScrollBars = ScrollBars.Both, WordWrap = false, Font = new Font("Consolas", 10F), MaxLength = 65536 };
        readonly PropertyGrid properties = new PropertyGrid { Dock = DockStyle.Fill, ToolbarVisible = false, HelpVisible = true };
        readonly ThemeImage preview;
        readonly Label status = new Label { Dock = DockStyle.Bottom, AutoSize = true, Padding = new Padding(10) };
        readonly Timer debounce = new Timer { Interval = 350 };
        Dictionary<string, object> revision;
        string id, target;
        bool loading, busy, dirty, rawMode;
        internal event EventHandler ThemeSaved;
        internal ThemeEditorForm(string applicationDirectory, string dataRoot, string selected)
        {
            directory = Path.GetFullPath(applicationDirectory); root = Path.GetFullPath(dataRoot); tool = new ToolClient(directory); id = selected;
            Text = UiText.Get("MYIME 皮肤编辑器"); Font = new Font("Microsoft YaHei UI", 9F); AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(1000, 700); MinimumSize = new Size(760, 560); StartPosition = FormStartPosition.CenterParent;
            preview = new ThemeImage(directory, root);
            var split = new SplitContainer { Dock = DockStyle.Fill, SplitterDistance = 430, Size = new Size(1000, 620) };
            split.Panel1.Controls.Add(properties); split.Panel1.Controls.Add(source); source.Visible = false; split.Panel2.Controls.Add(preview);
            var actions = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(7), WrapContents = true };
            themes.SelectedIndexChanged += delegate { if(!loading && !busy)Read(); };
            actions.Controls.Add(themes);
            actions.Controls.Add(Action(UiText.Get("读取"), delegate { Read(); })); actions.Controls.Add(Action(UiText.Get("新建"), delegate { New(); }));
            actions.Controls.Add(Action(UiText.Get("另存为"), delegate { Copy(); })); actions.Controls.Add(Action(UiText.Get("保存"), delegate { Save(); }));
            actions.Controls.Add(Action(UiText.Get("TOML 原文"), delegate { rawMode = !rawMode; source.Visible = rawMode; properties.Visible = !rawMode; if (!rawMode) Describe(); }));
            var frame = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4 };
            frame.RowStyles.Add(new RowStyle(SizeType.AutoSize)); frame.RowStyles.Add(new RowStyle(SizeType.AutoSize)); frame.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); frame.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            actions.Dock = DockStyle.Fill; status.Dock = DockStyle.Fill;
            frame.Controls.Add(UiText.Selector(delegate { PresentationPublication.Publish(root, directory); }), 0, 0);
            frame.Controls.Add(actions, 0, 1); frame.Controls.Add(split, 0, 2); frame.Controls.Add(status, 0, 3); Controls.Add(frame);
            EventHandler languageChanged = delegate { if (properties.SelectedObject != null) TypeDescriptor.Refresh(properties.SelectedObject); properties.Refresh(); };
            UiText.Changed += languageChanged; Disposed += delegate { UiText.Changed -= languageChanged; };
            source.TextChanged += delegate { if (!loading) { dirty = true; debounce.Stop(); debounce.Start(); } };
            debounce.Tick += delegate { debounce.Stop(); preview.ShowDraft(source.Text); };
            properties.PropertyValueChanged += delegate(object sender, PropertyValueChangedEventArgs e) {
                var field = e.ChangedItem.PropertyDescriptor as ThemeProperty;
                if (field != null) UpdateDraft(new Dictionary<string, object> { { field.Path, field.WireValue } });
            };
            FormClosing += delegate(object sender, FormClosingEventArgs e) { if (busy) e.Cancel = true; else if (dirty) e.Cancel = MessageBox.Show(this, UiText.Get("放弃未保存的皮肤编辑？"), UiText.Get("皮肤编辑器"), MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.Yes; };
            Disposed += delegate { debounce.Dispose(); };
            Shown += delegate { Catalog(selected); };
            UiText.Apply(this);
        }
        static Button Action(string name, EventHandler handler) { var button = new Button { Text = name, AutoSize = true, Margin = new Padding(5) }; button.Click += handler; return button; }
        bool Discard() { return !dirty || MessageBox.Show(this, UiText.Get("读取会放弃未保存的皮肤编辑，继续？"), UiText.Get("皮肤编辑器"), MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) == DialogResult.Yes; }
        void Run(Func<Dictionary<string, object>> operation, Action<Dictionary<string, object>> completed)
        {
            if (busy) return; busy = true; source.ReadOnly = true; properties.Enabled = false;
            Task.Factory.StartNew(operation).ContinueWith(task => {
                if (IsDisposed || !IsHandleCreated) return;
                try { BeginInvoke((System.Action)delegate {
                    if(IsDisposed || Disposing)return;
                    busy = false; source.ReadOnly = false; properties.Enabled = true;
                    if (task.IsFaulted) { status.Text = task.Exception.GetBaseException().Message; return; }
                    if (!Json.Flag(task.Result, "ok")) { status.Text = Json.Text(task.Result, "error", UiText.Get("操作失败，编辑内容已保留。")); return; }
                    try { completed(task.Result); } catch (Exception error) { status.Text = error.Message; }
                }); } catch (InvalidOperationException) { }
            });
        }
        void Catalog(string selected)
        {
            Run(() => tool.Send(new Dictionary<string, object> { { "command", "theme.catalog" }, { "user_root", Path.Combine(root, "themes") }, { "installed_root", Path.Combine(directory, "themes") } }), result => {
                loading=true;
                themes.Items.Clear(); var entries = Json.Get(result, "themes") as IEnumerable;
                if (entries != null) foreach (object entry in entries) { var value = Json.Map(entry); var choice = new ThemeChoice(Json.Text(value, "id"), Json.Text(value, "name")); themes.Items.Add(choice); if (choice.Id == selected) themes.SelectedItem = choice; }
                if (themes.SelectedIndex < 0 && themes.Items.Count > 0) themes.SelectedIndex = 0;loading=false; Read(false);
            });
        }
        void Read() { Read(true); }
        void Read(bool prompt)
        {
            var choice = themes.SelectedItem as ThemeChoice; if (choice == null)return;
            if(prompt && !Discard()){loading=true;for(int index=0;index<themes.Items.Count;index++)if(((ThemeChoice)themes.Items[index]).Id==id)themes.SelectedIndex=index;loading=false;return;}
            string selected = choice.Id, path = Path.Combine(root, "themes", selected, "theme.toml");
            Run(() => {
                var own = tool.Send(Json.Request("theme.read", path)); if (!Json.Flag(own, "ok") || Json.Flag(Json.Map(Json.Get(own, "revision")), "exists")) return own;
                var installed = tool.Send(Json.Request("theme.read", Path.Combine(directory, "themes", selected, "theme.toml")));
                if (Json.Flag(installed, "ok")) installed["revision"] = Json.Get(own, "revision"); return installed;
            }, result => { id = selected; target = path; revision = Json.Map(Json.Get(result, "revision")); SetSource(Json.Text(result, "text"), false); Describe(); status.Text = UiText.Format("正在编辑“{0}”；保存到用户主题目录。", choice.Name); });
        }
        void SetSource(string text, bool changed) { loading = true; source.Text = text.Replace("\r\n", "\n").Replace("\n", "\r\n"); loading = false; dirty = changed; preview.ShowDraft(source.Text); }
        void Describe()
        {
            var request = new Dictionary<string, object> { { "command", "theme.describe" }, { "text", source.Text } };
            Run(() => tool.Send(request), result => properties.SelectedObject = new ThemeProperties(result));
        }
        void UpdateDraft(Dictionary<string, object> changes)
        {
            string before = source.Text;
            Run(() => tool.Send(new Dictionary<string, object> { { "command", "theme.edit" }, { "text", before }, { "changes", changes } }), result => { SetSource(Json.Text(result, "text"), true); Describe(); });
        }
        string[] Identity(string title)
        {
            using (var dialog = new Form { Text = title, ClientSize = new Size(380, 160), StartPosition = FormStartPosition.CenterParent, FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false }) {
                var identifier = new TextBox { Left = 115, Top = 18, Width = 240, Text = "my_theme" };
                var name = new TextBox { Left = 115, Top = 58, Width = 240, Text = UiText.Get("我的皮肤") };
                dialog.Controls.Add(new Label { Left = 15, Top = 20, AutoSize = true, Text = UiText.Get("文件标识") }); dialog.Controls.Add(identifier);
                dialog.Controls.Add(new Label { Left = 15, Top = 60, AutoSize = true, Text = UiText.Get("显示名称") }); dialog.Controls.Add(name);
                var ok = new Button { Text = UiText.Get("确定"), Left = 195, Top = 110, DialogResult = DialogResult.OK }; var cancel = new Button { Text = UiText.Get("取消"), Left = 280, Top = 110, DialogResult = DialogResult.Cancel };
                dialog.Controls.Add(ok); dialog.Controls.Add(cancel); dialog.AcceptButton = ok; dialog.CancelButton = cancel;
                return dialog.ShowDialog(this) == DialogResult.OK ? new string[] { identifier.Text.Trim(), name.Text.Trim() } : null;
            }
        }
        void New()
        {
            if (!Discard()) return; var identity = Identity(UiText.Get("新建皮肤")); if (identity == null) return; PrepareIdentity(identity, false);
        }
        void Copy() { var identity = Identity(UiText.Get("另存皮肤")); if (identity != null) PrepareIdentity(identity, true); }
        void PrepareIdentity(string[] identity, bool copy)
        {
            string nextId = identity[0], nextName = identity[1], path = Path.Combine(root, "themes", nextId, "theme.toml"), original = source.Text;
            if (!MainForm.ValidThemeId(nextId)) { status.Text = UiText.Get("标识只允许小写字母、数字、下划线和短横线，最多 64 位。"); return; }
            Run(() => {
                var own = tool.Send(Json.Request("theme.read", path)); if (!Json.Flag(own, "ok")) return own;
                if (Json.Flag(Json.Map(Json.Get(own, "revision")), "exists")) return new Dictionary<string, object> { { "ok", false }, { "error", UiText.Get("主题已存在，读取后编辑或换一个标识。") } };
                var result = tool.Send(copy ? new Dictionary<string, object> { { "command", "theme.edit" }, { "text", original }, { "changes", new Dictionary<string, object> { { "id", nextId }, { "name", nextName } } } } : new Dictionary<string, object> { { "command", "theme.template" }, { "id", nextId }, { "name", nextName } });
                result["revision"] = Json.Get(own, "revision"); return result;
            }, result => { id = nextId; target = path; revision = Json.Map(Json.Get(result, "revision")); loading=true;for(int index=themes.Items.Count-1;index>=0;index--)if(((ThemeChoice)themes.Items[index]).Id==nextId)themes.Items.RemoveAt(index);var choice=new ThemeChoice(nextId,nextName);themes.Items.Add(choice);themes.SelectedItem=choice;loading=false;SetSource(Json.Text(result, "text"), true); Describe(); status.Text = UiText.Get("新皮肤尚未保存。"); });
        }
        void Save()
        {
            if (revision == null || target == null) { status.Text = UiText.Get("先读取或新建一个皮肤。"); return; }
            var request = Json.Request("theme.write", target); request["text"] = source.Text; request["expected"] = revision;
            Run(() => {var result=tool.Send(request);if(Json.Flag(result,"ok")){try{PresentationPublication.Publish(root,directory);}catch(Exception error){result["publication_error"]=error.Message;}}return result;}, result => { revision = Json.Map(Json.Get(result, "revision")); dirty = false;string failure=Json.Text(result,"publication_error");status.Text = failure.Length==0?UiText.Get("皮肤已保存，运行中的候选窗口将刷新。"):UiText.Get("皮肤已保存，但尚未即时发布：")+failure; if (ThemeSaved != null) ThemeSaved(this, EventArgs.Empty);if(failure.Length==0)Catalog(id); });
        }
    }
    internal sealed class ThemeProperties : ICustomTypeDescriptor
    {
        readonly PropertyDescriptorCollection fields;
        internal ThemeProperties(Dictionary<string, object> description)
        {
            var values = new List<PropertyDescriptor>();
            foreach (string key in new string[] { "name", "version", "description" }) values.Add(new ThemeProperty("", "theme.category.info", key, "string", Json.Get(description, key), null));
            var entries = Json.Get(description, "fields") as IEnumerable;
            if (entries != null) foreach (object entry in entries) { var value = Json.Map(entry); values.Add(new ThemeProperty(Json.Text(value, "section"), Json.Text(value, "category"), Json.Text(value, "key"), Json.Text(value, "kind"), Json.Get(value, "value"), Json.Get(value, "choices") as IEnumerable)); }
            fields = new PropertyDescriptorCollection(values.ToArray());
        }
        public AttributeCollection GetAttributes() { return AttributeCollection.Empty; }
        public string GetClassName() { return UiText.Get("皮肤"); }
        public string GetComponentName() { return UiText.Get("皮肤参数"); }
        public TypeConverter GetConverter() { return new TypeConverter(); }
        public EventDescriptor GetDefaultEvent() { return null; }
        public PropertyDescriptor GetDefaultProperty() { return null; }
        public object GetEditor(Type type) { return null; }
        public EventDescriptorCollection GetEvents() { return EventDescriptorCollection.Empty; }
        public EventDescriptorCollection GetEvents(Attribute[] attributes) { return GetEvents(); }
        public PropertyDescriptorCollection GetProperties() { return fields; }
        public PropertyDescriptorCollection GetProperties(Attribute[] attributes) { return fields; }
        public object GetPropertyOwner(PropertyDescriptor property) { return this; }
    }
    internal sealed class ThemeProperty : PropertyDescriptor
    {
        readonly string kind, category;
        readonly string[] choices;
        object value;
        internal readonly string Path;
        internal ThemeProperty(string section, string group, string key, string type, object initial, IEnumerable allowed) : base(section.Length == 0 ? key : section + "." + key, null)
        {
            Path = section.Length == 0 ? key : section + "." + key; category = group; kind = type;
            value = type == "integer" ? (object)Convert.ToInt32(initial) : type == "color" ? (object)ColorTranslator.FromHtml(Convert.ToString(initial)) : initial;
            choices = allowed == null ? null : allowed.Cast<object>().Select(item => Convert.ToString(item)).ToArray();
        }
        internal object WireValue { get { if (kind != "color") return value; var color = (Color)value; return "#" + color.R.ToString("X2") + color.G.ToString("X2") + color.B.ToString("X2"); } }
        public override string Category { get { return UiText.Get(category); } }
        public override string DisplayName { get { string key = "theme." + Path + ".title", text = UiText.Get(key); return text == key ? Path : text; } }
        public override string Description { get { string key = "theme." + Path + ".description", text = UiText.Get(key); return text == key ? DisplayName : text; } }
        public override Type ComponentType { get { return typeof(ThemeProperties); } }
        public override bool IsReadOnly { get { return false; } }
        public override Type PropertyType { get { return kind == "integer" ? typeof(int) : kind == "boolean" ? typeof(bool) : kind == "color" ? typeof(Color) : typeof(string); } }
        public override TypeConverter Converter { get { return kind == "boolean" ? new ThemeBoolean() : choices == null ? base.Converter : new ThemeChoices(choices); } }
        public override bool CanResetValue(object component) { return false; }
        public override object GetValue(object component) { return value; }
        public override void ResetValue(object component) { }
        public override void SetValue(object component, object next) { if (kind == "color" && ((Color)next).A != 255) throw new ArgumentException(UiText.Get("低功耗皮肤仅支持不透明颜色。")); value = next; }
        public override bool ShouldSerializeValue(object component) { return false; }
    }
    internal sealed class ThemeBoolean : BooleanConverter
    {
        public override object ConvertTo(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value, Type destinationType) { return destinationType == typeof(string) && value is bool ? UiText.Get((bool)value ? UiText.Get("显示") : UiText.Get("隐藏")) : base.ConvertTo(context, culture, value, destinationType); }
        public override object ConvertFrom(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value) { if (value is string) { if ((string)value == UiText.Get("显示")) return true; if ((string)value == UiText.Get("隐藏")) return false; } return base.ConvertFrom(context, culture, value); }
    }
    internal sealed class ThemeChoices : StringConverter
    {
        readonly string[] choices;
        internal ThemeChoices(string[] values) { choices = values; }
        public override bool GetStandardValuesSupported(ITypeDescriptorContext context) { return true; }
        public override bool GetStandardValuesExclusive(ITypeDescriptorContext context) { return true; }
        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext context) { return new StandardValuesCollection(choices); }
        public override object ConvertTo(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value, Type destinationType) { return destinationType == typeof(string) ? UiText.Get("theme.choice." + Convert.ToString(value)) : base.ConvertTo(context, culture, value, destinationType); }
        public override object ConvertFrom(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value) { foreach (string choice in choices) if (Convert.ToString(value) == UiText.Get("theme.choice." + choice)) return choice; return base.ConvertFrom(context, culture, value); }
    }
}
