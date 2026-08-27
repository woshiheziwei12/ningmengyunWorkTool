# Lemon Work Tool CLI 版本完整实施计划

## 目标

在保留现有 Qt Widgets 图形界面的同时，新增一个独立命令行程序 `LemonWorkToolCli.exe`。CLI 必须覆盖当前软件已有入口和待实现入口，并把登录也作为一等命令提供。

硬性约束：

- 每个命令都只输出 JSON，不提供默认文本输出。
- 成功、失败、进度、警告都使用机器可读结构。
- CLI 与 GUI 复用同一套 Core 业务逻辑，避免同一功能维护两份实现。
- 所有网络命令都支持从会话文件读取登录态，也支持显式传入 `ningmengcookie` / JWT。
- 日志中必须脱敏 token、cookie、密码、手机号中间位。

当前 GUI 功能入口：

| GUI 功能 | CLI 命令 | 当前代码状态 |
| --- | --- | --- |
| 登录：账号密码 | `auth login-password` | 已实现于 `PwdLoginWidget` |
| 登录：短信验证码 | `auth sms-send` / `auth login-sms` | 已实现于 `SmsLoginWidget` |
| 登录：微信扫码 | `auth wx-qrcode` / `auth wx-poll` / `auth login-wechat` | 已实现于 `MicroMsgLoginWidget` |
| 打开柠檬云官网并注入 cookie | `auth open-web` | 已实现于主窗口 |
| 获取 ningmengcookie | `auth cookie` | 已实现于主窗口 |
| 删除账套 | `accountsets delete` | 已实现于 `DeleteAccountSetWidget` |
| 备份下载账套 | `accountsets backup` | 已实现于 `BackupAccountSetWidget` |
| 修复账套 | `repair run` | 已实现主体逻辑，仍绑 UI |
| 修改账套启用日期 | `repair set-start-date` | 已包含于修复流程，需要拆出 |
| 截取账套特定年份数据 | `extract-year run` | GUI 入口存在，功能待实现 |
| 科目编码冲突处理 | `subject-conflict resolve` | GUI 入口存在，修复流程已有部分逻辑 |
| 兄弟科目名称重复处理 | `sibling-name-dedupe run` | 修复流程已有部分逻辑，独立入口待实现 |
| 解析 JSON 查询数据 | `json-query query` | 已实现于 `JsonQueryWidget` |
| 科目编码添加分隔符 | `subject-code separate` | 已实现于 `SubjectCodeSeparatorWidget` |
| 上传压缩包专业版 | `packages upload` | 已抽出 `ImportPackageUploader` |

## 输出规范

所有命令最终在 stdout 输出一个 JSON 对象。不要在 stdout 输出普通文本、表格或提示语。

成功结构：

```json
{
  "ok": true,
  "command": "accountsets.list",
  "requestId": "20260615-120000-0001",
  "timestamp": "2026-06-15T12:00:00+08:00",
  "durationMs": 1234,
  "data": {},
  "warnings": [],
  "log": {
    "path": "D:\\app\\LemonWorkTool\\log\\worktool_cli_20260615_120000_000.log"
  }
}
```

失败结构：

```json
{
  "ok": false,
  "command": "packages.upload",
  "requestId": "20260615-120000-0002",
  "timestamp": "2026-06-15T12:00:00+08:00",
  "durationMs": 218,
  "data": null,
  "warnings": [],
  "error": {
    "code": "AUTH_REQUIRED",
    "message": "未登录或 access token 为空",
    "exitCode": 3,
    "details": {
      "httpStatus": 401,
      "apiCode": null
    }
  },
  "log": {
    "path": "D:\\app\\LemonWorkTool\\log\\worktool_cli_20260615_120000_000.log"
  }
}
```

长任务进度：

- 默认只输出最终 JSON。
- 可选 `--events`，每个进度事件输出 JSONL 到 stderr，stdout 仍只输出最终 JSON。
- 事件结构固定包含 `type`、`command`、`requestId`、`timestamp`、`data`。

示例：

```jsonl
{"type":"progress","command":"accountsets.backup","requestId":"...","timestamp":"...","data":{"asId":123,"asName":"测试账套","progress":60}}
```

退出码：

| 退出码 | 含义 |
| --- | --- |
| 0 | 成功 |
| 1 | 参数错误 |
| 2 | 文件或目录错误 |
| 3 | 登录或鉴权失败 |
| 4 | 网络错误或超时 |
| 5 | 服务端业务失败 |
| 6 | JSON/ZIP/数据处理失败 |
| 7 | 用户交互超时，例如扫码超时 |
| 8 | 功能尚未实现 |
| 9 | 未知异常 |

## 工程结构

建议在当前解决方案中新增 Console 工程，并把业务逻辑逐步抽到 Core。

