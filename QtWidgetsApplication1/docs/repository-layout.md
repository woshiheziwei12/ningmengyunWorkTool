# 仓库内容管理

## 纳入版本控制

- 桌面、CLI、统一 Chrome 扩展、远端传输助手的源码、工程文件及产品资源。
- 可复用的测试源码、构建定义、打包与维护脚本；合成测试数据可随测试代码生成。
- README、领域词汇、全局规则、功能设计和脱敏后的问题分析。旧实测记录保留时间和适用范围，不冒充当前验证。
- 第三方固定版本依赖及许可证：WebView2 SDK 的头文件和 x86 `WebView2Loader.dll`、Qt 使用的 zlib 头文件、扩展使用的 fflate。DLL 采用精确白名单，不放开整个二进制目录。
- 只有结构和空值的配置示例、Node 依赖锁文件。

## 留在本机

| 类别 | 路径或文件 | 原因 |
| --- | --- | --- |
| 编译产物 | `Release/`、`Debug/`、`BuildCheck/`、`dist/`、EXE/PDB/OBJ/PCH/TLog | 可重新生成，体积大且与机器相关 |
| 调试记录 | `.diagnostics/`、`.codex-work/`、日志、截图、抓包、进程审计 | 属于现场数据，可能含账号、业务数据或本机路径 |
| 登录与凭据 | `LemonApp*.dat`、`session.dat`、`SqlServer211.local.json`、`.env*` | 含密码或会话信息；示例 `.env.example` 可提交 |
| 用户业务数据 | 账套 ZIP、数据库备份、MDF/LDF、Access 文件、新中大原始表文件 | 属于用户数据，不能作为源码依赖 |
| 运行状态 | 导账历史、临时传输任务、缓存 | 随运行变化，不是产品定义 |
| 开发依赖和包 | `node_modules/`、扩展 `unpacked/` 和 ZIP、Python 缓存 | 根据锁文件或源码生成 |
| IDE 状态 | `.vs/`、`.vscode/`、`.idea/`、`.user` 等 | 开发者本机配置 |

排除通过 `.gitignore` 实现，不删除本机原始文件。若要保留可复用的诊断脚本，应先整理成 `Tools/` 或 `Tests/` 下的独立入口，移除真实凭据、客户样本和对临时目录的隐式依赖，再提交。

## 构建与测试目录

- 维护的隔离构建入口统一放在 `Tools/Build/`；`BuildCheck/BrowserLogin/` 中的旧脚本保留在本机，已退出版本控制。
- 新隔离构建写入 `BuildCheck/Repository/<模式>/`，构建审计和截图留在此目录。测试入口包括 `-CredentialTests`、`-BridgeTests`、`-SmokeTest`。
- 各功能的 qmake `.pro` 与 MSBuild `.targets` 测试定义保留在 `Tests/`。它们需要在隔离目录中构建；不要将数据库实测环境变量设为默认值。
- 浏览器端到端测试使用独立 Chrome for Testing、合成 Cookie 和本机拦截页面，不连接个人浏览器或真实账套接口。

## 本机凭据迁移

2026-10-15 整理时，将恢复页面、自动实例路由、数据库清理和 211 共享连接中的固定密码移至被忽略的 `QtWidgetsApplication1/SqlServer211.local.json`，本机原值保留。源码只读取配置或环境变量；提交中的示例不含密码。

SQL 默认用户名为 `sa`，没有默认密码；SMB 没有默认凭据。部署人员按 README 配置，运行日志不能输出配置内容。数据库清理及 SQL 连接在缺少密码时报告配置问题；共享已有 Windows 会话时不覆盖或断开。

客户企业名称已从待提交的历史报告和合成测试中移除；原报告保存在被忽略的 `.diagnostics/repository-organize-originals/`。T+、U8 实测可分别用 `TPLUS_TEST_EXPECTED_NAME`、`U8_TEST_EXPECTED_NAME` 提供样本名称断言，不把客户名称写入源码；U8 仍核对登记名称与原业务库元数据一致。

## 提交前检查

检查 `git status` 和暂存差异，确认源码依赖完整，凭据文件、业务样本、构建产物未进入索引；用 `git check-ignore` 核对新增排除项。新增第三方依赖须记录来源、版本和许可证；执行与实际修改相符的离线检查，并保留真实环境验证的边界说明。

`python Tests/RepositoryChecks.py` 检查已纳入索引的文件、Qt 工程和资源依赖、生成文件白名单及关键忽略规则，并在本机配置存在时检查密码没有残留在源码中；输出只包含文件路径，不输出密码。
