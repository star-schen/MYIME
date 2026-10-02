# MYIME 离线维护协议 v1

`myime-tool.exe` 是独立 Rust 工具，供 C# 设置程序使用，也可由脚本调用。它不链接 librime、不注册输入法、不操作正在使用的 DLL，也不启动输入会话。设置、配置文件编辑和导入都不进入每次按键的链路。

使用 `myime-tool.exe --request`，向标准输入发送一个 UTF-8 JSON 对象，关闭输入，读取标准输出的一个 JSON 对象。程序退出后请求结束；没有常驻 IPC 服务。允许 UTF-8 BOM；请求上限 4 MiB，产品配置和原生 custom YAML 上限各 1 MiB。路径使用 JSON 转义，建议始终传绝对路径。

所有请求必须含 `version: 1` 和 `command`（兼容 `op` 别名）。成功响应含 `version: 1, ok: true`，失败响应为：

```json
{"version":1,"ok":false,"kind":"conflict","error":"文件已修改；请重新读取并比较，不会覆盖新内容。"}
```

退出码：0 成功；2 请求、配置或格式错误；3 revision 冲突或另一 MYIME 写入者占锁；4 文件 I/O 或标准输出失败。导入器可能附带更具体的 `kind`，如 `encoding`、`limit`、`exists`。不记录输入文件正文或按键内容。

## 文件 revision

```json
{"exists":true,"modified_unix_ms":"1791000000000","hash":"64个小写十六进制SHA256字符"}
```

`modified_unix_ms` 使用字符串，避免跨语言整数精度变化。`hash` 是原始文件字节的 SHA-256，包含 BOM、注释和换行。缺失文件的 revision 是 `exists:false, modified_unix_ms:"0"`，hash 为零长度内容的 SHA-256。调用者把完整对象原样传回 `expected`；不能只比较时间或 hash，也不能失败后不经重新读取就强制保存。

生产原子写入规则：同目录临时文件写入并 flush；内核 sidecar 锁串行化 MYIME 写入者；写入前和发布前重新比较完整 revision。Windows 已有文件使用 `ReplaceFileW`，保留独立 `.文件名.myime-...bak` 备份；缺失文件使用不带覆盖标志的 `MoveFileExW`，避免覆盖刚被另一进程创建的文件。成功响应的 `backup_path` 指向旧内容备份。`.文件名.myime-write.lock` 可以持续存在，锁随进程退出自动释放，不能按文件是否存在判断占锁。

如果 Windows 在替换中返回 1177，旧内容可能已经移到备份文件。工具尝试用不覆盖目标的重命名恢复旧文件；如果外部已创建新目标，则保留该目标和备份并明确报错，不继续覆盖。该失败语义遵循 [ReplaceFileW 官方说明](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)。

这是对外部编辑器的乐观冲突检测。Windows 没有任意文件的原子 compare-and-swap；不使用 sidecar 锁的编辑器可能在最后的检查与文件替换之间写入。保存期间不要同时用其他程序修改同一文件。本轮没有自动三方合并、双向 YAML 同步或强制覆盖动作。

## 产品配置

### config.read

```json
{"version":1,"command":"config.read","path":"C:\\Users\\example\\AppData\\Local\\MYIME\\config.toml","executable":"Editor.exe"}
```

`executable` 可省略。响应字段：

- `revision`、`text`、`backup_path`（读取时 null）。
- `valid`，以及无效源文档时的 `validation_error`。坏文档仍可读取原文修复；此时 `effective:null`。
- `effective:{schema,enabled,theme,options}`：按 Default → Windows → 匹配 EXE 合并，使用 Core 同一 `Config::parse/effective` 规则。不匹配应用时使用默认配置。
- `layers:{default:{...},windows:{...}}`：原始覆盖层；`ui.theme`、`options` 保持 TOML 对应层次。
- `profiles:[{executable,overrides,...}]`：保留未知字段，EXE 匹配忽略大小写。

