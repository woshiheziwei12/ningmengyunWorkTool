# 科目编码分隔符：混合 JSON 压缩包

ZIP 中可能同时包含 `AccountSet.json` 和 `FinancialTables.json`。此前程序要求每个 JSON 都具有非空 `GLAccountList`，因此将附带报表文件误判为缺少科目数据并终止整个任务。

当前规则：

- ZIP 内的 JSON 对象不含 `GLAccountList` 时，原样保留文件内容，不纳入已处理科目文件统计，并在处理日志中列出。
- 含 `GLAccountList` 的 JSON 继续执行编码分隔、父级更新、引用同步及 CSV 映射导出。
- 显式存在但为空或类型错误的 `GLAccountList`、无法解析的 JSON 仍然报错，并标出文件名。有效科目文件不能掩盖其他文件的这类错误。
- 整个 ZIP 没有可处理的科目文件时仍然失败。单独输入 JSON 时仍要求有效科目数据。
- 输入源文件保留，处理仅写工作副本和新输出文件。

## 回归验证

`Tests/SubjectCodeSeparatorTests.pro` 构建测试入口。测试直接调用页面实际使用的私有 ZIP 处理流程，不启动界面或连接网络。

在独立构建目录使用 Qt 5.12 MSVC 的 qmake/nmake 构建，遵循个人构建规则记录、复核编译进程和原启动 EXE/PDB；所有生成文件及最终产物必须位于该独立目录。运行时将相应 Qt `bin` 目录加入 PATH。

```text
SubjectCodeSeparatorTests.exe --fixtures
SubjectCodeSeparatorTests.exe "<待验证的原始 ZIP 路径>"
```

`--fixtures` 覆盖附带文件位于科目文件前后、附带 JSON 字节完全保留、父子编码及凭证/余额引用、CSV 映射、源 ZIP 不变、单 JSON 输入、缺失/空/错误类型科目列表、损坏 JSON 和混合有效/无效文件。

原始 ZIP 验证模式适用于一个科目 JSON 加任意附带 JSON，检查输出文件清单、附带文件逐字节一致、科目条数、引用与编码映射一致、非编码数据和源 ZIP 不变。所有测试输出均写入临时目录，结束后清理。
