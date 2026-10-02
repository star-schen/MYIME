# TXT / CSV 词库导入

导入在独立工具进程完成，路径是 `Importer → ImportedWord → Normalize → Rime 数据包`。不会访问 Rime 用户数据库、猜测拼音、修改第三方 schema，或按应用 EXE 自动启用词库。CSV 使用成熟 `csv` crate 解析，另有严格引号语法检查，不使用逗号字符串拆分。

## 输入格式

输入只接受 UTF-8，可带 UTF-8 BOM；GBK、UTF-16 等文件需先转换。上限为 16 MiB、100,000 个数据行；包含来源和记录开销的展开中间数据上限为 32 MiB（例如为每个词条复制很长的来源会触发此限制）。预览最多显示 100 个去重词条，但导出处理所有有效行。

TXT 每行是以下三列，以真正的 TAB 分隔。第一列必需，后两列可省略。空行和以 `#` 开头的注释行忽略。

```text
你好<TAB>ni hao<TAB>100
战舰世界<TAB>zhan jian shi jie<TAB>20
仅文字
```

CSV 第一行是列名；必需 `word`，可选 `code`、`frequency`、`source`，顺序任意。列名区分大小写，重复或未知列名报错。支持标准双引号包裹、逗号、`""` 转义双引号及引号内换行；行数不匹配、未闭合引号或引号后非法字符会返回实际物理行号。来源 `source` 可以带引号内换行，词条及编码不能带换行、TAB、其他控制字符。

```csv
word,code,frequency,source
你好,ni hao,100,手工整理
"含,逗号的词",ci,5,"来源,含逗号"
无编码,,,待补充
```

词条去掉首尾空白，编码去掉首尾空白并合并连续 ASCII 空格。词条和编码各最多 1024 字节；词条不能空白或以 Rime 注释标记 `#` 开头。频率只接受 `u64` 非负整数；不接受负数、小数或百分比。缺省频率在导出时为 1。来源最多 4096 字节，只允许换行、回车、TAB 三种控制字符（只能进入 JSON metadata），拒绝 NUL 等其他控制字符。

同一个 `(word, code)` 的重复记录只保留一条：频率取最大值，来源取按字典序最小的非空来源，不叠加频率；按词条和编码稳定排序。同一词的不同编码会分别保留。这些规则不改变 Rime 的候选排序算法。

没有编码的词条仍保留在中间结果和包内 metadata.json，但不写入本轮生成的字典。工具会明确报告数量，不会自行给文字生成拼音。所有词条都没有编码时拒绝生成字典。请使用目标 Rime 基础词典的完整码表编码；双拼方案通常引用全拼词典编码，而不是将屏幕上按键字符串当作词典编码。

## 预览与生成

设置程序的词库导入页先预览，再生成。预览记录输入文件的 mtime 和 SHA-256；输入文件改变后生成会拒绝并提示重新预览。包 ID 以小写 a-z 开头，后面只接受 a-z、0-9、下划线，最多 64 字节；`pinyin_simp`、`stroke`、`global` 和 `myime_*` 为保留名称。目录已经存在时拒绝覆盖。

离线 JSON CLI 通过 `myime-tool.exe --request` 的标准输入接收 UTF-8 JSON（不是文件名参数）。每次调用只有一个 JSON 响应，成功是 `{ "version": 1, "ok": true, "result": ... }`，失败返回 `kind` 与错误消息。设置程序负责标准输入和编码；命令行调用方式见维护协议说明。

预览请求：

```json
{
  "version": 1,
  "command": "import.preview",
  "input_path": "D:\\MyWords\\words.csv",
  "format": "csv",
  "source": "我的词库"
}
```

生成请求携带预览响应的整个 `result.input_revision`，额外字段：

```json
{
  "version": 1,
  "command": "import.write",
  "input_path": "D:\\MyWords\\words.csv",
  "format": "csv",
  "source": "我的词库",
  "input_revision": {
    "exists": true,
    "modified_unix_ms": "从预览复制",
    "hash": "从预览复制"
  },
  "packages_root": "D:\\MyWords\\packages",
  "id": "my_words",
  "name": "我的词库",
  "version_name": "1.0.0"
}
```

`version_name` 默认 1.0.0。所有输入、输出根路径必须是绝对路径。生成后的文件：

```text
packages/my_words/
  manifest.json                # 包类型、ID、版本、文件名及 SHA-256/字节数
  my_words.dict.yaml           # 官方 Rime YAML header + text/code/weight TSV
  myime_my_words.dict.yaml     # 聚合 pinyin_simp 与 my_words 字典
  myime_my_words.schema.yaml   # MYIME 自有派生 schema，不覆盖原 schema
  metadata.json                # 来源、全部中间词条、缺码和重复统计
  README.md                    # 编码与显式启用说明
```

manifest.json 最后写入，作为完整包标记。它记录五个 payload 文件的 SHA-256 和字节数（不对自身循环求 hash），并提供 `schema_id`、`generated_dictionary`、`generated_schema`。失败时只删除这次调用创建的文件和空目录；不会递归删除已有用户目录，也不会覆盖已经存在的包。

