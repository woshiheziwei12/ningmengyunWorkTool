# 企业微信文档读取

入口：工作台 → 查询与会话 → 企业微信文档读取（功能 ID 28）。

1. 粘贴“2026-运营支持组项目进度表”的完整企业微信文档链接。初始显示不含分享码的文档地址；需要分享码时粘贴原分享链接。
2. 点击“扫码登录”，在嵌入的官方页面完成登录。工具使用独立 WebView2 用户目录，并在登录成功后加密保存文档 Cookie，不读取 Chrome/Edge 的个人配置。页面单独显示保存是否成功。
3. 点击“获取最新数据”。首次获取尚未登录时也会打开扫码窗口。关闭重开后先尝试复用本地登录；8 秒内仍未通过文档访问验证时显示官方页面，网络慢时也可在该页面继续等待。每次重新读取文档目录，以本机当天日期筛选全部日期工作表并按日期升序读取。
4. 通过工作表标签查看内容。行号和列号保持原位置，悬停可查看完整文本和链接，Ctrl+C 复制选区。
5. 点击“导出 JSON”。导出采用原子写入；部分成功或停止读取的结果保留失败表及原因，`complete` 为 false。

只读取固定文档，不创建 GitLab MR，不修改文档或代码分支。该功能不依赖柠檬云账号的文档权限；需使用有权查看该文档的企业微信身份。

## 日期与数据约定

- `10-15` 使用文档标题开头的年份（目前 2026）；也识别 `2027-01-01`。非法日期、非日期表名均跳过。无法确定文档年份时明确失败，不猜年份。
- 读取全表（`startrow=0&endrow=-1`），校验工作表 ID 与版本。不同版本不混合，失败工作表提示重新获取。
- 普通文本、富文本、链接及数字按实际二进制协议解码。已核实的日期格式（`yyyy-mm-dd`、`yyyy/m/d`、`yyyy/mm/dd`）显示为日期，同时导出原始数字和格式。其他数字保留数值及可用格式信息，不承诺还原所有 Excel 格式。
- 当前协议解析依据本次文档样本及其官方前端协议定义。遇到未支持的公式、独立日期类型、其他未知值类型或损坏响应，整张表明确失败，避免静默丢失数据。超大响应限制为压缩响应 32 MiB、单个解码块 64 MiB、50 万个有值单元格、200 万行列位置。
- 标准同步成功响应可以带 `async_open_done=false`。只有服务器返回 `200156` 才进入 `async=1` 轮询；轮询完成后再读取，最多等待 30 次。
- 保留数据与坐标，不复制在线表格的颜色、字体、合并单元格外观、图片、评论或附件文件。

## 数据接口

`Common/WecomDocumentReader.h` 提供可复用的异步 `read(documentUrl, fromDate)`、`signIn(documentUrl)`、`cancel()`。传入独立的 `WecomBrowser` 传输实例，监听 `finished(Snapshot)`、`failed`、`progress`、`busyChanged`、`loginRequired`、`authenticated`。后续功能可直接消费 `Snapshot`，无需读取 UI 表格。

`Common/WecomDocumentData.h` 的解析、日期筛选和 JSON 转换不依赖 UI、登录或网络。`Snapshot::toJson()` 输出版本化格式：

```json
{
  "schemaVersion": 1,
  "documentId": "固定文档标识",
  "title": "文档标题",
  "fromDate": "2026-10-14",
  "fetchedAt": "UTC 时间",
  "coordinateBase": 0,
  "complete": true,
  "cancelled": false,
  "sheets": [
    {
      "id": "工作表标识",
      "name": "10-15",
      "date": "2026-10-15",
      "rowCount": 200,
      "columnCount": 35,
      "loaded": true,
      "error": "",
      "cells": [{"row": 1, "column": 0, "text": "序号", "links": []}]
    }
  ]
}
```

单元格是稀疏数组，坐标从 0 开始；未列出的坐标为空。数字单元格额外提供 `rawNumber`、`numberFormat`。不导出分享码、请求 URL、鉴权字段或登录配置。

## 让外部助手通过桌面工具读取

这里的“工具”就是编译生成的柠檬云工作助手。启动更新后的程序并进入工作台，企业微信登录完成后，外部助手可以发起读取，工具使用自己的会话联网取数并返回 JSON。最小化到托盘不影响接口；从菜单退出工具后接口停止。无需导出文件，也不需要把 Cookie 交给调用者。

调用脚本源码位于 `Tools/Read-WecomDocument.ps1`，构建时同时复制到 EXE 旁。用当前 Windows 用户运行：

```powershell
# 检查工具是否提供接口（不验证企业微信授权，也不启动浏览器）
pwsh -NoProfile -File .\Read-WecomDocument.ps1 -Status

# 从本机今天开始获取最新数据；标准输出为一条 UTF-8 JSON
pwsh -NoProfile -File .\Read-WecomDocument.ps1

# 也可传入今天或之后的 yyyy-MM-dd 日期
pwsh -NoProfile -File .\Read-WecomDocument.ps1 -FromDate (Get-Date).AddDays(1).ToString('yyyy-MM-dd')
```

成功返回 `{"protocolVersion":1,"ok":true,"data":Snapshot}`。`ok` 表示已返回读取结果，仍应检查 `data.complete`、`data.cancelled` 及各工作表的 `loaded/error`，不可将部分结果当成完整数据。每次 read 都重新联网获取，不返回上次页面缓存。使用文档页面当前填写的完整分享链接，未打开页面时使用固定文档的默认链接。

失败返回 `{"protocolVersion":1,"ok":false,"error":{"code":"...","message":"..."}}`，脚本退出码为 2。常见代码：

