# Qt zlib headers

`zlib.h` 与 `zconf.h` 是 Qt 使用的 zlib 1.2.11 头文件；`zlib.h` 标记为 `1.2.11 (Qt)`，使用 `Q_CORE_EXPORT`。恢复与远端传输模块通过现有 QtCore 链接对应符号，没有另外打包 zlib DLL。

保留头文件内 Jean-loup Gailly、Mark Adler 的版权和 zlib 许可全文。升级 Qt 时需同时检查头文件、导出符号和 32 位 ABI，不能直接用其他版本头文件替换。
