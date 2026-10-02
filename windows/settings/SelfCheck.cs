using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Threading;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal sealed partial class MainForm
    {
        internal void SelfCheck(string artifacts)
        {
            var initial = tool.Send(Json.Request("config.read", configPath)); Require(Json.Flag(initial, "ok"), "read"); LoadSnapshot(initial);
            Require((string)general.Changes()["theme"] == "light" && general.Changes()["ascii_mode"] == null, "inherited layer controls");
            var request = Update("profile", "TestEditor.exe", new Dictionary<string, object> { { "theme", "dark" }, { "ascii_mode", false }, { "schema", "pinyin_simp" } });
            var saved = tool.Send(request); Require(Json.Flag(saved, "ok"), "profile save"); LoadSnapshot(saved); LoadProfile("TestEditor.exe");
            Require(profileExe.Text == "TestEditor.exe" && (string)profile.Changes()["theme"] == "dark", "profile controls");
            var stale = tool.Send(request); Require(!Json.Flag(stale, "ok") && Json.Text(stale, "kind") == "conflict", "stale write rejected");
            Require(Json.Text(saved, "text").Contains("preserved"), "unknown configuration preserved");
            Require(ToolClient.Quote("a\\b\"c\\") == "\"a\\b\\\"c\\\\\"", "command quoting");
            string patchPath = Path.Combine(dataRoot, "rime", "patches", "default.custom.yaml");
            var oldPatch = tool.Send(Json.Request("yaml.read", patchPath)); Require(Json.Flag(oldPatch, "ok"), "missing patch read");
            var patchRequest = Json.Request("yaml.write", patchPath); patchRequest["expected"] = Json.Get(oldPatch, "revision"); patchRequest["text"] = "patch:\n  menu/page_size: 7\n";
            var newPatch = tool.Send(patchRequest); Require(Json.Flag(newPatch, "ok"), "yaml write"); patch.Text = "patch:\r\n  menu/page_size: 7\r\n";
            Directory.CreateDirectory(artifacts); ShowInTaskbar = false; Opacity = 0; Show();
            bool received = false; Run(() => tool.Send(Json.Request("config.read", configPath)), result => { received = true; Status("设置自测：读取、继承、配置保存、冲突拒绝和 YAML 写入通过。仅使用隔离数据目录。"); });
            WaitForMaintenance(); Require(received && tabs.Enabled, "asynchronous helper completion");
            deployedSchema = "myime_gui_fixture"; EnableDeployedSchema(); WaitForMaintenance();
            var layers = Json.Map(Json.Get(snapshot, "layers")); Require(Json.Text(Json.Map(Json.Get(layers, "windows")), "schema") == deployedSchema && Json.Text(Json.Map(Json.Get(layers, "default")), "schema") == "pinyin_simp", "deployment enabled in Windows layer only");
            var profileRead = Json.Request("config.read", configPath); profileRead["executable"] = "TestEditor.exe";
            var overridden = tool.Send(profileRead); Require(Json.Flag(overridden, "ok") && Json.Text(Json.Map(Json.Get(overridden, "effective")), "schema") == "pinyin_simp", "explicit profile schema preserved");
            themeId.Text = "gui_fixture"; themeName.Text = "设置主题自测"; NewTheme(); WaitForMaintenance(); Require(themeRevision != null && themeDirty, "theme template controls");
            SaveTheme(); WaitForMaintenance(); Require(File.Exists(Path.Combine(dataRoot, "themes", "gui_fixture", "theme.toml")) && !themeDirty, "user theme save");
            importPath.Text = Path.Combine(dataRoot, "words.csv"); importFormat.SelectedItem = "csv";
            File.WriteAllText(importPath.Text, "word,code,frequency,source\n你好,ni hao,100,fixture\n输入法,shu ru fa,20,fixture\n", new UTF8Encoding(false));
            PreviewImport(); WaitForMaintenance(); Require(importRevision != null && importRevision.Count != 0 && importRows.Rows.Count == 2, "import preview controls");
            WriteImport(); WaitForMaintenance(); Require(File.Exists(Path.Combine(dataRoot, "dictionaries", "my_words", "manifest.json")), "import package creation");
            Status("设置自测通过：配置 / 继承 / 冲突 / Patch / 主题保存 / 导入预览和数据包。仅使用隔离目录。");
            for (int index = 0; index < tabs.TabPages.Count; index++) { tabs.SelectedIndex = index; PerformLayout(); tabs.SelectedTab.CreateControl(); themeText.Select(0, 0); patch.Select(0, 0); raw.Select(0, 0); using (var image = new Bitmap(Width, Height)) { DrawToBitmap(image, new Rectangle(Point.Empty, Size)); image.Save(Path.Combine(artifacts, "settings-" + index + ".png")); } }
            generalDirty = profileDirty = rawDirty = patchDirty = themeDirty = false; Hide(); Close();
        }
        void WaitForMaintenance() { var deadline = Stopwatch.StartNew(); while (busy && deadline.ElapsedMilliseconds < 30000) { Application.DoEvents(); Thread.Sleep(5); } Require(!busy, "asynchronous maintenance timeout"); }
        static void Require(bool result, string description) { if (!result) throw new InvalidOperationException("Settings self-check failed: " + description); }
    }
}