| code | 处理方式 |
| --- | --- |
| `tool_not_running` | 启动更新后的工具并进入工作台；旧版本不提供接口 |
| `access_denied` | 使用与工具相同的 Windows 用户；沙箱限制连接时需要正常用户上下文的执行权限 |
| `login_required` | 到工具中完成官方登录，完成后重新请求；网络慢也可能需要在官方页面确认 |
| `busy` | 工具正在登录/读取或有模态操作，等待后重试 |
| `read_failed` | 查看具体错误，必要时到文档页重新扫码 |
| `invalid_date` | 日期必须为今天或之后，使用 yyyy-MM-dd |
| `read_timeout` | 请求超过等待时间，查看工具进度或停止任务；不要立即并发重试 |
| `tool_disconnected` | 工具关闭或连接中断，重新打开后再请求 |

接口基于 Windows 命名管道 `LemonWorkTool.Wecom.v1.<当前用户SID>`，服务端仅授予同一 Windows 用户访问权限，不监听 HTTP/TCP 端口。协议为每连接一条以换行结尾的 UTF-8 JSON，请求 `{"protocolVersion":1,"action":"read"}` 或 `{"protocolVersion":1,"action":"status"}`；read 可选 `fromDate`。拒绝其他字段和动作，不能替换成其他文档或触发 MR/写入。

请求上限 8 KiB、结果上限 64 MiB，服务端五分钟返回超时，脚本默认等待 310 秒。客户端退出后底层读取仍可能继续，界面可点击停止；保持忙碌直到实际任务完成。仅首先启动并取得用户级锁的工具实例提供接口，其他实例会在文档页面提示冲突。锁文件随退出释放，意外退出后由 Qt 检查失效锁。访问权限与 Windows 命名管道行为见 [Qt QLocalServer 文档](https://doc.qt.io/qt-6/qlocalserver.html)。

## 运行与构建

使用本机 Microsoft Edge WebView2 Evergreen Runtime；SDK 版本和来源见 `ThirdParty/WebView2/README.md`。构建自动将 x86 `WebView2Loader.dll` 复制到 EXE 旁；分发时必须一起携带。Qt 工程仍使用 Qt 5.12 Win32，不额外引入 Qt WebEngine。

登录配置目录为当前用户本地数据目录下 `LemonCloud/LemonWorkTool/WecomWebView`。会话失效后重新扫码；不将 Cookie 写入源码、日志或 JSON。请求使用官方页面同源浏览器会话和正常 TLS 校验。

### 授权期限与重启复用

- 企业微信网页会话未提供可确认的统一授权期限，不能套用柠檬云 Cookie/JWT 续期规则或写死为若干天。服务器可提前撤销会话、改变文档权限；是否可用以每次联网验证为准。工具不会延长服务器授权。
- WebView2 原生保存持久 Cookie；会话 Cookie 的 `Expires=-1` 不等于长期授权。为支持关闭程序后复用，在登录成功及读取结束时用 Windows DPAPI（当前用户范围）加密保存 `session.dat`，采用原子替换。应在界面显示保存完成后退出；保存失败不影响当前读取，但下一次可能需要扫码。
- 重启先解密并过滤过期记录，只恢复浏览器缺失的文档域 Cookie，保留名称、域、路径、Secure、HttpOnly、SameSite 和原到期时间；会话 Cookie 仍为 `-1`，不人为延长。只使用此工具自己的配置目录。损坏或无法解密时提示并回退官方页面，不影响其他功能。
- 此记录按 Windows 用户加密，不是可跨电脑/跨用户拷贝的授权文件。没有服务端验证之前，界面只说发现或恢复了本地记录，不显示“授权有效”。不会把凭据包含在表格 JSON 中。

## 验证

`Tests/WecomDocumentTests.targets` 是可选的独立测试入口。验证日期边界、固定链接校验、字符串/富文本/数字/日期映射、损坏内容、异步模式、部分结果、版本变化、取消后迟到回调、导出格式及页面初始状态。

本地抓包回归可运行 `Tests/prepare_wecom_capture.py CAPTURE.txt BuildCheck/WecomDocument/fixtures`，再向测试 EXE 传入 `--capture-dir`。脚本仅输出筛选后的数据响应，不保存请求头、Cookie 或原始请求 URL；测试数据含私人表格内容，仅放忽略目录。

测试 EXE 的 `--browser-smoke` 只验证 WebView2 和官方页面加载，不代表手机扫码及账号权限已验证。真实扫码需由账号持有人完成。

会话测试覆盖 DPAPI 磁盘往返、明文检查、损坏拒绝、过期及域名过滤、保存失败、保存期间忙碌与取消后迟到回调。`--session-smoke` 在独立目录中向真实 WebView2 恢复合成 Cookie，再通过 CookieManager 读回核对属性，全程不发网络请求、不使用真实账号。此测试不等同真实企业微信授权到期或跨进程扫码联调。

`WecomLocalServerTests.cpp` 使用独立命名管道验证状态、实时请求分发、日期/字段约束、分段请求、并发忙碌、部分结果、断开及登录错误。向测试程序传入 `--client-script Tools/Read-WecomDocument.ps1` 后，还会在独立 PowerShell 进程测试实际调用脚本：只替换临时副本的测试端点，传回含中文及多块内容的 JSON，不使用真实账号。

构建遵循根规则：`/nr:false`，隔离所有产物与中间路径，构建前后记录原 EXE/PDB SHA-256、跟踪构建进程，按归属核实并定向清理 PDB 服务。不会替换用户原启动 EXE/PDB。
