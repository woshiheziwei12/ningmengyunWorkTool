# 柠檬云工作助手命令 API

在 CLI 所在目录打开 PowerShell 7，或用完整路径调用 `LemonWorkToolCli.exe`。程序通过标准输出返回一个 UTF-8 JSON 对象。加 `--pretty` 可以展开显示。脚本应读取退出码和 JSON 字段。

```powershell
.\LemonWorkToolCli.exe help
.\LemonWorkToolCli.exe help wecom read --pretty
.\LemonWorkToolCli.exe help access convert --pretty
```

`help` 列出当前程序支持的命令，包括已有的登录和账套操作。`help <命令>` 提供下列命令的参数和示例。旧命令的设计说明见 [CLI 实施计划](../CLI_IMPLEMENTATION_PLAN.md)。

## 命令、参数与示例

<!-- BEGIN CLI REFERENCE -->

### `auth cookie`

读取 CLI 会话或后台工作台当前登录的柠檬云 Cookie。

`auth cookie [--source session|desktop] [--reveal] [--http-env Pro|Dev2] [--timeout-ms MS]`

- `--http-env` desktop 模式可指定预期环境 Pro 或 Dev2，工作台环境不匹配时报错。
- `--reveal` 在本次 JSON 的 data.ningmengCookie 返回完整值。默认脱敏。
- `--source` session 读取 CLI 登录会话（默认），desktop 读取当前工作台。
- `--timeout-ms` desktop 模式的等待上限，默认 5000 毫秒。

```powershell
.\LemonWorkToolCli.exe auth cookie
.\LemonWorkToolCli.exe auth cookie --source desktop --http-env Dev2
```

### `wecom status`

检查企业微信使用桌面接口还是独立读取模式。

`wecom status [--timeout-ms MS]`

- `--timeout-ms` 等待上限，默认 5000 毫秒。

```powershell
.\LemonWorkToolCli.exe wecom status
```

### `wecom documents list`

列出当前表格清单中的 ID 和显示名称。

`wecom documents list [--timeout-ms MS]`

- `--timeout-ms` 等待上限，默认 5000 毫秒。

```powershell
.\LemonWorkToolCli.exe wecom documents list
```

### `wecom read`

读取已保存的文档或临时访问企业微信表格链接。新链接默认读取全部页签。

`wecom read [--document ID | --url URL | --url-stdin] [--from-date YYYY-MM-DD] [--timeout-ms MS] [--legacy-json]`

- `--document` 清单中的 ID。未指定文档或链接时使用固定进度表 project-progress。
- `--from-date` 日期范围模式的起始日期，含当天。默认本机今天，允许今天或未来。
- `--legacy-json` 供原 PowerShell 调用使用，输出 v1 JSON，退出码为 0 或 2。
- `--timeout-ms` 等待上限，默认 310000 毫秒。
- `--url` 手动访问完整企业微信表格链接。同一个文档 ID 已保存时沿用清单策略。
- `--url-stdin` 从标准输入读取一行 UTF-8 链接，适合含分享码的链接。

```powershell
.\LemonWorkToolCli.exe wecom read --document project-progress
.\LemonWorkToolCli.exe wecom read --url 'https://doc.weixin.qq.com/sheet/new-sheet-id'
$link | .\LemonWorkToolCli.exe wecom read --url-stdin
```

### `wecom login`

桌面工具退出后，打开官方授权窗口并保存已验证的扫码会话。

`wecom login [--document ID | --url URL | --url-stdin] [--timeout-ms MS]`

- `--document` 用于验证权限的清单 ID，默认固定项目进度表。
- `--timeout-ms` 等待上限，默认 180000 毫秒。
- `--url` 用完整企业微信表格链接验证扫码授权。
- `--url-stdin` 从标准输入读取一行 UTF-8 链接，用于验证扫码授权。

```powershell
.\LemonWorkToolCli.exe wecom login
```

### `packages target`

核对后台工作助手此环境的实际专业版导账地址。

`packages target --http-env Pro|Dev2 [--timeout-ms MS]`

- `--http-env` 必填，须与工作助手当前环境一致。
- `--timeout-ms` 单次查询默认等待 35000 毫秒。

```powershell
.\LemonWorkToolCli.exe packages target --http-env Dev2
```

### `packages upload`

用后台工作助手的当前登录会话提交专业版账套 ZIP，返回导入任务 ID。