```text
QtWidgetsApplication1/
  Common/
    globaldata.*
    httprequesthelper.*
    importpackageuploader.*
    lemonconfighelper.*
    logger.*
    usertoken.*
  Core/
    authservice.*
    sessionstore.*
    accountsetservice.*
    backupservice.*
    repairaccountsetservice.*
    jsonqueryservice.*
    subjectcodeseparatorservice.*
    subjectconflictservice.*
    siblingnamededupeservice.*
    yearsextractservice.*
    zipservice.*
    cliresult.*
    masking.*
  Cli/
    main.cpp
    clicommandrouter.*
    cliarguments.*
    jsonwriter.*
    progresssink.*
  Gui/
    现有 QWidget 页面逐步改为调用 Core
```

依赖原则：

- `Core` 可以依赖 QtCore、QtNetwork，但不能依赖 QWidget、QMessageBox、QTableWidget、QClipboard。
- `Gui` 只负责页面展示、按钮事件、弹窗确认和表格渲染。
- `Cli` 只负责参数解析、会话加载、调用 Core、输出 JSON、返回退出码。
- ZIP 操作优先封装为 `ZipService`，替换当前散落的 PowerShell 片段；第一阶段可以保留 PowerShell 实现，但必须集中封装。

## 会话与配置

默认配置目录：

```text
exe目录\config
```

文件建议：

```text
config\cli.json
config\sessions\default.json
```

`cli.json` 示例：

```json
{
  "env": "Pro",
  "defaultProfile": "default",
  "defaultOutputDir": "D:\\output",
  "backupOutputDir": "D:\\backup",
  "queryPackageDir": "JsonQueryPackages",
  "repairPackageDir": "RepairAccountSetPackages",
  "subjectCodePackageDir": "SubjectCodeSeparatorPackages",
  "networkTimeoutMs": 120000,
  "pollIntervalMs": 3000
}
```

`sessions/default.json` 示例：

```json
{
  "profile": "default",
  "createdAt": "2026-06-15T12:00:00+08:00",
  "updatedAt": "2026-06-15T12:00:00+08:00",
  "userId": 123456,
  "serviceId": 1,
  "appAsId": "xxx",
  "ningmengCookie": "encrypted-or-plain-token",
  "jwtToken": "encrypted-or-plain-token",
  "refreshToken": "encrypted-or-plain-token",
  "expiresIn": 7200
}
```

安全要求：

- 默认不保存密码。
- `--save-session` 只保存 cookie/JWT/refresh token/serviceId/appAsId。
- 支持 `--no-save-session`，只把登录结果输出到 JSON。
- 后续可用 Windows DPAPI 加密会话字段；第一阶段如使用明文，必须在计划和 README 中明确提示。
- 所有命令参数优先级：命令行参数 > 环境变量 > session > cli.json > 内置默认值。

推荐环境变量：

```text
LEMON_PROFILE
LEMON_NINGMENG_COOKIE
LEMON_JWT_TOKEN
LEMON_SERVICE_ID
LEMON_APP_AS_ID
```

## 全局参数

所有命令支持：

```powershell
LemonWorkToolCli.exe <command> [args] `
  --profile default `
  --config D:\path\cli.json `
  --session D:\path\session.json `
  --events `
  --pretty