`package.validate` 请求 `{ "version": 1, "command": "package.validate", "path": "绝对包目录" }` 校验目录名与 ID、文件类型、大小、hash、词条与统计，以及派生字典/schema 是否符合 Rust 唯一模板；拒绝目录外符号链接和任意 schema 指令注入。平台工具会将包复制到隔离 workspace，验证完整副本后才交给官方 Rime 部署。校验成功响应的 `result` 是 `{ "path": ..., "manifest": ..., "summary": ... }`。Hash 用于识别修改和损坏；它不是第三方发布者的身份签名。

## 启用与原生 Rime 兼容

生成数据包不等于已经启用。通过设置中的显式隔离部署操作验证后，默认使用稳定的 `myime_global` 自有 schema，同时包含所有用户明确部署启用的包；部署第二包不会静默移除第一包。未部署的包不会自动被扫描启用。单包 `myime_<id>` 方案仍保留，供用户明确在 AppProfile 等配置中选择；窗口不会猜测应用词库。不能仅把字典文件放入目录就期望所有已有 schema 自动加载。

Rust `dictionary.combine` 在尚未发布的 workspace 生成全局聚合字典，基础 `pinyin_simp` 后是按 ID 排序的全部已启用包，并保留 `myime_global.custom.yaml` 用户 hook。平台只在官方 Rime 编译、schema 验证和 `config.check-schemas` 确认所有配置引用的方案存在后发布。回退不能把还被配置引用的方案从目标数据中移走；需先协调默认、Windows 和 EXE Profile，包括禁用的 profile（当前初始化仍创建 Provider）。

本轮派生方案只针对附带的 `pinyin_simp` 基础 schema：编码使用完整拼音，例如 `zhan jian shi jie`，不能使用双拼按键串、五笔码等其他码系。Rust 生成聚合字典的 `import_tables: [pinyin_simp, <id>]`，以及使用 `__include: pinyin_simp.schema:/` 的独立 schema；原始 YAML 的 `schema` mapping 直接提供 schema_id/name，内部再通过 `__include: pinyin_simp.schema:/schema` 保留基础 metadata（包括 stroke dependencies）。translator/dictionary 通过 `__patch` 设置。由于显式 `__patch` 会禁用 Rime 的自动 custom hook，我们将 `myime_<id>.custom:/patch?` 明确放进补丁列表，保留用户后续手改 YAML 的入口。这些数据模板统一由 Rust 维护，Windows 工具只复制、调用官方编译与管理平台目录。见 [Rime 官方 include / patch 规则](https://github.com/rime/home/wiki/Configuration)。

最初模板只在 `__patch` 中设置 schema ID。实际 librime 1.17 在编译 include/patch 之前，会先从原始文件直接读取 `schema/schema_id`；那种模板会被拒绝为 invalid schema definition。因此现在提供直接可读的 identity，同时通过嵌套 include 保留基础 metadata，并维持 custom hook。这是官方部署生命周期的要求，不需要修改 librime，也不影响跨平台数据包设计。模板校验拒绝旧的错误格式；旧测试包需重新生成。参见 [官方 SchemaUpdate 源码](https://github.com/rime/librime/blob/1.17.0/src/rime/lever/deployment_tasks.cc#L315)。

派生字典有独立的 Rime 学习身份；本轮不会把原基础方案的学习数据强行复制或合并到新字典。以后支持其他 base schema 以及用户词库协调时，应分别设计显式配置和 Rime 原生 merge 流程。

底层使用官方字典格式：YAML 的 `name`、`version`、`sort: by_weight`、`use_preset_vocabulary: false`、`columns: [text, code, weight]`，`...` 后为 TAB 分隔码表。保持 Rime schema、dictionary、用户 custom YAML 的边界；第三方 schema 和用户数据库均不由导入器写入。参见 [Rime 官方方案与码表说明](https://github.com/rime/home/wiki/RimeWithSchemata) 和 [官方导入建议](https://github.com/rime/home/wiki/UserGuide)。

本轮实现 TXT、CSV。百度、搜狗、QQ、微软拼音及二进制格式仍需要各自专门的 Importer；它们应输出统一中间模型，经检查和转换后生成数据包，不能直接操作 Rime userdb。缺码词的自动编码、字形转换等需要明确的 Converter 与 Rime 词典规则，不能由 GUI 或 Host 猜测。

## 自动测试范围

核心测试覆盖 UTF-8/BOM、CRLF、缺码、重复合并、CSV 逗号/引号/多行来源、严格报错行号、字节/行数上限、频率溢出、控制字符注入、新包写入、生成 template/custom hook、hash 校验、非法路径、非 regular 文件及拒绝覆盖，另有双包全局聚合精确模板、顺序、来源保留与已发布目录拒绝。CLI 测试覆盖预览后输入变更冲突、缺失文件、相对路径及必须先预览的约束；实际子进程测试用中文 CSV 完成预览、写包、包校验和冲突拒绝，也覆盖全局合并命令及 Windows/Profile/禁用状态的 schema 悬空检查。测试只使用临时 fixture，不打开真实用户词库。
