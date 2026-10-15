# 科目余额表导账附件目录核实

核实日期：2026-09-22。只读取服务源码，没有更改或运行服务。

## 目录契约

- `QtWidgetsApplication1/Resource/import/subject-balance-software.json` 为数组，完整包含 SoftwareEnum 的 70 个有效成员（69 个解析器）；`files` 为实际 multipart 固定槽位，`required` 按解析器控制流，不盲从接口列表。
- `supportsAuxiliaryFiles=true` 才开放动态辅助附件；用户填写的辅助类别名称原样作为 multipart key，不发送 `auxiliary` 这类虚构固定 key。同类别不得重复，也不得占用核心 `trialbalance/voucher/subject` key。Excel 扩展名为 `.xls/.xlsx`。
- 各软件辅助类别必须与余额/凭证内的原类别一致。企明星对名称做 NormalizeProjectTypeName 与类型映射；不代表任意名称均有效。
- `uncertain=true` 是已发现的入口声明与解析路径差异，`notes` / slot `notes` 可直接上屏；并非未找到软件解析器。
- 会计准则没有加入软件限制枚举：BaseAnalyzeExcelHandleAbstract.cs:1020 的科目映射仅支持 1/2，但调用依赖数据和具体分支，不能据此声明所有软件必然仅支持 1/2；客户端保留全部枚举交由服务校验。

## 统一证据

服务根：`D:/workspace/stage2/lemonacc/LemonWCFService/LemonWCFService/LemonAcc.ImportFromOther`。

- `CommonEnum.cs:111` 有 70 个 SoftwareEnum 成员；`AnalyzeExcelHandleFactory.cs:62-399` 映射到 69 个解析器（速达 V7/V7Net 复用）。
- `BaseAnalyzeExcelHandleAbstract.cs:90/94/98` 定义固定 key；`:335` 读取余额可为空，`:351/365` 读取科目/凭证缺失会抛错。故解析器无条件调用 GetVoucherWorkbook 时，即使随后判断 null 返回空列表，凭证仍为必填。
- `ImportFromOtherHandle.cs:328-368` 文件类型入口：易代账 voucher/trialbalance 可 ZIP；芸豆 voucher 可 ZIP；亿企 subject 可 TXT；诺诺 subject 可 ZIP。其它全部 Excel。365 subject 特例不表示解析器会消费，未开放独立科目槽位。
- `ImportFromOtherCommon.cs:11-34` 科目清单为 14 必填、7 选填，仅作历史元数据参考；实际解析器存在下述差异。
- 旧软件典型辅助规则：ACNetReadExcelHandle.cs:105-122 筛选所有非余额/凭证 key，并把 file.Key 用作辅助类别编码和名称、file.Value 用作该类别 Excel。相同模式经各类扫描；好会计虽然创建 aaeFiles 却不使用其文件内容，不开放无效辅助上传。

## 与入口清单不一致及具体限制

1. 精斗云云会计：清单 subject 选填，但解析器 `JingDouYunYunKuaiJiReadExcelHandle.cs:62-63` 无条件读科目和凭证；`:103` 仅接受凭证导入模板。目录标必填。
2. 布袋云：清单 subject 选填，`BudaiYunReadExcelHandle.cs:153-164/267/320` 顺序无条件读科目、凭证、余额。目录标科目必填。
3. 唯易：清单 subject 选填，`WeiYiReadExcelHandle.cs:79-88/291-294` 明确 2+1 且缺科目即抛错。目录标必填。
4. 金蝶星空旗舰版：清单未列科目；`YunXingKongUltimateReadExcelHandle.cs:28` 无条件读科目。目录补为必填。
5. 博科 B8：清单未列科目；`BoKeReadExcelHandle.cs:170-180` 支持可选科目。目录新增选填。
6. 亿企：`YiQiReadExcelHandle.cs:1272-1290` subject TXT 才建立独立科目标记；`:890-910` TXT 模式余额来自 trialbalance，否则余额来自 subject Excel。`:923-944` 虽注释称优先独立凭证，实际仅扫描 BalanceWorkbook，没有 voucher 文件读取；`:579-584` 无凭证表返回空列表。页面不提供会被忽略的独立 voucher 槽位，并告知组合工作簿要求；不能宣称独立凭证导入可用。trialbalance 标选填且 TXT 时条件必填由备注解释。
7. 易代账：`ChanjetYDZReadExcelHandle.cs:399` 无条件 GetVoucherWorkbook，会先将 ZIP 当 Excel 读取，后面的 ZIP 分支无法据静态代码保证可达。余额 ZIP 有独立路径 `:549-580`，目录保留两者入口允许扩展名，凭证明确提醒。
8. 芸豆：`YunDouReadExcelHandle.cs:425` 直接 GetVoucherWorkbook，未发现凭证解压路径；保留入口允许 ZIP 并提醒。
9. 天蓬云账：`:138-140/150/237` 科目、凭证无条件读取；`:576-590` 余额只在工作簿存在时读取，余额标选填。
10. 易代账余额 `ChanjetYDZReadExcelHandle.cs:549-554` 仅在有 trialbalance key 时读，余额标选填。
11. 凭证缺失明确允许的软件，不因有 GetVoucherWorkbook 调用就判必填：SDV5Net 在 `SuperDataReadExcelHandle.cs:2538-2543` 先空返回；柠檬云在 `NingMengYunReadExcelHandle.cs:370-373` 先空返回；金财、慧财税、91云账、大账房、慧代账、浪潮云、金账师只有存在文件时进入读取。慧算账反例 `HuiSuanZhangReadExcelHandle.cs:297-299` 缺文件明确抛错。

