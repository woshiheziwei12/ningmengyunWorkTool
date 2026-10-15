# 科目合并验证

## 行为

- 不支持撤销。确认合并前显示影响及不可撤销提示；误操作后取消并重新打开原始包。
- 只保存当前账套、累计映射、操作次数和文字日志，不保留历史账套快照。
- 每次在新的 JSON 容器中组装科目、期初、凭证及根对象。期初先聚合再逐行写入；凭证数组不反复 replace。
- 检查每次 JSON 写入、映射目标及输出层级；失败不发布结果。内存不足和 JSON 存储超限均返回错误，界面恢复等待光标并保留当前数据。

## 引擎测试

`SubjectMergeTests.pro` 是独立 Qt Core 测试工程。在新的构建目录中使用 Qt 5.12/MSVC x86 的 qmake 和 nmake 编译，遵守仓库的进程审计和 PDB 清理规则。

```text
SubjectMergeTests.exe [AccountSet.json] [新的诊断输出.json]
```

无参数部分生成 18,000 条凭证，连续执行 24 次合并，并核对每条凭证编码、其他字段和计数；另测反向期初合计、末级/非末级混合、错误输入时源数据与调用方结果不变、JSON 导出重读。

可选 AccountSet.json 参数用于本次深圳市凯博样本：分别保留 3141、4104，再连续合并 12 组同名科目。输出路径必须是新文件；不覆盖输入。

修复前，该连续合并测试在第 5 轮因 JSON 存储超限失败。修复后，24 轮及真实样本测试全部通过。

## 界面与实际 ZIP 路径

`SubjectMergeUiTests.targets` 仅供隔离验证，通过 `ForceImportBeforeCppTargets` 导入。它替换测试构建的 main.cpp 和 subjectmergewidget.cpp 编译项，让测试直接使用私有对话框及真实读包、导出代码；正式项目不导入此文件。

```text
SubjectMergeUiTests.exe 原始.zip 独立诊断输出目录
```

必须同时隔离 OutDir、IntDir、QtIntDir、TLogLocation、编译 PDB/PCH、链接 PDB 和清理日志路径。MSBuild 必须带 `/nr:false`，构建前后记录原启动 EXE/PDB 存在状态和 SHA-256，并审计、清理本次 PDB 服务。

运行使用 Qt 的 **windows** 平台插件，APPDATA/LOCALAPPDATA 指向诊断目录。主测试窗口以 WA_DontShowOnScreen 渲染；消息框自动回答。Qt 5.12 的 offscreen 插件在 Windows QMessageBox 的系统菜单调用中存在访问异常，不适用于此测试。

覆盖撤销按钮不存在、未合并禁止导出、取消不变更数据、确认后计数和映射正确、连续合并、错误输入保持原状态、光标恢复、ZIP/CSV 导出，以及重新打开 ZIP 后内容与内存结果相同。保存 `subject-merge.png` 供视觉复核。

## 本次结果（2026-09-20）

- 引擎 24 轮生成样本、真实样本两种利润分配方向及 12 轮连续合并通过。
- Windows 插件下全部界面及 ZIP 导出重读测试通过，截图已复核。
- 完整 Release/Win32 隔离构建通过；原启动文件存在状态及哈希不变。
- 诊断脚本、构建审计和输出位于 `.diagnostics/subjectmerge-fix/`。正式启动 EXE 未替换。