```

说明：

- `--pretty` 只影响 JSON 缩进，不改变结构。
- `--events` 开启 JSONL 进度事件。
- `--profile` 指定会话名。
- `--session` 直接指定会话文件，适合 CI 或一次性脚本。

## 命令设计

### 基础命令

#### `version`

```powershell
LemonWorkToolCli.exe version
```

输出：

```json
{
  "ok": true,
  "command": "version",
  "data": {
    "name": "LemonWorkToolCli",
    "version": "1.0.0",
    "qtVersion": "5.x",
    "build": {
      "commit": "",
      "time": ""
    }
  }
}
```

#### `help`

```powershell
LemonWorkToolCli.exe help
LemonWorkToolCli.exe help --command accountsets.backup
```

输出 JSON 命令清单、参数、示例，不输出普通文本。

### 登录与认证

#### `auth login-password`

对应 GUI 账号密码登录。

```powershell
LemonWorkToolCli.exe auth login-password --username 13800000000 --password ****** --save-session
```

流程：

1. 调用 `signInUrl`，表单参数沿用 GUI：`client_id=123456`、`client_secret=abcdef`、`grant_type=password`。
2. 将返回的 `access_token` 作为 `ningmengcookie`。
3. 调用 `authHost + /v2/api/jwt` 获取 JWT。
4. 解析 JWT payload 中的 `sub` 为 `userId`。
5. 调用 `wbgateMenuUrl` 或 `/wb/plan` 获取 `serviceId` 与 `appAsId`。
6. 按 `--save-session` 写入 session。

输出字段：

```json
{
  "data": {
    "profile": "default",
    "userId": 123456,
    "serviceId": 1,
    "appAsId": "xxx",
    "hasNingmengCookie": true,
    "hasJwtToken": true,
    "expiresIn": 7200,
    "sessionSaved": true
  }
}
```

#### `auth sms-send`

对应 GUI 发送短信验证码。

```powershell
LemonWorkToolCli.exe auth sms-send --mobile 13800000000
```

流程：

- 调用 `registerVerifyCodeUrl`。
- 参数沿用 GUI：`Mobile`、`ClientId=123456`、`ClientType=1017`、`IsVms=false`。
- 输出 `stamp` 和 `timeStamp`，供 `auth login-sms` 使用。

输出字段：

```json
{
  "data": {
    "mobileMasked": "138****0000",
    "stamp": "xxx",
    "timeStamp": "xxx",
    "expiresInSeconds": 59
  }
}
```

#### `auth login-sms`

对应 GUI 短信验证码登录。

```powershell
LemonWorkToolCli.exe auth login-sms --mobile 13800000000 --code 123456 --stamp xxx --timestamp xxx --save-session
```

流程：

1. 调用 `verifyCodeSignInUrl` 获取 `AuthCode`。
2. 调用 `signInUrl`，`grant_type=authorization_code` 换 token。
3. 后续 JWT、serviceId、appAsId 流程同密码登录。

#### `auth wx-qrcode`

对应 GUI 微信扫码登录第一步。

```powershell
LemonWorkToolCli.exe auth wx-qrcode --out D:\tmp\wx-login.png
```

流程：

1. 调用 `wxQrTokenUrl` 获取 `SceneId`。
2. 调用 `wxQrCodeUrl?sceneid=...` 下载二维码。
3. 保存二维码图片。

输出字段：

```json
{
  "data": {
    "sceneId": "xxx",
    "qrImagePath": "D:\\tmp\\wx-login.png",
    "pollCommand": "LemonWorkToolCli.exe auth wx-poll --scene-id xxx --save-session"
  }
}
```

#### `auth wx-poll`

对应 GUI 微信扫码轮询。

```powershell
LemonWorkToolCli.exe auth wx-poll --scene-id xxx --timeout 120 --save-session
```

流程：

1. 每 3 秒调用 `wxLoginScanedUrl?sceneid=...`。
2. `state=false` 继续轮询。
3. `state=timeout` 返回扫码超时，退出码 7。
4. `state=unbind` 返回未绑定错误。
5. 其他非空 `state` 视为 `AuthCode`，再走 token/JWT/serviceId 流程。

#### `auth login-wechat`

组合命令，生成二维码并等待扫码。

```powershell
LemonWorkToolCli.exe auth login-wechat --qr-out D:\tmp\wx-login.png --timeout 120 --save-session --events
```

输出最终登录结果；进度事件输出二维码路径、轮询状态、剩余时间。

#### `auth refresh`

从已有 `ningmengcookie` 刷新 JWT、serviceId、appAsId。

```powershell
LemonWorkToolCli.exe auth refresh --ningmeng-cookie xxx --save-session
```

#### `auth status`

```powershell
LemonWorkToolCli.exe auth status
```

输出当前 profile 是否有可用 session、cookie/JWT 是否存在、serviceId/appAsId 是否完整。

#### `auth cookie`

对应 GUI “获取 ningmengcookie”。

```powershell
LemonWorkToolCli.exe auth cookie
LemonWorkToolCli.exe auth cookie --reveal
```

默认只输出脱敏值；`--reveal` 才输出完整 cookie。

#### `auth logout`

```powershell
LemonWorkToolCli.exe auth logout --profile default
```

删除本地 session。若后续确认服务端有注销接口，再增加远端注销。

#### `auth open-web`

对应 GUI “打开柠檬云官网”。

```powershell
LemonWorkToolCli.exe auth open-web --browser edge
```

流程：

- 复用当前临时浏览器扩展方案，将 `ningmengcookie` 注入 `.ningmengyun.com`。
- 启动 Edge 并打开 `wbgateMenuUrl`。
- 如果 CLI 环境不允许启动 GUI 程序，返回 `GUI_NOT_AVAILABLE`。

输出字段：

```json
{
  "data": {
    "browser": "edge",
    "started": true,
    "profileDir": "C:\\Users\\...\\Temp\\lemoncloud_auth\\...",
    "url": "https://..."
  }
}
```

### 账套在线操作

#### `accountsets plans`

列出 `/wb/plan` 中当前账号可用的财务 plan。

```powershell
LemonWorkToolCli.exe accountsets plans
```

输出字段：

```json
{
  "data": {
    "items": [
      {
        "serviceId": 1,
        "appAsId": "xxx",
        "productType": "acc",
        "productTypeDescription": "柠檬云财务",
        "validStatus": "有效",
        "administrator": 123456
      }
    ]
  }
}
```

#### `accountsets list`

对应删除/备份页面加载账套列表。

```powershell
LemonWorkToolCli.exe accountsets list
LemonWorkToolCli.exe accountsets list --service-id 1 --app-as-id xxx
```

流程：

- 调用 `/wb/plan`，筛选 `productType=acc`、`validStatus=有效`、administrator 匹配当前用户。
- 对每个 plan 调用 `jproApiHost + /api/AccountSetOnlyAuth/TopInfoV2?serviceId=...&appasid=...`。
- 请求头包含 `appasid`，Cookie 包含 `ningmengcookie` 和 `service-id`。

输出字段：

```json
{
  "data": {
    "count": 1,
    "items": [
      {
        "asId": 123,
        "asName": "测试账套",
        "appAsId": "xxx",
        "asStartDate": "2024-01",
        "serviceId": 1,
        "productTypeDescription": "专业版"
      }
    ]
  }
}
```

#### `accountsets delete`

对应 GUI 删除账套，放入回收站。

```powershell
LemonWorkToolCli.exe accountsets delete --as-id 123 --yes
LemonWorkToolCli.exe accountsets delete --as-id 123 --as-id 456 --yes
```

流程：

- 若未提供 `--service-id` / `--app-as-id`，先用 `accountsets list` 自动定位账套。
- 调用 `jproApiHost + /api/AccountSetOnlyAuth/DropAccountSetToRecycleBinV2?asId=...&serviceid=...&appasid=...`。
- 服务端 `state=1000` 且 `data=true` 视为成功。
- 必须要求 `--yes`，避免脚本误删。

输出字段：

```json
{
  "data": {
    "total": 2,
    "success": 2,
    "failed": 0,
    "items": [
      {
        "asId": 123,
        "asName": "测试账套",
        "ok": true,
        "message": "删除成功"
      }
    ]
  }
}
```

#### `accountsets backup`

对应 GUI 备份下载账套。

```powershell
LemonWorkToolCli.exe accountsets backup --as-id 123 --out D:\backup --events
LemonWorkToolCli.exe accountsets backup --all --out D:\backup --parallel 3 --poll-interval 3000
```

流程：

1. 若未指定 service/appAsId，先加载账套列表定位。
2. 调用 `/api/Backup/ProcessBackup?allowTemporaryFile=1&isProcess=1&appasid=...`。
3. 读取返回 `data.fileName`。
4. 轮询 `/api/Backup/GetFileRecord?filename=...&appasid=...`。
5. `progress >= 100` 后调用 `/api/Backup/DownloadFile?fileName=...&fileType=0&appasid=...`。
6. 手动处理 301/302 下载重定向。
7. 保存到 `--out`。

输出字段：

```json
{
  "data": {
    "total": 1,
    "success": 1,
    "failed": 0,
    "items": [
      {
        "asId": 123,
        "asName": "测试账套",
        "fileName": "backup.zip",
        "localFilePath": "D:\\backup\\backup.zip",
        "bytes": 1024,
        "ok": true,
        "message": "已保存"
      }
    ]
  }
}
```

### 压缩包上传

#### `packages upload`

对应 GUI “上传压缩包专业版”，基于现有 `ImportPackageUploader`。

```powershell
LemonWorkToolCli.exe packages upload --input D:\data\package.zip --edition pro
LemonWorkToolCli.exe packages upload --input D:\data\package.zip --service-id 1 --as-id 123 --as-name 测试账套
```

参数：

- `--input`：ZIP 文件。
- `--edition`：`standard`、`pro`、`aa`、`aa-pro`，默认按 `serviceId` 自动判断。
- `--as-id`：可选，修复后重新上传时使用。
- `--as-name`：可选，默认使用 ZIP 文件名。
- `--zip-file-name`：可选，默认使用 ZIP 文件名不含扩展名。
- `--timeout-ms`：默认 120000。

输出字段：

```json
{
  "data": {
    "input": "D:\\data\\package.zip",
    "fileSize": 123456,
    "edition": "pro",
    "httpStatus": 200,
    "apiCode": 0,
    "importId": "xxx"
  }
}
```

### JSON 查询

#### `json-query import`

对应 GUI 导入 ZIP/JSON 到查询目录。

```powershell
LemonWorkToolCli.exe json-query import --input D:\data\package.zip
LemonWorkToolCli.exe json-query import --input D:\data\AccountSet.json --query-dir D:\work\JsonQueryPackages
```

输出导入后的 JSON 路径列表。

#### `json-query scan`

对应 GUI 扫描查询目录。

```powershell
LemonWorkToolCli.exe json-query scan --query-dir D:\work\JsonQueryPackages
```

输出字段包含账套名、启用日期、结束期间、数组数量、记录数量、有效状态。

#### `json-query query`

对应 GUI 查询数据。

```powershell
LemonWorkToolCli.exe json-query query --input D:\data\AccountSet.json --source GLVoucherList --year 2024 --acct-code 1122 --page 1 --page-size 100
LemonWorkToolCli.exe json-query query --input D:\data\package.zip --customer-code C001 --supplier-code S001 --keyword 销售 --include-raw
```

参数：

- `--input`：JSON 或 ZIP。ZIP 自动解压其中 JSON。
- `--source`：例如 `GLAccountList`、`GLBalanceList`、`GLVoucherList`、`GLProjectList`、`GLProjectTypeList`、`FADepreciationList`、`GLFixedAssetList`、`GLInvoiceList`、`GLJournalList`、`GLPeriodList`。
- `--year`：年份过滤。
- `--acct-code`：科目编码或科目名称过滤。
- `--customer-code`：客户编码过滤，包含辅助核算客户。
- `--supplier-code`：供应商编码过滤，包含辅助核算供应商。
- `--keyword`：全字段关键字过滤。
- `--page` / `--page-size`：分页。
- `--include-raw`：返回每条记录原始 JSON。
- `--max-results`：默认 3000，避免一次性输出过大。

输出字段：

```json
{
  "data": {
    "source": "GLVoucherList",
    "total": 245,
    "page": 1,
    "pageSize": 100,
    "pageCount": 3,
    "columns": ["来源", "AcctCode", "AcctName", "CustomerCode", "原始 JSON"],
    "items": [
      {
        "sourceKey": "GLVoucherList",
        "source": "凭证",
        "year": "2024",
        "period": "01",
        "acctCode": "1122",
        "acctName": "应收账款",
        "customerCode": "C001",
        "supplierCode": "",
        "summary": "销售",
        "debit": "100.00",
        "credit": "",
        "amount": "",
        "fields": {}
      }
    ]
  }
}
```

### 修复账套

#### `repair import`

对应 GUI 拖入 ZIP 并解压扫描。

```powershell
LemonWorkToolCli.exe repair import --input D:\data\package.zip --repair-dir D:\work\RepairAccountSetPackages
```

输出解压出的 JSON 列表。

#### `repair scan`

对应 GUI 刷新修复目录。

```powershell
LemonWorkToolCli.exe repair scan --repair-dir D:\work\RepairAccountSetPackages
```

输出字段：

```json
{
  "data": {
    "count": 1,
    "items": [
      {
        "asName": "测试账套",
        "startDate": "2024-01-01",
        "jsonPath": "D:\\work\\RepairAccountSetPackages\\AccountSet.json",
        "valid": true,
        "status": "待修复"
      }
    ]
  }
}
```

#### `repair run`

对应 GUI “修复选中账套”。

```powershell
LemonWorkToolCli.exe repair run --input D:\data\AccountSet.json --out D:\out
LemonWorkToolCli.exe repair run --input D:\data\package.zip --out D:\out --zip --upload
```

修复内容：

- 可选覆盖 `AccountSet.StartDate`。
- 根据启用日期更新 `StartPeriod`。
- 根据凭证 `MakeDate` 修正 `GLVoucherList.IntYear`、`IntPeriod`。
- 根据最大凭证期间更新 `EndPeriod`。
- 处理同一 `AcctCode` 下多个 `AcctName` 的科目冲突。
- 处理兄弟科目同名，默认重命名为 `原名称_科目编码`。
- 输出修复后的 JSON。
- 可选打包为 ZIP。
- 可选调用 `packages upload` 上传。

参数：

- `--input`：JSON、ZIP 或目录。
- `--out`：输出目录。
- `--start-date yyyy-MM-dd`：强制修改账套启用日期。
- `--auto-start-date earliest-voucher`：当最早凭证早于启用日期时，自动改为最早凭证日期。
- `--conflict-policy fail|first|mapping`：科目编码冲突策略，默认 `fail`。
- `--conflict-map D:\map.json`：`mapping` 策略使用，格式为 `{ "1001": "库存现金" }`。
- `--sibling-name-policy suffix-code|fail|ignore`：兄弟科目重名策略，默认 `suffix-code`。
- `--zip`：输出 ZIP。
- `--upload`：修复后上传。

输出字段：

```json
{
  "data": {
    "total": 1,
    "success": 1,
    "failed": 0,
    "items": [
      {
        "asName": "测试账套",
        "inputJsonPath": "D:\\data\\AccountSet.json",
        "outputJsonPath": "D:\\out\\AccountSet_repaired.json",
        "outputZipPath": "D:\\out\\AccountSet_repaired.zip",
        "startDateChanged": true,
        "startDate": "2024-01-01",
        "startPeriod": "2024-01",
        "endPeriod": "2024-12",
        "subjectConflictsResolved": 2,
        "siblingRenamed": [
          {
            "acctCode": "100101",
            "originalName": "明细",
            "newName": "明细_100101"
          }
        ],
        "upload": {
          "requested": true,
          "ok": true,
          "importId": "xxx"
        }
      }
    ]
  }
}
```

#### `repair set-start-date`

独立提供“修改账套启用日期”能力。

```powershell
LemonWorkToolCli.exe repair set-start-date --input D:\data\AccountSet.json --date 2024-01-01 --out D:\out
```

只修改 `AccountSet.StartDate` 与 `StartPeriod`，不做其他修复。

### 科目编码添加分隔符

#### `subject-code separate`

对应 GUI “科目编码添加分隔符”。

```powershell
LemonWorkToolCli.exe subject-code separate --input D:\data\AccountSet.json --out D:\out
LemonWorkToolCli.exe subject-code separate --input D:\data\package.zip --out D:\out
```

处理内容：

- 从 `GLAccountList` 建立旧编码到新编码映射。
- 根据父子前缀关系给子级编码加 `.` 分隔符。
- 更新 `AcctCode`、`AcctParent`、`AcctLevel`、`AcctFullCode`、`AcctRoot`。
- 递归更新其他 JSON 节点中的 `AcctCode` 引用。
- 输出新 ZIP。
- 输出 mapping CSV，列为 `OldCode,NewCode,ParentOldCode,ParentNewCode,Level,Reassigned`。

输出字段：

```json
{
  "data": {
    "input": "D:\\data\\package.zip",
    "outputZipPath": "D:\\out\\package_acctcode_fixed.zip",
    "mappingPath": "D:\\out\\package_acctcode_fixed_mapping.csv",
    "jsonCount": 1,
    "accountCount": 120,
    "changedAccountCount": 80,
    "reassignedCount": 0,
    "referenceUpdateCount": 320
  }
}
```

### 科目编码冲突处理

#### `subject-conflict scan`

对应 GUI 待实现入口，以及修复流程中的冲突检测。

```powershell
LemonWorkToolCli.exe subject-conflict scan --input D:\data\AccountSet.json
```

扫描同一 `AcctCode` 对应多个 `AcctName` 的情况。

#### `subject-conflict resolve`

```powershell
LemonWorkToolCli.exe subject-conflict resolve --input D:\data\AccountSet.json --map D:\conflict-map.json --out D:\out
```

`conflict-map.json`：

```json
{
  "1001": "库存现金",
  "1122": "应收账款"
}
```

处理方式：

- 对每个冲突编码只保留映射指定名称的科目。
- 未提供映射且存在冲突时默认失败。
- 可选 `--policy first` 自动保留第一项，但必须在输出 warnings 中提示。

### 兄弟科目名称重复处理

#### `sibling-name-dedupe scan`

```powershell
LemonWorkToolCli.exe sibling-name-dedupe scan --input D:\data\AccountSet.json
```

扫描同一父级下 `AcctName` 重复的科目。

#### `sibling-name-dedupe run`

```powershell
LemonWorkToolCli.exe sibling-name-dedupe run --input D:\data\AccountSet.json --out D:\out --policy suffix-code
```

处理方式：

- 默认保留第一个同名兄弟科目不变。
- 后续重复项改名为 `原名称_科目编码`。
- 输出 rename report。

### 截取特定年份数据

#### `extract-year run`

对应 GUI 待实现入口。

```powershell
LemonWorkToolCli.exe extract-year run --input D:\data\AccountSet.json --year 2024 --out D:\out
LemonWorkToolCli.exe extract-year run --input D:\data\package.zip --year 2024 --out D:\out --zip
```

建议处理规则：

- 保留 `AccountSet` 基础信息。
- 将 `AccountSet.StartDate` 调整到目标年份首个有效期间，或通过 `--start-date` 指定。
- 更新 `StartPeriod`、`EndPeriod`。
- 过滤 `GLVoucherList`、`GLBalanceList`、`GLPeriodList`、`GLJournalList`、`GLInvoiceList` 等含年份/日期/期间字段的数组。
- 对没有年份字段但被保留数据引用的基础资料，如科目、辅助核算、客户、供应商，应默认保留。
- 对无法判断年份的数据集保留并在 warnings 中列出。

参数：

- `--year 2024`：目标年份。
- `--start-date yyyy-MM-dd`：输出账套启用日期。
- `--keep-master-data true|false`：是否保留基础资料，默认 true。
- `--zip`：输出 ZIP。

状态：

- 当前代码未实现，需要作为独立 `YearExtractService` 开发。

### 配置命令

#### `config show`

```powershell
LemonWorkToolCli.exe config show
```

输出当前合并后的配置，敏感字段脱敏。

#### `config set`

```powershell
LemonWorkToolCli.exe config set --key defaultOutputDir --value D:\output
```

写入 `cli.json`。

## Core 服务拆分清单

| 服务 | 职责 | 来源 |
| --- | --- | --- |
| `AuthService` | 密码/短信/微信登录、JWT、plan/service 参数获取 | `PwdLoginWidget`、`SmsLoginWidget`、`MicroMsgLoginWidget` |
| `SessionStore` | 读写 session、脱敏、过期检查 | 新增 |
| `AccountSetService` | plan 列表、账套列表、删除到账套回收站 | `DeleteAccountSetWidget`、`BackupAccountSetWidget` |
| `BackupService` | 发起备份、轮询、下载、保存 | `BackupAccountSetWidget` |
| `ImportPackageUploader` | ZIP 上传 | 已有公共类 |
| `JsonQueryService` | 导入、扫描、加载、索引、过滤、分页 | `JsonQueryWidget` |
| `RepairAccountSetService` | 修复日期、凭证期间、冲突、兄弟重名、输出文件 | `RepairAccountSetWidget` |
| `SubjectCodeSeparatorService` | 科目编码加分隔符、引用更新、mapping CSV、ZIP 输出 | `SubjectCodeSeparatorWidget` |
| `SubjectConflictService` | 冲突扫描和按映射保留 | `RepairAccountSetWidget` |
| `SiblingNameDedupeService` | 同父级同名扫描和重命名 | `RepairAccountSetWidget` |
| `YearExtractService` | 按年份截取账套数据 | 新增 |
| `ZipService` | 安全解压 JSON、目录压缩、单 JSON 打包 | 现有 PowerShell 片段集中封装 |
| `CliResult` | 统一 JSON envelope、错误码、退出码 | 新增 |

## API 与配置键

需要继续复用 `Resource/http/Pro/Account.json` 中的配置键：

| 配置键 | 用途 |
| --- | --- |
| `signInUrl` | 密码登录、authorization_code 换 token |
| `registerVerifyCodeUrl` | 发送短信验证码 |
| `verifyCodeSignInUrl` | 短信验证码换 AuthCode |
| `wxQrTokenUrl` | 获取微信二维码 sceneId |
| `wxQrCodeUrl` | 下载微信二维码图片 |
| `wxLoginScanedUrl` | 轮询微信扫码状态 |
| `authHost` | 获取 JWT，路径 `/v2/api/jwt` |
| `wbgateMenuUrl` | 获取 serviceId/appAsId 或打开官网 |
| `jproApiHost` | 账套列表、删除、备份 API |
| `importerApiHost` | 标准版上传 host |
| `proImporterApiHost` | 专业版上传 host |
| `aaImporterApiHost` | AA 上传 host |
| `aaProImporterApiHost` | AA 专业版上传 host |

已识别 API：

```text
POST signInUrl
POST registerVerifyCodeUrl
POST verifyCodeSignInUrl
GET  wxQrTokenUrl
GET  wxQrCodeUrl?sceneid=...
GET  wxLoginScanedUrl?sceneid=...
POST authHost/v2/api/jwt
GET  wbgateMenuUrl
GET  https://wbgate.ningmengyun.com/wb/plan?v=0&currentPage=1&pageSize=10
GET  jproApiHost/api/AccountSetOnlyAuth/TopInfoV2?serviceId=...&appasid=...
POST jproApiHost/api/AccountSetOnlyAuth/DropAccountSetToRecycleBinV2?asId=...&serviceid=...&appasid=...
POST jproApiHost/api/Backup/ProcessBackup?allowTemporaryFile=1&isProcess=1&appasid=...
POST jproApiHost/api/Backup/GetFileRecord?filename=...&appasid=...
GET  jproApiHost/api/Backup/DownloadFile?fileName=...&fileType=0&appasid=...
POST <importerHost>/api/AccImport/ImportAccountSetFileAsyncV2
```

## 实施阶段

### 阶段 1：CLI 基础骨架

交付：

- 新增 `LemonWorkToolCli.exe` Console 工程。
- `QCoreApplication` 入口。
- `CliCommandRouter` 子命令路由。
- `CliResult` 统一 JSON 输出。
- `CliError` 和退出码。
- `--profile`、`--config`、`--session`、`--pretty`、`--events` 全局参数。
- `version`、`help`、`config show`。

验收：

```powershell
LemonWorkToolCli.exe version
LemonWorkToolCli.exe help
```

stdout 均为合法 JSON。

### 阶段 2：登录与会话

交付：

- `AuthService`。
- `SessionStore`。
- `auth login-password`。
- `auth sms-send`。
- `auth login-sms`。
- `auth wx-qrcode`。
- `auth wx-poll`。
- `auth login-wechat`。
- `auth refresh`。
- `auth status`。
- `auth cookie`。
- `auth logout`。

验收：

- 密码登录后能保存 session。
- `auth status` 能读取 session。
- 后续在线命令无需重复传 cookie。
- token/cookie 不出现在日志明文中。

### 阶段 3：账套在线操作

交付：

- `AccountSetService`。
- `BackupService`。
- `accountsets plans`。
- `accountsets list`。
- `accountsets delete`。
- `accountsets backup`。
- `auth open-web`。

验收：

- 列表结果与 GUI 删除/备份页一致。
- 删除命令必须显式 `--yes`。
- 备份能轮询进度并下载 ZIP。
- 多账套备份结果能逐项返回成功/失败。

### 阶段 4：上传压缩包

交付：

- 复用并补强 `ImportPackageUploader`。
- `packages upload`。
- 支持 `edition` 到 host key 的映射。
- 支持 `serviceId` 自动判断版本。

验收：

- 专业版 ZIP 上传成功返回 `importId`。
- 鉴权失败、HTTP 失败、服务端 Code 失败都能进入统一错误结构。

### 阶段 5：JSON 查询服务化

交付：

- `JsonQueryService`。
- `json-query import`。
- `json-query scan`。
- `json-query query`。
- 支持 JSON/ZIP 输入。
- 支持 source、year、acct-code、customer-code、supplier-code、keyword、分页、include-raw。

验收：

- 查询结果与 GUI 查询页一致。
- 大 JSON 不一次性输出超过 `--max-results`。
- ZIP 中无 JSON 时返回 `DATA_PROCESS_FAILED`。

### 阶段 6：修复账套服务化

交付：

- `RepairAccountSetService`。
- `SubjectConflictService`。
- `SiblingNameDedupeService`。
- `repair import`。
- `repair scan`。
- `repair run`。
- `repair set-start-date`。
- `subject-conflict scan`。
- `subject-conflict resolve`。
- `sibling-name-dedupe scan`。
- `sibling-name-dedupe run`。

验收：

- 修复后的 JSON 与 GUI 当前输出一致。
- 冲突处理在 CLI 下不能弹窗，必须通过 policy/map 参数控制。
- `--upload` 复用 `packages upload` 同一逻辑。

### 阶段 7：科目编码添加分隔符

交付：

- `SubjectCodeSeparatorService`。
- `subject-code separate`。
- 输出 ZIP 和 mapping CSV。
- 递归更新所有 `AcctCode` 引用。

验收：

- 示例包 `BuildCheck\SubjectCodeSeparatorSample` 输出与现有 GUI 逻辑一致。
- CSV 带 UTF-8 BOM，Excel 可正常打开中文。

### 阶段 8：截取特定年份数据

交付：

- `YearExtractService`。
- `extract-year run`。
- 明确各数据集年份字段识别策略。
- 输出过滤报告。

验收：

- 指定年份凭证、余额、期间被正确保留。
- 基础资料保留策略可配置。
- 输出账套的 `StartPeriod` / `EndPeriod` 合理。

### 阶段 9：打包、文档、回归

交付：

- Release 包中包含 `LemonWorkToolCli.exe`。
- `CLI_USAGE.md` 使用说明。
- `CLI_COMMANDS.schema.json` 可选，用于描述输出结构。
- GUI 回归测试。
- CLI 集成测试脚本。

验收：

- GUI 原功能不退化。
- CLI 所有命令 stdout 都可被 `ConvertFrom-Json` 解析。
- Release ZIP 包含 DLL、配置、说明、示例脚本。

## 测试计划

### 单元测试

- `CliResult` 成功/失败 JSON 格式。
- 参数缺失、类型错误、非法日期。
- session 读写、脱敏、profile 切换。
- JWT payload 解析 userId。
- plan 筛选规则。
- 科目编码父子关系识别。
- 科目编码冲突重分配。
- `AcctCode` 引用递归更新。
- 科目冲突扫描。
- 兄弟科目重名扫描和重命名。
- JSON 查询 year/acct/customer/supplier/keyword 过滤。
- 分页边界。
- ZIP Slip 防护。

### 集成测试

- `auth login-password` 到 `accountsets list` 全链路。
- `accountsets backup --as-id ...` 真实下载。
- `packages upload` 真实上传专业版 ZIP。
- `json-query query` 查询大数据包。
- `repair run --zip` 输出可重新导入的 ZIP。
- `repair run --upload` 修复并上传。
- `subject-code separate` 对样例包输出 ZIP 和 mapping。
- 所有失败场景输出统一错误 JSON。

### 回归测试

- GUI 登录。
- GUI 删除账套。
- GUI 备份下载。
- GUI 修复账套。
- GUI JSON 查询。
- GUI 科目编码添加分隔符。
- GUI 上传压缩包。

## 风险与处理

| 风险 | 处理 |
| --- | --- |
| 登录态字段含义混乱，`access_token` 在部分流程中同时作为 cookie 使用 | 抽象 `AuthTokens`，明确 `ningmengCookie` 与 `jwtToken` 两个字段 |
| 现有网络逻辑大量在 QWidget 中异步回调 | Core 使用同步封装或 signal + event loop，CLI 入口只等待服务返回 |
| CLI 无弹窗，冲突处理不能依赖用户现场选择 | 所有冲突都通过 `--policy` 和 mapping 文件表达 |
| ZIP 操作目前依赖 PowerShell | 先封装为 `ZipService`，后续可替换为 C++ ZIP 库 |
| 大 JSON 输出过大 | 默认分页和 `--max-results`，原始 JSON 需显式 `--include-raw` |
| 删除账套是危险操作 | 强制 `--yes`，输出待删除清单，建议后续支持 `--dry-run` |
| token/cookie 泄漏 | 默认脱敏，`auth cookie --reveal` 才输出完整值，日志永不输出明文 |
| 未实现功能边界不清 | 阶段 8 前补充样例输入/输出和业务确认 |

## 推荐优先级

1. CLI 骨架和统一 JSON 输出。
2. 登录与 session，因为所有在线命令依赖它。
3. `accountsets list`，作为验证认证链路的最小在线命令。
4. `packages upload`，已有公共上传类，改造成本最低。
5. `accountsets delete` 与 `accountsets backup`。
6. `json-query query`。
7. `subject-code separate`。
8. `repair run`、`subject-conflict resolve`、`sibling-name-dedupe run`。
9. `extract-year run`。
10. 打包、文档和回归测试。

这个顺序可以保证每个阶段都有可独立验收的命令，同时逐步把 GUI 中的业务代码迁移到 Core。
