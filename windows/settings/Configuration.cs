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
            Run(() => Persist(request), result => { if (layer == "profile") currentExe = exe; AcceptSave(result); });
        }
        void Reload(bool prompt)
        {
            if (prompt && Dirty() && !ConfirmDiscard(UiText.Get("重新读取配置"))) return;
            Run(() => tool.Send(Json.Request("config.read", configPath)), result => { LoadSnapshot(result); Status(UiText.Get("已读取。皮肤可即时刷新；输入方案在当前组合结束后更新。"), Json.Get(result, "valid") is bool && !Json.Flag(result, "valid")); RefreshThemes(); });
        }
        Dictionary<string,object> Persist(Dictionary<string,object> request){
            var result=tool.Send(request);if(!selfCheck && Json.Flag(result,"ok")){try{PresentationPublication.Publish(dataRoot,applicationDirectory);}catch(Exception error){result["publication_error"]=error.Message;}}return result;
        }
        void AcceptSave(Dictionary<string, object> result) { LoadSnapshot(result);string failure=Json.Text(result,"publication_error");Status(failure.Length==0?UiText.Get("配置已保存并通知运行中的输入法；输入方案在当前组合结束后更新。"):UiText.Get("配置已保存，但尚未即时发布：")+failure,failure.Length!=0); }
        internal void LoadSnapshot(Dictionary<string, object> value)
        {
            snapshot = value; loading = true; raw.Text = Json.Text(value, "text").Replace("\r\n", "\n").Replace("\n", "\r\n");
            profileList.Items.Clear(); var profiles = Json.Get(value, "profiles") as IEnumerable;
            if (profiles != null) foreach (object item in profiles) { string exe = Json.Text(Json.Map(item), "executable"); if (exe.Length != 0) profileList.Items.Add(exe); }
            profileList.SelectedItem = currentExe; loading = false; rawDirty = false; LoadGeneral(); LoadProfile(currentExe);
            LoadLearning(value);
            RefreshEffectiveSummary();
        }
        void RefreshEffectiveSummary()
        {
            var state = Json.Map(Json.Get(snapshot, "effective"));
            effective.Text = Json.Get(snapshot, "valid") is bool && !Json.Flag(snapshot, "valid") ? UiText.Get("配置语法错误，请到高级配置修复：") + Json.Text(snapshot, "validation_error") : UiText.Get("本次读取结果：") + Json.Text(state, "schema") + "  ·  " + (Json.Flag(state, "enabled") ? UiText.Get("启用") : UiText.Get("禁用")) + UiText.Get("  ·  主题 ") + Json.Text(state, "theme", "default");
        }
        void LoadGeneral() { var layers = Json.Map(Json.Get(snapshot, "layers")); general.SetContext(currentScope == 0 ? "default" : "windows"); general.LoadLayer(Json.Map(Json.Get(layers, currentScope == 0 ? "default" : "windows"))); generalDirty = false; }
        void LoadProfile(string exe)
        {
            currentExe = exe; loading = true; profileExe.Text = exe; var layer = new Dictionary<string, object>(); var profiles = Json.Get(snapshot, "profiles") as IEnumerable;
            if (profiles != null) foreach (object item in profiles) { var entry = Json.Map(item); if (String.Equals(Json.Text(entry, "executable"), exe, StringComparison.OrdinalIgnoreCase)) { layer = Json.Map(Json.Get(entry, "overrides")); break; } }
            profile.LoadLayer(layer); loading = false; profileDirty = false;
        }
        static bool ValidExecutable(string name) { return name.Length > 4 && name.Length <= 260 && name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0 && name.IndexOfAny(new char[] { '/', '\\' }) < 0; }
        static bool ValidId(string id) { return id.Length >= 1 && id.Length <= 64 && id.All(c => (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'); }
        internal static bool ValidThemeId(string id) { return ValidId(id); }
        void RefreshThemes()
        {
            var previous = themeList.SelectedItem as ThemeChoice; string selected = previous == null ? Json.Text(Json.Map(Json.Get(snapshot,"effective")),"theme","default") : previous.Id;
            Run(() => tool.Send(new Dictionary<string, object> { { "command", "theme.catalog" }, { "user_root", Path.Combine(dataRoot, "themes") }, { "installed_root", Path.Combine(applicationDirectory, "themes") } }), result => {
                var choices = new List<ThemeChoice>(); var entries = Json.Get(result, "themes") as IEnumerable; themeList.Items.Clear();
                if (entries != null) foreach (object entry in entries) { var value = Json.Map(entry); var choice = new ThemeChoice(Json.Text(value, "id"), Json.Text(value, "name")); choices.Add(choice); themeList.Items.Add(choice); if (choice.Id == selected) themeList.SelectedItem = choice; }
                if (themeList.SelectedIndex < 0 && themeList.Items.Count > 0) themeList.SelectedIndex = 0; general.Themes(choices); profile.Themes(choices);
            });
        }
        void LaunchExecutable(string name, string arguments)
        {
            string path = Path.Combine(applicationDirectory, name); if (!File.Exists(path)) { Error(UiText.Get("完整安装包中缺少 ") + name); return; }
            try { Process.Start(new ProcessStartInfo(path, arguments) { UseShellExecute = false, WorkingDirectory = applicationDirectory }); } catch (Exception error) { Error(error.Message); }
        }
        void OpenFolder(string path, bool create)
        {
            try { if (create) Directory.CreateDirectory(path); if (!Directory.Exists(path)) { Status(UiText.Get("目录尚未创建：") + path, true); return; } Process.Start(new ProcessStartInfo("explorer.exe", ToolClient.Quote(path)) { UseShellExecute = false }); } catch (Exception error) { Error(error.Message); }
        }
    }
}
