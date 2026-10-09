using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;
namespace Myime.Settings
{
    // Shared, data-only UTF-8 packs. Message IDs are source phrases or stable
    // theme.* IDs. Packs cannot execute code, change schemas or modify input.
    internal static class UiText
    {
        const int Limit = 256 * 1024;
        internal sealed class Language
        {
            internal string Id, Name, Culture;
            internal Dictionary<string, string> Messages;
            public override string ToString() { return Name; }
        }
        sealed class Binding { internal string Key, LastText; internal string[] Items; }
        static readonly ConditionalWeakTable<object, Binding> bindings = new ConditionalWeakTable<object, Binding>();
        static Dictionary<string, Language> languages = new Dictionary<string, Language>(StringComparer.OrdinalIgnoreCase);
        static Language current;
        static Dictionary<string, string> defaults = new Dictionary<string, string>(StringComparer.Ordinal);
        static string directory, root;
        static readonly string systemCulture = CultureInfo.CurrentUICulture.Name;
        internal static event EventHandler Changed;
        internal static string CurrentId { get { return current == null ? "zh-Hans" : current.Id; } }
        internal static string UserDirectory { get { return Path.Combine(root, "languages"); } }
        internal static string Warning { get; private set; }
        internal static bool Applying { get; private set; }
        internal static string Get(string key)
        {
            string text; var selected = current;
            if (selected != null && selected.Messages.TryGetValue(key, out text)) return text;
            return defaults.TryGetValue(key, out text) ? text : key;
        }
        internal static string Format(string key, params object[] values) { return String.Format(CultureInfo.CurrentUICulture, Get(key), values); }
        internal static bool ValidId(string id) { return id != null && id.Length > 0 && id.Length <= 32 && id.All(c => c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '-'); }
        static Language Parse(string text, string expected)
        {
            if (Encoding.UTF8.GetByteCount(text) > Limit) throw new IOException("Language pack exceeds 256 KiB");
            var json = new JavaScriptSerializer { MaxJsonLength = Limit, RecursionLimit = 8 };
            var value = Json.Map(json.DeserializeObject(text));
            string id = Json.Text(value, "id"), name = Json.Text(value, "name");
            if (!(Json.Get(value, "format_version") is int) || (int)Json.Get(value, "format_version") != 1 || !(Json.Get(value, "id") is string) || !(Json.Get(value, "name") is string) || !ValidId(id) || !String.Equals(id, expected, StringComparison.OrdinalIgnoreCase) || name.Length == 0 || name.Length > 80 || !(Json.Get(value, "messages") is Dictionary<string, object>)) throw new IOException("Invalid language pack identity");
            var messages = new Dictionary<string, string>(StringComparer.Ordinal); var entries = Json.Map(Json.Get(value, "messages"));
            if (entries.Count > 2048) throw new IOException("Too many language messages");
            foreach (var entry in entries) {
                var translation = entry.Value as string;
                if (entry.Key.Length == 0 || entry.Key.Length > 512 || translation == null || translation.Length > 4096) throw new IOException("Invalid language message");
                messages.Add(entry.Key, translation);
            }
            return new Language { Id = id, Name = name, Culture = Json.Text(value, "culture"), Messages = messages };
        }
        static Language Embedded(string id)
        {
            using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("MYIME.languages." + id + ".json"))
            using (var reader = new StreamReader(stream, Encoding.UTF8)) return Parse(reader.ReadToEnd(), id);
        }
        static string Preference(string dataRoot)
        {
            try { string path = Path.Combine(dataRoot, "ui-language.txt"); if (File.Exists(path) && new FileInfo(path).Length <= 128) { string id = File.ReadAllText(path, Encoding.UTF8).Trim(); if (ValidId(id)) return id; } }
            catch (IOException) { } catch (UnauthorizedAccessException) { }
            string culture = systemCulture;
            return culture.StartsWith("zh-TW", StringComparison.OrdinalIgnoreCase) || culture.StartsWith("zh-HK", StringComparison.OrdinalIgnoreCase) || culture.StartsWith("zh-MO", StringComparison.OrdinalIgnoreCase) || culture.StartsWith("zh-Hant", StringComparison.OrdinalIgnoreCase) ? "zh-Hant" : "zh-Hans";
        }
        static Dictionary<string, Language> Catalog(string applicationDirectory, string dataRoot, out string warning)
        {
            var catalog = new Dictionary<string, Language>(StringComparer.OrdinalIgnoreCase);
            foreach (string id in new string[] { "zh-Hans", "zh-Hant" }) catalog.Add(id, Embedded(id));
            var failures = new List<string>();
            foreach (string folder in new string[] { Path.Combine(applicationDirectory, "languages"), Path.Combine(dataRoot, "languages") }) {
                try {
                    if (!Directory.Exists(folder)) continue;
                    foreach (string path in Directory.EnumerateFiles(folder, "*.json").Take(128)) {
                        try {
                            string id = Path.GetFileNameWithoutExtension(path);
                            if (!ValidId(id) || new FileInfo(path).Length > Limit) throw new IOException("Invalid language pack file");
                            var pack = Parse(File.ReadAllText(path, Encoding.UTF8), id); Language previous;
                            if (catalog.TryGetValue(id, out previous)) { var merged = new Dictionary<string, string>(previous.Messages, StringComparer.Ordinal); foreach (var entry in pack.Messages) merged[entry.Key] = entry.Value; pack.Messages = merged; }
                            catalog[id] = pack;
                        } catch (Exception error) { if (!(error is IOException || error is UnauthorizedAccessException || error is ArgumentException || error is InvalidOperationException || error is FormatException || error is OverflowException)) throw; failures.Add(Path.GetFileName(path)); }
                    }
                } catch (IOException) { failures.Add(folder); } catch (UnauthorizedAccessException) { failures.Add(folder); }
            }
            warning = String.Join(", ", failures); return catalog;
        }
        internal static void Configure(string applicationDirectory, string dataRoot)
        {
            directory = Path.GetFullPath(applicationDirectory); root = Path.GetFullPath(dataRoot);
            defaults = Embedded("zh-Hans").Messages;
            Reload(Preference(root));
        }
        static void Reload(string selected)
        {
            string warning; languages = Catalog(directory, root, out warning); Warning = warning;
            Language language;
            current = languages.TryGetValue(selected, out language) ? language : languages["zh-Hans"];
            try { var culture = CultureInfo.GetCultureInfo(current.Culture.Length == 0 ? current.Id : current.Culture); Thread.CurrentThread.CurrentUICulture = culture; CultureInfo.DefaultThreadCurrentUICulture = culture; } catch (CultureNotFoundException) { }
        }
        internal static void Select(string id)
        {
            if (!languages.ContainsKey(id)) throw new IOException(Get("语言包不存在，请刷新列表。"));
            Directory.CreateDirectory(root);
            string path = Path.Combine(root, "ui-language.txt"), temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try { File.WriteAllText(temporary, id, new UTF8Encoding(false)); if (File.Exists(path)) File.Replace(temporary, path, null); else File.Move(temporary, path); }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
            Reload(id); Notify();
        }
        static void Notify()
        {
            Applying = true;
            try { foreach (Form form in Application.OpenForms) Apply(form); }
            finally { Applying = false; }
            var changed = Changed; if (changed != null) changed(null, EventArgs.Empty);
        }
        static string Key(string text)
        {
            if (defaults.ContainsKey(text)) return text;
            foreach (var entry in defaults) if (entry.Value == text) return entry.Key;
            foreach (var language in languages.Values) foreach (var entry in language.Messages) if (entry.Value == text) return entry.Key;
            return null;
        }
        static string Translate(object owner, string text)
        {
            Binding binding;
            if (bindings.TryGetValue(owner, out binding) && binding.LastText != text) { bindings.Remove(owner); binding = null; }
            if (binding == null) { string key = Key(text); if (key == null) return text; binding = new Binding { Key = key }; bindings.Add(owner, binding); }
            binding.LastText = Get(binding.Key); return binding.LastText;
        }
        internal static void Apply(Control control)
        {
            // Editable/user data, candidate strings and theme names are untouched.
            if (!(control is TextBoxBase) && !(control is ComboBox) && !(control is ListBox)) control.Text = Translate(control, control.Text);
            var tree = control as TreeView;
            if (tree != null) foreach (TreeNode node in tree.Nodes) node.Text = Translate(node, node.Text);
            var grid = control as DataGridView;
            if (grid != null) foreach (DataGridViewColumn column in grid.Columns) column.HeaderText = Translate(column, column.HeaderText);
            var combo = control as ComboBox;
            if (combo != null && combo.DropDownStyle == ComboBoxStyle.DropDownList && combo.DataSource == null) {
                Binding binding;
                if (!bindings.TryGetValue(combo, out binding)) {
                    var keys = combo.Items.Cast<object>().Select(item => item is string ? Key((string)item) : null).ToArray();
                    binding = new Binding { Items = keys }; bindings.Add(combo, binding);
                }
                if (binding.Items != null && binding.Items.Length == combo.Items.Count) {
                    int selected = combo.SelectedIndex;
                    for (int index = 0; index < binding.Items.Length; index++) if (binding.Items[index] != null && combo.Items[index] is string) { string next = Get(binding.Items[index]); if ((string)combo.Items[index] != next) combo.Items[index] = next; }
                    if (combo.SelectedIndex != selected) combo.SelectedIndex = selected;
                }
            }
            foreach (Control child in control.Controls) Apply(child);
        }
        internal static Control Selector(Action publish)
        {
            var panel = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(7), WrapContents = true };
            var list = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 200 }; bool updating = false;
            panel.Controls.Add(new Label { Text = Get("界面语言"), AutoSize = true, Margin = new Padding(5, 10, 5, 5) }); panel.Controls.Add(list);
            Action fill = delegate { updating = true; list.Items.Clear(); foreach (var language in languages.Values.OrderBy(item => item.Id)) { list.Items.Add(language); if (language.Id == CurrentId) list.SelectedItem = language; } updating = false; };
            EventHandler changed = delegate { fill(); }; Changed += changed; panel.Disposed += delegate { Changed -= changed; };
            list.SelectedIndexChanged += delegate {
                if (updating || list.SelectedItem == null) return;
                try { Select(((Language)list.SelectedItem).Id); if (publish != null) publish(); }
                catch (Exception error) { fill(); MessageBox.Show(panel.FindForm(), Get("语言设置未完全保存：") + error.Message, Get("界面语言"), MessageBoxButtons.OK, MessageBoxIcon.Warning); }
            };
            var refresh = new Button { Text = Get("刷新语言包"), AutoSize = true, Margin = new Padding(5) };
            refresh.Click += delegate { Reload(CurrentId); Notify(); if (!String.IsNullOrEmpty(Warning)) MessageBox.Show(panel.FindForm(), Get("已忽略无效语言包：") + Warning); try { if (publish != null) publish(); } catch (Exception error) { MessageBox.Show(panel.FindForm(), Get("语言设置未完全保存：") + error.Message); } };
            var folder = new Button { Text = Get("语言包目录"), AutoSize = true, Margin = new Padding(5) };
            folder.Click += delegate { try { Directory.CreateDirectory(UserDirectory); Process.Start(new ProcessStartInfo("explorer.exe", ToolClient.Quote(UserDirectory)) { UseShellExecute = false }); } catch (Exception error) { MessageBox.Show(panel.FindForm(), error.Message, Get("语言包目录")); } };
            panel.Controls.Add(refresh); panel.Controls.Add(folder); fill(); return panel;
        }
        internal static void Publish(string applicationDirectory, string dataRoot, string stage)
        {
            string warning; var catalog = Catalog(applicationDirectory, dataRoot, out warning); Language selected;
            if (!catalog.TryGetValue(Preference(dataRoot), out selected)) selected = catalog["zh-Hans"];
            // Only menu phrases cross into sandbox presentation storage.
            var messages = new Dictionary<string, string>(); var fallback = Embedded("zh-Hans").Messages;
            foreach (string key in new string[] { "MYIME 输入模式", "MYIME：英文（左键切换尚未实现）", "MYIME：中文（左键切换尚未实现）", "设置", "按分组使用学习习惯", "全局学习（分组仅作标签）", "跟随应用分组", "共享学习不可用；基础输入可继续", "共享设置不可用；使用本地设置" }) { string text; messages[key] = selected.Messages.TryGetValue(key, out text) ? text : fallback[key]; }
            string folder = Path.Combine(stage, "languages"); Directory.CreateDirectory(folder);
            var pack = new Dictionary<string, object> { { "format_version", 1 }, { "id", selected.Id }, { "name", selected.Name }, { "culture", selected.Culture }, { "messages", messages } };
            File.WriteAllText(Path.Combine(folder, selected.Id + ".json"), new JavaScriptSerializer().Serialize(pack), new UTF8Encoding(false));
            File.WriteAllText(Path.Combine(stage, "ui-language.txt"), selected.Id, new UTF8Encoding(false));
        }
    }
}
