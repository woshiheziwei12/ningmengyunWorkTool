# Jet3 数据恢复组件

`LemonJetRecovery.exe` 随工作助手主程序部署，使用 Qt5Core Release 和 Windows 自带 PowerShell/.NET；本机还需 32 位 Jet/DAO 3.6。无需完整 Access，不依赖导账工具源码目录、Python、诊断脚本或网络服务。PowerShell 脚本编入 Qt 资源，仅在运行时释放到本次暂存目录，不修改系统执行策略。

主工程自动构建此组件，始终为 Release Win32，以匹配导账工具现有 MDB Tools SDK 的 /MD ABI。组件 OutDir 跟随主程序，IntDir 使用主工程的 JetRecovery 子目录，Qt/PDB/PCH/TLog 均隔离。不要将此静态库直接链接进 Debug 主程序。

主程序通过 Windows Job 监督组件，启动握手前不会读库或创建子进程。组件只读取源文件快照，扫描业务表后通过 DAO/OleDb 新建 Jet4，写入及建立索引后分别回读比较全部业务字段。完成后输出无密码 Jet4 和 `.recovery.json`（源/目标哈希、表数、行数、索引数、遗漏项目、用途限制）。报告含本地文件路径，不含数据库密码或业务记录。

支持 Jet3 类型 1–12；其他类型、读取错误、无效编码和超限输入明确失败。输入最大 256 MiB、50 万行、400 万单元格；单个长值最大 64 MiB、JSON 快照最大 128 MiB。仅恢复业务数据和可解析索引，不恢复工作组权限、关系约束、查询/窗体/宏。结果用于查询、导账，不直接替换原账套继续记账。

发布时保留 `QtWidgetsApplication1/ThirdParty/MdbTools/COPYING.LIB` 和 README/预编译清单。SDK 来源和 ABI 说明见该目录。回归步骤见 `Tests/README-Access2003Conversion.md`；客户账套及临时快照不得纳入源码。
