using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;
namespace Myime.Settings
{
    // Blank controls mean inheritance; the Rust helper owns the merge rules.
    internal sealed class LayerEditor : TableLayoutPanel
    {
        readonly ComboBox enabled = Choice("继承", "启用", "禁用");
        readonly TextBox schema = new TextBox();
        readonly ComboBox theme = new ComboBox { DropDownStyle = ComboBoxStyle.DropDown };
        readonly ComboBox asciiMode = Choice("继承", "英文", "中文");
        readonly ComboBox fullShape = Choice("继承", "全角", "半角");
        readonly ComboBox asciiPunct = Choice("继承", "英文标点", "中文标点");
        internal event EventHandler Edited;
        bool loading;
        internal LayerEditor()
        {
            AutoSize = true; Dock = DockStyle.Top; ColumnCount = 2; Padding = new Padding(12);
            ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 180)); ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            Add("输入法状态", enabled); Add("Rime Schema ID", schema); Add("候选主题 ID", theme);
            Add("初始输入模式", asciiMode); Add("字符宽度", fullShape); Add("标点模式", asciiPunct);
            var note = new Label { AutoSize = true, ForeColor = Color.DimGray, Dock = DockStyle.Fill,
                Text = "空白 / 继承：移除此层覆盖。输入方案必须已部署，选项的最终行为由 Rime schema 决定。" };
            Controls.Add(note, 0, RowCount); SetColumnSpan(note, 2); RowCount++;
            foreach (Control control in new Control[] { enabled, schema, theme, asciiMode, fullShape, asciiPunct })
                control.TextChanged += delegate { if (!loading && Edited != null) Edited(this, EventArgs.Empty); };
        }
        static ComboBox Choice(params string[] items) { var choice = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList }; choice.Items.AddRange(items); choice.SelectedIndex = 0; return choice; }
        void Add(string title, Control control)
        {
            control.Dock = DockStyle.Fill; control.Margin = new Padding(4, 7, 4, 7);
            var label = new Label { Text = title, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(4, 10, 4, 10) };
            Controls.Add(label, 0, RowCount); Controls.Add(control, 1, RowCount); RowCount++;
        }
        internal void Themes(IEnumerable<string> ids) { string old = theme.Text; loading = true; theme.Items.Clear(); theme.Items.Add(""); foreach (string id in ids) theme.Items.Add(id); theme.Text = old; loading = false; }
        static int Selection(Dictionary<string, object> layer, string key) { object value = Json.Get(layer, key); return value is bool ? ((bool)value ? 1 : 2) : 0; }
        internal void LoadLayer(Dictionary<string, object> layer)
        {
            loading = true; enabled.SelectedIndex = Selection(layer, "enabled"); schema.Text = Json.Text(layer, "schema");
            theme.Text = Json.Text(Json.Map(Json.Get(layer, "ui")), "theme"); var options = Json.Map(Json.Get(layer, "options"));
            asciiMode.SelectedIndex = Selection(options, "ascii_mode"); fullShape.SelectedIndex = Selection(options, "full_shape"); asciiPunct.SelectedIndex = Selection(options, "ascii_punct"); loading = false;
        }
        static object Boolean(ComboBox box) { return box.SelectedIndex == 0 ? null : (object)(box.SelectedIndex == 1); }
        internal Dictionary<string, object> Changes()
        {
            return new Dictionary<string, object> { { "enabled", Boolean(enabled) }, { "schema", Empty(schema.Text) }, { "theme", Empty(theme.Text) },
                { "ascii_mode", Boolean(asciiMode) }, { "full_shape", Boolean(fullShape) }, { "ascii_punct", Boolean(asciiPunct) } };
        }
        static object Empty(string text) { text = text.Trim(); return text.Length == 0 ? null : (object)text; }
    }
}