`packages upload --http-env Pro|Dev2 --input FILE [--expected-importer ADDRESS] [--timeout-ms MS] [--as-name NAME] [--as-id ID] [--zip-file-name NAME] [--edition professional]`

- `--as-id` 账套标识，用于业务日志。
- `--as-name` 账套名称，默认 ZIP 文件名。
- `--edition` 可省略，支持 pro 或 professional。
- `--expected-importer` packages target 返回的完整地址。实际地址变化时拒绝上传。
- `--http-env` 必填。Pro 为正式环境，Dev2 为测试环境，须与工具当前登录环境一致。
- `--input` 要上传的本地 ZIP 文件。
- `--timeout-ms` CLI 等待受理响应的上限，默认 310000 毫秒。
- `--zip-file-name` 上传表单中的文件名，不含 .zip，默认取文件名。

```powershell
.\LemonWorkToolCli.exe packages upload --http-env Dev2 --input 'D:\input\账套.zip'
.\LemonWorkToolCli.exe packages upload --http-env Pro --input 'D:\input\账套.zip'
```

### `packages status`

通过提交时的导账地址查询指定任务，返回实际业务结果。

`packages status --http-env Pro|Dev2 --import-id ID [--wait] [--timeout-ms MS]`

- `--http-env` 必填。须与工作助手及导入任务的环境一致。
- `--import-id` 上传返回的导入任务 ID。
- `--timeout-ms` 单次查询默认 35000 毫秒。带 --wait 时默认共等待 300000 毫秒。
- `--wait` 轮询到结果返回，间隔 3 秒。

```powershell
.\LemonWorkToolCli.exe packages status --http-env Dev2 --import-id '任务ID' --wait
```

### `bak inspect`

通过后台工作助手读取指定恢复链路的备份清单。

`bak inspect --input FILE|DIR [--platform PLATFORM] [--instance INSTANCE] [--staging-dir DIR] [--timeout-ms MS]`

- `--input` 与所选平台匹配的备份文件或目录。通用 BAK 在目录内递归查找 .bak。
- `--instance` 平台恢复可在读取时指定本机实例。默认使用 .，或唯一发现的命名实例。通用 BAK 在 restore 指定。
- `--platform` bak（默认）、t3、u8、tplus、kis-pro、inspur、xinzhongda、suda-3000-pro、suda-3000-std、suda-v3-industrial、suda-v3、suda-v5、suda-v7、suda-5000-pro、suda-7000。
- `--staging-dir` SQL Server 服务可读取的暂存目录，默认沿用工作助手配置。
- `--timeout-ms` 等待清单的上限，默认 310000 毫秒。

```powershell
.\LemonWorkToolCli.exe bak inspect --input 'D:\Backup\accounting.bak'
.\LemonWorkToolCli.exe bak inspect --platform t3 --input 'D:\Backup\UFData.BA_'
```

### `bak restore`

将选定的备份恢复为本机 SQL Server 新库，平台链路同时核对可选账套。

`bak restore --catalog-id ID --set N --instance INSTANCE --database NAME [--wait] [--timeout-ms MS]`

- `--catalog-id` bak inspect 返回的 catalogId。
- `--database` 要新建的目标数据库名称。
- `--instance` 清单中的本机 SQL 实例，例如 . 或 .\SQLEXPRESS。平台恢复须与读取实例一致。
- `--set` 清单中的 set 编号，从 1 开始。
- `--timeout-ms` 受理等待默认 5000 毫秒，带 --wait 时总等待默认 310000 毫秒。
- `--wait` 等待恢复结果，省略时返回任务 ID。

```powershell
.\LemonWorkToolCli.exe bak restore --catalog-id $catalog.data.catalogId --set 2 --instance '.' --database 'Accounting_Restored' --wait
```

### `bak status`

读取当前工作助手保留的 BAK 恢复任务状态。

`bak status [--job-id ID] [--timeout-ms MS]`

- `--job-id` bak restore 返回的 jobId，省略时查最近任务。
- `--timeout-ms` 单次查询默认等待 5000 毫秒。

```powershell
.\LemonWorkToolCli.exe bak status --job-id $restore.data.jobId
```

### `access convert`

批量转换 AIS、AIY、MDB，在输出目录生成 Access 2002–2003 格式文件。

`access convert --input PATH [--input PATH ...] --output-dir DIR [--mode MODE] [--no-recursive] [--password-stdin]`

