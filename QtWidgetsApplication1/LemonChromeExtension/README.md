# 柠檬云账套工具 · Chrome 扩展

代码位于 `LemonChromeExtension/`，与 `QtWidgetsApplication1/` 工程目录同级。扩展独立运行，不需要启动或编译 Qt 程序。

## 1.1.0：合并为一个扩展

只安装本项目的 `LemonChromeExtension/unpacked/`，名称仍是“柠檬云账套工具”。它同时提供：账套备份下载、删除、导账包上传，桌面工具“打开官网”的登录同步，以及网页面板中的 ningmengcookie 获取/显示/复制。

**从旧版迁移一次：**在 `chrome://extensions/` 移除原来的“柠檬云账套工具”和单独的“柠檬云登录助手”，再加载这里的 `unpacked`。统一版沿用登录助手的固定 ID `nmahgegihmcfiendmbakhelgokloehgi`，不能对旧账套扩展直接刷新以改变 ID；后续版本只需重新加载统一版。移除扩展不会清除柠檬云网站的登录 Cookie。固定 ID 保持与已发布的桌面同步版兼容。

### 网页获取 ningmengcookie

1. 打开已登录的柠檬云网页，点击右下角“账套工具”（也可使用“独立工具页”）。
2. 切到“登录 Cookie”，点击“获取 Cookie”。不需要先读取账套，不请求 JWT 或套餐接口。
3. 值默认遮挡；可点击“显示”，或“复制 Cookie”。复制会重新读取当前页 Cookie，避免复制切换账号前的旧值。
4. 切换工具页签、离开页面或点击“清除显示”会清除面板值，最长保留 60 秒；不删除浏览器 Cookie。只有用户主动复制才写入剪贴板，不自动清空或改写用户后续的剪贴板内容。

读取使用当前源页面的准确 URL 和所属 Cookie Store，不回退到另一个域名或桌面账号；支持 Cookie API 能读取的 HttpOnly Cookie。凭据只展示在扩展自己的页面/iframe 中，不发给宿主网页脚本、不写入操作日志或持久存储。未登录、来源页变化、权限缺失时清除旧值并显示失败。

### 桌面“打开官网”同步

先将安装本扩展的 Chrome 设为默认浏览器，在桌面同步版程序登录并点击“打开官网”。扩展通过本次点击创建的 60 秒本机连接接收授权，替换目标网关适用的 ningmengcookie，再跳转官网。正式和测试环境共用 `.ningmengyun.com` Cookie，因此此操作会切换浏览器的柠檬云账号。

不再需要单独的 `LemonLoginExtension`。现有 `Release/启动浏览器同步版.cmd` 对应的同步协议保持兼容。未安装统一版时，连接页面会停留在安装提示；安装后回桌面重新点击即可。

授权只通过 `127.0.0.1` 临时端口和 256 位一次性票据交给固定扩展 ID；Cookie 不进入网址、连接页面、日志或临时文件。桌面领取后清除桥接副本，完成/失败/超时关闭监听；不安装常驻服务。Cookie 替换前在内存保留原值与属性，清除重叠同名项后写入 Secure、SameSite=Lax 会话 Cookie并核对有效值；失败尝试恢复原登录。业务接口请求继续使用原有临时 DNR 规则，不修改网页 Cookie。

## 1.0.2 测试环境修复

- 测试套餐列表改为直接访问 `https://etest.ningmengyun.com/wb/plan`。该站点的 HTTPS 响应已启用 HSTS（`max-age=31536000; includeSubDomains`）；旧的 HTTP 地址可能被浏览器升级，使按原 URL 匹配的单次认证规则失效。保持拒绝任意重定向的策略，直接使用已经验证可达的 HTTPS 地址。
- `jproh5test2.ningmengyun.com`、`jproh5test.ningmengyun.com`、`jproh5graytest.ningmengyun.com` 都识别为同一测试环境，并有对应回归测试；不会调用正式环境接口。错误提示现在保留 HTTP/HTTPS 协议，方便区分失败地址。
- 已验证匿名 HTTP/HTTPS 套餐接口均返回 HTTP 200，HTTPS 返回上述 HSTS 策略。模拟 HSTS 升级导致单次规则不匹配的回归测试由失败转为通过；用户浏览器内的已登录测试环境仍待更新后确认。正式环境已由用户确认可以读取。
- 更新后在 `chrome://extensions/` 重新加载扩展，再刷新测试页面，确认面板显示 `v1.0.2`。

## 1.0.1 修复

