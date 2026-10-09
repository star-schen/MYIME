using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
namespace Myime.Settings
{
    internal sealed class ThemeChoice
    {
        internal readonly string Id, Name;
        internal ThemeChoice(string id, string name) { Id = id; Name = name; }
        public override string ToString() { return Name; }
    }
    // The UI timer owns transitions; workers never depend on native handles.
    internal sealed class ThemeImage : UserControl
    {
        readonly PictureBox image = new PictureBox { Dock = DockStyle.Fill, SizeMode = PictureBoxSizeMode.Zoom, BackColor = SystemColors.Window };
        readonly Label message = new Label { Dock = DockStyle.Fill, AutoSize = true, Padding = new Padding(8) };
        readonly Timer pump = new Timer { Interval = 80 };
        readonly string directory, root;
        string selected = "default", draft;
        int revision, activeRevision;
        DateTime queuedAt;
        bool pending;
        Task<Bitmap> active;
        internal ThemeImage(string applicationDirectory, string dataRoot)
        {
            directory = applicationDirectory; root = dataRoot; Dock = DockStyle.Fill; MinimumSize = new Size(100, 100);
            var footer = new TableLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, ColumnCount = 2 };
            footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); footer.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            var retry = new Button { Text = UiText.Get("重试预览"), AutoSize = true, Margin = new Padding(5) }; retry.Click += delegate { Queue(); };
            footer.Controls.Add(message, 0, 0); footer.Controls.Add(retry, 1, 0);
            var frame = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2 };
            frame.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); frame.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            frame.Controls.Add(image, 0, 0); frame.Controls.Add(footer, 0, 1); Controls.Add(frame);
            pump.Tick += delegate { Advance(); };
            VisibleChanged += delegate { if (Visible && !IsDisposed) { pump.Start(); Advance(); } };
            HandleCreated += delegate { if (!IsDisposed) { pump.Start(); Advance(); } };
            Disposed += delegate {
                pump.Stop(); pump.Dispose();
                if (image.Image != null) { image.Image.Dispose(); image.Image = null; }
                var abandoned = active; active = null;
                if (abandoned != null) abandoned.ContinueWith(task => { if (task.Status == TaskStatus.RanToCompletion) task.Result.Dispose(); else if (task.IsFaulted) task.Exception.Handle(error => true); });
            };
        }
        internal void ShowTheme(string id) { selected = id; draft = null; Queue(); }
        internal void ShowDraft(string text) { draft = text; Queue(); }
        void Queue() { ++revision; pending = true; queuedAt = DateTime.UtcNow; pump.Start(); }
        void Advance()
        {
            if (IsDisposed || Disposing) return;
            if (active != null && active.IsCompleted) {
                var completed = active; active = null;
                if (completed.Status == TaskStatus.RanToCompletion) {
                    if (activeRevision == revision) { var previous = image.Image; image.Image = completed.Result; if (previous != null) previous.Dispose(); message.Text = UiText.Get("示例候选 · 图片预览"); }
                    else completed.Result.Dispose();
                } else if (completed.IsFaulted) {
                    var failure = completed.Exception.GetBaseException();
                    if (activeRevision == revision) message.Text = UiText.Get("预览未更新：") + failure.Message;
                } else if (activeRevision == revision) message.Text = UiText.Get("预览已取消，请重试。");
            }
            if (active == null && pending && Visible && IsHandleCreated && (DateTime.UtcNow - queuedAt).TotalMilliseconds >= 180) {
                activeRevision = revision; string id = selected, text = draft; pending = false;
                message.Text = UiText.Get("正在更新预览…");
                active = Task.Factory.StartNew(() => Render(directory, root, id, text));
            }
            if (active == null && (!pending || !Visible || !IsHandleCreated)) pump.Stop();
        }
        static Bitmap Render(string directory, string root, string id, string draft)
        {
            string stem = Path.Combine(Path.GetTempPath(), "myime-theme-" + Guid.NewGuid().ToString("N"));
            string textPath = stem + ".toml", imagePath = stem + ".bmp";
            try {
                string arguments = "--render --data-root " + ToolClient.Quote(root) + " --theme " + ToolClient.Quote(id) + " --output " + ToolClient.Quote(imagePath);
                if (draft != null) { File.WriteAllText(textPath, draft, new UTF8Encoding(false)); arguments += " --theme-file " + ToolClient.Quote(textPath); }
                using (var process = Process.Start(new ProcessStartInfo(Path.Combine(directory, "myime-theme-preview.exe"), arguments) { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden, WorkingDirectory = directory })) {
                    if (process == null) throw new IOException(UiText.Get("无法启动预览绘制程序。"));
                    if (!process.WaitForExit(15000)) { try { process.Kill(); } catch (InvalidOperationException) { } throw new IOException(UiText.Get("绘制超时。")); }
                    if (process.ExitCode != 0 || !File.Exists(imagePath)) throw new IOException(UiText.Get("主题无效或绘制失败，保留上一次预览。"));
                }
                using (var source = new Bitmap(imagePath)) return new Bitmap(source);
            } finally { if (File.Exists(textPath)) File.Delete(textPath); if (File.Exists(imagePath)) File.Delete(imagePath); }
        }
    }
}
