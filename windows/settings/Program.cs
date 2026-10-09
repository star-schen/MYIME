using System;
using System.IO;
using System.Text;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal static class Program
    {
        [STAThread]
        static int Main(string[] arguments)
        {
            string directory = AppDomain.CurrentDomain.BaseDirectory; bool testing = Array.IndexOf(arguments, "--self-check") >= 0, editor = false, publish = false; string theme = "default", module = "", ownerSid = null;
            string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MYIME");
            for (int index = 0; index < arguments.Length; index++) {
                if (arguments[index] == "--self-check") continue;
                if (arguments[index] == "--data-root" && index + 1 < arguments.Length) root = arguments[++index];
                else if (arguments[index] == "--theme-editor") editor = true;
                else if (arguments[index] == "--theme" && index + 1 < arguments.Length) theme = arguments[++index];
                else if (arguments[index] == "--module" && index + 1 < arguments.Length) module = arguments[++index];
                else if (arguments[index] == "--publish") publish = true;
                else if (arguments[index] == "--owner-sid" && index + 1 < arguments.Length) ownerSid = arguments[++index];
                else { if (!testing) MessageBox.Show(UiText.Get("参数无效。支持 --data-root <目录> 或 --self-check。")); return 2; }
            }
            string fixture = null;
            try
            {
                UiText.Configure(directory, root);
                if (publish) { if (testing || editor || !Path.IsPathRooted(root)) throw new ArgumentException("Invalid publication arguments"); PresentationPublication.Publish(root,directory,ownerSid); return 0; }
                Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                if (testing) { fixture = Path.Combine(directory, "settings-selfcheck-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(fixture); root = fixture; File.WriteAllText(Path.Combine(root, "config.toml"), "# preserved comment\n[default]\nschema='pinyin_simp'\npreserved='value'\n[default.ui]\ntheme='light'\n", new UTF8Encoding(false)); }
                if (editor && !testing) { using (var form = new ThemeEditorForm(directory, root, theme)) Application.Run(form); }
                else using (var form = new MainForm(directory, root, testing)) { if (testing) form.SelfCheck(Path.Combine(directory, "test-artifacts")); else { form.OpenModule(module); Application.Run(form); } }
                return 0;
            }
            catch (Exception error) { if (publish) { Console.Error.WriteLine(error.Message); } else if (testing) { string artifacts = Path.Combine(directory, "test-artifacts"); Directory.CreateDirectory(artifacts); File.WriteAllText(Path.Combine(artifacts, "settings-error.txt"), error.ToString(), Encoding.UTF8); } else MessageBox.Show(error.Message, UiText.Get("MYIME 设置启动失败"), MessageBoxButtons.OK, MessageBoxIcon.Error); return 1; }
            finally { if (fixture != null && Path.GetFullPath(fixture).StartsWith(Path.GetFullPath(directory), StringComparison.OrdinalIgnoreCase) && Path.GetFileName(fixture).StartsWith("settings-selfcheck-", StringComparison.Ordinal)) Directory.Delete(fixture, true); }
        }
    }
}
