# 2026-10-09 Release 编译和安装包

用户在源码修改完成后授权“你来编译并观察错误，构建好包我下次选2”。本轮执行了正式 Release 编译、基础字典离线生成、打包和文件比对；没有安装、注销、重启搜索进程或运行输入与界面测试。

## 结果

- `scripts/build.ps1 -Configuration Release` 完成：带产品存储模块的 librime、Rust Core 与维护工具、C++ Host 和配套程序、C# 设置程序全部成功。
- `scripts/prepare-data.ps1 -RuntimeRoot build/package-data-20261009 -Configuration Release` 完成：使用独立构建目录生成官方基础字典，没有使用现有运行目录中的私人词库和配置。该步骤仅运行字典部署分支，没有执行输入测试分支。
- `scripts/package.ps1 -Configuration Release` 完成：安装包位于 `out/MYIME-Release`，可通过 `Install-MYIME.cmd` 的选项 2 安装。
- 对 34 个程序、DLL、配置、语言包、主题和字典文件逐一比较编译输出与安装包的 SHA256，全部相同。该比较未启动安装包中的程序。

| 文件 | SHA256 |
| --- | --- |
| myime_host.dll | 1A19723613B6D4359C0DE783C904B6C9CBA57B9CCE3DBC9E1A9D00DFA0D9E0AB |
| myime_core.dll | B708A260A827C173F1780D016295E8C82545C0D0D470C7233507FB565DD64352 |
| rime.dll | ABB256AE1464047F2D453E76FCF16BF287881A3BC15107222811F925309206B5 |
| myime-settings.exe | 66FE56472EB5D038FE83146BD25CA994D7330F42FBDE1836EDA46FFF9D85F14E |

## 本轮修复的构建问题

第一次构建在 librime 源码解包时报错：`archive_write_header(): Can't unlink already-existing object`，对应 `include/darts.h`。由源码指纹派生的生成目录已经包含上一次构建留下的文件，脚本再次直接往该目录解包。

`build-rime.ps1` 现在在需要重新生成 librime 时先重建该生成源码目录，再从固定版本的上游归档解包。清理前检查绝对路径、父目录、指纹目录名及重解析点；不会清理上游仓库、用户词库或整个依赖缓存。修复后正式 Release 构建成功。仍有上游类型转换、DLL 导出等编译警告，未修改上游算法来消除这些警告。

## 安装与验收范围

本轮没有更新 Windows 已安装的 MYIME。下一次运行 `Install-MYIME.cmd`，选择 **2：安装／升级已有 Release 安装包**；该选项不重新编译。安装成功后保存工作并注销、重新登录，使应用重新加载新版输入法。

编译成功只证明这套源码能生成配套二进制和安装包，不能证明开始菜单问题已经消失。搜索进程内的初始化、共享目录访问、输入法切换和候选框显示仍需安装后的实际验收。源码修复的行为和剩余限制见 [source-fix-2026-10-09.md](source-fix-2026-10-09.md)。

构建日志和逐文件哈希记录仅保存在本地工作目录，未向公开仓库上传原始日志或构建产物。
