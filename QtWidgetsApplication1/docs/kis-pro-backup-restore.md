# 金蝶专业版数据库恢复

入口：工作台 → 文件处理 → **恢复备份包** → 备份来源软件选择 **金蝶 KIS 专业版**。

1. 选择备份并确认所有实例共用的暂存目录。
2. 选择或拖入一个 `.bak`，点击“读取金蝶数据库清单”。拖入会留存到共享输入目录。
3. 在读取按钮下方确认自动匹配的实例，可手动改选。切换保留备份清单、勾选及库名，只校验目标连接、版本和暂存文件访问，不重新读取备份包。核对目标 AIS 库名及服务器数据/日志目录，点击“恢复所选数据库”。同名库不会覆盖，可修改目标名后重试。

读取时提示备份来源版本与可用目标版本，并自动匹配兼容实例（当前兼容目标优先，其次本地，再考虑 211）。SQL Server 2000 备份使用 SQL Server 2005/2008/2008 R2 直接恢复，已关闭自动升级中转。无兼容实例时停止，读取清单不开始恢复。备份包目录及所有实例共用的暂存目录均可调整并持久化。

恢复后的公司名称来自业务库的 t_SystemProfile（General/CompanyName），自动登记到 AcctCtl.dbo.t_ad_kdAccount_gl（FDBName、FAcctName）。控制库仅满足参考导账工具采集，不用于启动金蝶客户端。已有控制表不兼容时明确报错，不修改其结构；业务库已恢复但登记失败时保留业务库并显示部分完成。

参考：`D:/workspace/main1/old/LemonClientAccImport/LemonClientAccImportQT_V3.0.0/SQLServer/Kingdee/kisproexporthandler.h` 的 GetAccountSetInfoList，及同目录 cpp 的 InitAccountSet。

验证：Tests/KisProRestoreTests.pro；隔离编译脚本 .diagnostics/kis-pro-restore/build.ps1，带进程归属复核、PDB 定向清理及原 EXE/PDB 哈希保护。

验证脚本中的真实样本测试需设置 QTEST_FUNCTION_TIMEOUT=3600000，避免默认 5 分钟限制在大备份共享传输时强制终止。交互页面及保留恢复运行器没有该 QtTest 超时。

本机保留恢复可通过 Tests/KisProRestoreRunner.pro 编译的运行器调用相同后端；参数为一个 BAK、暂存目录以及可选的本机实例名（例如 `.\SQL2008R2`）。始终使用备份内原 AIS 库名和 AcctCtl，已有库拒绝覆盖。


## 本次验证结果（2026-09-23）

用户指定的 `.\SQL2008R2`（SQL Server 2008 R2 Express 10.50.4000.0）中已保留：

| 数据库 | 采集账套名称（原库 CompanyName） | 科目数 | 凭证表行数 |
| --- | --- | ---: | ---: |
| AIS20150212103229 | 样本企业 A（名称已脱敏） | 226 | 1094 |
| AIS20171213103813 | 样本企业 B（名称已脱敏） | 440 | 7200 |

两库均 ONLINE、CHECKDB 通过，AcctCtl.dbo.t_ad_kdAccount_gl 有对应登记。原始 BAK SHA-256 在恢复前后相同。默认实例中先前恢复的 AIS20150212103229 及其 AcctCtl 登记也按用户保留要求留存。

主程序隔离构建、26 个功能入口/22 条离线路由及小窗口验证通过；小型 SQL 集成验证 5 项通过，覆盖恢复、取消、备份身份变化、控制表不兼容、重名保护、中文/引号名称登记及 CHECKDB。原大文件 QtTest 验证曾触发默认 5 分钟超时，测试脚本现显式设置 1 小时；保留恢复改用无 QtTest 时限的运行器。

211 第二套中转在改为本机恢复前曾中断，未对其失败根因作确定归因。最终两份原备份均在本机兼容实例完整恢复，不需要 211。211 数据库清单已复核与开始前一致；本次中转文件及中断库的 4 个物理文件均确认不存在（含定向清理的孤立日志文件）。本地暂存文件已清理，最终没有 MSBuild/cl/link/mspdbsrv 残留。最后的隔离构建保护核验覆盖 32 项原 EXE/PDB，存在状态和 SHA-256 不变。

详细证据在 `.diagnostics/kis-pro-restore/`：`fixture-results.txt`、`final-accounts.json`、`final-local-databases.json`、`final-remote-databases.json`、`remote-physical-cleanup.json`、`final-build-processes.json` 和 `sql2008-*.log`。

新版启动脚本：`.diagnostics/kis-pro-restore/Release/bin/Start-KisPro.cmd`。页面选择恢复到 `.\SQL2008R2` 即直接使用该实例；仍拒绝覆盖已经恢复的同名库。
