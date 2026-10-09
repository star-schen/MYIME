using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;
namespace Myime.Settings
{
    // Blank controls mean inheritance; the Rust helper owns the merge rules.
    internal sealed class LayerEditor : TableLayoutPanel
    {
        readonly ComboBox enabled = Choice(UiText.Get("使用软件默认"), UiText.Get("启用"), UiText.Get("禁用"));
        readonly TextBox schema = new TextBox();
        readonly ComboBox theme = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
        readonly ComboBox focusPolicy = Choice(UiText.Get("使用软件默认"), UiText.Get("保留原位置草稿"), UiText.Get("取消未完成输入"), UiText.Get("提交当前候选"));
        readonly ComboBox capsPolicy = Choice(UiText.Get("使用软件默认"), UiText.Get("候选框处理大写字母"), UiText.Get("先提交中文，再直接输入大写"));
        readonly ComboBox asciiMode = Choice(UiText.Get("使用软件默认"), UiText.Get("英文"), UiText.Get("中文"));
        readonly ComboBox fullShape = Choice(UiText.Get("使用软件默认"), UiText.Get("全角"), UiText.Get("半角"));
        readonly ComboBox asciiPunct = Choice(UiText.Get("使用软件默认"), UiText.Get("英文标点"), UiText.Get("中文标点"));
        internal event EventHandler Edited;
        bool loading;
        string context = "default";
        readonly Label note = new Label { AutoSize = true, ForeColor = Color.DimGray, Dock = DockStyle.Fill };
        string Inherit { get { return context == "profile" ? UiText.Get("同 Windows 设置") : context == "windows" ? UiText.Get("同全局默认") : UiText.Get("使用软件默认"); } }
        internal LayerEditor()
        {
            AutoSize = true; Dock = DockStyle.Top; ColumnCount = 2; Padding = new Padding(12);
            ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 180)); ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            Add(UiText.Get("输入法状态"), enabled); Add("Rime Schema ID", schema); Add(UiText.Get("候选主题"), theme);
            Add(UiText.Get("初始输入模式"), asciiMode); Add(UiText.Get("字符宽度"), fullShape); Add(UiText.Get("标点模式"), asciiPunct);
            Add(UiText.Get("跨应用 / 输入框策略"), focusPolicy); Add(UiText.Get("大写锁定策略"), capsPolicy);
            Controls.Add(note, 0, RowCount); SetColumnSpan(note, 2); RowCount++;
            foreach (Control control in new Control[] { enabled, schema, theme, asciiMode, fullShape, asciiPunct, focusPolicy, capsPolicy })
                control.TextChanged += delegate { if (!loading && !UiText.Applying && Edited != null) Edited(this, EventArgs.Empty); };
            EventHandler languageChanged = delegate { SetContext(context); };
            UiText.Changed += languageChanged; Disposed += delegate { UiText.Changed -= languageChanged; }; SetContext(context);
        }
        internal void SetContext(string value)
        {
            context = value; bool before = loading; loading = true;
            foreach (var box in new ComboBox[] { enabled, focusPolicy, capsPolicy, asciiMode, fullShape, asciiPunct }) { int index = box.SelectedIndex; box.Items[0] = Inherit; box.SelectedIndex = index; }
            for (int index = 0; index < theme.Items.Count; index++) if (((ThemeChoice)theme.Items[index]).Id.Length == 0) theme.Items[index] = new ThemeChoice("", Inherit);
            note.Text = context == "profile" ? UiText.Get("同 Windows 设置：使用 MYIME 基础设置中的 Windows 通用配置；其中未设置的项使用全局默认。留空移除此应用的覆盖。") : context == "windows" ? UiText.Get("同全局默认：使用 MYIME 的全局默认值。留空移除此处的单独设置。") : UiText.Get("使用软件默认：使用 MYIME 内置值。留空移除此处的单独设置。");
            loading = before;
        }
        static ComboBox Choice(params string[] items) { var choice = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList }; choice.Items.AddRange(items); choice.SelectedIndex = 0; return choice; }
        void Add(string title, Control control)
        {
            control.Dock = DockStyle.Fill; control.Margin = new Padding(4, 7, 4, 7);
            var label = new Label { Text = title, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(4, 10, 4, 10) };
            Controls.Add(label, 0, RowCount); Controls.Add(control, 1, RowCount); RowCount++;
        }
        string themeId = "";
        internal void Themes(IEnumerable<ThemeChoice> choices) { var selected=theme.SelectedItem as ThemeChoice;if(selected!=null)themeId=selected.Id;loading = true; theme.Items.Clear(); theme.Items.Add(new ThemeChoice("", Inherit)); foreach (var choice in choices) theme.Items.Add(choice); SelectTheme(); loading = false; }
        void SelectTheme() { theme.SelectedIndex = -1; for (int index = 0; index < theme.Items.Count; index++) if (((ThemeChoice)theme.Items[index]).Id == themeId) theme.SelectedIndex = index; if (theme.SelectedIndex < 0 && themeId.Length != 0) { var choice = new ThemeChoice(themeId, UiText.Get("未安装主题：") + themeId); theme.Items.Add(choice); theme.SelectedItem = choice; } }
        static int Selection(Dictionary<string, object> layer, string key) { object value = Json.Get(layer, key); return value is bool ? ((bool)value ? 1 : 2) : 0; }
        internal void LoadLayer(Dictionary<string, object> layer)
        {
            loading = true; enabled.SelectedIndex = Selection(layer, "enabled"); schema.Text = Json.Text(layer, "schema");
            themeId = Json.Text(Json.Map(Json.Get(layer, "ui")), "theme"); SelectTheme(); var options = Json.Map(Json.Get(layer, "options"));
            var input = Json.Map(Json.Get(layer, "input")); focusPolicy.SelectedIndex = PolicyIndex(Json.Text(input, "focus"), new string[] { "retain", "cancel", "commit" }); capsPolicy.SelectedIndex = PolicyIndex(Json.Text(input, "caps_lock"), new string[] { "compose_uppercase", "commit_then_direct" });
            asciiMode.SelectedIndex = Selection(options, "ascii_mode"); fullShape.SelectedIndex = Selection(options, "full_shape"); asciiPunct.SelectedIndex = Selection(options, "ascii_punct"); loading = false;
        }
        static object Boolean(ComboBox box) { return box.SelectedIndex == 0 ? null : (object)(box.SelectedIndex == 1); }
        static int PolicyIndex(string value, string[] ids) { int index = Array.IndexOf(ids, value); return index < 0 ? 0 : index + 1; }
        static object PolicyValue(ComboBox box, string[] ids) { return box.SelectedIndex <= 0 ? null : (object)ids[box.SelectedIndex - 1]; }
        internal Dictionary<string, object> Changes()
        {
            var selectedTheme = theme.SelectedItem as ThemeChoice;
            return new Dictionary<string, object> { { "enabled", Boolean(enabled) }, { "schema", Empty(schema.Text) }, { "theme", selectedTheme == null || selectedTheme.Id.Length == 0 ? null : (object)selectedTheme.Id },
                { "ascii_mode", Boolean(asciiMode) }, { "full_shape", Boolean(fullShape) }, { "ascii_punct", Boolean(asciiPunct) },
                { "focus", PolicyValue(focusPolicy, new string[] { "retain", "cancel", "commit" }) }, { "caps_lock", PolicyValue(capsPolicy, new string[] { "compose_uppercase", "commit_then_direct" }) } };
        }
        static object Empty(string text) { text = text.Trim(); return text.Length == 0 ? null : (object)text; }
    }
}