- 修复请求封装将原生 `fetch` 当作普通对象方法调用的问题：在 Chrome Worker 中会因接收者不匹配触发 `Illegal invocation`，旧版错误处理将其统一显示为网络失败。现在显式绑定全局作用域，并添加模拟浏览器接收者检查的回归测试；原有箭头函数模拟未覆盖此行为。
- 失败提示包含具体阶段和接口域名/路径，区分内部调用错误、网络、HTTP 和解析错误；不显示查询参数或登录凭据。只读操作失败不再显示「已提交的操作请先核对结果」。
- `jpro6.ningmengyun.com` 本来就在正式环境及站点权限范围内，无需修改域名。更新后到 `chrome://extensions/` 点击扩展的重新加载按钮，再刷新柠檬云页面；面板标题下应显示 `v1.0.1`。
- 本次已复现上述调用模式的失败并验证修复，不代表用户浏览器的其他授权或网络问题已经排除。登录接口无凭据连通性检查为 HTTP 200；真实登录仍需重新加载扩展后验证。

## 安装（不需要 Node.js）

1. 在 Chrome 地址栏输入 `chrome://extensions/`。
2. 打开右上角「开发者模式」，点击「加载已解压的扩展程序」。
3. 选择本项目的 `LemonChromeExtension/unpacked` 目录（里面有 `manifest.json`）。如果使用 ZIP 安装包，先解压 `LemonChromeExtension.zip`，选择解压后直接包含 `manifest.json` 的 `LemonChromeExtension` 文件夹。
4. 登录柠檬云，刷新已打开的柠檬云页面，点击右下角「账套工具」。也可把扩展固定到 Chrome 工具栏后点击图标。
5. 点击「读取账套」，确认显示的账号和环境后使用。

Chrome 120 或以上版本；仅针对 Chrome 制作。扩展更新后在扩展管理页点击刷新，并刷新柠檬云页面。不要把包含 `node_modules` 和开发缓存的源码根目录作为发布安装目录；使用已整理好的 `unpacked`。

若网页自身的 iframe/CSP 策略阻止面板，点击工具栏图标收起空白面板，再点击网页右下角「独立工具页 ↗」。该工具页仍绑定原柠檬云页面；原页面需保持打开。

## 使用

### 备份下载

- 勾选一个或多个账套，点击「备份并下载」。全选只选择当前搜索结果，隐藏的已有选择保留，底部显示总选择数。
- 逐个发起备份，每 3 秒查询进度。完成后交给 Chrome 下载；确认 Chrome 下载完成后才显示「备份已下载」。
- 保存到 Chrome 默认下载目录中的 `柠檬云备份/`，名称包含账套名称、编号和时间，不覆盖已有文件。Chrome 开启了每次询问保存位置时仍可能弹出保存对话框。
- 等待最多 15 分钟。点击「停止后续任务」会停止轮询与后续账套，不撤回服务器已受理的备份，不取消已开始的 Chrome 下载。同一面板中再次点击备份会继续尚未完成的任务。

### 删除账套

- 选择账套后，确认框列出名称、账套编号、套餐编号、账号和环境。取消不提交请求。
- 使用现有桌面工具的 `DropAccountSetToRecycleBinV2` 接口，将账套移入回收站。实际权限与回收规则以柠檬云为准。
- 每次删除前重新读取该套餐的账套列表，核对编号、appAsId、名称和浏览器登录身份。只处理所选项；任一删除失败即停止后续删除，不自动重试。

### 上传专业版导账包

- 切换到「上传导账包」，选择目标套餐，选择或拖入一个 ZIP，核对确认框后提交。
- 在后台 Worker 中修改上传副本里的 `AccountSet.json`：`AccountSetType=1`、`AAId=0`、`ProServiceId=所选套餐编号`。不会写回原文件。
- 精确替换这三个数字字段，其他 JSON 原文保留，包括超过 JavaScript 安全整数范围的编号和高精度金额。ZIP 内其他文件的解压内容保持一致，压缩元数据可能变化。
- ZIP 必须恰好有一个 `AccountSet.json`，且目标字段唯一且为整数。最大压缩文件 128 MB、解压合计 256 MB；超过限制的文件请用现有桌面工具。
- 默认使用对应环境的专业版导账服务。上传表单字段名为原文件名去掉 `.zip`，与 Qt 实现一致；不支持本地导账服务地址。
- 上传完成显示任务 ID，并每 10 秒查询导入结果，最多等待 15 分钟。结果返回只表示收到服务端结果，是否导入成功需看结果内容。可以粘贴已有任务 ID 再次查询。
- 上传超时、网络失败或响应丢失可能发生在服务端已经接收后，请先在网页核对导入记录，避免重复提交。「停止后续任务」不会撤回正在上传或已经提交的导入任务。

执行期间保持工具所在页面打开；「收起」不会销毁面板或中断任务。刷新、关闭页面、重载扩展会中断本地等待，已提交的云端任务可能继续；操作记录只保留在当前面板。重新读取账套会清空选择和本地任务状态。

## 环境与认证