## 验证边界

本次为静态源码核实，未提交真实账套或执行每厂商样例。扩展名列表表达入口接受值，不等于所有文件模板变体均支持。条件规则与源码矛盾已显式标注，不能将 70 项目录称作 70 软件实测通过。

## 各软件定位

以下列出每个解析器的源码定位和关键文件读取语句，补充扫描上下文见 `.diagnostics/balance-import/key-context.txt` 与 `scan-report.txt`。

### 1007 精斗云_云会计 (JingDouYunYunKuaiJi)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\JingDouYunYunKuaiJiReadExcelHandle.cs:19`

- 62: GetSubjectWorkbook();
- 63: GetVoucherWorkbook();

### 1011 金蝶云星空 (KingdeeYunXingKong)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YunXingKongReadExcelHandle.cs:18`

- 46: GetSubjectWorkbook();
- 49: throw new Exception("亲，请上传科目数据附件。");
- 343: GetVoucherWorkbook();
- 346: throw new Exception("亲，请上传凭证附件。");
- 724: throw new Exception("亲，请上传科目余额表附件。");

### 1020 金蝶云星空旗舰版 (KingdeeYunXingKongUltimate)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YunXingKongUltimateReadExcelHandle.cs:16`

- 28: GetSubjectWorkbook();
- 413: GetVoucherWorkbook();
- 601: throw new Exception("亲，请上传科目余额表附件。");
- 607: throw new Exception("亲，请上传正确的科目余额表附件。");
- 807: throw new Exception("亲，请上传凭证附件。");
- 813: throw new Exception("亲，请上传正确的凭证附件。");

### 1012 金蝶云星辰 (KingdeeYunXingChen)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YunXingChenReadExcelHandle.cs:21`

- 51: GetSubjectWorkbook();
- 54: throw new Exception("亲，请上传科目数据附件。");
- 442: GetVoucherWorkbook();
- 445: throw new Exception("亲，请上传凭证附件。");
- 1175: throw new Exception("亲，请上传科目余额表附件。");

### 2001 畅捷通(用友)T3标准\普及版/用友UFERP-M8.11 (ChanjetT3Std)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ChanjetT3StdReadExcelHandle.cs:14`

- 72: throw new Exception("亲，请上传科目余额表附件。");
- 102: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 330: if (FileList.Any(u => u.Key == VoucherContentName))
- 332: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 2002 畅捷通T+标准版 (ChanjetTPlusStd)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ChanjetTPlusStdReadExcelHandle.cs:17`

- 162: throw new Exception("亲，请上传科目余额表附件。");
- 192: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 440: if (FileList.Any(u => u.Key == VoucherContentName))
- 442: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 2004 畅捷通T6 (ChanjetT6)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ChanjetT6ReadExcelHandle.cs:21`

