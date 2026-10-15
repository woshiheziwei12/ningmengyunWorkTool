# 全局规则（活文档）

本文件记录当前生效的跨功能约定。调整全局行为时直接更新对应条目，保持规则与实现一致。

## Chrome 扩展中的云端账套功能

- `LemonChromeExtension/` 与 Qt 工程目录同级，提供网页内面板和独立工具页，复用当前 Chrome 登录进行备份下载、删除和专业版导账包上传；不依赖 Qt 运行时。安装加载 `LemonChromeExtension/unpacked/`，操作与验证说明见该目录源码的 `README.md`。
- 扩展按宿主页面识别正式/测试环境，调用与桌面端一致的 JWT、套餐、账套、备份、回收站和专业版导账接口；仅所选账套参与操作。读取/切换账号使旧选择失效，删除前复核目标身份并显示确认清单。
- 业务凭据仅驻留浏览器会话；业务接口请求通过单次临时规则携带套餐 Cookie，不修改网页 Cookie。统一扩展额外支持用户主动的桌面登录同步和当前网页 Cookie 获取/复制；不读取浏览器本地文件，不把凭据发往第三方。备份交给 Chrome 下载，确认下载完成后报告下载成功。
- 扩展上传通过文件选择/拖放读取浏览器 File，在内存副本上精确替换三个导账元数据数字字段，保留其他 JSON 原文与源文件。浏览器无桌面共享输入目录权限，因此本节不套用下文 Qt 拖入文件留存规则。ZIP 上限 128 MB、解压上限 256 MB，大文件继续使用桌面工具。
- 扩展使用 Node 离线接口与 DOM 测试，不启动 MSBuild。Chrome 实际安装、DNR 匹配及线上操作需单独验证；离线测试不等同真实账号联调，验证不得默认删除或上传用户账套。
- 浏览器原生 `fetch` 注入请求封装时必须保留全局作用域接收者，不可直接当作封装对象的方法调用。接口错误需标出阶段与接口域名/路径，隐藏查询参数及凭据；只读查询与可能已生效的写操作分别提示。离线测试额外模拟浏览器的原生调用接收者检查。
- `jproh5test2`、`jproh5test`、`jproh5graytest` 三个前端域名均路由到测试环境。浏览器扩展的测试套餐接口固定使用 `https://etest.ningmengyun.com`，避免站点 HSTS 将旧 HTTP 地址升级后导致单次认证规则与实际 URL 不匹配；错误提示保留协议。此浏览器适配不修改 Qt 的环境配置。

- 网页面板“登录 Cookie”按用户点击读取当前源页面 URL 和 Cookie Store 中的 `ningmengcookie`，不需要先读取账套，不回退到其他域名。默认遮挡，允许显示和复制，复制前重新读取；失败、切换页签、离开页面或 60 秒到期清除面板值，不改浏览器 Cookie。仅扩展面板可请求明文，不传给宿主页面、不写日志；原业务会话响应仍不暴露凭据。

## 桌面浏览器登录同步

- “打开柠檬云官网”使用系统默认浏览器配合统一的 `LemonChromeExtension/unpacked/`（“柠檬云账套工具”）同步当前桌面账号后打开对应官网；正式为 `wbgate`，测试为 `etest`，浏览器跳转统一 HTTPS。备份/删除/上传、登录同步及网页 Cookie 获取共用一个扩展，浏览器需设为默认 Chrome；缺少扩展时本机说明页提供安装步骤及不切换账号直接打开官网的入口。
- 每次点击仅在 `127.0.0.1` 临时端口创建 60 秒有效的连接，使用随机 256 位一次性票据、固定扩展 ID、Host/Origin 检查和有界请求；Cookie 仅通过扩展后台领取，不出现在网址、页面、日志或临时文件中。领取后立即清除桌面端副本，超时/完成/取消关闭监听；全过程设置 `sessionBusyBrowserLogin`。不启动独立浏览器配置或常驻后台服务。
- 登录助手只操作当前标签页 Cookie Store 中适用于目标网关的 `ningmengcookie`，先在内存保留原值及属性，清除重叠同名项，再写入 `.ningmengyun.com` 会话 Cookie（Secure、SameSite=Lax）并核对有效值；失败尝试恢复原登录，其他 Cookie 保留。Chrome 删除重叠同名 Cookie 时可能同时影响域 Cookie，不能先写新值再删旧项。正式/测试环境共用此域名 Cookie，点击会切换浏览器中的柠檬云账号，不伪造服务端登录有效期。同步流程使用 Cookie API 和本机连接；扩展还保留账套功能所需权限，并新增 clipboardWrite 供用户主动复制，不持久化 Cookie 副本。
- 验证入口：`Tests/BrowserLoginBridgeTests.cpp` 的真实本机 HTTP 测试、`LemonChromeExtension/tests/login-sync.test.js` 的扩展逻辑测试，以及 `browser-e2e.mjs` 的独立 Chrome for Testing 测试。所有测试使用合成凭据，浏览器测试拦截官网导航，不视为真实账号登录成功验证。安装及操作说明见 `LemonChromeExtension/README.md`。

## Qt 与 MSVC 编译兼容

- 主项目使用 Qt 5.12 和 MSVC v143。Qt 的 `QList` 等头文件仍调用 `stdext::checked_array_iterator`，新版 MSVC 将其标记为 STL4043 弃用项，现有 SDL 检查配置会产生 C4996 编译错误。
- 正式 `QtWidgetsApplication1.vcxproj` 在公共 `ClCompile` 配置中定义 `_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING`，覆盖 Debug/Release Win32，并通过 `%(PreprocessorDefinitions)` 保留继承项。保留 SDL 检查，只对这一项第三方兼容提示进行抑制。
- 验证编译时，隔离配置只负责产物、中间文件和生成文件路径，不能额外注入正式项目缺少的兼容宏来掩盖正常构建问题。修改编译兼容配置后，使用正式项目分别验证 Debug、Release，遵守构建进程复核及原 EXE/PDB 哈希保护规则。
