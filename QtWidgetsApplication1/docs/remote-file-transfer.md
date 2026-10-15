# 公共远端文件传输

此能力独立于 T3、SQL Server 和具体业务。入口为 `QtWidgetsApplication1/Common/RemoteFileTransfer.h`；服务端程序为 `RemoteTransferAgent/RemoteTransferAgent.pro`。

## 工作流程

1. 调用方提供本地单文件和 UNC 共享根目录。
2. 普通文件在本地压缩为 GZIP；已有 GZIP 可直接上传，避免重复压缩。
3. 公共模块将压缩文件上传到共享目录内的独立任务文件夹，计算 SHA-256，并原子提交任务。
4. 常驻助手在服务器本地校验文件大小和 SHA-256，解压并检查 GZIP CRC。
5. 模块返回服务器本地路径、客户端共享路径和文件租约。业务方使用返回路径执行自己的操作。
6. 最后一份租约释放后清理该任务的临时文件；原输入文件保留。

例如本次配置：

| 含义 | 路径 |
| --- | --- |
| 客户端共享根目录 | `\\192.168.1.211\hzw` |
| 助手监视的服务器本地目录 | `C:\hzw` |
| 助手程序 | `C:\hzw\LemonTransferAgent\LemonTransferAgent.exe` |
| 服务器结果示例 | `C:\hzw\.lemon-transfer\jobs\<任务ID>\content.bin` |

`content.bin` 保留原文件内容。调用方不应依赖扩展名判断格式；T3 模块通过 SQL Server 读取备份头确认它是可恢复的数据库备份。

## 公共接口

`stageCopy(source, destinationDirectory, cancelled, progress)` 用于本地/SMB 原样传输，支持上传与取回，不依赖助手。它只创建唯一暂存文件，流式复制后回读目标核对 SHA-256；取消或失败删除未完成文件，成功返回文件租约。调用方在数据库停止使用后释放租约；需要展示清理失败时，应先显式删除并检查结果。此接口不压缩或解包，T3 自动升级用它传输解包后的旧备份和重新生成的备份。

`hasLiveAgent(sharedRoot, cancelled)` 通过观察心跳更新判断助手可用性；T3 中转据此自动选用压缩上传或共享直传。助手检测失败不影响直传能力；已经开始压缩上传后遇到传输错误会停止并报告，不静默重试数据库操作。

```cpp
#include "Common/RemoteFileTransfer.h"

auto file = RemoteFileTransfer::upload(
    localFile,
    QStringLiteral("\\\\192.168.1.211\\hzw"),
    RemoteFileTransfer::InputEncoding::RawFile,
    cancelled,
    progress);
if (!file.error.isEmpty()) {
    // 展示错误；不继续执行业务操作。
    return;
}

// file.serverPath：服务器本地路径，可交给服务器侧业务使用。
// file.sharedPath：客户端通过 SMB 访问结果的路径。
// file.job：共享租约；操作完成前必须保留 file 或复制该租约。
performServerOperation(file.serverPath);
```

`RawFile` 接收普通单文件，公共模块负责压缩。`Gzip` 表示调用方明确知道输入已经是完整的 GZIP 流。接口不依赖文件扩展名，不包含 T3 或 SQL 格式判断，不支持递归上传文件夹或 ZIP 目录结构。

上传会等待远端完成，应在业务工作线程调用；支持取消回调和进度消息。调用方应保留租约到服务器完全结束使用文件后再释放；本接口输出是临时文件，不是永久文件归档接口。

## 助手启动与部署

在共享所在的服务器运行 `StartAgent.cmd`，默认根目录为 `C:\hzw`；也可运行：

```text
LemonTransferAgent.exe --root "C:\hzw"
```

助手启动后自动处理任务。窗口关闭会最小化到托盘；“退出”会取消当前解压并结束程序。它是常驻桌面程序，注销或重启后需重新启动；没有安装 Windows 服务，也没有修改自动启动配置。

助手无需 SQL 账号；客户端共享访问沿用 Windows 的 SMB 登录。助手通过共享目录收取固定操作的数据任务，不开放额外端口，不执行传入的命令。

## 任务协议 v1

- 根目录下使用 `.lemon-transfer/agent.json` 发布协议版本、实例 ID、服务器本地根目录和心跳。客户端观察心跳变化，不依赖两台机器时钟同步。
- `.lemon-transfer/jobs/<32位随机ID>/` 隔离任务。客户端先完整上传 `payload.gz`，再原子提交 `request.json`；请求含协议版本、任务 ID、目标助手实例 ID、压缩字节数和 SHA-256。
- 助手发布 `status.json`：`working`、`done` 或 `error`。解压结果固定为 `content.bin`，请求不能指定任意输出路径。
- 压缩包大小上限 8 GiB，解压上限 64 GiB；拒绝损坏、截断、拼接 GZIP。禁止跟随任务目录和输入文件的重解析点。
- `cancel.json` 请求取消；`release.json` 请求助手在停止处理后清理任务。清理仅删除约定任务文件，不递归删除未知文件或文件夹。
- 助手通过目录锁防止同一目录启动多个实例。助手重启后实例 ID 改变，旧的未完成请求会失败，调用方需重新提交。
- 等待远端结果最多 15 分钟；失去心跳时报告失败。助手离线时无法立即清理的取消任务，会在助手再次启动后处理。

## T3 的接入方式

T3 页面通过下拉框选择本地或 211；选择 211 时自动填入共享目录，并默认勾选“在远端解压”。它先识别备份是 GZIP 还是原生 SQL Server 文件，再调用公共上传接口，使用返回的服务器本地路径执行备份头读取、校验和恢复。年度选择、数据库同名保护、SQL 认证等仍属于 T3 模块。

取消勾选时继续使用原有的本机解包、共享暂存方式。已修复 SQL Server 2008 R2 将正斜杠 UNC 误判为相对路径的问题，发给 SQL Server 的备份设备路径使用 Windows 标准反斜杠。

## 构建与验证

助手使用 Qt 5.12 MSVC Win32，可用 MSVC 2017 编译，以兼容已有旧服务器。测试和主程序构建使用独立目录，不能清理用户正常启动程序的中间文件。

`Tests/RemoteFileTransferTests.pro` 是独立公共模块测试，覆盖普通二进制文件、已有 GZIP、CRC 错误、截断、SHA-256 不符、取消、租约最后释放时清理、非法任务目录和助手缺失。

T3 集成测试增加 `T3_TEST_REMOTE_AGENT=1`，并将 `T3_TEST_STAGING` 设为共享根目录，可验证公共传输与实际数据库恢复的组合。测试仅创建随机命名的测试库，完成后清理；不要将其用于未授权服务器。

### 稳定性回归

- MSVC 2017 下 Qt 中文字面量错误转码已修复，助手使用明确的 UTF-8 字符串。`--verify-ui <文件>` 导出实际 Qt 标题、按钮和后端提示供 `Tests/AgentEncodingCheck.py` 检查，并生成界面截图；该模式不启动任务处理。
- 元数据读取允许 Windows 删除共享标志，避免客户端读文件阻止服务器原子替换；短暂写入失败自动重试，助手持续运行。`heartbeatSurvivesReaders` 会主动持有心跳文件，验证助手不误停。