- 89: throw new Exception("亲，请上传科目余额表附件。");
- 610: if (FileList.Any(u => u.Key == VoucherContentName))
- 612: var voucherFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 2006 用友U8 (YongYouU8)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YongYouU8ReadExcelHandle.cs:15`

- 43: if (FileList.Any(u => u.Key == VoucherContentName))
- 45: GetVoucherWorkbook();
- 318: GetVoucherWorkbook();
- 657: GetSubjectWorkbook();
- 888: throw new Exception("亲，请上传正确的科目余额表附件。");
- 960: throw new Exception("亲，请上传科目余额表附件。");
- 966: throw new Exception("亲，请上传正确的科目余额表附件。");
- 1030: throw new Exception("亲，请上传正确的科目余额表附件。");

### 2012 用友YonSuite (YongYouYonSuite)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YongYouYonSuiteReadExcelHandle.cs:15`

- 231: GetVoucherWorkbook();
- 365: throw new Exception("亲，请上传科目余额表附件。");
- 371: throw new Exception("亲，请上传正确的科目余额表附件。");
- 481: if (FileList == null || !FileList.ContainsKey(VoucherContentName))
- 486: GetVoucherWorkbook();
- 602: throw new Exception("亲，请上传正确的凭证数据附件。");

### 2013 用友NC (YongYouNC)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YongYouNCReadExcelHandle.cs:15`

- 56: if (BalanceWorkbook == null) throw new Exception("请上传用友 NC 科目余额表。");
- 232: GetVoucherWorkbook();

### 2007 畅捷通好会计 (ChanjetHKJ)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ChanjetHKJReadExcelHandle.cs:19`

- 166: var kmFile = FileList.Where(u => u.Key == SubjectContentName).First().Value;
- 268: var kmFile = FileList.Where(u => u.Key == SubjectContentName).First().Value;
- 391: throw new Exception("亲，请上传科目余额表附件。");
- 423: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 711: if(FileList.Any(u => u.Key == VoucherContentName))
- 713: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 2008 畅捷通易代账 (ChanjetYDZ)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ChanjetYDZReadExcelHandle.cs:17`

- 399: GetVoucherWorkbook();
- 438: GetSubjectWorkbook();
- 440: var subjectFile = FileList[SubjectContentName];
- 551: if (FileList.Any(u => u.Key == BalanceContentName))
- 553: var balanceFile = FileList.Where(u => u.Key == BalanceContentName).First().Value;
- 1623: if (FileList.Any(u => u.Key == VoucherContentName))
- 1625: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 1942: if (!FileList.ContainsKey(SubjectContentName))
- 1944: throw new Exception("亲，请上传科目表附件。");

### 2009 用友通 (ChanjetYYT)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ChanjetYYTReadExcelHandle.cs:15`

- 71: throw new Exception("亲，请上传科目余额表附件。");
- 101: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 297: if (FileList.Any(u => u.Key == VoucherContentName))
- 299: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 2010 畅捷通T1记账宝 (ChanjetT1JZB)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ChanjetT1JZBReadExcelHandle.cs:14`

- 39: throw new Exception("亲，请上传科目余额表附件。");
- 69: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 240: if (FileList.Any(u => u.Key == VoucherContentName))
- 242: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 2011 畅捷通T+13.0 (ChanjetTPlus13)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ChanjetTPlus13ReadExcelHandle.cs:19`

- 79: var kmFile = FileList.Where(u => u.Key == SubjectContentName).First().Value;
- 856: throw new Exception("亲，请上传正确的凭证附件。");
- 1281: if (FileList.Any(x => x.Key == VoucherContentName))
- 1283: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 3001 安易V5 (AnyiV5)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\AnyiReadExcelHandle.cs:19`

- 151: throw new Exception("亲，请上传科目余额表附件。");
- 181: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 393: if (FileList.Any(u => u.Key == VoucherContentName))
- 395: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 3002 安易V8 (AnyiV8)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\AnyiReadExcelHandle.cs:501`

- 527: throw new Exception("亲，请上传科目余额表附件。");
- 557: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 711: if (FileList.Any(u => u.Key == VoucherContentName))
- 713: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 3003 安易V6 (AnyiV6)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\AnyiReadExcelHandle.cs:729`

- 818: throw new Exception("亲，请上传科目余额表附件。");
- 848: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 1018: if (FileList.Any(u => u.Key == VoucherContentName))
- 1020: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 6001 AC.NET会稽源标准版 (ACNETStd)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ACNetReadExcelHandle.cs:18`

