# 柠檬云账套工作工具

Windows / Qt 桌面工具、命令行工具与 Chrome 扩展，提供账套备份、专业版导账、数据修复及数据库备份恢复。浏览器登录同步和网页 Cookie 获取共用一个扩展。

## 目录入口

| 目录 | 用途 |
| --- | --- |
| `QtWidgetsApplication1/` | Qt 桌面程序、公共模块、UI 和资源 |
| `LemonWorkToolCli/` | 命令行客户端，功能规划见 `CLI_IMPLEMENTATION_PLAN.md` |
| `LemonChromeExtension/` | 唯一维护的 Chrome 扩展源码、打包脚本及离线测试 |
| `RemoteTransferAgent/` | SQL Server 共享目录中的远端传输助手 |
| `Tests/` | C++、界面与辅助脚本测试；数据库实测需显式提供样本 |
| `Tools/` | 维护脚本、隔离构建入口和企业微信文档读取客户端 |
| `docs/` | 全局规则、模块设计、操作和验证说明 |

纳入标准和本机配置见 [仓库内容管理](docs/repository-layout.md)。业务词汇见 [CONTEXT.md](CONTEXT.md)，当前跨模块行为以 [全局规则](docs/global-rules.md) 为准。

## 构建桌面程序

主工程为 `QtWidgetsApplication1.sln`，使用 Windows、Qt 5.12、MSVC v143、Win32 和 Qt MSBuild 集成。数据库功能需要 Qt SQL 模块及 32 位 SQL Server ODBC Driver；企业微信文档功能需要 WebView2 Evergreen Runtime。

`Tools/Build/build.ps1` 是 PowerShell 7 的本机隔离构建入口。脚本的构建属性记录本机 Qt、MSBuild、Windows SDK 路径；其他机器需按安装位置调整。所有产物写入 `BuildCheck/Repository/`，不替换现有启动程序。

```powershell
pwsh -File Tools/Build/build.ps1 -BuildConfiguration Release
pwsh -File Tools/Build/build.ps1 -CredentialTests
pwsh -File Tools/Build/build.ps1 -BridgeTests
pwsh -File Tools/Build/build.ps1 -SmokeTest
pwsh -File Tools/Build/run-smoke.ps1 -Environment Pro
python Tests/RepositoryChecks.py
```

构建使用 `/nr:false`，隔离 OutDir、IntDir、PDB/PCH、Qt 生成目录及 TLog，记录构建进程并按归属清理残留 PDB 服务，复核已有程序文件哈希。构建与验证约束另见 `docs/global-rules.md`。

### 211 数据库与共享凭据

本地 SQL 实例继续使用 Windows 身份验证。211 的 SQL/共享凭据不包含在源码中：复制 `QtWidgetsApplication1/SqlServer211.example.json` 为同目录的 `SqlServer211.local.json`，在本机填入配置。此文件被 Git 忽略。

程序默认从 EXE 所在目录读取该文件，也可通过 `LEMON_SQL211_CONFIG` 指定绝对路径。`LEMON_SQL211_USER`、`LEMON_SQL211_PASSWORD`、`LEMON_SMB211_USER`、`LEMON_SMB211_PASSWORD` 分别覆盖配置中的对应值。

本机正式构建可加 `-CopyLocalSqlConfiguration` 将配置复制到隔离输出目录；默认不复制，所有测试构建也不复制。发布程序前不要把本机配置打入安装包。共享已通过 Windows 登录时继续使用已有会话。

## Chrome 扩展

安装和迁移见 [扩展说明](LemonChromeExtension/README.md)。开发需要 Node.js 24 或更新版本：

```powershell
cd LemonChromeExtension
npm ci --ignore-scripts
npm test
npm run package
```

Chrome 加载生成的 `LemonChromeExtension/unpacked/`；ZIP、解包目录、`node_modules` 均为本机产物。`LemonLoginExtension/` 仅保留旧入口迁移说明，不再维护第二个插件。

## 验证与数据

自动化默认使用合成凭据、临时目录和本机模拟接口。真实数据库恢复测试由各测试文档中的环境变量显式启用；普通构建不会执行恢复或删除。不要提交客户账套、原始表格、抓包、登录文件或含真实数据的截图。历史验证记录属于其记录日期的结果，不表示每次整理仓库都重新运行全部实测。
