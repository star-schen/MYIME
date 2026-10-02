using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace Myime.Settings
{
    // This maintenance client never participates in the IME key path.
    internal sealed class ToolClient
    {
        internal const int MaximumJsonChars = 4 * 1024 * 1024;
        readonly string executable;
        internal ToolClient(string applicationDirectory) { executable = Path.Combine(applicationDirectory, "myime-tool.exe"); }
        internal static JavaScriptSerializer Serializer() { return new JavaScriptSerializer { MaxJsonLength = MaximumJsonChars, RecursionLimit = 64 }; }
        internal Dictionary<string, object> Send(Dictionary<string, object> request)
        {
            if (!File.Exists(executable)) throw new IOException("找不到 myime-tool.exe，请使用完整的 MYIME 安装包。");
            request["version"] = 1;
            string json = Serializer().Serialize(request);
            if (json.Length > MaximumJsonChars) throw new IOException("请求超过设置工具的大小限制。");
            return Execute(executable, "--request", json, 25000);
        }
        internal static Dictionary<string, object> Execute(string executable, string arguments, string input, int timeout)
        {
            var start = new ProcessStartInfo(executable, arguments) {
                UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
                RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8,
                WorkingDirectory = Path.GetDirectoryName(executable)
            };
            using (var process = new Process { StartInfo = start })
            {
                if (!process.Start()) throw new IOException("无法启动配置工具。");
                Task<string> output = Task.Factory.StartNew(() => ReadBounded(process.StandardOutput));
                Task<string> errors = Task.Factory.StartNew(() => ReadBounded(process.StandardError));
                try
                {
                    if (input != null) { byte[] bytes = new UTF8Encoding(false).GetBytes(input); process.StandardInput.BaseStream.Write(bytes, 0, bytes.Length); }
                    process.StandardInput.Close();
                    if (!Task.WaitAll(new Task[] { output, errors }, timeout) || !process.WaitForExit(3000))
                        throw new IOException("工具响应超时；操作结果未确认，请重新读取后再操作。");
                    var result = Serializer().DeserializeObject(output.Result) as Dictionary<string, object>;
                    if (result == null || !result.ContainsKey("ok")) throw new IOException("工具返回了无效响应。");
                    return result;
                }
                catch (AggregateException error) { throw new IOException("读取工具响应失败。", error.GetBaseException()); }
                finally { try { if (!process.HasExited) process.Kill(); } catch (InvalidOperationException) { } }
            }
        }
        // A timeout or malicious helper cannot grow the settings process without bound.
        static string ReadBounded(StreamReader reader)
        {
            var text = new StringBuilder(); var buffer = new char[4096]; int count;
            while ((count = reader.Read(buffer, 0, buffer.Length)) > 0)
            { if (text.Length + count > MaximumJsonChars) throw new IOException("工具响应超过大小限制。"); text.Append(buffer, 0, count); }
            return text.ToString();
        }
        internal static string Quote(string argument)
        {
            if (argument.IndexOf('\0') >= 0) throw new ArgumentException("路径含无效字符。");
            var output = new StringBuilder("\""); int slashes = 0;
            foreach (char c in argument)
            { if (c == '\\') { slashes++; continue; } if (c == '"') { output.Append('\\', slashes * 2 + 1); output.Append(c); } else { output.Append('\\', slashes); output.Append(c); } slashes = 0; }
            output.Append('\\', slashes * 2); return output.Append('"').ToString();
        }
    }
    internal static class Json
    {
        internal static Dictionary<string, object> Map(object value) { return value as Dictionary<string, object> ?? new Dictionary<string, object>(); }
        internal static object Get(Dictionary<string, object> value, string key) { object result; return value.TryGetValue(key, out result) ? result : null; }
        internal static string Text(Dictionary<string, object> value, string key, string fallback = "") { return Get(value, key) as string ?? fallback; }
        internal static bool Flag(Dictionary<string, object> value, string key) { return Get(value, key) is bool && (bool)Get(value, key); }
        internal static Dictionary<string, object> Request(string command, string path) { return new Dictionary<string, object> { { "command", command }, { "path", path } }; }
    }
}