- 根据原页面域名是否包含 `test` 自动选择正式/测试环境，面板顶部展示环境，不提供任意服务地址输入。
- 正式：`auth.ningmengyun.com`、`wbgate.ningmengyun.com`、`jproapi.ningmengyun.com`、`prodaozhangapi.ningmengyun.com`。
- 测试：`authtest.ningmengyun.com`、`etest.ningmengyun.com`、`jproapitest2.ningmengyun.com`、`proimportertest2.ningmengyun.com`。套餐接口 `etest` 直接使用 HTTPS 以适配浏览器 HSTS，其余接口协议沿用现有 `Resource/http/Dev/Account.json`。
- 读取当前页面所属浏览器 Cookie Store 中的 `ningmengcookie`，通过 `/v2/api/jwt` 换取 JWT。套餐请求使用 JWT；上传及结果查询使用 `ningmengcookie` 作为 Bearer，与当前桌面实现一致。
- 凭据仅保存在 Chrome `storage.session` 和临时请求规则中，不写入本地持久存储、不显示到宿主网页、不输出到操作日志。用户主动获取 Cookie 时仅在扩展面板中短时显示，详见上文。关闭浏览器清除会话；原页面关闭也清除绑定的会话。
- 临时 DNR 规则仅匹配带随机标识的单次扩展请求，按请求拼装套餐 Cookie 和必要请求头；业务接口请求不调用 `cookies.set/remove`；仅用户从桌面发起的登录同步会替换对应登录 Cookie。规则在请求后清理，异常残留最长约 16 分钟由定时器清理。
- 不读取本地浏览器数据库、网页 localStorage、密码，不监听用户浏览流量。没有遥测、远程脚本或第三方服务。

权限：`cookies` 用于复用登录、按需读取及桌面同步；`clipboardWrite` 用于主动复制 Cookie；`storage` 用于浏览器会话内保存状态；`downloads` 用于保存备份和核对下载状态；`declarativeNetRequestWithHostAccess` 用于独立请求头；`alarms` 用于清理临时规则。站点权限限 `ningmengyun.com` 及子域的 HTTP/HTTPS，以及用于桌面登录同步的 `http://127.0.0.1/*`。

## 开发与验证

2026-10-15：统一版已通过全部 44 项离线测试，以及独立 Chrome for Testing 中正式/测试两环境的桌面同步、真实 Cookie API 读取、复制前刷新、遮挡/显示和未登录处理验证。官网导航在本机拦截，剪贴板使用内存测试接收器；未触碰个人浏览器 Cookie 或系统剪贴板。界面截图位于 `BuildCheck/UnifiedExtension/browser-e2e/`。



源码文件：`background.js` 认证与业务接口，`transport.js` 请求规则，`content.js` 页面入口，`panel.*` 面板，`zip.js` / `zip-worker.js` 压缩包处理。

开发测试使用 Node.js 24 或更高版本：

```powershell
npm ci --ignore-scripts
npm test
npm run package
```

运行扩展不依赖 npm。`vendor/fflate.js` 为固定版本 fflate 0.8.3 的 ESM 浏览器包，MIT 授权，许可证同目录；`jsdom` 仅为开发测试依赖，不包含在安装包中。所有运行时代码均随扩展本地打包。

`npm run preview` 提供 `http://127.0.0.1:18765/panel.html?tabId=7` 离线模拟界面，显示「离线演示 · 模拟数据」，不访问柠檬云，不做真实备份、删除或上传。

已完成：44 项离线自动化测试（含套件项），覆盖认证、分页、请求清理、正式/三个测试域名、备份下载状态、所选删除范围、账号变更、上传认证、ZIP 字段精度、附件一致性、搜索全选、取消确认、停止/继续、上传结果交互，以及原生 fetch 接收者、接口错误定位和 HSTS 升级后的认证规则匹配。

历史验证边界（1.0.x）：当时 Chrome 自动化连接返回 `Unable to load browser request-header policy`，未在真实 Chrome 加载扩展，也未验证真实 Cookie、Chrome DNR 实际匹配、网页嵌入策略和线上接口响应。未执行真实账套删除、上传或备份。安装后先使用「读取账套」检查账号与列表，再对你选定的账套进行操作；不能把离线测试视作线上联调通过。

## 接口来源

对应现有 `backupaccountsetwidget.cpp`、`deleteaccountsetwidget.cpp`、`pwdloginwidget.cpp`、`importpackageuploadwidget.cpp` 与 `Common/importpackageuploader.cpp`。

- [Chrome Cookie API](https://developer.chrome.com/docs/extensions/reference/api/cookies)
- [Chrome DNR API](https://developer.chrome.com/docs/extensions/reference/api/declarativeNetRequest)
- [Chrome 扩展跨域请求](https://developer.chrome.com/docs/extensions/develop/concepts/network-requests)
- [fflate 项目](https://github.com/101arrowz/fflate)

隔离浏览器联调：先在项目根目录执行 `pwsh -File Tools/Build/build.ps1 -BridgeTests`，再安装开发用 Playwright/Chrome for Testing 并运行 `node LemonChromeExtension/tests/browser-e2e.mjs`。测试使用 `BuildCheck/Repository/Tests-Release/bin/BrowserLoginBridgeTests.exe`；可用 `LEMON_BRIDGE_TEST_EXE` 指定其他隔离产物。此联调依赖不进入扩展安装包。