缺失文件可读取默认状态，不会因读取而创建任何配置。

### config.update

```json
{
  "version":1,
  "command":"config.update",
  "path":"D:\\fixture\\config.toml",
  "expected":{"exists":false,"modified_unix_ms":"0","hash":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
  "scope":"profile",
  "executable":"Editor.exe",
  "changes":{"theme":"dark","enabled":true,"ascii_mode":false,"full_shape":null}
}
```

`scope` 为 `default`、`windows` 或 `profile`，后者必须传 EXE basename。允许编辑：`enabled`、`schema`、`theme`、`ascii_mode`、`full_shape`、`ascii_punct`。后三项写入该层的 `options`；theme 写入 `ui.theme`。缺失字段保持；null 删除该层覆盖，恢复继承。空 `changes` 不添加空表。产品文本直接交给运行时相同的 `Config::parse`，接受并保留正常的单个 UTF-8 BOM；不会额外移除多个 BOM 并把原本无效的文件误报为有效。

`toml_edit` 保留注释、未识别字段和现有表结构，包含用户手写的 inline profiles 数组。不猜测应用词库或游戏策略。所有结果经 Core 配置解析验证后原子保存，返回同 `config.read` 字段。删除已知覆盖不会删除 profile 中未知字段。

### config.write

```json
{"version":1,"command":"config.write","path":"D:\\fixture\\config.toml","expected":{},"text":"[default.ui]\ntheme='dark'\n"}
```

实际调用必须填完整 expected 对象。此命令供高级编辑器保存完整原文。保存前使用同一 Core 配置规则验证，语法或配置值错误不会破坏已有文件。用户在其他编辑器修改后再次保存返回冲突，保留外部版本和编辑器中尚未保存的文本。

### config.check-schemas

```json
{"version":1,"command":"config.check-schemas","path":"D:\\fixture\\config.toml","schemas":["pinyin_simp","myime_global","myime_my_words"]}
```

平台在发布新代际、回退或恢复基础数据之前传入目标实际已编译的 schema IDs。工具使用生产 `Config::parse/effective`，检查 Windows 默认有效方案及每个 EXE Profile，包含 `enabled=false` 的 profile：当前 Provider 初始化仍要求其方案存在。`platform.windows.schema` 覆盖 default 的规则保持一致。不读取运行期输入状态，也不写任何配置。

成功顶层响应含 `valid:true, checked:[{executable,schema,enabled}]`。默认项 executable 为 `<default Windows>`。缺失方案返回 `kind:"missing_schema"`、退出码 2，error 明确列出应用、enabled 状态和缺失 ID；平台不得因此继续发布，用户应先协调配置再重试。没有配置文件时检查内置默认 `pinyin_simp`，不会创建文件。

## Rime custom YAML

### yaml.read / yaml.write / yaml.validate

读写只允许文件名以 `.custom.yaml` 结尾，不接受 `.schema.yaml` 或 `.dict.yaml`。GUI 使用 MYIME 独立 patches 目录，不直接改安装目录基础数据。

```json
{"version":1,"command":"yaml.read","path":"D:\\fixture\\patches\\default.custom.yaml"}
```

```json
{"version":1,"command":"yaml.write","path":"D:\\fixture\\patches\\default.custom.yaml","expected":{},"text":"# keep this\npatch:\n  menu/page_size: 7\n  future/value: true\n"}
```

```json
{"version":1,"command":"yaml.validate","text":"patch:\n  menu/page_size: 7\n"}
```

`read/write` 响应含 revision、text、backup_path、valid、validation_error。`write` 使用维护中的 `serde_yaml_ng` 验证单文档 mapping；空文件可表示没有 patch。验证后保存用户原文，不用序列化器重新排布、丢掉注释或删除未知 native 配置。语法验证不能替代 librime 实际部署和 schema 语义验证。

### yaml.patch.preview

```json
{"version":1,"command":"yaml.patch.preview","page_size":7}
```