- 75: throw new Exception("亲，请上传科目余额表附件。");
- 105: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 322: if (FileList.Any(u => u.Key == VoucherContentName))
- 324: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 555: throw new Exception("亲，请上传正确的凭证附件。");

### 7001 浪潮PS (InspurPS)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\InspurReadExcelHandle.cs:21`

- 175: throw new Exception("亲，请上传科目余额表附件。");
- 205: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 391: if (FileList.Any(u => u.Key == VoucherContentName))
- 393: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 7002 浪潮云会计 (InspurYun)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\InspurReadExcelHandle.cs:723`

- 806: throw new Exception("亲，请上传科目余额表附件。");
- 836: if (FileList.Any(u => u.Key == VoucherContentName))
- 838: GetVoucherWorkbook();
- 1105: if (FileList.Any(u => u.Key == VoucherContentName))

### 8001 新中大银色快车SE (XinZhongDa)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\XinZhongDaReadExcelHandle.cs:19`

- 32: throw new Exception("亲，请上传科目余额表附件。");
- 38: throw new Exception("亲，请上传正确的科目余额表附件。");
- 124: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 247: if (FileList.Any(u => u.Key == VoucherContentName))
- 249: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 358: throw new Exception("亲，请上传正确的凭证附件。");

### 8002 新中大V4 (XinZhongDaV4)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\XinZhongDaV4ReadExcelHandle.cs:16`

- 244: GetVoucherWorkbook();
- 287: GetVoucherWorkbook();

### 11002 速达V3 (SDV3)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:276`

- 322: throw new Exception("亲，请上传科目余额表附件。");
- 352: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 490: if (FileList.Any(u => u.Key == VoucherContentName))
- 492: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 11003 速达V3.Net/V30.Net/V300.Net (SDV3NET)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:596`

- 639: throw new Exception("亲，请上传科目余额表附件。");
- 669: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 798: if (FileList.Any(u => u.Key == VoucherContentName))
- 800: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 11011 速达5000/速达5000Pro商业 (SD5000)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:908`

- 958: throw new Exception("亲，请上传科目余额表附件。");
- 988: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 1153: if (FileList.Any(u => u.Key == VoucherContentName))
- 1155: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 11012 速达V5 (SDV5)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:1333`

- 1397: throw new Exception("亲，请上传科目余额表附件。");
- 1427: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 1593: if (FileList.Any(u => u.Key == VoucherContentName))
- 1595: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 11013 速达V5.Net/V50.Net/V500.Net (SDV5NET)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:1746`

- 1802: throw new Exception("亲，请上传科目余额表附件。");
- 1808: throw new Exception("亲，请上传正确的科目余额表附件。");
- 1947: if (!FileList.Any(f => f.Key == VoucherContentName))
- 1952: GetVoucherWorkbook();
- 2262: if (AcctCodeToAaeTypes.Count == 0 && FileList.Any(f => f.Key == VoucherContentName))
- 2373: throw new Exception("亲，请上传正确的凭证附件。");
- 2538: if (!FileList.Any(f => f.Key == VoucherContentName))
- 2543: GetVoucherWorkbook();

### 11021 速达7000 (SD7000)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:2861`

- 2911: throw new Exception("亲，请上传科目余额表附件。");
- 2941: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 3106: if (FileList.Any(u => u.Key == VoucherContentName))
- 3108: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 11022 速达V7 (SDV7)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:3125`

- 3189: throw new Exception("亲，请上传科目余额表附件。");
- 3219: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 3385: if (FileList.Any(u => u.Key == VoucherContentName))
- 3387: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 11023 速达V7.Net/V70.Net/V700.Net (SDV7NET)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:3125`

- 3189: throw new Exception("亲，请上传科目余额表附件。");
- 3219: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 3385: if (FileList.Any(u => u.Key == VoucherContentName))
- 3387: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 11024 速达荣耀BAS/天耀3000BAS/4000BAS (SDHonorBAS)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:3405`

- 3455: throw new Exception("亲，请上传科目余额表附件。");
- 3485: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 3622: if (FileList.Any(u => u.Key == VoucherContentName))
- 3624: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 3721: throw new Exception("亲，请上传正确的科目余额表附件。");

### 11025 速达天耀3000Pro/速达3000cloud (SDTY3000Pro)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:3802`

