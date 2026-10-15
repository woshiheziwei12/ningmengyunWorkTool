# 破解速达备份包

入口：主窗口 → 数据与维护工具 → 破解速达备份包。

与其他修复功能共享输入、输出目录；文件选择器从共享输入目录打开，默认在共享输出目录生成 `原文件名_解包.bak`，目录不存在时自动创建。
可直接拖入一个本地备份文件（页面、输入框和日志区均可），再点击“开始破解”；运行期间禁止更换文件或目录。
拖入文件的留存行为遵循[全局规则](../docs/global-rules.md)。
也可另存为其他新文件名，另存为所选目录同步为共享输出目录；切回页面时自动刷新共享路径。
解包在工作线程执行，可取消，完成后可打开输出目录。不会覆盖已有文件。
无需小畅导账、速达软件或额外附带的解包 DLL。

支持已验证的 `SD19960617-1998-0908-SDSOFT-ZWLQ` 单卷格式，以及 `SuperData SQLServer Backup File` v1 包装的单卷 CAB（速达 Pro `.SDB`）；结果须含 SQL Server 的 TAPE 文件标记。
输出保留备份原来的数据库版本：SQL Server 2000 备份不能直接还原至 SQL Server 2022。
SuperData/CAB 必须仅含一个 `SDBackup.bak` 文件，压缩包大小不超过 Windows Cabinet API 的有符号 32 位范围（小于 2 GiB）。其他速达格式、分卷包及非 SQL Server 内容不在本次支持范围。

## 实现

- `QtWidgetsApplication1/Common/SudaBackupUnpacker.h/.cpp`：181 字节头、16 KiB 分块逆变换、流式 zlib、校验、取消和临时文件保护。
- `QtWidgetsApplication1/SudaBackupWidget.h/.cpp`：本地解包页面。
- 固定种子 `0xBC2FF`；每字节 `seed = seed * 0x08088405 + 1`（32 位回绕），减去 `(uint64(seed) * 块内位置) >> 32` 的低 8 位，再送入 zlib。
- 块内位置从 1 开始，每 16 KiB 重置；随机状态跨块连续。
- `ThirdParty/zlib` 仅包含随本机 Qt 5.12.0 提供的 zlib 1.2.11 头文件及原版权声明；使用 Qt5Core 已导出的 `z_` 前缀函数，不使用出现崩溃的旧 `zlib1.lib`。升级 Qt 时须重新验证这些导出。
- SuperData 分支解析 31 字节标记、16 位版本、三个长度前缀字符串及 CAB 长度，不通过扫描 `MSCF` 猜测偏移；校验内外包大小与单卷属性。通过 Windows 自带的 `Cabinet.dll` / `FDICopy` 流式解包，链接 `Cabinet.lib`，无需附带解包工具或生成中间 CAB 文件。虚拟输入游标限定在内嵌 CAB 范围；输出始终写入指定目标旁的新临时文件，不使用包内文件名作为路径。取消、大小上限、完整性校验、防覆盖及失败清理与旧格式保持一致。

## 验证

本机 MSBuild Release/Win32 整体编译成功。所有构建目录（含 OutDir、IntDir、TLogLocation、QtIntDir）均隔离；原 EXE/PDB 的存在状态及哈希复核不变。

`Tests/SudaBackupTests.pro` 是独立 Qt 测试工程。请在新的构建目录里使用 Qt 5.12 的 qmake 和 MSVC x86 环境编译，勿在主工程目录执行清理。

```bat
rem 在新的、独立的测试构建目录运行
call "D:\app\visualStdio\base\VC\Auxiliary\Build\vcvarsall.bat" x86
"D:\app\qt5_12\5.12.0\msvc2017\bin\qmake.exe" "D:\learnspace\ningmengyunWorkTool\QtWidgetsApplication1\Tests\SudaBackupTests.pro" "CONFIG+=release" "CONFIG-=debug"
nmake /NOLOGO
set "PATH=D:\app\qt5_12\5.12.0\msvc2017\bin;%PATH%"
release\SudaBackupTests.exe "D:\Backup\KDG4X42026-09-11.BAK" "D:\app\小畅导账\Temp\KDG4X42026-09-11.bak" "suda-feature.png"
```

测试包含真实样本对照、损坏/截断/非 SQL Server 内容、16 KiB 边界、大小上限、取消、输出保护、页面输入/忙碌/完成状态。需传入本次真实样本及已解包的对照文件。
`TAPE` 签名和解压校验通过并不替代 SQL Server 的数据库完整性检查。

`Tests/SudaSuperDataTests.pro` 是独立核心回归工程，运行 `SudaSuperDataTests.exe "D:\test\速达pro\提示科目不存在\012026-09-19.SDB"`。新增测试在修改前复现文件头拒绝，修改后通过：真实 MSZIP 样本输出 65,228,288 字节，SHA-256 与独立 Windows expand 解包结果相同（`efc522c6269b0c9765075fca851cb4df02232254983b76c74ee2b55f7a67009e`）。还覆盖头部长度/终止符/版本、截断、尾部数据、压缩块损坏、多文件/分卷拒绝、路径穿越名称、取消、大小限制、防覆盖、临时文件清理、中文路径、存储型 CAB、旧格式跨块与损坏回归。验证范围只到解包内容一致，不还原或检查账套数据。

`Tests/SudaBackupWidgetTests.pro` 默认不依赖外部样本，使用生成的备份包验证共享输出、切页同步、页面/输入框/日志区拖放、忙碌状态、创建输出目录及防覆盖。使用独立构建目录编译，运行 `SudaBackupWidgetTests.exe [截图路径] [上述真实 SDB 样本路径]`；传入样本时额外验证真实 SDB 拖入留存、解包完成及输出哈希。测试使用临时 INI 配置和离屏窗口，不修改用户的共享目录设置。

2026-09-20：上述核心和页面测试均通过，完整 Release/Win32 隔离构建通过。原启动 EXE/PDB 的 10 项存在状态及哈希记录前后相同；定向清理并确认本次构建创建的 PDB 服务退出。过程记录位于 `.diagnostics/suda-pro-20260920/`。
