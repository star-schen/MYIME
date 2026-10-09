using System;
using System.Collections.Generic;
using System.IO;
namespace Myime.Settings
{
    internal sealed partial class MainForm
    {
        void LearningMaintenancePage()
        {
            var page=Page(UiText.Get("词库维护"));page.Name="learning-maintenance";var actions=Flow();
            actions.Controls.Add(Button(UiText.Get("合并旧词库槽"),delegate { MaintainLearning("--merge-slots",null); }));
            actions.Controls.Add(Button(UiText.Get("备份共享词库"),delegate { string destination=Path.Combine(dataRoot,"tools","learning-backups",DateTime.Now.ToString("yyyyMMdd-HHmmss")+"-"+Guid.NewGuid().ToString("N").Substring(0,8));MaintainLearning("--backup",destination); }));
            actions.Controls.Add(Button(UiText.Get("整理存储文件"),delegate { MaintainLearning("--checkpoint",null); }));
            actions.Controls.Add(Button(UiText.Get("备份目录"),delegate { OpenFolder(Path.Combine(dataRoot,"tools","learning-backups"),true); }));
            page.Controls.Add(Note(UiText.Get("这是手动维护模块，关闭设置后没有常驻整理进程。新词成功提交到共享学习库后，其他应用下一次查询即可使用。默认忽略应用标签；按组使用需在‘学习与分组’中开启。\r\n\r\n旧版的独立词库槽会保留。合并使用 Rime 的词库合并规则，跳过仍被进程占用的槽；关闭使用旧版的应用后可再次合并。大量旧词库合并会占用写锁，建议暂停输入后操作；新输入遇到写锁超时会保留正常输入，并记录本次学习未写入。整理仅执行被动 WAL checkpoint，可能留下其他进程仍在读取的日志页。\r\n\r\n备份通过 SQLite 在线备份接口生成，包含全局学习、分组记录和应用标签；请保护好备份。AI 分组只处理应用元数据，不参与词语输入和词库合并。")));
            page.Controls.Add(actions);
        }
        void MaintainLearning(string operation,string output)
        {
            string executable=Path.Combine(applicationDirectory,"myime-learning-tool.exe");
            if(!File.Exists(executable)){Error(UiText.Get("完整安装包中缺少 myime-learning-tool.exe。"));return;}
            string arguments=operation+" --data-root "+ToolClient.Quote(dataRoot);if(output!=null)arguments+=" --output "+ToolClient.Quote(output);
            Run(()=>ToolClient.Execute(executable,arguments,null,180000),result=>{
                string message=operation=="--merge-slots"?UiText.Get("已合并 ")+Convert.ToString(Json.Get(result,"count"))+UiText.Get(" 个旧词库；跳过 ")+Convert.ToString(Json.Get(result,"skipped"))+UiText.Get(" 个正在使用的槽。原文件保留。"):operation=="--backup"?UiText.Get("备份已完成：")+Json.Text(result,"output"):UiText.Get("已对 ")+Convert.ToString(Json.Get(result,"count"))+UiText.Get(" 个词库请求存储整理。");
                Status(message);
            });
        }
    }
}
