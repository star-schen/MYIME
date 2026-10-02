using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal sealed partial class MainForm
    {
        Dictionary<string, object> Revision() { return Json.Map(Json.Get(snapshot, "revision")); }
        Dictionary<string, object> Update(string layer, string exe, Dictionary<string, object> changes) { var request = Json.Request("config.update", configPath); request["expected"] = Revision(); request["scope"] = layer; request["executable"] = exe; request["changes"] = changes; return request; }
        void SaveLayer(LayerEditor editor, string layer, string exe)
        {
            if (!PermitOtherDrafts(layer == "profile" ? "profile" : "general")) return; var request = Update(layer, exe, editor.Changes());
            Run(() => tool.Send(request), result => { if (layer == "profile") currentExe = exe; AcceptSave(result); });
        }
        void Reload(bool prompt)
        {
            if (prompt && Dirty() && !ConfirmDiscard("重新读取配置")) return;
            Run(() => tool.Send(Json.Request("config.read", configPath)), result => { LoadSnapshot(result); Status("已读取。保存后重启目标应用；无需卸载输入法。", Json.Get(result, "valid") is bool && !Json.Flag(result, "valid")); });
        }
        void AcceptSave(Dictionary<string, object> result) { LoadSnapshot(result); Status("配置已保存。请重启需要使用新配置的应用；无需卸载输入法。"); }
        internal void LoadSnapshot(Dictionary<string, object> value)
        {
            snapshot = value; loading = true; raw.Text = Json.Text(value, "text").Replace("\r\n", "\n").Replace("\n", "\r\n");
            profileList.Items.Clear(); var profiles = Json.Get(value, "profiles") as IEnumerable;
            if (profiles != null) foreach (object item in profiles) { string exe = Json.Text(Json.Map(item), "executable"); if (exe.Length != 0) profileList.Items.Add(exe); }
            profileList.SelectedItem = currentExe; loading = false; rawDirty = false; LoadGeneral(); LoadProfile(currentExe);
            var state = Json.Map(Json.Get(value, "effective"));
            effective.Text = Json.Get(value, "valid") is bool && !Json.Flag(value, "valid") ? "配置语法错误，请到高级配置修复：" + Json.Text(value, "validation_error") : "本次读取结果：" + Json.Text(state, "schema") + "  ·  " + (Json.Flag(state, "enabled") ? "启用" : "禁用") + "  ·  主题 " + Json.Text(state, "theme", "default");
        }
        void LoadGeneral() { var layers = Json.Map(Json.Get(snapshot, "layers")); general.LoadLayer(Json.Map(Json.Get(layers, currentScope == 0 ? "default" : "windows"))); generalDirty = false; }
        void LoadProfile(string exe)
        {
            currentExe = exe; loading = true; profileExe.Text = exe; var layer = new Dictionary<string, object>(); var profiles = Json.Get(snapshot, "profiles") as IEnumerable;
            if (profiles != null) foreach (object item in profiles) { var entry = Json.Map(item); if (String.Equals(Json.Text(entry, "executable"), exe, StringComparison.OrdinalIgnoreCase)) { layer = Json.Map(Json.Get(entry, "overrides")); break; } }
            profile.LoadLayer(layer); loading = false; profileDirty = false;
        }
        static bool ValidExecutable(string name) { return name.Length > 4 && name.Length <= 260 && name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0 && name.IndexOfAny(new char[] { '/', '\\' }) < 0; }
        static bool ValidId(string id) { return id.Length >= 1 && id.Length <= 64 && id.All(c => (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'); }
        void RefreshThemes()
        {
            var ids = new SortedSet<string>(StringComparer.Ordinal) { "default", "light", "dark", "ribbon" };
            foreach (string root in new string[] { Path.Combine(applicationDirectory, "themes"), Path.Combine(dataRoot, "themes") }) if (Directory.Exists(root)) foreach (string path in Directory.GetDirectories(root)) { string id = Path.GetFileName(path); if (ValidId(id) && File.Exists(Path.Combine(path, "theme.toml"))) ids.Add(id); }
            string selected = themeList.SelectedItem as string; themeList.Items.Clear(); foreach (string id in ids) themeList.Items.Add(id); themeList.SelectedItem = selected ?? "default"; general.Themes(ids); profile.Themes(ids);
        }
        void LaunchExecutable(string name, string arguments)
        {
            string path = Path.Combine(applicationDirectory, name); if (!File.Exists(path)) { Error("完整安装包中缺少 " + name); return; }
            try { Process.Start(new ProcessStartInfo(path, arguments) { UseShellExecute = false, WorkingDirectory = applicationDirectory }); } catch (Exception error) { Error(error.Message); }
        }
        void OpenFolder(string path, bool create)
        {
            try { if (create) Directory.CreateDirectory(path); if (!Directory.Exists(path)) { Status("目录尚未创建：" + path, true); return; } Process.Start(new ProcessStartInfo("explorer.exe", ToolClient.Quote(path)) { UseShellExecute = false }); } catch (Exception error) { Error(error.Message); }
        }
    }
}
