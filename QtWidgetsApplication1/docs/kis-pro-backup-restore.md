# 金蝶专业版数据库恢复

入口：工作台 → 文件处理 → **恢复备份包** → 备份来源软件选择 **金蝶 KIS 专业版**。

1. 选择备份并确认所有实例共用的暂存目录。
2. 选择或拖入一个 `.bak`，点击“读取金蝶数据库清单”。拖入会留存到共享输入目录。
3. 在读取按钮下方确认自动匹配的实例，可手动改选。切换保留备份清单、勾选及库名，只校验目标连接、版本和暂存文件访问，不重新读取备份包。核对目标库名及服务器数据/日志目录，点击“恢复所选数据库”。原库名和目标库名均不要求 AIS 前缀，目标名只含字母、数字和下划线；系统库及采集控制库不可用作目标。同名库不会覆盖，可修改目标名后重试。

读取时提示备份来源版本与可用目标版本，并自动匹配兼容实例（当前兼容目标优先，其次本地，再考虑 211）。SQL Server 2000 备份使用 SQL Server 2005/2008/2008 R2 直接恢复，已关闭自动升级中转。无兼容实例时停止，读取清单不开始恢复。备份包目录及所有实例共用的暂存目录均可调整并持久化。

恢复至 ONLINE 后检查导账工具使用的 t_SystemProfile、t_Account、t_Voucher、t_VoucherEntry 关键字段，允许凭证表为空。通过后从 t_SystemProfile（General/CompanyName）读取公司名称，自动登记到 AcctCtl.dbo.t_ad_kdAccount_gl（FDBName、FAcctName）。缺表、缺字段或公司名称无效时保留已恢复库并报告数据校验失败，不登记为可采集账套。该检查验证核心结构兼容，不代表全部模块导账已通过。控制库仅满足参考导账工具采集，不用于启动金蝶客户端。已有控制表不兼容时明确报错，不修改其结构；业务库已恢复但登记失败时保留业务库并显示部分完成。

参考：`D:/workspace/old/LemonClientAccImport/LemonClientAccImportQT_V3.0.0/SQLServer/Kingdee/kisproexporthandler.h` 的 GetAccountSetInfoList，及同目录 cpp 的 InitAccountSet、InitAccountSubject、InitVoucher。导账工具按控制表中的库名读取账套，不依赖 AIS 前缀。

验证：Tests/KisProRestoreTests.pro；隔离编译脚本 .diagnostics/kis-pro-restore/build.ps1，带进程归属复核、PDB 定向清理及原 EXE/PDB 哈希保护。

验证脚本中的真实样本测试需设置 QTEST_FUNCTION_TIMEOUT=3600000，避免默认 5 分钟限制在大备份共享传输时强制终止。交互页面及保留恢复运行器没有该 QtTest 超时。

本机保留恢复可通过 Tests/KisProRestoreRunner.pro 编译的运行器调用相同后端；参数为一个 BAK、暂存目录以及可选的本机实例名（例如 `.\SQL2008R2`）。使用备份内原库名和 AcctCtl，原库名不符合目标字符规则时需改用页面指定目标，已有库拒绝覆盖。


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

## 非 AIS 库名支持验证（2026-11-02）

- UE 前缀的 SQL Server 2012 原生备份在修改前可稳定复现库名拦截；修改后真实样本全套测试 6 项通过，空凭证小型夹具全套测试 6 项通过。覆盖非 AIS 原库/目标名、禁止覆盖及控制库保护，以及缺少凭证明细字段时恢复库保留但不登记。
- 样本已通过同一恢复后端保留到本机默认实例，ONLINE、CHECKDB 和 AcctCtl 登记通过；原始 BAK 哈希不变。随机测试库已清理。
- 使用现有 LemonAcctImporterCli 的 KisPro 入口实际枚举并选择样本，成功生成本地基础财务采集包：科目 83、期初 53、凭证明细 3277，与源库凭证明细数量一致。附加模块关闭，未上传云端。
- 对本机两个 KIS 库直接读取结构：AIS 库 961 张实体表、UE 库 4017 张，共有 817 张。不同库名前缀不能代替结构或实际采集验证；例如 t_User 在两库分别为表和视图，采集器均可读取。
- 主程序隔离构建通过，原启动 EXE/PDB 共 28 项存在状态和哈希不变，本次 PDB 服务定向清理后最终无 MSBuild/cl/link/mspdbsrv 残留。新版启动脚本：`.diagnostics/kis-pro-non-ais/Release/bin/Start-KisPro.cmd`。结构明细及运行证据见 `.diagnostics/kis-pro-non-ais/`。