- 3815: throw new Exception("亲，请上传科目余额表附件。");
- 3821: throw new Exception("亲，请上传正确的科目余额表附件。");
- 3896: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 4039: if (FileList.Any(u => u.Key == VoucherContentName))
- 4041: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 11026 速达3000财务STD (SDTY3000STD)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SuperDataReadExcelHandle.cs:4307`

- 4367: throw new Exception("亲，请上传科目余额表附件。");
- 4398: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 4437: if (FileList.Any(u => u.Key == VoucherContentName))
- 4439: GetVoucherWorkbook();
- 4630: GetVoucherWorkbook();

### 16001 四方财务 (SiFang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\SiFangReadExcelHandle.cs:22`

- 56: throw new Exception("亲，请上传科目余额表附件。");
- 61: throw new Exception("亲，请上传正确的科目余额表附件。");
- 167: throw new Exception("亲，请上传科目余额表附件。");
- 195: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 427: GetVoucherWorkbook();
- 639: throw new Exception("亲，请上传科目余额表附件。");
- 645: throw new Exception("亲，请上传正确的科目余额表附件。");

### 17001 航天信息A6 (AisinoA6)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\AisinoReadExcelHandle.cs:247`

- 383: throw new Exception("亲，请上传科目余额表附件。");
- 411: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 639: if (FileList.Any(u => u.Key == VoucherContentName))
- 641: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 810: throw new Exception("亲，请上传正确的凭证附件。");

### 17002 航天信息A3 (AisinoA3)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\AisinoReadExcelHandle.cs:887`

- 1036: throw new Exception("亲，请上传科目余额表附件。");
- 1064: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 1318: if (FileList.Any(u => u.Key == VoucherContentName))
- 1320: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 19001 芸豆会计 (YunDou)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YunDouReadExcelHandle.cs:16`

- 33: throw new Exception("亲，请上传科目余额表附件。");
- 39: throw new Exception("亲，请上传正确的科目余额表附件。");
- 425: GetVoucherWorkbook();
- 577: throw new Exception("亲，请上传凭证附件。");
- 583: throw new Exception("亲，请上传正确的凭证附件。");

### 22001 亿企代账 (YiQi)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YiQiReadExcelHandle.cs:23`

- 48: ISheet sheet = GetBalanceSheet();
- 172: throw new Exception("亲，请上传科目余额表附件。");
- 205: ISheet sheet = GetBalanceSheet();
- 259: ISheet sheet = GetBalanceSheet();
- 521: ISheet sheet = GetVoucherSheet();
- 582: ISheet sheet = GetVoucherSheet();
- 888: private ISheet GetBalanceSheet()
- 899: GetSubjectWorkbook();
- 914: throw new Exception("导入失败，请上传正确科目余额表!");
- 923: private ISheet GetVoucherSheet()
- 1275: if (FileList.Any(u => u.Key == SubjectContentName))
- 1278: var kmFile = FileList.Where(u => u.Key == SubjectContentName).First().Value;

### 23001 企明星 (QiMingXing)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\QiMingXingReadExcelHandle.cs:15`

- 132: TryGetSubjectWorkbook();
- 133: GetVoucherWorkbook();
- 273: if (FileList == null || !FileList.TryGetValue(SubjectContentName, out ImportFileInfo fileInfo))
- 971: + "且无法从企明星已验证科目定义中确定该父级名称。请上传科目表，或在科目余额表中补充完整父级科目后重试。");

### 24001 云代账 (YunDaiZhang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YunDaiZhangReadExcelHandle.cs:21`

- 150: throw new Exception("亲，请上传科目余额表附件。");
- 253: if (FileList.Any(u => u.Key == VoucherContentName))
- 257: GetVoucherWorkbook();
- 700: GetVoucherWorkbook();

### 25001 诺诺云记账 (NuoNuo)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\NuoNuoReadExcelHandle.cs:19`