- `--input` 输入文件或目录，可重复。目录默认递归扫描。
- `--mode` recovery 为默认数据恢复模式，access2003 使用旧版 Access 转换。
- `--no-recursive` 目录只扫描当前层。
- `--output-dir` 输出目录，缺失时自动创建。
- `--password-stdin` 仅 access2003 模式，从标准输入读取一行 UTF-8 密码。

```powershell
.\LemonWorkToolCli.exe access convert --input 'D:\input\sample.mdb' --output-dir 'D:\output'
.\LemonWorkToolCli.exe access convert --input 'D:\input\batch' --output-dir 'D:\output' --no-recursive
```

<!-- END CLI REFERENCE -->

## 柠檬云 Cookie

`auth cookie` 默认读取 CLI 自己保存的会话。后台技能要使用工作台当前登录态时，显式传 `--source desktop`。同一个 Windows 用户需打开新版工具并登录到工作台，随后可以将工具缩到托盘。每次命令从当前会话取值，工作台续期后下一次调用会读到新值。

```powershell
# 在后台测试中先核对目标环境，避免误用正式会话。
$reply = .\LemonWorkToolCli.exe auth cookie --source desktop --http-env Dev2 --reveal | ConvertFrom-Json
if (-not $reply.ok) { throw $reply.error.message }
$cookie = $reply.data.ningmengCookie
```

`--http-env Dev2` 校验测试环境，改为 `Pro` 校验正式环境。省略时返回值仍包含实际 `data.environment`，调用方可核对。默认结果脱敏，`--reveal` 只在本次 stdout 的 JSON 内返回完整值。自动化将命令输出直接解析并在本进程中使用，避免回显 JSON 或将完整值写入日志。

工具未启动或停在登录界面时，工作台通道尚未就绪，CLI 返回 `TOOL_NOT_RUNNING`。通道已就绪而当前会话取不到 Cookie 时返回 `AUTH_REQUIRED`。环境切换中返回 `BUSY`，指定环境与实际不符返回 `ENVIRONMENT_MISMATCH`。

同一 Windows 用户同时运行多个工作台时，CLI 连接最先取得 Auth 通道的实例。`--http-env` 校验该实例的环境。需要改用另一实例时，退出当前提供通道的工作台，再重新启动并登录目标工作台，由它注册通道。

## 企业微信调用流程

1. 用 `wecom status` 查看运行方式。`data.mode=desktop` 表示工作台的 Windows 本机命名管道可用，`data.mode=standalone` 表示 CLI 将按需启动读取组件。`data.running` 表示桌面接口是否在线。`savedSession` 只表示本机有会话文件，授权有效性以联网读取为准。状态查询无需启动 WebView2。
2. 用 `wecom documents list` 查表格清单。将 `data.documents[].id` 传给 `wecom read --document`。桌面工具退出后，CLI 优先读取自己的 EXE 同目录下的 `WecomDocuments.json`，缺失时使用同一个 Windows 用户目录，再回退到内置进度表。GUI 和 CLI 放在同一目录时会共享其中的清单。省略 ID 或用 `project-progress` 时，继续读取固定运营支持组项目进度表。
3. 用 `wecom read` 获取最新数据。工作台在线时复用本机管道，退出后在 CLI 进程内启动隐藏的 WebView2 读取组件，复用当前 Windows 用户的浏览器配置和 DPAPI 加密授权，命令完成后释放组件。读取仍通过企业微信官网并需要网络。
4. 调用 `wecom read` 后检查 `data.complete`、`data.cancelled` 和各工作表的 `loaded`、`error`。成功返回 `sheets`、实际 `documentId` 和 `fetchedAt`。日期范围模式返回 `fromDate`。清单标记为 `latest-date` 的文档按页签名称日期只读取一页，返回 `sheetSelection` 和该页的 `sheets[0].date`。每次重新联网读取，没有匹配的工作表时 `sheets` 为空数组。清单损坏时，列表、按 ID 和手动链接入口都会返回 `CATALOG_ERROR`。原固定进度表入口可用内置绑定继续读取。

手动访问支持 `wecom read --url` 和 `wecom read --url-stdin`。把完整分享链接作为参数，或通过标准输入提供。用链接中的文档 ID 查找已有配置，新文档会返回 `sheetSelection=all` 并按原顺序读取全部页签。手动读取只获取数据，保存到列表由桌面操作完成。`--from-date` 用于文档本身的日期范围策略。

