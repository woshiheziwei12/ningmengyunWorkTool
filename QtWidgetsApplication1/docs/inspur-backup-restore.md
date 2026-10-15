# 浪潮备份恢复

入口：工作台 → 文件处理 → **恢复备份包** → 备份来源软件选择 **浪潮**。

读取备份时显示来源版本和兼容范围，并自动选择兼容实例；优先保留兼容的当前目标，再选择其他本地实例，最后考虑 211，不进行中转。扫描时保存本地实例版本；211 固定版本为已确认的 SQL Server 2008 R2 Express 10.50.4000.0。备份包目录和所有实例共用的暂存目录可调整并持久化。

1. 选择备份并确认所有实例共用的暂存目录。
2. 选择或拖入 `.MSS` 备份包，也支持已经解出的原生 `.BAK`。拖入文件会先留存到共享输入目录。
3. 点击“读取浪潮数据库清单”，检查原库名、备份时间、目标库名及服务器数据/日志目录。本地暂存默认 `D:\Backup`。
4. 在读取按钮下方确认自动匹配的实例，可手动改选。切换保留备份清单、勾选及库名，只校验目标连接、版本和暂存文件访问，不重新读取备份包。点击“恢复所选数据库”。已有目标库不会覆盖，重名时修改目标库名后再恢复。

MSS 包内的 INI 和 BAK 必须同路径同名且唯一。支持 UTF-8 和 GB18030 的 INI，不使用包内密码。业务库名须以字母或下划线开头，仅含字母、数字、下划线，最长 128 字符。

恢复时自动创建或复用 `cwmaster.dbo.gsdbztxx`，登记目标库名、账套显示名称及业务表架构。依据参考工程 `D:\workspace\main1\old\LemonClientAccImport\LemonClientAccImportQT_V3.0.0` 的 `SoftwareProfileRegistry.cpp` 和 `InspurPsAccountSetManager::GetAccountSetList`：采集连接系统库 `cwmaster`，通过 `gsdbztxx_owner + ',' + gsdbztxx_base` 定位账套。该最小控制库用于导账采集，不等同完整浪潮系统安装。

MSS 保留 INI 的 `AccName`，并核对 `DBOwner` 对应恢复库内的 `LSCONF`。裸 BAK 必须只有一个 `LSCONF` 所属架构，账套名称取 `CW_SYDWMC`。不会把非 dbo 架构改为 dbo，也不会改写业务数据。

控制表缺列、额外必填列或登记冲突时不会修改已有结构和记录。业务库已恢复而登记失败时明确显示部分完成，并保留业务库供排查。停止恢复会等待当前 SQL 操作及登记结束。

SQL Server 高版本备份不能降级恢复；SQL Server 2000 备份应选择 SQL Server 2008 R2 等兼容实例。此入口不自动连接中转服务器。

验证入口：`Tests/InspurRestoreTests.pro`。离线覆盖 INI 编码及一致性、解包路径与数量边界、防覆盖、拖放留存及实例切换。设置 `INSPUR_TEST_SOURCE`、`INSPUR_TEST_STAGING`、`INSPUR_TEST_SERVER` 启用真实恢复验证，使用随机业务库和随机控制库，验证与采集器相同的账套查询及 DBCC CHECKDB，再定向清理测试库。