响应 `text` 是独立预览 patch，`requires_deploy:true`。page_size 范围 1–100，属于 Rime 引擎分页配置，候选窗口不按展示容量重排。该命令不写文件；保存为隔离 patches 目录的 `default.custom.yaml` 后，需使用独立部署工具构建并发布成功代际。不能把任意命名的 `.myime.custom.yaml` 当成已经被 Rime 读取的配置。

## 用户主题编辑

`theme.read {path}` 和 `theme.write {path,expected,text}` 共用 revision 和原子备份规则。路径必须以 `themes/<有效主题ID>/theme.toml` 结束；写入使用生产 `Theme::parse` 验证，metadata.id 必须等于父目录名，上限 64 KiB。完整 TOML 原文保存，不丢未知字段或注释；坏文件 read 返回 valid:false 可修复。设置程序只允许保存到用户目录，内置主题读取后应创建新 ID 另存。

`theme.template {id,name?}` 从同一内置默认主题资产生成完整可编辑模板；不创建目录或文件。GUI 可先拿新路径的缺失 revision，再保存模板。

```json
{"version":1,"command":"theme.template","id":"mytheme","name":"我的主题"}
```

## 导入

`import.preview` 和 `import.write` 返回值包在 `result` 内。输入为 UTF-8 TXT/CSV；先 preview，再把 `result.input_revision` 传给 write。生成的是新的数据词库包，不能覆盖已有同名包，不触碰 userdb，不默认启用。

```json
{"version":1,"command":"import.preview","input_path":"D:\\fixture\\words.csv","format":"csv","source":"自己的词表"}
```

```json
{"version":1,"command":"import.write","input_path":"D:\\fixture\\words.csv","format":"csv","input_revision":{},"packages_root":"D:\\fixture\\dictionaries","id":"my_words","name":"我的词表","version_name":"1.0.0"}
```

完整列定义、频率合并、无 code 词条处理和生成包格式见 [importing.md](importing.md)。

### dictionary.combine

```json
{"version":1,"command":"dictionary.combine","shared_dir":"D:\\fixture\\rime\\workspaces\\g-new\\shared"}
```

供原生离线部署工具使用。只处理给定绝对目录中已显式部署的 `myime_<id>.dict.yaml` 及其原始 `<id>.dict.yaml`、单包 schema；不扫描未启用的用户词库包目录。保留来源和单包方案，按安全 package ID 排序生成 `myime_global.dict.yaml` 和 `myime_global.schema.yaml`，import_tables 首项为 `pinyin_simp`，随后为所有包 ID。明确保留 `myime_global.custom:/patch?` custom hook。固定 `global` 包 ID 被保留，防止与产品产物冲突。

结果包在 `result:{schema:"myime_global",packages:[...]}` 内；没有已启用包时 schema 为 `pinyin_simp`，不写聚合文件。上限 64 个包；拒绝相对目录、符号链接、缺失来源和已经存在 ready 标记的已发布 workspace。两个文件使用修订安全原子写入，整个 workspace 的发布由原生工具在官方 Rime 部署和验证成功后完成，不能在正在使用的数据目录上直接合并。

## 构建和测试

工具单独构建以避免 Cargo feature 合并把原生 Rime 带进维护程序，也避免与 Native Core DLL 的输出路径混用：

```powershell
cargo build -p myime-tool --release --locked --target-dir target/maintenance-release
cargo test -p myime-core --no-default-features --lib --locked --target-dir target/maintenance-tests
cargo test -p myime-tool --locked --target-dir target/maintenance-tests
```

测试创建新的临时 fixture 目录；不会读写真实 `%LOCALAPPDATA%/MYIME/config.toml` 或用户词库。覆盖外部修改 hash/mtime、备份、进程锁释放、未知值/注释保留、继承清除、inline profiles、协议大小/版本、坏文件原文恢复和 YAML 白名单。真实 GUI 接入和数据部署另有独立测试。