```powershell
$link = Read-Host '粘贴完整企业微信表格链接'
$link | .\LemonWorkToolCli.exe wecom read --url-stdin
```

预置清单中的站会总结表使用 `latest-date`。用下列命令按页签名称读取日期最大的一页。

```powershell
.\LemonWorkToolCli.exe wecom read --document e3_ARwAqwY-AAkCNsNndJmHGR5K578aV
```

记账系统项目进度表使用 `from-date-in-name` 策略。从页签名称中提取日期，读取今天及之后的所有匹配页签。例如 `记账-11-26（上线）` 按 11 月 26 日处理。结果同时返回 `data.sheetSelection`、`data.fromDate` 和 `sheets[].date`。传入 `--from-date` 可指定今天或更晚的起始日。

```powershell
.\LemonWorkToolCli.exe wecom read --document e3_AKcA1gZhAA0CNHYIuso0NR2O0Y8CZ
```

独立模式返回 `LOGIN_REQUIRED` 时，在交互式 Windows 用户会话中运行 `error.details.loginCommand` 指定的 `wecom login` 命令。手动链接使用原链接作为参数或标准输入。扫码授权通过并加密保存后返回 `data.saved=true`，随后可重试读取。工作台在线时可直接在桌面工具扫码。独立读取需要 Windows 图形会话和 WebView2 Runtime，首次授权或授权失效时由账号本人扫码。

`--timeout-ms` 范围为 1 至 600000。桌面端单次读取最多等待五分钟，调用方提前超时后，已有任务可能仍在运行。原 PowerShell 脚本的参数、固定表格、结果格式和退出码继续兼容。脚本和新版 CLI 放在同一目录时，也能在桌面工具退出后读取。详见 [企业微信文档读取说明](wecom-document-reader.md)。

## Access 调用流程

CLI 可单独运行。默认恢复模式由同目录的 `LemonJetRecovery.exe` 处理 Jet3，Jet4 直接复制。Jet3 恢复需要 Windows 的 32 位 Jet/DAO 3.6。旧版转换需要 Access 2003，本机未安装时会给出逐文件错误。

转换保留源文件。同名结果使用编号后缀。目录批量处理时，建议使用独立的输出目录，便于下次扫描只包含输入。全部成功时从 `data.items` 读取逐文件结果。每个结果包含 `input`、`output`、`ok`、`copied`、`recovered`、`recoveredTables`、`recoveredRows`、`report`、`warnings` 和 `error`。混合输入中的空目录或空路径会单独记录为失败，有效输入继续处理。有失败时从 `error.details.items` 读取原结构，已成功的文件可以继续使用。Jet3 恢复结果用于查询和导账，支持范围见 [Access 验证说明](../Tests/README-Access2003Conversion.md)。

旧版转换可以从标准输入读取密码。省略密码或输入空行时，沿用 GUI 的默认密码策略。下面的 PowerShell 7 示例使用遮挡输入。

```powershell
(Read-Host '账套密码' -MaskInput) | .\LemonWorkToolCli.exe access convert --input 'D:\input\sample.ais' --output-dir 'D:\output' --mode access2003 --password-stdin
```

## 专业版账套 ZIP 上传

在同一 Windows 用户下登录新版工作助手并进入工作台。工作台缩到托盘后，CLI 仍可通过本机命名管道使用当前登录会话和选定套餐。上传和查询都显式传入 `--http-env Pro`（正式）或 `--http-env Dev2`（测试）。指定环境和工作台环境不同会返回 `ENVIRONMENT_MISMATCH`，并在 `error.details` 中给出两个环境。

```powershell
$expected = 'https://proimportertest2.ningmengyun.com'
$target = .\LemonWorkToolCli.exe packages target --http-env Dev2 | ConvertFrom-Json
if ($target.ok -and $target.data.importerAddress -ceq $expected) {
    $upload = .\LemonWorkToolCli.exe packages upload --http-env Dev2 --input 'D:\input\账套.zip' --expected-importer $target.data.importerAddress | ConvertFrom-Json
    if ($upload.ok) {
        .\LemonWorkToolCli.exe packages status --http-env Dev2 --import-id $upload.data.importId --wait
    }
}
```

`packages target` 是只读查询，返回 `data.environment` 和 `data.importerAddress`。将已确认的地址与查询结果核对，通过后把返回的原始地址传给 `--expected-importer`。提交时地址变化会返回 `IMPORTER_MISMATCH`。

