using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
namespace Myime.Settings
{
    internal sealed partial class MainForm
    {
        string deployedSchema;
        void RefreshPatchFiles()
        {
            bool wasLoading = loading; loading = true;
            string root = Path.Combine(dataRoot, "rime", "patches"), old = patchFile.Text; patchFile.Items.Clear(); patchFile.Items.Add("default.custom.yaml");
            if (Directory.Exists(root)) foreach (string file in Directory.GetFiles(root, "*.custom.yaml")) if (!patchFile.Items.Contains(Path.GetFileName(file))) patchFile.Items.Add(Path.GetFileName(file)); patchFile.Text = old; loading = wasLoading;
        }
        bool PatchPath(out string path)
        {
            string name = patchFile.Text.Trim(); path = "";
            if (!name.EndsWith(".custom.yaml", StringComparison.OrdinalIgnoreCase) || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.IndexOfAny(new char[] { '/', '\\' }) >= 0 || name.Length > 180) { Error(UiText.Get("只接受文件名 *.custom.yaml，不接受目录或完整路径。")); return false; }
            path = Path.Combine(dataRoot, "rime", "patches", name); return true;
        }
        void ReadPatch()
        {
            if (patchDirty && !ConfirmDiscard(UiText.Get("读取 Patch"))) return; string path; if (!PatchPath(out path)) return;
            Run(() => tool.Send(Json.Request("yaml.read", path)), result => { loading = true; patch.Text = Json.Text(result, "text").Replace("\r\n", "\n").Replace("\n", "\r\n"); loading = false; patchRevision = Json.Map(Json.Get(result, "revision")); patchDirty = false; Status(UiText.Get("Patch 已读取；保存和部署是两个明确的操作。")); });
        }
        void SavePatch()
        {
            if (patchRevision == null) { Error(UiText.Get("请先读取这个 Patch 文件，再保存。不存在的文件也需要先读取其修订状态。")); return; }
            string path; if (!PatchPath(out path)) return; var request = Json.Request("yaml.write", path); request["expected"] = patchRevision; request["text"] = patch.Text;
            Run(() => tool.Send(request), result => { patchRevision = Json.Map(Json.Get(result, "revision")); patchDirty = false; RefreshPatchFiles(); Status(UiText.Get("Patch 已保存，尚未发布。部署并发布后，新启动的应用使用新数据。")); });
        }
        void DeployData(string dictionary, bool reset)
        {
            if (patchDirty) { Error(UiText.Get("请先保存或重新读取 Patch，再部署。")); return; }
            string exe = Path.Combine(applicationDirectory, "myime-data-tool.exe"); if (!File.Exists(exe)) { Error(UiText.Get("安装包中缺少 myime-data-tool.exe。")); return; }
            string arguments = (reset ? "--reset" : "--build") + " --data-root " + ToolClient.Quote(dataRoot);
            if (!reset) arguments += " --patch-dir " + ToolClient.Quote(Path.Combine(dataRoot, "rime", "patches")); if (dictionary != null) arguments += " --dictionary-dir " + ToolClient.Quote(dictionary);
            Run(() => {
                var result = ToolClient.Execute(exe, arguments, null, 180000);
                if (reset && !Json.Flag(result, "ok")) result["error"] = Json.Text(result, "error") + UiText.Get("\r\n恢复基础前，请在基础设置中选择 Windows 设置，将 Schema ID 设为 pinyin_simp 并保存；在应用配置中清除或改正不在基础数据内的 Schema 覆盖。不会自动修改这些配置。");
                return result;
            }, result => { deployedSchema = Json.Text(result, "schema", "pinyin_simp"); Status((reset ? UiText.Get("已恢复基础数据") : UiText.Get("数据部署成功并已发布：") + Json.Text(result, "workspace")) + UiText.Get("。方案 ID：") + deployedSchema + UiText.Get("；点击启用最近部署方案，再重启目标应用。")); });
        }
        void EnableDeployedSchema()
        {
            if (String.IsNullOrEmpty(deployedSchema)) { Error(UiText.Get("请先成功部署词库包或恢复基础数据。")); return; } if (!PermitOtherDrafts("theme")) return;
            string desired = deployedSchema;
            var request = Update("windows", "", new Dictionary<string, object> { { "schema", desired } });
            Run(() => Persist(request), result => {
                LoadSnapshot(result);
                if (!String.Equals(Json.Text(Json.Map(Json.Get(result, "effective")), "schema"), desired, StringComparison.Ordinal)) { Error(UiText.Get("Windows 配置已保存，但返回的有效方案与部署方案不一致。请重新读取并检查配置；未宣称启用成功。")); return; }
                string publicationError=Json.Text(result,"publication_error");Status(UiText.Get("已在 Windows 配置层保存 ") + desired + UiText.Get("。AppProfile 明确指定的方案仍可覆盖它；新部署的数据请重启目标应用后使用。")+(publicationError.Length==0?"":UiText.Get(" 尚未发布配置：")+publicationError),publicationError.Length!=0);
            });
        }
        void RollbackData()
        {
            string exe = Path.Combine(applicationDirectory, "myime-data-tool.exe"); if (!File.Exists(exe)) { Error(UiText.Get("安装包中缺少 myime-data-tool.exe。")); return; }
            Run(() => ToolClient.Execute(exe, "--rollback --data-root " + ToolClient.Quote(dataRoot), null, 30000), result => { deployedSchema = Json.Text(result, "schema", "pinyin_simp"); Status(UiText.Get("已回退上次成功发布的数据。方案：") + deployedSchema + UiText.Get("；可点击启用最近部署方案，重启目标应用后生效。文件与学习记录没有被删除。")); });
        }
        Dictionary<string, object> ImportRequest(string command) { return new Dictionary<string, object> { { "command", command }, { "input_path", importPath.Text }, { "format", importFormat.SelectedItem as string ?? "txt" }, { "source", Path.GetFileName(importPath.Text) } }; }
        void PreviewImport()
        {
            if (!File.Exists(importPath.Text)) { Error(UiText.Get("请先选择 TXT 或 CSV 文件。")); return; } var request = ImportRequest("import.preview");
            Run(() => tool.Send(request), response => { var result = Json.Map(Json.Get(response, "result")); importRevision = Json.Map(Json.Get(result, "input_revision"));
                var summary = Json.Map(Json.Get(result, "summary")); importSummary.Text = UiText.Get("已解析 ") + Convert.ToString(Json.Get(summary, "input_rows")) + UiText.Get(" 行；可导出 ") + Convert.ToString(Json.Get(summary, "exportable_rows")) + UiText.Get("；重复 ") + Convert.ToString(Json.Get(summary, "duplicates")) + UiText.Get("；缺少编码 ") + Convert.ToString(Json.Get(summary, "missing_code_rows")) + UiText.Get("。最多显示 100 条。");
                importRows.Rows.Clear(); var sample = Json.Get(result, "sample") as IEnumerable; if (sample != null) foreach (object item in sample) { var row = Json.Map(item); importRows.Rows.Add(Json.Text(row, "word"), Json.Text(row, "code"), Json.Get(row, "frequency"), Json.Text(row, "source")); } Status(UiText.Get("预览完成，尚未写入数据。生成包要求输入文件保持原修订。"));
            });
        }
        void WriteImport()
        {
            if (importRevision == null || importRevision.Count == 0) { Error(UiText.Get("请先解析预览，再生成数据包。")); return; } string id = importId.Text.Trim();
            if (!ValidPackageId(id) || importName.Text.Trim().Length == 0) { Error(UiText.Get("包 ID：首位小写字母，后续小写字母、数字或下划线，最多 64 位；名称不能为空。")); return; }
            var request = ImportRequest("import.write"); request["input_revision"] = importRevision; request["packages_root"] = Path.Combine(dataRoot, "dictionaries"); request["id"] = id; request["name"] = importName.Text.Trim();
            Run(() => tool.Send(request), response => Status(UiText.Get("词库包已生成。请部署指定词库包，再启用最近部署方案。未修改 userdb。")));
        }
        static bool ValidPackageId(string id) { if (id.Length == 0 || id.Length > 64 || id[0] < 'a' || id[0] > 'z') return false; foreach (char c in id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false; return true; }
    }
}