- 232: throw new Exception("亲，请上传科目列表附件。");
- 555: if (FileList.Any(u => u.Key == SubjectContentName))
- 557: var subjectFile = FileList.Where(u => u.Key == SubjectContentName).First().Value;
- 824: if (FileList.Any(u => u.Key == VoucherContentName))
- 826: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 26001 博科B8 (BoKeB8)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\BoKeReadExcelHandle.cs:18`

- 65: throw new Exception("亲，请上传科目余额表附件。");
- 71: throw new Exception("亲，请上传正确的科目余额表附件。");
- 172: if (FileList != null && FileList.TryGetValue(SubjectContentName, out var fileInfo))
- 415: GetVoucherWorkbook();
- 598: throw new Exception("亲，请上传凭证附件。");
- 604: throw new Exception("亲，请上传正确的凭证附件。");
- 619: throw new Exception("亲，请上传正确的凭证附件。");
- 904: GetVoucherWorkbook();

### 27002 金财代账 (JinCaiDaiZhang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\GaoXinJinCaiReadExcelHandle.cs:423`

- 467: if (FileList.Any(u => u.Key == VoucherContentName))
- 469: GetVoucherWorkbook();
- 672: throw new Exception("亲，请上传科目余额表附件。");
- 1011: if (FileList.Any(u => u.Key == VoucherContentName))

### 27003 慧财税/微宝 (HuiCaiShui)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\HuiCaiShuiReadExcelHandle.cs:16`

- 44: if (FileList.Any(u => u.Key == VoucherContentName))
- 46: GetVoucherWorkbook();
- 131: throw new Exception("亲，请上传科目余额表附件。");
- 137: throw new Exception("亲，请上传正确的科目余额表附件。");
- 458: if (FileList.Any(u => u.Key == VoucherContentName))

### 28001 润衡财务 (RunHeng)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\RunHengReadExcelHandle.cs:18`

- 141: throw new Exception("亲，请上传科目余额表附件。");
- 171: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 326: if (FileList.Any(u => u.Key == VoucherContentName))
- 328: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 402: throw new Exception("亲，请上传正确的凭证附件。");

### 30001 金算盘eERP-E (EErpE)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\JinSuanPanReadExcelHandle.cs:15`

- 81: throw new Exception("亲，请上传科目余额表附件。");
- 111: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 273: if (FileList.Any(u => u.Key == VoucherContentName))
- 275: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 30002 金算盘eERP-B (EErpB)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\JinSuanPanReadExcelHandle.cs:292`

- 374: throw new Exception("亲，请上传科目余额表附件。");
- 404: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 583: if (FileList.Any(u => u.Key == VoucherContentName))
- 585: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 31001 金蝶账无忧V5.0 (ZhangWuYou)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ZhangWuYouReadExcelHandle.cs:23`

- 58: var kmFile = FileList.Where(u => u.Key == SubjectContentName).First().Value;
- 362: throw new Exception("亲，请上传科目余额表附件。");
- 387: if (FileList.Any(u => u.Key == VoucherContentName))
- 389: GetVoucherWorkbook();
- 477: string sheetCurrencyStr = GetBalanceSheetCurrencyText(sheet);
- 633: GetBalanceSheetCurrencyText(sheet),
- 661: private static string GetBalanceSheetCurrencyText(ISheet sheet)
- 675: GetVoucherWorkbook();

### 32001 云帐房/经纬财云/经纬天工 (YunZhangFang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YunZhangFangReadExcelHandle.cs:18`

- 184: throw new Exception("亲，请上传科目余额表附件。");
- 209: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 509: if (FileList.Any(u => u.Key == VoucherContentName))
- 511: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 827: throw new Exception("亲，请上传正确的凭证附件。");

### 32010 91云账 (NinetyOneYunZhang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\NinetyOneYunZhangReadExcelHandle.cs:18`

- 229: if (FileList.Any(u => u.Key == VoucherContentName))
- 231: GetVoucherWorkbook();
- 478: throw new Exception("亲，请上传科目余额表附件。");

### 33001 慧算账 (HuiSuanZhang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\HuiSuanZhangReadExcelHandle.cs:16`

- 86: throw new Exception("亲，请上传科目余额表附件。");
- 297: if (!FileList.Any(u => u.Key == VoucherContentName))
- 299: throw new Exception("亲，请上传凭证表附件。");
- 302: if (FileList.Any(u => u.Key == VoucherContentName))
- 304: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 562: if (FileList == null || !FileList.ContainsKey(SubjectContentName))
- 568: GetSubjectWorkbook();
- 620: throw new Exception("亲，请上传正确的科目表附件。");
- 815: if (!FileList.Any(u => u.Key == VoucherContentName))
- 820: GetVoucherWorkbook();
- 861: if (!FileList.Any(u => u.Key == VoucherContentName))
- 866: GetVoucherWorkbook();