上传受理返回 `data.status=accepted`、`data.importId`、`data.importerAddress` 和 `data.historySaved`。CLI 查询使用该任务保存的导账地址，并校验环境、账号和套餐。桌面历史列表使用同一范围。历史只保留最近 100 条，旧版未记录归属的任务可在工作助手切换到提交时的环境，选择对应地址并手动输入 ID 查询。

查询成功时 `data.result` 是匹配任务的服务端结果。同步查询的退出码为 0 表示查询成功，业务结果由 `data.status` 判断。

| `data.status` | 含义 |
| --- | --- |
| `running` | 对应任务的结果尚未返回，`finished=false` |
| `succeeded` | 导入成功 |
| `partial` | 导入部分成功，原始错误详情见 `result` |
| `failed` | 导入失败，原因见 `result` |
| `action_required` | 服务端结果提示需要人工处理 |
| `unknown` | 已收到该任务结果，返回字段不足以判定业务成功或失败 |

使用 `--wait` 可等到结果返回。等待超时可用原 `importId` 继续查询。若上传返回 `UPLOAD_STATE_UNKNOWN`，先核对工作助手中的导入记录，再决定是否重传。若受理成功且 `historySaved=false`，同一工作台进程仍可用返回的任务 ID 查询。保持该进程运行并保留任务 ID，直到获取导入结果。

## 本机 SQL Server BAK 恢复

进入新版工作助手的工作台后，可以缩到托盘。用同一 Windows 用户运行 CLI，恢复使用 Windows 身份验证和本机 32 位 SQL Server ODBC 驱动。

```powershell
$catalog = .\LemonWorkToolCli.exe bak inspect --input 'D:\Backup\accounting.bak' | ConvertFrom-Json
$catalog.data.entries | Format-Table set, position, database, sourceVersion, restorable, error
$catalog.data.localInstances | Format-Table instance, version

# 将 2、. 和 Accounting_Restored 改为已确认的清单编号、本机实例和新库名。
if ($catalog.ok) {
    .\LemonWorkToolCli.exe bak restore --catalog-id $catalog.data.catalogId --set 2 --instance '.' --database 'Accounting_Restored' --wait
}
```

- `bak inspect` 读取一个文件或目录，返回 `catalogId`、读取实例、发现的本机实例及备份集清单。`set` 是清单中的行编号，`position` 是该备份集在原文件中的位置。多个 BAK 的位置可以相同。不可读的文件也列出并附原因。
- 选择 `restorable=true` 的行，再明确指定 `--instance` 和 `--database`。实例支持 `.` 或 `.\实例名`。恢复前重新核对文件身份、实际 SQL Server 版本、目标库同名和 `VERIFYONLY`。版本不兼容、源文件变更或目标库已存在时失败，既有数据库和源 BAK 受保护。
- `bak restore` 返回 `jobId` 和任务状态。带 `--wait` 时等待终态，成功需满足 `data.status=succeeded`。恢复失败时，退出码为 5，任务和逐库结果在 `error.details`。如果等待超时，可用 `bak status --job-id ID` 继续查询。状态查询的退出码为 0 表示查到了任务，恢复结果以 `status` 为准。
- 清单和任务状态保留在当前工作台内。重新读取清单后使用新的 `catalogId`。工作台保留最近 16 个恢复任务，退出或切换工作台后重新读取清单。CLI 等待超时后已受理的任务继续在工作台执行。

### 按平台恢复并检查账套列表

```powershell
$catalog = .\LemonWorkToolCli.exe bak inspect --platform t3 --input 'D:\Backup\UFData.BA_' | ConvertFrom-Json
if ($catalog.ok) {
    $sets = @($catalog.data.entries | Where-Object restorable)
    if ($sets.Count -eq 1) {
        .\LemonWorkToolCli.exe bak restore --catalog-id $catalog.data.catalogId --set $sets[0].set --instance $catalog.data.readerInstance --database $sets[0].recommendedDatabase --wait
    }
}
```

