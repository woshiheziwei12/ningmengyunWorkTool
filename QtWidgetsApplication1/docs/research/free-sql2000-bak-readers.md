# T3 SQL Server 2000 备份：免费本地处理方案

核查日期：2026-09-17。范围：现有 SQL Server 2000（内部版本 539）T3 备份，本机 Windows 11；用户已取消 211 中转，仅研究免费本地方式。本文不代表已安装或已完成样本恢复。

## 结论

没有核实到能直接在 Windows 11 上免费、完整且可靠地恢复当前 `.BA_` 样本的现成工具。可选路线是本地旧版 SQL Server Express，或对开源文件解析器做适配。后者有新的候选，但尚未实测当前备份。

## 本地数据库引擎

- [SQL Server 2008 R2 Express 官方下载](https://www.microsoft.com/en-us/download/details.aspx?id=30438)明确是免费产品，单数据库上限 10 GB。该版本是原生处理 SQL Server 2000 备份的可选引擎；微软的 [SQL Server 2012 升级指南](https://download.microsoft.com/download/9/5/3/9533501a-6f3e-4d03-a6a3-359af6a79877/sql_server_2012_upgrade_technical_reference_guide_white_paper.pdf)要求 SQL Server 2000 先经过 2005/2008/2008 R2 等中间版本。
- 直接安装在 Windows 11：无需购买数据库软件，但属于兼容性试验。本机尚未验证安装、启动和恢复成功。[微软兼容矩阵](https://learn.microsoft.com/en-us/troubleshoot/sql/database-engine/install/windows/use-sql-server-in-windows)不支持 Windows 11 上的 SQL Server 2008 R2，支持的版本从 2017 开始。不能把“不受官方支持”说成“必然不能运行”，也不能据此承诺可用。
- 本地虚拟机：VirtualBox 基础包采用 GPL；见[官方许可](https://github.com/VirtualBox/virtualbox/blob/main/doc/License-gpl-3.0.html)。[Oracle 文档](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/Introduction.html)列出 Windows 7 等旧版 Windows 客体，属于有限支持。虚拟机内运行适配旧 Windows 的 SQL Server Express 是可行架构，但旧 Windows 的适用授权与安装介质仍需具备，不能称为无条件零成本。
- 面向本项目的设计建议：若仅需要采集，可直接连接本机旧实例或本地虚拟机中的实例，完成恢复与控制库登记后采集，免去再次备份并搬入 SQL Server 2022。性能仍须实际测量，未给出提速倍数。

## 新发现的免费文件解析候选

[pfak/mssql-mdf-parser](https://github.com/pfak/mssql-mdf-parser)采用 MIT 许可，纯 Python 标准库实现。README 明确支持 SQL Server 2000、内部版本 539，能导出表结构及 CSV/SQLite 数据，无需运行 SQL Server。

它目前不能直接完成本任务：

1. 输入为 MDF，而当前文件是包含原生备份的 `.BA_`；备份集解析、数据文件重建仍是缺失环节，不是改扩展名即可解决。
2. 作者明确声明 decimal/numeric 输出未应用 scale，不能原样用于财务金额。
3. 离行 text/ntext/image 只返回指针，没有完整重组。
4. 无聚集索引的堆表可能包含孤立页，行完整性仍需验证。

因此它是本地定制采集的开发起点，不是经过验证的完整恢复工具。后续若选此路线，应先做一个年度的只读可行性验证，检查账套元信息、中文、精确金额、记录数和记录内容，再决定是否开发全部采集功能。本次没有执行第三方解析代码。

## 其他候选与排除项

- [BAK-SHELL](https://github.com/Dankular/BAK-SHELL)：虽然直接读取 BAK，但本次源码核查未找到明确 SQL Server 2000 支持分支；系统目录解析、金额和编码处理不能据此保证 T3 数据正确。未找到清晰的标准开源许可证，不能因为源码公开就认定可自由集成。
- 免费预览、试用版或每表限量导出的恢复软件不满足用户“免费完整采集”的要求，不作为可交付方案。

## 本次操作状态

211 中转恢复进程已停止；本次三个本地/远端暂存文件及所属已完成传输任务目录已清理。只读复核未发现本次临时数据库，211 原有九个年度账套库及 UFSystem 均为 ONLINE。本次研究没有安装软件，没有继续开发中转功能，也未回滚此前代码。
