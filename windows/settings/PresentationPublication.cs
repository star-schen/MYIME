using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
namespace Myime.Settings
{
    // Only a bounded product projection and theme manifests cross the sandbox.
    // API credentials and user dictionaries never enter this read-only channel.
    internal static class PresentationPublication
    {
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern uint RegisterWindowMessage(string message);
        [DllImport("user32.dll", SetLastError = true)] static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        // Same installer metadata protocol as Host/SharedProfile. No stored SID:
        // the current effective identity or explicit installer owner is authoritative.
        internal static string SharedRoot(string directory, string ownerSid = null)
        {
            string common = Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);
            string locator = Path.Combine(directory, "shared-storage-root.txt");
            if (File.Exists(locator)) {
                if ((File.GetAttributes(locator) & FileAttributes.ReparsePoint) != 0 || new FileInfo(locator).Length > 65536) throw new IOException(UiText.Get("共享数据定位文件无效。"));
                common = File.ReadAllText(locator, new UTF8Encoding(false, true));
                if (String.IsNullOrEmpty(common) || common.IndexOfAny(new char[] { '\r', '\n', '\0' }) >= 0) throw new IOException(UiText.Get("共享数据根目录无效。"));
            }
            if (!Path.IsPathRooted(common) || !String.Equals(common, Path.GetFullPath(common), StringComparison.OrdinalIgnoreCase)) throw new IOException(UiText.Get("共享数据根目录必须是规范的绝对路径。"));
            string sid;
            if (ownerSid == null) { using (var identity = WindowsIdentity.GetCurrent()) sid = identity.User.Value; }
            else sid = new SecurityIdentifier(ownerSid).Value;
            return Path.Combine(common, "MYIME", "users", sid);
        }
        internal static void Publish(string root, string directory, string ownerSid = null)
        {
            string shared = SharedRoot(directory, ownerSid);
            string catalog = Path.Combine(shared, "presentation");
            if (!Directory.Exists(catalog)) throw new IOException(UiText.Get("配置已保存，但共享展示目录尚未由新版安装器建立。安装新版后重新保存即可发布。"));
            var tool = new ToolClient(directory);
            var projected = tool.Send(Json.Request("config.presentation", Path.Combine(root, "config.toml")));
            if (!Json.Flag(projected, "ok")) throw new IOException(Json.Text(projected, "error"));
            using (var publicationLock = new FileStream(Path.Combine(catalog, ".publish.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None)) {
                string generation = "p-" + Guid.NewGuid().ToString("N"), stage = Path.Combine(catalog, generation);
                Directory.CreateDirectory(stage);
                File.WriteAllText(Path.Combine(stage, "config.toml"), Json.Text(projected, "text"), new UTF8Encoding(false));
                UiText.Publish(directory, root, stage);
                var request = new Dictionary<string, object> { { "command", "theme.catalog" }, { "user_root", Path.Combine(root, "themes") }, { "installed_root", Path.Combine(directory, "themes") } };
                var themes = tool.Send(request); if (!Json.Flag(themes, "ok")) throw new IOException(Json.Text(themes, "error"));
                var entries = Json.Get(themes, "themes") as System.Collections.IEnumerable;
                if (entries != null) foreach (object entry in entries) {
                    string id = Json.Text(Json.Map(entry), "id"); if (!MainForm.ValidThemeId(id)) continue;
                    string path = Path.Combine(root, "themes", id, "theme.toml"); if (!File.Exists(path)) path = Path.Combine(directory, "themes", id, "theme.toml");
                    var theme = tool.Send(Json.Request("theme.read", path)); if (!Json.Flag(theme, "ok") || !Json.Flag(theme, "valid")) continue;
                    string target = Path.Combine(stage, "themes", id); Directory.CreateDirectory(target); File.WriteAllText(Path.Combine(target, "theme.toml"), Json.Text(theme, "text"), new UTF8Encoding(false));
                }
                string current = Path.Combine(catalog, "current"), temporary = Path.Combine(catalog, ".current-" + generation);
                File.WriteAllText(temporary, generation, new UTF8Encoding(false));
                if (File.Exists(current)) File.Replace(temporary, current, null); else File.Move(temporary, current);
            }
            PostMessage(new IntPtr(0xffff), RegisterWindowMessage("MYIME.PresentationChanged.v1"), IntPtr.Zero, IntPtr.Zero);
        }
    }
}