### 35001 大账房 (DaZhangFang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\DaZhangFangReadExcelHandle.cs:22`

- 34: if (FileList.Any(u => u.Key == VoucherContentName))
- 36: GetVoucherWorkbook();
- 201: throw new Exception("亲，请上传科目余额表附件。");
- 450: if (FileList.Any(u => u.Key == VoucherContentName))

### 40001 卓账财务 (ZhuoZhangCaiWu)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ZhuoZhangCaiWuReadExcelHandle.cs:15`

- 66: throw new Exception("亲，请上传科目余额表附件。");
- 96: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 246: if (FileList.Any(u => u.Key == VoucherContentName))
- 248: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 50001 贝云 (BeiYun)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\BeiYunReadExcelHandle.cs:15`

- 73: throw new Exception("亲，请上传科目余额表附件。");
- 103: var aaeFiles = FileList.Where(u => u.Key != BalanceContentName && u.Key != VoucherContentName).ToList();
- 225: if (FileList.Any(u => u.Key == VoucherContentName))
- 227: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;

### 60002 管家婆财贸双全 (GJPCMSQ)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\GuanJiaPoReadExcelHandle.cs:331`

- 348: GetVoucherWorkbook();
- 409: throw new Exception("亲，请上传科目余额表附件。");
- 415: throw new Exception("亲，请上传正确的科目余额表附件。");
- 805: GetVoucherWorkbook();
- 1154: throw new Exception("亲，请上传正确的凭证附件。");

### 61002 鲸通财税 (JingTongCaiShui)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\JingTongCaiShuiHandle.cs:13`

- 22: throw new Exception("亲，请上传科目余额表附件。");
- 28: throw new Exception("亲，请上传正确的科目余额表附件。");
- 141: GetSubjectWorkbook();
- 306: GetVoucherWorkbook();
- 475: throw new Exception("亲，请上传凭证附件。");
- 481: throw new Exception("亲，请上传正确的凭证附件。");

### 62002 365云财税 (YunCaiShui365)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YunCaiShui365ExcelHandle.cs:16`

- 31: throw new Exception("亲，请上传科目余额表附件。");
- 37: throw new Exception("亲，请上传正确的科目余额表附件。");
- 147: GetVoucherWorkbook();
- 374: GetVoucherWorkbook();
- 570: throw new Exception("亲，请上传凭证附件。");
- 576: throw new Exception("亲，请上传正确的凭证附件。");
- 683: throw new Exception("亲，请上传赋值核算附件。");
- 689: throw new Exception("亲，请上传正确的赋值核算附件。");

### 63001 慧代账 (HuiDaiZhang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\HuiDaiZhangReadExcelHandle.cs:18`

- 33: if (FileList.Any(u => u.Key == VoucherContentName))
- 35: GetVoucherWorkbook();
- 123: throw new Exception("亲，请上传科目余额表附件。");
- 129: throw new Exception("亲，请上传正确的科目余额表附件。");
- 779: if (FileList.Any(u => u.Key == VoucherContentName))

### 64001 众马云/舒跃云/捷锐云 (ZhongMaYun)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\ZhongMaYunReadExcelHandle.cs:14`

- 36: throw new Exception("亲，请上传科目余额表附件。");
- 42: throw new Exception("亲，请上传正确的科目余额表附件。");
- 134: GetSubjectWorkbook();
- 254: throw new Exception("亲，请上传正确的科目表附件。");
- 693: GetVoucherWorkbook();
- 697: throw new Exception("亲，请上传正确的凭证表附件。");
- 922: GetVoucherWorkbook();
- 926: throw new Exception("亲，请上传正确的凭证表附件。");

### 65001 鲸算盘 (JingSuanPan)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\JingSuanPanReadExcelHandle.cs:17`

- 39: throw new Exception("亲，请上传科目余额表附件。");
- 45: throw new Exception("亲，请上传正确的科目余额表附件。");
- 149: GetSubjectWorkbook();
- 246: throw new Exception("亲，请上传正确的科目表附件。");
- 515: GetVoucherWorkbook();
- 519: throw new Exception("亲，请上传正确的凭证表附件。");

### 66001 好记云 (HaoJiYun)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\haoJiYunReadExcelHandle.cs:19`

