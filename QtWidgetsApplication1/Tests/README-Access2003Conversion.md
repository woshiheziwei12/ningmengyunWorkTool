# Jet3 数据恢复与 Access 密码循环回归

默认模式将 Jet3 业务表恢复到新的 Jet4 文件，不启动 Access。需要本机 32 位 Jet/DAO 3.6、Windows PowerShell；旧 Access 模式仍需 Access.Application.11。客户数据不纳入仓库，测试在隔离目录复制输入后运行并复核源 SHA-256。

## 构建及运行

使用 PowerShell 7：

```powershell
pwsh -NoProfile -File Tools/Build/build.ps1 -AccessConversionTests
& "$env:WINDIR/SysWOW64/WindowsPowerShell/v1.0/powershell.exe" -NoProfile -ExecutionPolicy Bypass -File Tests/New-AccessConversionFixtures.ps1 -OutputDirectory "$PWD/BuildCheck/AccessFixtures"
& ./Tests/Run-Access2003ConversionTests.ps1 -SourcePaths ./BuildCheck/AccessFixtures/plain-jet3.mdb,./BuildCheck/AccessFixtures/protected-jet3.mdb -Expectation recovered -Repeat 2
& ./Tests/Run-Access2003ConversionTests.ps1 -SourcePaths ./BuildCheck/AccessFixtures/compatible-jet4.mdb -Expectation copied
& ./Tests/Run-Access2003ConversionTests.ps1 -SourcePaths ./BuildCheck/AccessFixtures/plain-jet3.mdb -Expectation timeout
```

测试程序及恢复组件位于 `BuildCheck/Repository/AccessConversion-Release/bin`。构建沿用独立 OutDir / IntDir / PDB / PCH / Qt / TLog、`/nr:false`、进程审计和原 EXE/PDB 哈希保护。主工程自动构建恢复组件。

`recovered` 要求业务表和记录非空、报告存在，且没有启动 Access；`copied` 验证 Jet4 直接复制；`failure` 验证明失败且不留数据库文件。成功输出检查 Jet4 文件头。外层测试超时默认 45 秒，可按批量数量调整。产品单文件超时 180 秒，内层 Jet4 写入 150 秒。设置 `JET_RECOVERY_TEST_TIMEOUT_MS=500` 可在恢复子进程工作期间验证强制超时。

## 独立数据类型校验

`Test-JetRecoveryTypes.ps1` 由 32 位 Windows PowerShell 运行。不带 `RecoveredPath` 时创建合成 Jet3；带上该参数后，通过独立 OleDb 查询比较源和目标，不以原生读取器自身作为预期结果。涵盖全部 12 种支持类型、显式 AutoNumber、NULL/空串、中文、分页 Memo/OLE、负日期和精确 Currency。

```powershell
$ps32="$env:WINDIR/SysWOW64/WindowsPowerShell/v1.0/powershell.exe"
& $ps32 -NoProfile -ExecutionPolicy Bypass -File Tests/Test-JetRecoveryTypes.ps1 -FixturePath "$PWD/BuildCheck/types-jet3.mdb"
# 恢复后填入实际目标路径。
& $ps32 -NoProfile -ExecutionPolicy Bypass -File Tests/Test-JetRecoveryTypes.ps1 -FixturePath "$PWD/BuildCheck/types-jet3.mdb" -RecoveredPath '<恢复目标路径>'
```

测试程序直接支持 `ui <隔离输出目录> <Jet3样本>`，检查默认模式、密码框切换、拖入留存、忙碌状态、完成日志和截图；以及 `preserved <隔离输出目录> <Jet3样本>`，确认已有输出不被覆盖。需将 Qt bin 加入 PATH；界面测试使用 Windows Qt 插件及隐藏窗口，不能使用本机 Qt 5.12 的 offscreen 插件（截图时会崩溃）。

## 旧 Access 模式

```powershell
$env:ACCESS_TEST_PASSWORD='intentionally-wrong'
try {
    & ./Tests/Run-Access2003ConversionTests.ps1 -SourcePaths ./BuildCheck/AccessFixtures/protected-jet3.mdb -Expectation password-rejected -Repeat 2
} finally { Remove-Item Env:/ACCESS_TEST_PASSWORD }
```

每文件只自动提交一次密码；同一弹窗持续 5 秒或再次索要密码就取消，下一文件独立计数。弹窗操作只针对本次 Access COM 实例的 hWndAccessApp 所属进程。测试脚本外层超时不按进程名终止 Access，须单独核实残留归属。

## 本机验证与边界（2026-11-10）

- 两份实际 AIS 在集成入口各连续恢复两次：每份 172 张业务表、分别 5137 / 6335 条记录；全部字段逐行多重集校验通过，源文件哈希不变，未启动 Access。
- 各恢复 292 个索引；GLVch/InternalID 源索引无法完整解析，报告和界面明确列为遗漏。权限、关系约束、保存查询、窗体和宏不在恢复范围内。结果仅供查询、导账，不直接替换原账套继续记账。
- 合成带密码 Jet3 的原生恢复、Jet4 复制、全类型独立查询对比、截断失败、超时清理、旧模式重复错误密码及界面流程通过。Release / Debug 主程序与 Release 恢复组件均隔离编译通过。
- 原生读取核查确认两份客户 Jet3 的数据库密码字段为空、数据页未标记加密；历史 DAO 3031/3033 错误和 Access 密码弹窗不足以断定数据库有密码。原生路径绕过了本机 Access 既有兼容/认证异常，没有修复 Access 引擎本身。测试期间本机 DAO/OleDb 也曾间歇返回密码无效，重试后通过；产品遇到引擎失败会报告失败并撤回输出，不会自动循环重试。