- `bak inspect --platform` 选择平台入口。平台恢复要求 `restore --instance` 与清单的 `readerInstance` 一致。省略读取实例时优先选择本机默认实例 `.`，其次是唯一的命名实例。缺少明确目标时，从错误中的 `localInstances` 选择后重新读取。通用 BAK 清单也提供 `defaultInstance` 供调用方选择。
- 平台清单的 `recommendedDatabase` 通常是源库名。T3 和 U8 库名保留库类型和年度，账套号使用三位数字。U8 选择标记 `includesAllSets=true` 的首个业务库行，会一起恢复同包的其他业务库及元数据库。
- 速达支持平台备份包或解包后的原生 `.bak`。所选版本决定目标库名筛选及账套表读取，清单用 `databaseNamePattern` 返回筛选条件。3000 Pro 识别以 `CWACCSET` 或 `SDAccset` 结尾、包含 `ACCSET.ACCSETNAME` 的账套库。新中大目标库名为 `USER` 加单位编号和备份年度。
- 平台结果的 `accountPickerVerified=true` 和 `accounts` 表示工作台用当前 Windows 用户在目标实例确认了数据库可访问、账套列表可查询。若库已经恢复但账套列表验证失败，失败结果仍保留 `items[].databaseRestored`，便于核对并保留已恢复的库。通用 BAK 只验证数据库恢复。

## JSON 和退出码

成功时 `ok` 为 `true`，退出码为 0，业务结果在 `data`。失败时 `ok` 为 `false`，`data` 为 `null`，根据 `error.code` 判断原因，`error.message` 可显示给用户。

| 错误码 | 退出码 | 处理方式 |
| --- | --- | --- |
| `ARGUMENT_ERROR`、`INVALID_DATE`、`INVALID_DOCUMENT`、`DOCUMENT_NOT_FOUND` | 1 | 检查必填参数、日期或清单 ID |
| `BAK_CATALOG_NOT_FOUND`、`INVALID_BACKUP_SET`、`INVALID_SQL_INSTANCE`、`INVALID_PLATFORM`、`INSTANCE_MISMATCH` | 1 | 重新读取备份清单，选择对应平台、可恢复的备份集和本机实例 |
| `FILE_ERROR`、`CONVERSION_START_FAILED`、`CATALOG_ERROR` | 2 | 检查输入、输出目录或表格清单 |
| `LOGIN_REQUIRED`、`ACCESS_DENIED`、`SESSION_SAVE_FAILED` | 3 | 使用同一 Windows 用户，按提示扫码或检查授权保存 |
| `AUTH_REQUIRED`、`ENVIRONMENT_MISMATCH` | 3 | 登录指定的正式或测试工作台 |
| `IMPORTER_MISMATCH` | 3 | 停止上传，重新核对目标环境和实际导账地址 |
| `TOOL_NOT_RUNNING`、`READ_TIMEOUT`、`TOOL_DISCONNECTED`、`CONNECTION_FAILED` | 4 | 查看桌面工具状态，等在途任务结束后再读 |
| `UPLOAD_STATE_UNKNOWN`、`RESULT_WAIT_TIMEOUT` | 4 | 核对受理记录，或用原任务 ID 继续查询 |
| `RESTORE_STATE_UNKNOWN` | 4 | 先查 `bak status`，并核对目标库状态 |
| `BUSY`、`READ_FAILED` | 5 | 等待当前任务结束或查看具体错误 |
| `UPLOAD_FAILED`、`QUERY_FAILED`、`IMPORT_NOT_FOUND`、`IMPORTER_UNAVAILABLE` | 5 | 查看服务端错误，核对任务归属和导账地址 |
| `BAK_INSPECT_FAILED`、`BAK_RESTORE_FAILED`、`RESTORE_JOB_NOT_FOUND` | 5 | 查看错误详情，检查 SQL 实例、版本、文件权限或目标库 |
| `PARTIAL_CONVERSION`、`CONVERSION_FAILED` | 6 | 查看 `error.details.items` 的逐文件结果 |
| `RESPONSE_TOO_LARGE`、`INVALID_RESPONSE` | 6 | 检查桌面端版本或从桌面端导出 |
| `UNSUPPORTED_ACTION`、`UNSUPPORTED_VERSION` | 8 | 更新到匹配的桌面工具和 CLI |

## 更新手册

修改命令后，构建 CLI 并运行以下命令更新参考区。加 `-Check` 可检查参考区是否和当前 CLI 一致。运行前提、结果字段和错误处理在同一份手册里维护。

```powershell
pwsh -NoProfile -File Tools/Update-CommandApi.ps1 -CliPath 'D:\path\LemonWorkToolCli.exe'
```