- 118: throw new Exception("亲，请上传科目余额表附件。");
- 308: if (FileList.Any(u => u.Key == VoucherContentName))
- 310: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 518: throw new Exception("亲，请上传正确的凭证附件。");

### 67001 金账云/金账师 (JinZhangShi)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\JinZhangShiExcelHandle.cs:29`

- 69: if (FileList.Any(file => file.Key == VoucherContentName))
- 71: GetVoucherWorkbook();
- 1062: throw new Exception("亲，请上传正确的凭证附件。");

### 67002 金蝶K/3 cloud (JinDieK3Cloud)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\KingdeeK3CloudReadExcelHandle.cs:19`

- 114: throw new Exception("亲，请上传科目余额表附件。");
- 196: GetVoucherWorkbook();
- 282: throw new Exception("亲，请上传科目余额表附件。");
- 345: GetSubjectWorkbook();
- 348: throw new Exception("亲，请上传科目表附件。");
- 429: GetVoucherWorkbook();
- 469: throw new Exception("亲，请上传正确的科目表附件。");
- 517: throw new Exception("亲，请上传正确的凭证附件。");

### 67003 柠檬云 (NingMengYun)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\NingMengYunReadExcelHandle.cs:18`

- 187: throw new Exception("亲，请上传科目余额表附件。");
- 193: throw new Exception("亲，请上传正确的科目余额表附件。");
- 370: if (!FileList.Any(x => x.Key == VoucherContentName))
- 545: if (subjectReadCompleted || FileList == null || !FileList.Any(x => x.Key == SubjectContentName))
- 550: GetSubjectWorkbook();
- 787: if (!string.IsNullOrEmpty(projTypeCode) && ((FileList == null || FileList.Any(x => x.Key == VoucherContentName))))
- 831: if (FileList == null || !FileList.Any(x => x.Key == VoucherContentName))
- 1099: GetVoucherWorkbook();
- 1108: throw new Exception("亲，请上传正确的凭证附件。");

### 67004 布袋云 (BuDaiYun)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\BudaiYunReadExcelHandle.cs:17`

- 58: throw new Exception("亲，请上传科目余额表或科目期初表附件。");
- 161: ReadBalanceOrInitial();
- 267: GetSubjectWorkbook();
- 272: throw new Exception("亲，请上传正确的科目表附件。");
- 320: GetVoucherWorkbook();
- 325: throw new Exception("亲，请上传正确的凭证附件。");
- 404: private void ReadBalanceOrInitial()

### 68001 易企记 (YiQiJi)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\YiQiJiReadExcelHandle.cs:18`

- 73: TryGetSubjectWorkbook();
- 74: GetVoucherWorkbook();
- 271: if (FileList == null || !FileList.TryGetValue(SubjectContentName, out var fileInfo))

### 69001 唯易 (WeiYi)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\WeiYiReadExcelHandle.cs:18`

- 87: GetRequiredSubjectWorkbook();
- 88: GetVoucherWorkbook();
- 291: if (FileList == null || !FileList.TryGetValue(SubjectContentName, out var fileInfo))

### 70001 八戒财税-天蓬云账 (TianPengYunZhang)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\TianPengYunZhangReadExcelHandle.cs:16`

- 140: ReadBalanceOrInitial();
- 150: GetSubjectWorkbook();
- 237: GetVoucherWorkbook();
- 240: ISheet sheet = GetVoucherSheet();
- 251: private ISheet GetVoucherSheet()
- 576: private void ReadBalanceOrInitial()

### 99999 通用模板 (GeneralTemplate)

解析器：`D:\workspace\stage2\lemonacc\LemonWCFService\LemonWCFService\LemonAcc.ImportFromOther\SoftwareAnalyzeExcelHandle\GeneralTemplateAnalyzeExcelHandle.cs:18`

- 39: throw new Exception("亲，请上传科目余额表附件。");
- 190: if (FileList.Any(u => u.Key == VoucherContentName))
- 192: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 518: if (FileList.Any(u => u.Key == VoucherContentName))
- 520: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 735: if (FileList.Any(u => u.Key == VoucherContentName))
- 737: var pzFile = FileList.Where(u => u.Key == VoucherContentName).First().Value;
- 1216: throw new Exception("亲，请上传正确的凭证附件。");
