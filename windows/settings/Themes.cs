using System;
using System.Collections.Generic;
using System.IO;
namespace Myime.Settings
{
    internal sealed partial class MainForm
    {
        string editingThemePath;
        string ThemePath(string id) { return Path.Combine(dataRoot, "themes", id, "theme.toml"); }
        void ReadTheme()
        {
            if (themeList.SelectedItem == null || (themeDirty && !ConfirmDiscard("读取另一个主题"))) return;
            string id = themeList.SelectedItem.ToString(), target = ThemePath(id), installed = Path.Combine(applicationDirectory, "themes", id, "theme.toml");
            Run(() => {
                var own = tool.Send(Json.Request("theme.read", target)); if (!Json.Flag(own, "ok") || Json.Flag(Json.Map(Json.Get(own, "revision")), "exists")) return own;
                var source = tool.Send(Json.Request("theme.read", installed)); if (Json.Flag(source, "ok")) source["revision"] = Json.Get(own, "revision"); return source;
            }, result => { SetThemeDocument(result, id, target); Status("已读取主题；保存会写入自己的用户主题目录。安装文件不被修改。"); });
        }
        void NewTheme()
        {
            string id = themeId.Text.Trim(); if (!ValidId(id)) { Error("主题 ID 使用 1–64 位小写字母、数字、下划线或短横线。"); return; }
            if (themeDirty && !ConfirmDiscard("新建主题模板")) return; string name = themeName.Text.Trim(), path = ThemePath(id);
            Run(() => {
                var own = tool.Send(Json.Request("theme.read", path)); if (!Json.Flag(own, "ok")) return own;
                if (Json.Flag(Json.Map(Json.Get(own, "revision")), "exists")) return new Dictionary<string, object> { { "ok", false }, { "error", "这个用户主题已经存在，请从列表选择并读取，或使用新的 ID。" } };
                var request = new Dictionary<string, object> { { "command", "theme.template" }, { "id", id }, { "name", name.Length == 0 ? "我的主题" : name } };
                var result = tool.Send(request); if (Json.Flag(result, "ok")) result["revision"] = Json.Get(own, "revision"); return result;
            }, result => { SetThemeDocument(result, id, path); themeDirty = true; Status("已生成灰色模板，尚未写入文件。可以编辑各项，再保存我的主题。"); });
        }
        void SetThemeDocument(Dictionary<string, object> result, string id, string path)
        {
            loading = true; themeId.Text = id; themeName.Text = Json.Text(result, "name", "我的主题"); themeText.Text = Json.Text(result, "text").Replace("\r\n", "\n").Replace("\n", "\r\n"); loading = false;
            themeRevision = Json.Map(Json.Get(result, "revision")); editingThemePath = path; themeDirty = false;
        }
        void SaveTheme()
        {
            string id = themeId.Text.Trim(); if (!ValidId(id) || themeRevision == null || editingThemePath != ThemePath(id)) { Error("请先读取选中的主题，或生成新的模板；修改 ID 后需要重新读取其修订状态。"); return; }
            var request = Json.Request("theme.write", editingThemePath); request["expected"] = themeRevision; request["text"] = themeText.Text;
            Run(() => tool.Send(request), result => { SetThemeDocument(result, id, editingThemePath); RefreshThemes(); themeList.SelectedItem = id; Status("用户主题已保存。可打开实时预览查看；设置全局默认主题后重启目标应用。"); });
        }
    }
}
