# ATHENA XML v3：规范、原生数学 AST、Vault 迁移与实现交接

## 当前目标与优先级（用户于 2026-10-18 明确，以此节为准）

**最新执行状态：真实 `/home/felix/data/Notes` 已于本轮成功迁移为 v3，新桌面二进制已安装；发布后的 834 文档字节核对与 Notes Root 814 文档独立冷读全部通过。下一阶段是移除旧运行时/迁移工具及部署 NAS/iPad 新客户端，不是再次迁移 Notes。下文较早的“未发布/未改真实 Notes”仅为历史记录。**

- ATHENA 尚未到 1.0；兼容性不是目标。唯一交付目标是让用户自己的 Notes 无缝完成 v2 到 v3 升级，并最终只保留 v3 支持。
- 迁移当前文档、需要保留的文档历史、索引和引用，验证实际打开、编辑、保存、重启、搜索、导出与同步。无法无损导出的结构明确报错，不自动降级。
- 不提供旧格式备份恢复，不维持旧 Hodarium revision DAG、受保护旧格式证据或新旧图桥接的兼容能力。下文相关长期兼容要求和 epoch bridge 草案是已撤销的旧计划，不再作为交付门槛。
- Hodarium 切换须阻止旧设备/旧日志把 v2 数据重新写入新 Vault，但不需要保留旧修订的恢复入口。实际重置生产同步状态仍须在停止旧写者并核验迁移结果后明确执行，不得在准备阶段擅自删除。
- 迁移与验收完成后删除 v2/更早格式运行时、旧数学兼容分支以及本次和此前的迁移工具；不保留独立兼容工具。迁移当次的失败保护不等于长期旧格式支持。
- 用户随后明确授权：已自行将 Notes 复制为 `Notes-backup`，无需独立验证该备份，也不重复备份。在 Notes 的其它副本上验证通过、达到正式切换条件后，可以直接迁移真实 `/home/felix/data/Notes`，无需再次索要迁移许可。准备阶段仍只写独立副本或私有测试目录；正式迁移须停止旧写者并完成 Hodarium v3 切换保护。不得拿较早副本的验证代替切换时当前 Notes 的迁移检查。此授权不要求立即绕过尚未完成的发布闸；不自动 commit/push。
- 当前工作优先补齐正常用户编辑与真实副本闭环，不再扩展通用跨版本兼容或大量旧协议对抗性审计。用户明确禁止使用 subagent。

以下各日期小节保留为实施记录，其中的旧待办必须按本节裁剪，不能照旧继续扩张范围。

### Hodarium v3 与原子发布（2026-10-18，最新进展）

- 同步运行时已统一使用 `ath-xml-v3` / semantic version 4：盘点、保存捕获、关闭文档应用、已打开 BufferActor 应用、冲突比较/合并/另存、路径冲突检测。不再在这些入口读取或写入 XML v2。
- 传输边界为 `ATHENA-HODARIUM-VAULTS-v3`、`ATHENA-HODARIUM-EXCHANGE-v2`、`ATHENA-HODARIUM-REVISION-v2`；拒绝旧协议及错误格式/语义版本。未发布候选继续禁止同步，包括悬空 marker 符号链接。
- 新客户端使用 `revisions-v3-*` 与 `conflicts-v3-*`，不读取旧 journal 或草稿。保留设备身份、成员高水位、Vault 密钥及 commitment；冲突、分支集合、决议 token 使用独立 v3 role，旧决议不能授权新合并。恢复成员资格时也只查上一代的 v3 journal。不迁移旧 DAG。
- 修复了真实测试暴露的 BufferActor `can_apply_saved_document` / `apply_saved_document` 存储版本硬编码；v3 干净 buffer 可应用远程更新，脏 buffer 仍拒绝覆盖。
- 主程序构建通过。5 个套件通过（`hodarium_revisions_test`、`hodarium_path_collisions_test`、`document_upgrade_file_test`、`buffer_actor_test`、`xml_v3_test`），日志 `/tmp/athena-v3-hodarium-final-ctest.log`。`Oriented Manifolds` 迁移副本经过两个私有 Vault 的盘点、传输、落盘、反向修改、删除、保护历史恢复，日志 `/tmp/athena-v3-hodarium-oriented.log`。第一次真实文件测试仅初始化了 document 标签而未初始化标准 DRD，测试初始化修正后通过；未放宽 Reader。
- 隔离 Secret Service 下的身份、TLS、TCP/P2P、撤销和锁定测试通过，另有本地独立 Relay 测试通过：`/tmp/athena-v3-hodarium-identity.log`、`/tmp/athena-v3-hodarium-relay-final.log`。未使用用户 keyring 或生产 Relay。生产 NAS/iPad 尚未更新，不能把上述结果当成生产联合验收。
- 实现显式 `--upgrade-vault-xml-v3 --publish VAULT`：重新预检和 staging，重新取得源/候选目录 lease，核对整个源目录未变化，移走候选 marker 后 fsync 并原子交换。旧目录仅移动到私有迁移工作目录，不再复制一份备份；提交前取消/失败清理候选、不替换源。副本验证与生产切换结果继续记录在此，未通过当前 Notes 副本验证前不执行真实 Notes 发布。
- 新增并验证独立 headless v3 构建：新建/绑定 Vault 使用 model 2，绑定复用候选拒绝检查；编译 v3 codec、数学 AST 和同一份 named-symbol registry（Qt resource 内嵌，不需要桌面资源目录）。抽出无文件系统/legacy import 依赖的 `native_xml_codec.cpp` 供两种 target 共用。Docker build context 同步包含符号 JSON。
- headless 冷读/写回上述 812 篇旧测试副本全部通过（`/tmp/athena-v3-headless-corpus.log`）；两个独立 headless 进程使用临时 HTTPS Authority 的实际 P2P 初次传输、更新、删除、保护历史、重启和反向同步测试通过（`/tmp/athena-v3-headless-p2p.log`）。生产 NAS 尚未部署本轮构建。
- 从当前真实 Notes 只读复制的工作副本为 `/home/felix/tmp/athena-v3-publication.VjdWoW/source`。第一次发布检查中 834 个源文件通过，历史库非空 WAL 阻止发布，未修改原始 Notes。发布器新增只在私有 snapshot 中恢复/checkpoint SQLite WAL 后再迁移的流程，避免丢弃 WAL 或写原始库；独立 `--check` 仍保持严格只读。需丢弃的派生缓存沿用现有清单，不对它们做 WAL 恢复。
- 此工作副本已完整发布成功，目录 fsync 成功：834 当前文档、290 历史文档版本（291 条历史记录中一条不是文档）、10,618 索引树、2 条既有 warning。日志 `/tmp/athena-v3-current-notes-publication.log`；旧目录仅原子移动到同父目录 `.source.xml-v3-upgrade-e56e0f7a-b281-4748-a01f-19b996f531d1/vault`。不是对正式 Notes 的发布。
- 最新发布保护与 Hodarium 核心 5 项回归通过（`/tmp/athena-v3-wal-final-ctest.log`），包括并发修改拒绝、提交前取消不改源、私有 WAL 恢复保留已提交数据及源 DB/WAL 字节不变。最新主程序六项运行时测试通过（`/tmp/athena-v3-final-runtime-tests.log`），包含网站导出、搜索、严格导出、编辑/恢复、v3 Vault 与 MathML。
- 最新工作副本的 Notes Root **814** 篇文档 headless 冷读/写回及身份/metadata 校验通过（`/tmp/athena-v3-current-notes-headless.log`）。其最新 `Oriented Manifolds` 的实际键盘编辑、保存、重启、自动保存恢复通过（`/tmp/athena-v3-current-notes-edit-recovery.log`），两私有 Vault 的同步/反向修改/删除恢复通过（`/tmp/athena-v3-current-notes-sync.log`）。**814/814** 篇桌面正常加载/保存审计也全部通过（`/tmp/athena-v3-current-notes-runtime.log`），包含原正文、UUID、typed properties 与输入未改动检查。
- 在上述验证后，确认本机没有存活的用户 ATHENA / headless node 进程，仅有旧 zombie；未执行 killall/pkill。通过临时文件、cmp、原子 rename、再次 cmp 安装新 `ATHENA/bin/ATHENA.bin`（612971240 字节），随后按用户授权执行真实 `/home/felix/data/Notes` 的 `--publish`，**退出码 0、原子交换成功、目录 fsync 成功**，日志 `/tmp/athena-v3-notes-production-publication.log`。正式结果为 834 当前文档、290 历史文档版本、10,618 索引树、原有 2 warnings。旧目录仅移动至 `/home/felix/data/.Notes.xml-v3-upgrade-56822bc1-689f-45ca-b925-c9a1eb6525b1/vault`，不是额外复制备份。未检查用户 Notes-backup，也未修改 NAS/iPad。
- 发布后只读确认正式 Vault 为 model 2、没有候选 marker，834 个 v3 文档与已验收副本逐字节一致。正式 Notes Root 的独立 headless 冷读/写回内存树检查 **814/814 通过、退出码 0**，日志为 `/tmp/athena-v3-notes-production-readonly.log`；此检查不写 Vault、不启动同步。

### 正式切换后的剩余工作

- 本机 Notes 已迁移，新桌面二进制已安装；**尚未移除 v2/更早运行时与迁移工具**。清理时不要继续开发旧备份/旧 Hodarium 证据兼容，也不要为旧数据重新迁移用户 Notes。
- NAS headless v3 源码与本机构建/独立进程测试已完成，但生产容器未部署；iPad 新 IPA 也未构建/部署。旧客户端被新 wire 边界拒绝，不会向 v3 Vault 写旧内容。不能把本地及私有同步测试称作生产跨设备联合验收。
- 未自动 commit 或 push；保留现有工作树变更。用户 Notes-backup 不需验证，也不能据此自行删掉它。迁移后若要清理旧目录/旧 journals，应另行区分临时迁移产物、用户备份和仍有用的设备密钥。

### 历史内容修复进展（2026-10-18）

- 用户确认 `fint` 为带横线积分，`ohm` 为大写 Omega。一次性迁移器将其分别转为原生 `BIG_OPERATOR("⨍", "0")` 和 `Ω`；积分后续脚标按 v3 MathScripts 保存，保留原身份。不为它们增加 Reader 兼容别名。
- 针对已检查的历史结构：修复被拆成数字标签的完整 Unicode 转义；将 `mathllap` 转为零宽左重叠 RESIZE，`raise` 转为 MOVE，`kern` 转为 SPACE；成对的 `Big(<)`/`Big(>)` 转为手动级别 2 的角括号 DELIMITED；已知 `bbox*` 边框参数转为原生 ornament；四参数的历史引用块转为保留来源、正文、标题和链接的 quote-env。
- 转换按结构而非 Notes 文件名匹配。不猜测无法完整解码的字符、带独立 metadata 的待合并字符/括号端点、未支持的边框参数或模糊长度。例如 `1*2pt` 不能被当成 `12pt`。普通字面文本 `<#4EC0>` 不自动改写。
- 新冷声明检查使用编译时生成的内置/随包声明集合，不使用被 v2 解码器临时扩张的运行时标签表。当前文档、历史和索引树的预检均使用此检查；不能再把同进程回读成功当作冷启动成功。
- 主程序和 `xml_v3_test`、`preview_math_font_test`、`clipboard_origin_version_test` 已重新构建。三个套件全部通过，包含重叠宽度、字形包围盒、抬升及符号 box 结果的排版断言。日志：`/tmp/athena-v3-historical-final-build.log`、`/tmp/athena-v3-historical-final-ctest.log`。
- 授权的旧离线副本 `/home/felix/data/Notes (1)/Notes Root` 的 812 篇 `.ath` 转换、冷声明检查和 XML 回读通过，仍明确报告原有 2 条 `SCRIPT_WITHOUT_SEQUENCE_BASE` 警告：`/tmp/athena-v3-historical-corpus.log`。这不是原始生产 Notes 当前文件集合的全量验收，也不代表正式启用 v3。
- 最新主程序已实际生成新隔离候选 `/home/felix/tmp/athena-v3-historical-check-84ZvRm/candidate`，退出码 0：832 个当前 `.ath`、261 个历史版本、10,592 个索引树、3,328 个失效缓存哈希、2 条原有警告；源副本扫描校验未改变，日志 `/tmp/athena-v3-historical-private-stage.log`。这里 832 指 `.ath` 当前文档，包括 Notes Root 之外的维护报告，不是样式计数。候选继续明确标记不可发布；未覆盖安装二进制、未启动同步、未改原始 Notes。
- 对上述**新候选**用仅初始化随包声明的独立进程运行 `clipboard_origin_version_test isolatedCorpusColdRoundtrip`，`ATHENA_V3_CLIPBOARD_CORPUS` 指向 `candidate/Notes Root`：**812 documents, 0 rejected**，XML v3 冷读、带版本剪贴板序列化、反序列化和 v3 粘贴校验全部通过，退出码 0，日志 `/tmp/athena-v3-historical-cold-clipboard.log`。没有用先读取 v2 的同进程冒充冷读。

### 本地 v3 Vault 与真实编辑闭环（2026-10-18，后续进展）

- `vault_load` 不再笼统拒绝 `node_model_version=2`，允许本地 v3 Vault；未知模型版本仍拒绝。任何带 `.athena/xml-v3-candidate-manifest.json` 的未发布候选仍在数据库初始化前拒绝，包括悬空符号链接形式的标记和无法检查标记的情况。没有移除上一轮候选的标记，没有开放生产发布。
- Hodarium 仍维持旧 wire 的格式闸，v3 Vault 不会被伪装成 v2 发送，也不能应用旧 v2 修订。本地编辑可用不等于同步切换完成；不要为继续工作重新引入旧 DAG 桥接目标。
- 新增 `tests/scheme/native-v3-persistence-test.py/.scm`。使用真实主程序、隔离 profile、实际 BufferActor：加载、键盘追加文字和 `(x^2)`、正常保存、第二进程重开/重存、自动保存和第三进程恢复；检查旧正文结构、原有 UUID/typed properties、恢复后身份与版本。`--vault` 还测试候选拒绝不写文件/数据库、本地 v3 Vault 打开以及新建文档保存为 v3。
- `--document` 始终先复制输入；`--styles` 将依赖样式复制到测试 profile。`Oriented Manifolds`、`PDE Lecture Notes II`、`LAI Lecture Notes IV` 的隔离测试均通过，Oriented Manifolds 还通过启用本地 v3 Vault 的版本。PDE 第一次缺少自定义样式导致 frame 的 DRD 未加载；带上实际 dependencies 后通过，不是靠放松 source-role 校验绕过。
- 最新构建成功，未覆盖已安装的 `ATHENA/bin/ATHENA.bin`。5 项 Scheme 运行时测试通过：文件持久化、本地 Vault 持久化、结构化搜索、严格导出、MathML 路由。7 项重新构建的核心测试通过：`artifact_v3_runtime_test`、`document_node_model_test`、`native_v3_document_creation_test`、`xml_v3_test`、`native_v3_input_widget_test`、`native_v3_math_editor_test`、`preview_math_font_test`。日志分别为 `/tmp/athena-v3-local-vault-final-ctest.log`、`/tmp/athena-v3-local-vault-core-final.log`。
- 全库真实运行时审计通过：上述候选 Notes Root 的 **812/812** 篇文档在私有副本中经过正常加载、保存、关闭，检查原正文结构、UUID、typed properties 与输入文件未改动。每 64 篇使用新进程，日志 `/tmp/athena-v3-corpus-runtime-batched.log`，测试副本 `/home/felix/tmp/v3-persistence-checks/native-v3-persistence-f5nbhdjb`。此前单进程审计收到 SIGKILL；用户确认约 17:40 结束过所有 ATHENA 进程，该次中断不能算作文件或运行时故障证据，分批重跑完整通过。
- 一次并行构建期间的旧 `native_v3_hodarium_shared_audit_test` 超过 120 秒，不能记录为通过。未发布候选拒绝已另外由新的真实运行时测试和核心测试验证；本轮不继续扩展已取消目标的旧协议审计。
- 迁移审计现在输出文件名及原树路径。原有两条警告均在 `Sources/Lecture Notes/Differential Equations/DE Assignments 2025-2026 XV.ath`：`1/0/30/0/3/0/2/0/7/1/5/1` 与 `1/0/30/0/7/2/0/0/1/1/5/1`。仍是 `SCRIPT_WITHOUT_SEQUENCE_BASE`，未自动猜测修正；日志 `/tmp/athena-v3-corpus-warning-locations.log`。

> **状态：分节记录实施时间；面向后续本地 Codex 开发。**
>
> **重要：这是一份“已确定的设计 + 各次工作树实现状态 + 待办”文档，不是已发布格式的上线公告。** 原先未提交的 XML v3 基线已于 2026-10-16 进入 `aa6cd0372`；之后仍可能存在新的未提交修改。本文中的 HEAD、测试结果和已实现状态均为各小节注明时间的快照，应以当前源码和 `git status` 为准。
>
> **安全约束：真实 Notes 的迁移已获有条件授权，以本文顶部最新授权为准；达到副本验证与正式切换条件前不得写入真实 Vault。不要验证或重复创建用户已完成的 Notes-backup。不得未经用户允许提交或推送。** ATHENA 仓库内的 notes/ 是本交接文档的目标，与用户真实的 Notes Vault 完全不同。已有的无关文件 notes/native-menu-command-loss-handoff.md 必须保留。

## 0. 一页结论

ATHENA 是由 TeXmacs 演进出的 C++/Qt 编辑器。本项目不是给 XML v2 换几个较短的 XML tag，而是要同步重构**持久化文档表示、原生数学 AST、编辑/排版/导入导出以及整库迁移和历史同步协议**。

已经确定的核心方向：

1. **唯一规范格式 XML v3；迁移期间使用明确调用的离线 v2 → v3 migrator。** v3 Reader 必须直接生成 v3 原生 AST，不用运行时兼容映射，不先生成 around / rsub 等旧树再升级。完成用户 Notes 升级验收后删除 v2 Reader、迁移器及此前的迁移工具，不长期保留兼容入口。
2. **物理 XML 结构严格、无重复拼写、无同一概念的两套编码。** 文本始终写为 `<t><v>...</v></t>`；文档、片段和数学结构都有固定形状。
3. **数学公式按词法 token 保存，但不强制完整语义解析。** 比如 A/B 是 A、/、B 三个数学 token，斜线不能被误变为“成对定界符”或自动改为分式。真正上下排列的分式继续保留 FRAC 语义。
4. **自动括号配对是 v3 编辑不变量。** 不持久化孤立 LEFT/RIGHT。定界表达式始终是一棵 DELIMITED，显式给出左右字形与自动/自然/手动尺寸策略。
5. **大算符本身只是 BIG_OPERATOR 的操作数字形**，其上下限由统一五槽 MathScripts 表达，**没有独立 Big Operator 带上下限的另一套结构**。
6. **所有 display formula 都是 MathBlock**；原 eqnarray 应为 MathBlock 的子 MathGrid（eqnarray kind）；公式中多余的单孩子 DOCUMENT 从**原生 AST** 中消失；真正多段正文/enunciation 的 DOCUMENT 不动。
7. **历史和同步切换不能被忽略，但不维护旧格式兼容。** 保留所需的正常文档历史和引用；Hodarium 在停止旧写者后切换到新格式，拒绝旧设备重新写入。旧备份恢复、旧 Hodarium DAG 和受保护旧格式证据不属于支持目标；不要继续开发旧图桥接。
8. **普通迁移遇确实有歧义的东西发 warning，--strict 把 warning 当错误。** 严格格式错误、结构丢失、破坏身份/引用的情况本来就直接失败，不能通过宽松模式吞掉。

**目前有成果但不具备上线条件：** 早期的 812/812 同进程回读结论不足以证明候选可用：v2 Reader 会临时注册旧标签，掩盖新进程的 v3 Reader 无法识别它们。2026-10-18 已增加独立于运行时标签表的冷声明校验，并为真实副本中的已确认历史结构增加一次性转换；具体验证见后续记录。隔离候选、历史和索引转换、原生编辑/排版、导出已有部分测试。**正式 Vault 发布/原子交换和普通运行时打开候选仍有保护闸**；不能把转换测试通过等同于生产迁移验收完成。

## 1. 项目与设计约束

### 1.1 代码位置

- 项目：/home/felix/data/Software/TeXmacs/texmacs
- 分支：master；原先积压的 XML v3 改动已在 `aa6cd0372` 提交；新的功能修改以当前 `git status` 为准。
- 仓库自身开发指引：AGENTS.md；动手前先读它，尤其“逐层追踪表示边界”“不能靠猜测去修 Typesetter”“不可擅自提交”的规则。
- 前一阶段的 Vault 升级器/节点模型背景：notes/node-model-migration.md、notes/node-model-handoff.md；原有核心源代码 src/Data/Convert/Xml/vault_format_upgrade.cpp。
- 当前主要构建目录：build_qt6；GUI-less 专项测试目标：xml_v3_test；类型排版测试：preview_math_font_test。

### 1.2 不能背离的语义原则

- **Canonical**：一个概念一个规范结构；不能一会儿用短标签、一会儿用旧节点，一会儿从“标准括号对”推导。特别是 **不要建立内置的“标准 l/r 配对表”来补缺失属性**，例如 ( 与 ) 的配对只是实例，不是省略 r 的授权。
- **Explicit**：存档中不可依赖排版器去猜测 l/r、MathScripts 槽位、是否编号、grid 的类型。MathScript 的 placement 也不可由当前字体或环境隐式代替。
- **Native**：导入、编辑、排版、TeX/HTML 等应直接消费对应的 v3 AST；不要为省事先合成历史 AROUND、BIG_AROUND、RSUB、TFORMAT 后交回旧逻辑。
- **Identity**：UUID、typed metadata、reference、旧节点引用路径以及有身份的单孩子 CONCAT，不能简单套用“无 metadata 的 AST 简化”规则。
- **No silent loss**：任何尚未支持的数学结构，不能被转换为看起来可用但丢失编号、布局、引用、历史或 metadata 的内容。失败/报告阻断优先于无声降级。
- **No compatibility baggage**：v3 Reader 不需要 v2 识别器、兼容别名、旧样式降级接口；离线 migrator 才能解释历史约定。当前 v2 runtime 与 v3 样板代码暂时并存是为了确保安全，**不是允许在 v3 runtime 长期保存旧节点**。

## 2. XML v3 当前物理编码规范

> 本节以当前 src/Data/Convert/Xml/athena_document_xml_v3.cpp 和 src/Data/Tree/math_tree_v3.cpp 为准。设计目标已定，但正式发布前还需要完成 schema 规范化与跨导出验证。

### 2.1 根、文本、复合节点、二进制

| 语义 | 唯一编码 | 备注 |
| --- | --- | --- |
| 完整文档 envelope | `<a v="3" k="d">ROOT</a>` | ROOT 必须为原生 DOCUMENT |
| 树片段 envelope | `<a v="3" k="t">ROOT</a>` | 允许一个独立树节点；不得含多个并列根 |
| 文本原子 | `<t><v>VALUE</v></t>` | **即使无 UUID、无属性，也永远不能用 <t>VALUE</t>** |
| 二进制原子 | `<b><v e="b">BASE64</v></b>` | 仅限 RAW_DATA 的一个 bytes child |
| 通用复合节点 | `<n t="document">CHILDREN</n>` | t 必填，是注册过的原生 node tag |
| node UUID | 节点属性 `i="UUID"` | 同一文档内 UUID 唯一，不另写属性副本 |
| typed metadata | 子元素 `<x><p ...>...</p></x>` | 顺序固定；若无普通属性则不写 x |

所有字符内容使用 UTF-8；不能表示或会因 XML 换行规范化丢失的字符，使用规范 Base64 `e="b"`。合法文本依然需要 `<v>` 包装；二进制 payload 必须 Base64。XML tag t 和属性名 n 含禁用字符时可使用 `te="b"` / `ne="b"` 的 Base64；这是**字节传输**，不是第二种数学符号编码。Reader 检查 Base64 规范形式。

以下是完整示意（真正 XML Writer 还会写 XML declaration，空元素可写成自闭合）：

~~~xml
<a v="3" k="d">
  <n t="document">
    <n t="math-block" b="0">
      <d l="(" r=")" s="a">
        <n t="concat">
          <t><v>x</v></t>
          <m k="|"/>
          <t><v>y</v></t>
        </n>
      </d>
    </n>
  </n>
</a>
~~~

注意 `concat` 与 `document` **不能合并为同一语义**：前者代表一个水平/inline 有序序列，允许文字、分式、脚标、定界符混合；后者代表段落/纵向多行文档。只删除公式内部历史遗留的单孩子 DOCUMENT，保留真正的正文/enunciation DOCUMENT，以及为保留 UUID 而必须存在的 CONCAT。

### 2.2 Typed properties

`<x>` 中按规范次序存放命名 `<p n="..." t="...">`；list 使用 `<i t="...">` 作为无名子项，**不能把这里的 i 元素与 UUID 属性 i 混为一谈**。

| 属性 t | 数据 | 编码举例 |
| --- | --- | --- |
| s | string/UTF-8 | `<p n="title" t="s">Text</p>` |
| b | bool | `<p n="verified" t="b">true</p>`（true/false） |
| i | int64 | `<p n="rank" t="i">42</p>`（规范整数） |
| f | finite double | `<p n="ratio" t="f">0.5</p>`（规范浮点） |
| l | property list | `<p n="list" t="l"><i t="s">a</i></p>` |
| d | property dictionary | `<p n="dict" t="d"><p n="k" t="s">v</p></p>` |
| r | typed UUID reference | `<p n="ref" t="r" i="UUID"/>` |
| h | rich_text native subtree | `<p n="rich" t="h"><t><v>text</v></t></p>` |

节点的身份也可以出现在 text、binary、数学节点和嵌套 rich_text 中。Reader 拒绝重复 UUID、未知属性/标签、意外嵌套、非规范数字、未注册 AST tag、非法 XML namespace/DTD、资源超限、无效 UTF-8。现有 codec_limits 包含 input/output byte limit、text limit、node count、depth（见 athena_document_xml.hpp），不是无限深度的通用 XML parser。

有三个数学节点的**结构性 metadata**不应既写成普通 x/property 又写在节点属性上：MathScripts 的 math:scripts-policy → p，MathGrid 的 math:grid-kind → k，MathBlock 的 math:block-numbered → b。Writer 只写一次，Reader 还原成原生 typed metadata；若两处重复则拒绝。

### 2.3 严格 Reader、片段与迁移边界

- src/Data/Convert/Xml/athena_document_xml.hpp 暴露 read_xml_v3/write_xml_v3，参数包括 xml_kind::document 和 xml_kind::fragment。
- src/Data/Convert/Xml/athena_document_xml_v3.cpp 是独立实现；v3 Reader 只接受 v="3" 与指定 k，**不**尝试识别 v2 envelope/旧属性写法，也不偷偷调用 v2→v3 migrator。
- 原 read_xml/read_xml_v2 是已有系统/离线输入的工具；不得把它们混进 v3 Reader 的正常打开路径。
- 不能用 make_tree_label 动态注册未知标签来让坏 v3 文件蒙混过关；应检查已有 DRD/native 标签。当前数学旧 core node 经 v3 Writer/Reader 校验拒绝，其他非数学扩展按照 DRD 管理。

## 3. 原生数学 AST：逐节点约定

定义与校验：src/Kernel/Types/tree_label.hpp、src/Data/Drd/standard_tree_labels.inc、src/Data/Tree/math_tree_v3.hpp/.cpp。XML 的专用形态只负责外部编码，**数学 AST 是 C++ tree 中的实际节点**。

### 3.1 DELIMITED：唯一的成对定界表达式

**XML：**

~~~xml
<d l="(" r=")" s="a"><t><v>x</v></t></d>
<d l="⌈" r="⌋" s="a"><t><v>x</v></t></d>
<d l="" r="|" s="a"><t><v>f(x)</v></t></d>
<d l="(" r=")" s="m" ls="2" rs="5"><t><v>x</v></t></d>
~~~

**C++**：DELIMITED 六个物理 slot：0 左 Unicode 字形，1 右 Unicode 字形，2 sizing，3 ls，4 rs，5 **唯一可编辑内容子树**。这些 slot 是 AST descriptor，**不是**旧式 open/body/close sibling 节点。DRD 仅将 slot 5 视为正文。

- l 和 r 总是必填，Unicode 字形或空串；至少一侧非空。比如 l="" / r="|" 是合法的单侧定界。**不允许通过“标准配对”省略任何一边**。
- s="a" automatic sizing；s="f" fixed/natural size；s="m" 手动尺寸。**只有 m 必须有 ls/rs 两个独立尺寸**（当前规范整数 0..1024），其余模式禁止带 ls/rs。
- 历史不显示端点 "."、<nobracket>、texmacs:nobracket 在 migrator 中变成空串；v3 没有 "." sentinel。
- 自动括号编辑必须构造完整 DELIMITED；不允许持久化未配对 LEFT/RIGHT。不能混淆“automatic bracket pairing（输入编辑动作）”与 “s=a（定界符排版尺寸）”。
- 完全不可见的旧括号对在迁移时可消除，但必须保留有 UUID 的外层身份；若旧 endpoint 自身拥有身份而无法无损承接，必须失败。

**中间分隔符不是上述左右端点。** `<m k="|"/>` → MATH_MIDDLE(U+007C)，既可在 DELIMITED 内也可独立在表达式中，保留 separator 的间距和伸缩语义。历史 MID("/") 和 MID("backslash") 是线性除法或差集算符，分别转为普通数学 token "/" / "\"，**绝不**改成 <m>、<d> 或 FRAC。这项曾经有约 803 条误报，现已修正。

### 3.2 BIG_OPERATOR + MathScripts：不能再保留 Big-around 语法

~~~xml
<o k="∑" u="0"/>
<n t="scripts" p="o">
  <o k="∑" u="0"/>
  <t><v></v></t>
  <t><v></v></t>
  <t><v>i=1</v></t>
  <t><v>n</v></t>
</n>
~~~

- BIG_OPERATOR 仅有 (glyph, upright) 两个 descriptor，XML `o` 的 k 是 Unicode 字形，u="0"/"1"；它不是有 body 的大算符作用域。对积分 upright 采用专门字形排版策略。
- MATH_SCRIPTS **恰好五个内容子树**，顺序固定：base、left-sub、left-sup、right-sub、right-sup。无值也需要保留空 `<t><v></v></t>` slot；没有另一个简化形状。其节点上的 typed 属性 math:scripts-policy 唯一映射为 `p`：a=auto、d=display limits、o=over/under、r=right side。
- 它适用于普通变量、∑、∫、左右前置脚标、独立上标记号；不用两套“Big Operator 上下限”表示。独立的 ^{op}、^{-1} 有合法的空 base；对字母/核素的前置脚标映射到 left 槽，不需要硬塞给一个不存在的前置 base。
- 旧串行 `concat("GL", math(concat(rsub(2),...)))` 的无身份 math/concat 模式切换不能被误认为基底断裂。当前 migrator 会在合适的数学上下文中平展逻辑 token，保留有身份的父节点与 script UUID。

### 3.3 其余数学节点

| native label | 原生 slot 与属性 | XML v3 形态 |
| --- | --- | --- |
| MATH_ACCENT | [accent Unicode glyph, o/u, expression]；typed bool math:accent-stretch | `<n t="math-accent">...</n>` + x/p metadata |
| MATH_ANNOTATION | [expression, below, above]，可空 label slot | `<n t="math-annotation">...` |
| MATH_LONG_ARROW | [Unicode arrow glyph 或 MATH_SYMBOL, below, above] | `<n t="math-long-arrow">...` |
| MATH_VARIANT | [kind, expression]；kind=bb/rm/it/bf/ss/tt/sl/cal | `<n t="math-variant">...` |
| MATH_SYMBOL | [stable semantic symbol ID]，与注册表配方解析分离 | `<n t="math-symbol">...` |
| MATH_GRID | ROW 的有序序列，每个 ROW 含若干 CELL，每个 CELL 一个 body；typed math:grid-kind | `<n t="math-grid" k="...">...` |
| MATH_BLOCK | [display math expression 或 MATH_GRID]；typed bool math:block-numbered | `<n t="math-block" b="0|1">...` |

**MathSymbol** 不是与 Unicode 文本互为别名的第二套任意 spelling。存稳定的注册身份 ID，靠 src/System/Language/named_symbol* 的权威 registry 渲染；普通可表达的数学字形优先 Unicode token。历史 texmacs:mathd、backassign、longhookrightarrow 等从 v2 显式迁移到规范 semantic ID。

**MathGrid kind** 当前接受 matrix、eqnarray、align、cases、stack、generic。列对齐规则目前：eqnarray 右/中/左循环，align 右/左交替，matrix/stack 居中，cases 左对齐；cell 上可用 math:cell-format 结构化字典覆盖。例如 border-left/border-right/border-top/border-bottom、background、height、height-mode、align、color；不应长期以旧 TFORMAT/CWITH 程序作为原生存档。

**MathBlock 编号** 由 math:block-numbered 一位属性表达。旧 `equation` 对应 numbered=true，`equation*` 为 false。**特别注意**：此仓库 env-math 的旧 `eqnarray` 宏实际直接调用 `eqnarray*`，通常**不递增 equation counter**；不能按 TeX 同名环境的直觉将所有旧 eqnarray 无条件转为 numbered=true。

**已确认的逐行编号模型（正在实现和联合验证）**：直接作为 MathBlock 内容的 eqnarray/align/stack MathGrid，其 ROW 采用 typed `math:row-numbering` 策略，语义为 inherit / numbered / unnumbered。缺少属性是 inherit 的唯一规范存储形式；有属性时仅接受字符串 numbered 或 unnumbered，不另存 inherit 别名。继承只取直接 MathBlock 的 bool，不向嵌套 grid 传播。matrix/cases/generic 仍为整块编号，不能混入行级编号属性。

ROW 可另有 `math:row-tag` rich_text；存在时用该原生富文本作为显示标签和引用值，不递增自动 equation counter，且与自动编号策略独立。空标签与没有标签不同。ROW 原有 UUID、CELL 内容中的 LABEL 和 typed reference 保留，不以当前显示序号建立身份，也不因改编号重建身份。普通文字 TABLE 的 ROW 不受这些数学规则影响。

**导出策略已由用户确认：默认明确报错，不自动有损降级。** 原生排版和每种外部格式分别检查能否表达语义。例如 LaTeX eqnarray 不支持自定义 tag 时明确拒绝，不能偷偷改为另一种列布局；不编号且无自定义 tag 的行若含 equation label，也不能让它错误引用前一个计数器值。可表达的混合编号和自定义标签需验证原生计数器、引用、LaTeX 编译及 MathJax 配置一致性；不能仅以 XML roundtrip 成功宣布完成。

**数学 token 约定**：按词法而非完整语义解析。例 A/B 以 CONCAT(A, "/", B) 保存；不产生一个分母为空的 FRAC，不把 "/" 保存成 MATH_MIDDLE。大多数名字和数字照数学词法切分，但没有强制构造乘法 AST、函数调用 AST 或完整 CAS expression tree。这个选择是用户最终明确拍板的。

## 4. 离线 XML v2 → XML v3 的 Vault 升级设计

这不是 XML 字符串替换。需要将**所有持久化源（文档、typed rich_text、历史、索引、引用、同步图）**一致地变成新模型。早期“旧 TeXmacs 文本格式 → XML”升级器、后来的 XML v1 → v2 node-model Vault 升级器提供了**清单、克隆、文件描述符限制、私有 staging、完整重验、目录原子交换及失败恢复**等可重用的基础设施，但本轮绝不能直接沿用旧格式业务映射。

### 4.1 数学 AST 迁移阶段

入口：src/Data/Convert/Xml/math_v2_to_v3.hpp/.cpp 的 migrate_math_v2_tree(source, strict)；返回文档、按原路径记录的 warning、删除的旧 UUID 清单、节点转换计数。**只接收已经由 read_xml_v2 解码的旧原生 tree**；不会被 v3 Reader 在正常打开文件时调用。

已实现/需继续审查的重写：

| 历史结构 | v3 结果 | 注意事项 |
| --- | --- | --- |
| AROUND / VAR_AROUND(LEFT, body, RIGHT) | DELIMITED(l,r,s,body) | 对端点原始 texmacs 名称解码成 Unicode；不可见 "." → 空；不猜缺失配对 |
| CONCAT 里独立 LEFT ... RIGHT | 完整 DELIMITED | 嵌套成对配对；有身份的端点不能静默合并；无匹配时 warning/strict error |
| MID("/")、MID("backslash") | 普通词法数学 token | 分别是除法/差集；不是定界符 |
| MID("|") 等真实 separator | MATH_MIDDLE | 可在 <d> 内外；继承间距及 stretch |
| BIG / BIG_AROUND | BIG_OPERATOR + MathScripts/CONCAT | 保留算符语义和上下限 scope，不带着旧 big-around |
| RSUB/RSUP/LSUB/LSUP/RPRIME/LPRIME | 五槽 MathScripts | 搜寻正确逻辑基底，复合基底不与最后字母混淆；允许明确独立脚标 |
| WIDE、VAR_WIDE | MATH_ACCENT | 语义 accent Unicode + o/u + stretch 属性；包括 bar、vec、bind、brace 等历史 recipe |
| ABOVE / BELOW | MATH_ANNOTATION | 处理嵌套 prime、空脚标占位 |
| LONG_ARROW | MATH_LONG_ARROW | 容纳明确的 Unicode 箭头和命名符号，保留上下标签 |
| math-alpha-bbb、math-up、math-it、math-bf 等 | MATH_VARIANT | kind 有明确字体/文本模式语义 |
| NAMED_SYMBOL | 规范 MATH_SYMBOL 或 Unicode token | 仅允许已注册/有定义的 semantic ID；移除冗余旧拼写 |
| equation/equation* | MATH_BLOCK(content) | 内部只有一个真实正文内容，移除历史多余单孩子 DOCUMENT |
| eqnarray/eqnarray* / align | MATH_BLOCK(MATH_GRID) | grid kind 明确；**不能简单按名字推断是否编号** |
| 多段落 display DOCUMENT | MATH_GRID(kind=stack) | 每段是一个行单元格，序列/身份必须保留 |
| TFORMAT/TABLE/ROW/CELL/CWITH | MATH_GRID/ROW/CELL + typed cell-format | 解析应用于具体 cell 的历史范围、覆盖、负索引和样式；不在 v3 存一个无法解释的 CWITH 语言 |
| WITH(mode=math) 内的数学 | v3 math subtree | 不许把 WITH 的参数字符串误送进数学 lexer |
| 旧 metadata 的 rich_text/list/dict 内公式 | 递归迁移 rich_text | 不能只走主文档 child 数组 |

**脚标算法曾有重要误报**。初版 422 条 script warning：398 条位于一个局部 CONCAT 的第一个位置、24 条直接在其他数学结构中。先识别数学模式内部没有身份的嵌套 math/concat，跨其容器边界寻找真实 base，修掉 290 条；再处理核素前置上下标、独立 ^{op}、限制竖线、嵌套 prime 和空占位。现在全量源码只剩 2 条 SCRIPT_WITHOUT_SEQUENCE_BASE，均来自历史 `|^{2}|` 形式，不能凭猜测认定 ^2 的真实基底。--strict 应拒绝它们；**不要通过抹掉 warning 来达成“零警告”。**

**真正不可见的两端点**可以去掉冗余 wrapper，但如果 wrapper 有 UUID，应把它保存为有身份的 CONCAT；UUID 不应偷偷转给另一个语义节点。真正可见的历史端点可能是 `CONCAT(NAMED_SYMBOL(texmacs:lvert))`，已修复错误当成空端点的情况；切勿退回“非 atomic 一律 empty”的旧 bug。

**20 个单孩子公式 DOCUMENT UUID 的例子**：旧 Notes 样本中 13 个 equation*→document→concat，7 个 eqnarray→document→tformat→table；人工核对全库（当时 811 文件）未发现针对这些 wrapper UUID 的用户编写引用，只有 node identity 与派生 references 数据。已同意**不为这些无实质含义的包装 UUID 建立永久兼容映射**。迁移可删冗余包装/其派生 reference 项，但必须证明实际用户引用、typed reference、外部 Artifact identity 没有依赖它。不能把这一实例推广成“任意 UUID 可丢弃”。

### 4.2 UUID、reference 与路径迁移

迁移器复制原文身份到同一语义节点。有时原 script wrapper 的 UUID 必须保留在其 script expression 上或额外的 identity-owning CONCAT 上；如果无法无损归属，应发明确 warning 或硬错误。对 source_ids 计算 v2/v3 差集，暴露 removed_identities，清理的只允许是 `references` 中明确对应已删除包装的**派生节点引用记录**。

Vault preflight 另检查：文档文本、metadata 的 typed reference 与 rich_text、Artifact 名称树/提取树、Artifact 的 source_uuid/source_nodes，以及 Enunciation/Bold-text 的 identity_focus/identity_host/identity_before/identity_after。这些引用到被删除 UUID 时必须阻断，而不是自动将跨文档边跳到相邻节点。当前代码有初版词法 UUID 检索和目标列清单，未来仍须扩大到**全部持久化身份生产者/消费者**，并为路径/offset 更改建立可验证 mapping、反向链及否定测试。

### 4.3 历史版本：用户明确选择完整历史迁移（不是仅保留最新版本）

src/Data/Convert/Xml/vault_xml_v3_history.hpp/.cpp：打开 .athena/document-history.sqlite，按版本 ID 重构所有文档历史源。旧 Fossil-delta 按 base 链依次应用，检查长度、hash、最大 delta 深度；把每一版文档都转换为 v3，并在**staged 副本**里重新计算 v3 内容 hash 与可选 Fossil delta（保持既有 id/base 因果链、chain depth 约束及历史元信息）。

历史版本的 **id 保持不变**，但 v3 payload 字节与 content hash 会变化。对 Hodarium 受保护历史快照，额外存 `xml_v3_protected_evidence(version_id, original_payload, original_content_hash, original_size)`，保存**原始 v2 完整字节证据**，并限制删除；这样既能使用 v3 版本恢复，又不丢失先前用于外部同步/审计的受保护内容证明。提交后需对每个历史 id 再重构一次，严格比对 v3 内容并由 v3 Reader 回读。

此逻辑**不是**对外部 Hodarium 已签名 revision 的重签或重新赋 ID（详见 §5）；不能因本地历史数据库通过就认为跨设备的历史恢复/同步链已经一致。

### 4.4 Artifact/Enunciation/Bold-text 的索引树片段

src/Data/Convert/Xml/vault_xml_v3_indexes.hpp/.cpp 的索引迁移只能在私有 staged Vault 执行：

- Artifact 数据库 artifact_names.name_tree 由 v2 fragment → v3 fragment，保留 artifact identity 和关联键。
- Enunciation 数据库 entries.keyword_tree（如 schema 确有该列）同理；不存在对应列的数据库应按已核实 schema 处理，不能盲选错列。
- Bold-text 的 entries.keyword_tree 旧值可能有 `base64-v1:` 封装；解码 v2 fragment，再重编码为 `base64-v3:`。
- 对 metadata schema 记录相应 tree-format 升级；派生 storage/semantic/input hash、locator/range cache 等必须失效并在 v3 runtime 中重算；**不能只改 payload 而保留旧输入 hash，误导增量缓存**。
- SQLite 更新采用事务，foreign_keys、quick_check、WAL checkpoint 与 staged 路径 confinement；绝不打开真实 source 数据库为 RW。

这批逻辑已有隔离 fixture 测试，但最终必须审核每个正式数据库版本实际的表/列及衍生数据消费者。若某 metadata 写进了不能转换的列，正确动作是阻断，不是静默跳过。

### 4.5 整库事务：check、stage、publish 是三个不同阶段

目前有的是**两个入口（check 和 stage），第三阶段 publish 未实现/被禁止**。

1. **check-only**：获取原 Vault lease / confined path，扫描源 Vault，读取全部活动文档，v2→v3→v3 codec 往返，收集 warning/删除身份并审计所有实际引用；只读恢复历史、只读检查索引、可选审计外部 Hodarium；最后重新扫描并验证源未变化。SQLite 用 immutable URI 只读，避免读操作偷偷创建 -shm；非空 WAL 要求停同步并 checkpoint 后再检查，防止 immutable 读忽略未提交到主 DB 的修订。
2. **stage-only**：必须先 check，做前后 inventory 竞态比较，再独占候选空目录（权限 0700），克隆完整 Vault，仅在私有候选中重写 .ath、历史和索引；修改候选 Vaultfile.json 中 node_model_version **从 1（v2 node model）升级至 2（v3 node model）**；重新从磁盘读取候选 v3 文档/历史并比对；必要时重新审计外部状态指纹；写候选 manifest 并耐久化。失败时删除候选而不动源。
3. **publish（尚未上线）**：未来在**所有编辑器、导出、Artifact、Hodarium** 都能原生消费 v3 后，定义跨库/外部状态的 quiescence/freeze、准备/提交/恢复、同步协议版本交接、受保护记录一致性、回滚和重试。可借鉴先前通过 RENAME_EXCHANGE 的整库原子切换基础设施，但单纯文件夹交换**不能**原子提交外部修订数据库/远端因果图。当前不得解禁。

**CLI（现在只能操作测试拷贝、尤其 stage）**：

~~~sh
# 只验证；不会写 v3 数据
ATHENA.bin --upgrade-vault-xml-v3 --check [--strict] [--hodarium-state PRIVATE_STATE_DIR] VAULT_DIRECTORY

# 在新的、尚不存在的目录生成断开的候选副本；不交换原目录
ATHENA.bin --upgrade-vault-xml-v3 --stage NEW_DIRECTORY [--strict] [--hodarium-state PRIVATE_STATE_DIR] VAULT_DIRECTORY
~~~

选项写法示意：方括号是“可选参数”的文档惯例，不是命令行要字面输入的字符。真实 Notes Vault 不允许拿来执行上述 **--stage**。如需测试，先使用隔离的小型 fixture 或用户明确授权的、与源 Vault 物理分开的脱机拷贝。**--strict 遇到原 2 条歧义时会失败；不能为了上线暗中自动重解释原文。**

候选结果标志：`.athena/xml-v3-candidate-manifest.json` 中 source_node_model_version=1、candidate_node_model_version=2、documents/history/index 数量、Hodarium 指纹及 binding 数、`hodarium_protocol_cutover_verified=false`、`eligible_for_atomic_publication=false`。**即便已写出完整候选也不能打开生产/作为 sync source**；src/ATHENA/Data/vault.cpp 在启动前拒绝 node_model_version>=2 的 Vault，避免旧进程读取或偷偷重写 v3 文档。

## 5. 外部 Hodarium 一致性：最关键的尚未完成部分

### 5.1 为什么不允许只迁移本地历史

Hodarium revision 是由格式、semantic version、路径、payload 内容、parents 等封装/哈希确定身份的**内容寻址因果修订**。对一个 `ath-xml-v2` revision 改写 XML 字节，就会改 revision ID；父修订边、远端 head/applied pointer、receive/apply intent、冲突审批及保护快照仍指向旧 digest。**原地替换 SQLite revisions.payload 或借用旧 revision ID 是数据破坏**。只保存原始 protected snapshot 也不等于完成协议切换。

### 5.2 目前只实现了外部状态的不可变审计

新模块：src/Data/Convert/Xml/vault_xml_v3_hodarium.hpp/.cpp 的 audit_external_hodarium_v3(vault, external_state)。该审计要求**显式**指定独立的 Hodarium state 目录（不做不可靠的隐式目录猜测），包含 device.sqlite 及 revisions-*.sqlite；使用 SQLite immutable read-only，非空 WAL 为阻断；对数据库进行 quick_check / foreign_key_check，并在审计前后核对 SHA-256 数据库字节指纹、文件大小与 mtime。

逐条读取 revisions descriptor/payload，按当前 protocol=1、seal_revision 重算 ID/parents，统计被绑定 Vault 的 `ath-xml-v2` (semantic_version=3) 修订、applied 修订、未完成 apply intent、incoming receive、conflict approval、enable/disable binding。对共享 group 的未完成传输采取保守统计，不能因为另一 Vault 的队列尚未提交就放行共享因果日志。对多数据库的名称+digest 作确定顺序的总 SHA-256 fingerprint。

check 与 stage 可以通过 `--hodarium-state STATE_DIR` 触发审计。stage manifest 记录 fingerprint、existing bindings 及 coordinated_cutover_required；阶段末重新审计并与最初 fingerprint 比较，变化则抛错、撤销候选。**注意：有绑定（哪怕 disabled）仍可能有历史远端引用；不可从“当前同步关闭”推断旧 revision 图可改。**

上述只是**只读审计、危险状态识别、候选清单证据**，**尚未**建立 v3 Hodarium wire format、跨设备暂停/协商、新旧 revision 图链接、保护记录继承、双端原子切换、远端恢复及冲突重放。不应把 `hodarium_external_state_audited=true` 理解成 `hodarium_protocol_cutover_verified=true`。后者目前刻意永远为 false。

### 5.3 待设计/实现的完整协调 cutover（必须先行实现与测试）

1. 明确外部协议版本（例如新 format/semantic version 的精确合同）、旧 revision 留存策略、v3 new revision 的 ID/parents 映射规则。**不能**改写已有签名/内容寻址 v2 revision 的 ID，却继续沿用其父链指针。
2. 对所有本地/远端设备、group、Vault bindings 建立可审计的共同静止边界；拒绝未完成 incoming/apply/outbox、未决 conflict approvals、不同步的远端状态或未 checkpoint 的 WAL。
3. 给出跨本地 Vault、外部 device/revision DB、远端 revision DAG 的**阶段化 prepare/commit**；定义 crash 后发现半提交状态时的决定性恢复（继续、回滚、隔离），而非希望两个目录交换天然具备跨设备原子性。
4. 保留旧受保护 revision/payload 及其 hash 的证据和可读恢复路径；验证 normal historical restore 和 Hodarium “protected apply” 都仍可执行，而不是只保留一个 BLOB。
5. 模拟断网、延迟到达的 v2 revision、重复发送、冲突解决、失败重试、双设备版本不一致、签名验证、WAL/SHM 并发与进程中途崩溃；输出可复验的 cutover receipt，最后才可能将发布标志设置为 true。

**禁止的快捷方式**：把外部 revisions.sqlite 当成一次普通 format 字符串替换；重用 v2 digest；因为只读审计通过就让正式 v3 Vault 可打开；在缺少远端协商时只在本地 RENAME_EXCHANGE。

## 6. 已落地的工作（源码文件级清单）

以下只表示“**本工作树有对应实现和对应测试/验证**”，不等于已经切换全部生产调用点。特别区分独立函数、隔离测试成功、主运行时启用三个层级。

### 6.1 格式与原生数学基础：基本完成

| 文件 | 本次改动 |
| --- | --- |
| src/Kernel/Types/tree_label.hpp | 新增 DELIMITED、MATH_MIDDLE、BIG_OPERATOR、MATH_SCRIPTS、MATH_ACCENT、MATH_ANNOTATION、MATH_LONG_ARROW、MATH_GRID、MATH_BLOCK、MATH_VARIANT、MATH_SYMBOL |
| src/Data/Drd/standard_tree_labels.inc | 注册新节点和固定 arity，限制可编辑的 operand slot |
| src/Data/Tree/math_tree_v3.hpp/.cpp | Unicode glyph、手动尺寸、MathScripts 位置、MathGrid 行列形状及 metadata 校验；规范构造器；symbol registry identity |
| src/Data/Convert/Xml/athena_document_xml_v3.cpp | 独立紧凑严格 v3 Reader/Writer；UUID、rich_text、typed properties、Base64、资源预算及 v3 结构校验 |
| src/Data/Convert/Xml/athena_document_xml.hpp | read_xml_v3/write_xml_v3 显式 API |
| tests/Data/Convert/Xml/xml_v3_test.cpp | 基本 codec round-trip、严格拒绝旧格式、边界/UUID/属性/字节/数学节点、迁移及 Vault 相关 fixture |

**保留的 v2 生产兼容暂态**：现有 v2 读写器、v2 编辑/Typesetter 的旧节点在源码里尚未被整体删掉。原因是生产应用尚未 v3 cutover；这是迁移期间的安全隔离，不是要求 v3 永久兼容旧表示。代码完成后，必须重新扫描旧节点在 v3 核心路径中的残留和动态宏 fallback，再决定统一清理。

### 6.2 离线数学树转换：通过完整现存样本只读审计

- src/Data/Convert/Xml/math_v2_to_v3.hpp/.cpp 实现 §4.1 的显式 migrator、历史符号解码、嵌套 rich metadata、warning/strict、UUID 差分与派生引用删除。
- 通过多轮实际讲义抽样后修复：非 atomic 但可见的 lvert/rvert、MID(/) 除法与竖线、算符上下限、跨 math/concat 的基底、空基底独立上标、前置上下标、核素、v|_Γ、D^{prime}、传统 bind 重音、rubber-* 箭头、eqnarray 的额外 CWITH。
- 对 20 个历史单孩子数学 DOCUMENT wrapper，当前采用直接消除（不维护永久 UUID alias）的约定；其外部真实引用必须通过 Vault preflight 的 identity 扫描证明安全。
- **最新真实文档结果**见 §7：812/812 可转换及回读，0 失败，2 条数学原文歧义警告。

### 6.3 原生 Typesetter：主要节点有直接路径，局部几何对照已通过

| 文件 | 内容 |
| --- | --- |
| src/Typeset/Concat/concater.hpp/.cpp | 新 AST 分派，不先降级成旧 tag |
| src/Typeset/Concat/concat_math.cpp | 原生 DELIMITED/MATH_MIDDLE/BIG_OPERATOR/五槽 MATH_SCRIPTS/ACCENT/ANNOTATION/LONG_ARROW/VARIANT/GRID/BLOCK |
| src/Typeset/Concat/concat_symbol.cpp | 直接从稳定 MathSymbol ID 查询现有权威 symbol registry 及字体 recipe |
| src/Typeset/Table/table.hpp/.cpp、cell.cpp | MathGrid type、列对齐及 cell-format 结构化属性的原生表格排版 |
| tests/Typeset/preview_math_font_test.cpp | 新旧 Unicode 字形下的最终 box 几何、数学轴/自动括号、竖线伸缩、固定/左右独立手动尺寸、脚本/大算符/积分、变体/箭头与 MathBlock |

已修复的实测几何问题包括：高分式下 <d s="a"> 的自动括号高度（使用数学轴与 get_bracket_extents 而非只看 body bounding box）、<m> 的正确 MIDDLE_BRACKET_ITEM 与伸缩、BIG_OPERATOR 带 limits 的尾随间距和 condensed/script-position、左右脚标与 over/under limits 同时存在时的定位、长箭头额外间距，以及 math-bf 等宏必须切到 text font mode 的差异。

**当前 MathBlock 的编号能力（2026-10-16 更新）**：单行及直接子 eqnarray/align/stack MathGrid 均直接使用现有 next-equation/the-equation；后者按 ROW 的策略分别决定编号。自定义 rich tag 不推进计数器，UUID 目标与显式 LABEL 绑定该行标签，编号 box 按行基线放置并参与宽高计算。matrix/cases/generic 保持整块编号，嵌套 grid 不继承外层编号。不得将多行 grid 偷偷编成一个 equation。专项测试为 native_v3_row_numbering_test；真实视觉、复杂编辑和生产切换仍需后续验收。

### 6.4 原生编辑路径：局部可用，未全量切换

src/Edit/Modify/edit_math.cpp：在已识别为 v3 数学上下文中，插入开括号时即构造完整 DELIMITED，选择内容进入唯一 body slot；关括号必须匹配/移动光标，**不写孤立 RIGHT**；数学 separator 将 "/" 留在词法 token，真正竖线写为 MATH_MIDDLE；make_script 能按前一个 math lexical token 生成/编辑五槽 MATH_SCRIPTS 并保留 UUID（例如 x+y → x+y_i^2）；支持独立空基底脚标。普通 v2 编辑上下文继续走原路径，尚未切换为永久新 runtime。

tests/Edit/Modify/utf8_editor_test.cpp 有编辑树、metadata、v3 XML 往返、Undo 的功能断言。先前的退出清理信号 139 已定位到 Linux 分配器链接顺序，并由统一的 athena_link_allocator 链接规则修正；新测试进程已成功 exit 0，不再依赖 LD_PRELOAD。仍需按每次实际进程退出码验收，不能仅看用例 PASS。

**尚未全部直接 v3 化**：大算符插入/UI 命令、矩阵/公式块编辑、剪贴板/粘贴、Undo 各种跨树操作、删除/合并脚标/定界符、cursor & selection、自动补全、输入法宏、数学环境切换，及从 TeX/MathML 导入直接生成新 AST。不能说“编辑器已整体支持 v3”。

### 6.5 LaTeX 与 MathJax HTML：已有 metadata-aware 实现，但有明确范围

| 文件 | 内容 |
| --- | --- |
| src/Data/Convert/Tex/totex_context.cpp | 新 v3 数学原生 LaTeX 分派；不合成旧 AROUND/RSUP；定界字形向 TeX 控制序列的*输出边界*翻译；处理 p/ls/rs、脚本分组、编号、grid 类型、部分 accent/arrow/variant |
| src/Data/Convert/convert.hpp | latex_export_prepare_v3_source API |
| src/Scheme/Glue/basic.xml | 公开 metadata-aware v3 source 准备接口到 Guile |
| ATHENA/progs/convert/latex/init-latex.scm | texmacs->latex-document 先保留原生 typed metadata，再走现有 converter/serializer |
| ATHENA/progs/convert/html/tmhtml.scm | 原生 math-block/inline v3 节点走 MathJax，避免双层 display wrappers；HTML 原生数学的 dispatcher |
| ATHENA/progs/convert/html/init-html.scm | texmacs->html-document、texmacs-tree → html-document 路由；要求 v3 数学指定 mathjax=on；旧 v2 route 基线不变 |
| tests/Edit/Modify/utf8_editor_test.cpp | 有 Guile 的实际 TeX 与 HTML 序列化对照，而不是只看 Scheme intermediate |

已覆盖的 LaTeX：s=a/f/m 定界符和中间 separator 的局部/嵌套作用域；Unicode 大算符命令（upright 无现成可靠规则则拒绝）；MathScripts 空基底或复合基底 TeX 分组、上/下限策略；部分 MathAccent/MathAnnotation/MathLongArrow/MathSymbol/MathVariant；MathBlock equation/equation* 及单行/未编号 MathGrid、矩阵外的 equation 包装。其他不支持的**v3 源数学对象不再错误退回 Scheme-only 的“保留对象”逃生口**，因为那会丢失 UUID 和 typed metadata，必须明确失败。

已覆盖的 HTML：native tree 的 metadata-aware 转换入口 + MathJax 模式，可在 HTML 保留编号 equation、sum/limits、inline scripts；旧 v2 基线与 converter routing 有测试。**当前 v3 数学在非 MathJax 的 CSS/PNG/原生 HTML 路径不作为可用结果，明确要求 mathjax=on**。这是一条安全边界，不是最终目标。所有 HTML 直出、复杂表格/数学布局、跨浏览器视觉测试仍在待办中。

### 6.6 Vault/check/stage、历史、索引和外部状态审计：已实现但正式发布被禁止

- src/Data/Convert/Xml/vault_format_upgrade.hpp/.cpp：只读 upgrade_vault_xml_v3、显式私有 stage_vault_xml_v3_candidate、完整 inventory/lease/clone/staged rewrite/re-read、UUID 活动引用检查、候选 manifest、禁止原位 publish。
- src/Data/Convert/Xml/vault_xml_v3_history.hpp/.cpp：每一个版本的恢复/重编码、Fossil-delta、protected original evidence。
- src/Data/Convert/Xml/vault_xml_v3_indexes.hpp/.cpp：索引数据 fragment 重写、Base64 v1 → v3、版本 metadata、computed hash/caches 失效和外部 UUID 绑定预检查。
- src/Data/Convert/Xml/vault_xml_v3_hodarium.hpp 和 src/Data/Convert/Xml/vault_xml_v3_hodarium.cpp：**只读、无副作用**的外部 Hodarium journal/binding/causal revision 审计与 SHA-256 状态指纹。
- src/ATHENA/ATHENA/athena.cpp：CLI --upgrade-vault-xml-v3 --check/--stage/--hodarium-state/--strict。
- src/ATHENA/Data/vault.cpp：常规运行时启动前拒绝 node_model_version>=2 的 v3 候选 Vault。
- tests/Data/Convert/Xml/xml_v3_test.cpp：临时 Vault、历史保护证据、Hodarium signed revision 篡改/非空 WAL/指纹变化、外部并发修改、活动 source UUID 丢失、候选目录不可发布等 fixture。

**特别重要的状态区分：** `--stage` 已经能够产生新目录，不代表 `--publish` 存在。下一位 Codex 不能因为 stage 回归全绿就删除保护条件。未来真正的 publish 必须把本地历史、Artifact、外部 Hodarium 与远端 revision graph 纳入一体化切换事务。

## 7. 测试与现实证据（快照，非保证以后永远通过）

### 7.1 已观察到的结果

| 范围 | 已观察到的状态 | 容易误读之处 |
| --- | --- | --- |
| ATHENA 主程序 build_qt6/ATHENA.bin | 2026-10-15 的 /tmp/athena-v3-oct15-followup-main.log 为 Built target ATHENA.bin | 仅证明编译/链接成功，非 v3 正式启动/编辑完成 |
| 全量真实 Notes 只读审计 | /tmp/athena-v3-oct15-followup-notes.log：812 个 v3 roundtrips、0 non-v2、0 failures、**2 warnings** | 测试只读取并在内存转换 Notes；不是对整个真实 Vault 履行 history/index/Hodarium cutover |
| 该只读审计统计量 | **125550 delimiters，231146 scripts，28068 blocks，2 SCRIPT_WITHOUT_SEQUENCE_BASE** | 数字随用户数据增长可改变；不能写死为迁移器的预期数量 |
| 相关 CTest | 先前复测的 XML v3、preview_math_font、legacy XML、Vault upgrade、node model、history、native math keyboard 7 组均通过 | 只表示列出的回归组合，不是全仓库 189 组全部通过 |
| Scheme Glue 生成器 | /tmp/athena-v3-oct15-glue.log：14 tests，OK | 不代表 HTML/TeX 端到端一定支持每种数学节点 |
| v3 HTML MathJax | /tmp/athena-v3-html-converter-test.log：3 passed、0 failed；包含 v2 基线与 native v3 断言 | 日志同时有“MathJax=off 必须拒绝”的预期错误输出；不是 HTML 崩溃 |
| 编辑器内的 TeX/HTML/Undo QTest | 多次用例功能断言报告 PASS；仍需解决 utf8_editor_test 测试进程**退出阶段状态 139** | PASS 不等于进程 exit 0；应区分 tear-down 问题和断言失败 |
| detached Vault + protected history fixture | xml_v3_test 中已有 stage/history/index/UUID/Hodarium 验证用例 | 不包含真实跨设备远端 cutover 和生产发布 |

请不要从聊天早期的“477 / 811、529 / 811、749 / 811、810 / 811”数字判断当前质量；那些是**已修复缺陷时的阶段性审计**。最终样本以 812/812、2 warnings 为准。那 2 条指向历史中的 `|^{2}|`，无法确定 exponent 真实基底，必须保留 warning/strict failure。

### 7.1.1 2026-10-16 新增验证记录

本段更新前表中的旧阶段状态；旧日志仅作为历史证据，不是当前失败列表。

- `build_qt6/src/ATHENA.bin` 已成功重新构建，日志 `/tmp/athena-v3-main-build.log`，退出码 0。未覆盖用户安装目录中的二进制。Linux ELF 的首个 DT_NEEDED 为 `libmimalloc.so.3`，原测试退出阶段的分配器问题已修复。
- `/tmp/athena-v3-sixteen-tests-final.log`：16 个专项套件全部通过，退出码 0。覆盖 codec、严格版本分派、迁移、原生 MathML、行编号排版/引用、原生编辑/Undo、actor 存储、Artifact/节点引用、命名空间及真实 Scheme 导出路由。
- `/tmp/athena-v3-legacy-regression-tests.log`：6 个旧格式回归套件全部通过，退出码 0，包括 node model、node location、旧 XML、Vault upgrade、File History 和 preview math font。
- `python3 tests/scheme/glue-generator-test.py -q`：17 个测试通过。导出异常的声明式绑定与 Guile unwind/continuation 恢复已有回归，不将失败吞成 `#f`。
- 原生 LaTeX 导出测试实际调用 pdflatex 并检查辅助文件中的编号序列 `1 / T / 2`，确认自定义 tag 不消耗自动计数器。不能表达的 eqnarray 自定义 tag 等情况明确报错，不自动改布局。
- `/tmp/athena-v3-row-corpus.log`：用户授权副本 `/home/felix/data/Notes (1)/Notes Root` 中 812 份文档完成 v3 roundtrip，0 failures、2 条既有脚标歧义 warning。此计数不包含 Vault 根目录下的 maintenance 历史报告。
- `/tmp/athena-v3-real-copy-preflight-fixed.log`：整库预检退出码 0，832 份当前文档、261 份 `.ath` 历史版本（其中 1 份受保护）、10592 个索引树片段、2 条既有 warning。历史数据库共有 262 行；另一行是受保护的 Hodarium JSON 逻辑记录，保留原始证据而非作为 `.ath` 转换。检查确认源副本未变。
- 预检曾在 maintenance 报告的 `cork:3E` 处阻断，现已依据随应用发布的 `cork-escaped-to-ascii.json` 将 `cork:3C`/`cork:3E` 无损迁移为字面 `<`/`>`，保留两个原始身份拥有者。未跳过报告，未添加 Vault 特例，未猜测其它未知 Cork 编码。
- 随后更新了 RAG 本地/委托索引及 Vault 搜索的语义指纹版本传递。磁盘路径保留解码结果的格式；实时搜索通过 `snapshot_document` 同一 Actor 响应取得文档树和显式存储版本（`argument[0]`）。不能根据树是否有 metadata 猜测 v3。`/tmp/athena-v3-copy-integration-tests.log` 中 `native_v3_cork_migration_test`、`rag_document_revision_test`、`vault_map_sqlite_test`、`vault_search_test`、`buffer_actor_test`、`xml_v3_test` 六组重新构建后全部通过，退出码 0。
- `/tmp/athena-v3-real-copy-stage.log`：已成功从授权副本生成 `/home/felix/tmp/athena-xml-v3-row-model-20261016`，退出码 0。832 份当前文档、261 份历史版本、10592 个索引树片段已转换并回读；3328 个派生 hash 已失效处理。迁移器比较了源目录前后清单，确认源副本未变。候选 history SQLite 的 `quick_check=ok`，仍有 262 行历史和 2 行 protected 记录。
- 候选 manifest 的 `hodarium_protocol_cutover_verified=false` 与 `eligible_for_atomic_publication=false` 均保留。它是**离线、未发布的测试产物**，不是可交给现有在线同步集群的生产 Vault，也不是完整 v3 目标已完成的证明。

### 7.2 常用隔离命令

从 ATHENA 仓库根目录执行。Notes 的绝对路径**只允许作为只读 corpus 参数**；命令不得在该目录创建新文件、日志、候选、SQLite WAL。首次接手先做 git status 和 git diff --check，不要改无关工作树。

~~~sh
git status --short
git diff --check

cmake --build build_qt6 --target ATHENA.bin xml_v3_test preview_math_font_test utf8_editor_test -j20

# 选择性运行历史上通过的 7 组
ctest --test-dir build_qt6 \
  -R '^(xml_v3_test|preview_math_font_test|athena_document_xml_test|vault_format_upgrade_test|document_node_model_test|document_history_store_test|native_math_keyboard_editor_test)$' \
  --output-on-failure

# 全量真实资料只读审计（已发生的用途仅限 READ）
ATHENA_PATH="$PWD/ATHENA" \
LD_LIBRARY_PATH="$PWD/ATHENA/lib:${LD_LIBRARY_PATH:-}" \
ATHENA_V3_READ_ONLY_CORPUS='/home/felix/data/Notes/Notes Root' \
build_qt6/tests/xml_v3_test optionalReadOnlyCorpusAudit

# Scheme Glue
python3 tests/scheme/glue-generator-test.py -q

# v3 HTML/LaTeX/edit 功能断言：必须检查整个进程成功退出
QT_QPA_PLATFORM=offscreen ATHENA_PATH="$PWD/ATHENA" \
LD_LIBRARY_PATH="$PWD/ATHENA/lib:${LD_LIBRARY_PATH:-}" \
build_qt6/tests/utf8_editor_test nativeV3HtmlMathJaxKeepsMathBlockAndScriptMetadata
~~~

测试不应靠临时修改用户 Notes 来“修复”警告。需要检查某份资料时使用 XML v2 Reader/只读脚本列出具体路径、周围 formula 和 UUID，并用独立内存 fixture 复现。

## 8. 目前正在做/只做了一半（最高风险的交接信息）

### 8.1 数学原生编辑、编号和排版

- 新节点有 DRD、基本编辑入口和排版分派，**但不是已经完成的 v3 专用编辑器**。auto_close 的括号、分隔符与大算符入口现已按 v3 编辑上下文转交原生编辑器；仍需审计其它 math menu、delete/backspace、selection、scripts 组合及文档切换路径。
- 当前 v3 格式偏好要求**强制 automatic bracket pairing**，最终输入路径应严格保证这条不变量；任何从 LaTeX、键盘宏、剪贴板或源码编辑绕入孤立 LEFT/RIGHT 的路径都需要覆盖。
- MathBlock 与多行 MathGrid 的行计数、抑制、自定义 rich tag、UUID/显式 LABEL 绑定和编号基线已有原生实现及专项回归。现有编号 toggle 通过受观察的 metadata 修改行策略，保留 UUID、正文、光标和 Undo；不插入旧 eq-number 节点。旧式 eqnarray 在本仓库不代表必定编号，不能“照 LaTeX 名字想当然”。
- MathGrid 的某些 typed cell-format 已迁移/排版，但导出到 CSS HTML/TeX 及引用位置的几何锚点仍不完整。需要对所有 grid kind、跨行/跨列及大型公式做视觉、交互和引用测试。

### 8.2 导入导出

- LaTeX 的 metadata-aware 路由解决了 tree→Scheme 会丢失 p/k/b 的问题，但**仍只实现了明确支持的 TeX 命令子集**。manual delimiter 单独尺寸、某些 Unicode 大算符 font-specific upright、复杂 math-symbol、MathAccent、带格式 MathGrid 等应继续扩充；不能用丢失 typed property 的备用 serialization“救场”。
- HTML 当前为原生 tree→metadata-aware→MathJax 的可测试线路；非 MathJax/native CSS/PNG 与 MathML export 尚未支持，明确报错。真实 Scheme document/snippet 路由已有 fail-closed 测试，不允许经保留对象或图片路径绕开失败。
- MathML 有独立原生 v3 importer，显式 mathml-v3 document/snippet/stree converter 直接返回 native tree，不经 metadata-dropping Scheme AST。原 v2 HTML/MathML 自动识别路径尚未切换；原生 LaTeX 导入仍待实现。不能先构造历史 AROUND/BIG_AROUND 再调用运行时 migrator。

### 8.3 整库候选与外部状态审计

- 检查与 stage 在临时 Vault fixture 已有代码，但**不是一个经过全部 failpoint、时钟/权限/跨设备/磁盘空间/WAL/回滚演练的发布系统**。
- 外部 Hodarium fingerprint 目前显式指定目录；还需要核实多个绑定/多 group、历史 disabled binding、跨路径和复制旧 state 的安全语义。真实设备上的 remote freeze 和 DAG 切换没有实施。
- Artifact 和 Enunciation/Bold-text 的每个当前/历史 schema、派生 hash/cache 重建都需要最终逐 schema 验收。UUID/引用路径的完整 mapping 及全覆盖仍应加强。
- 授权整库副本已完成一次 preflight/stage（见 7.1.1），但尚未完成 GUI 编辑/保存/重启、历史恢复、搜索、跨设备同步和失败回滚的端到端验收。源文档语义指纹与 Qt 结构化查询传输是两个独立边界；后者的后续接入和测试状态见 8.4。
- 在这几块完成前保留 Vaultfile v3 正常打开拒绝和 manifest eligible_for_atomic_publication=false，切忌先让写入生产的 UI 入口“试用”。

### 8.4 运行时接入审计（2026-10-16 后续批次）

以下修改已进入工作树，专项验证结果见下；这仍不表示生产端到端验收完成：

- 新建源文档按目标 Vault 的 node_model_version 选择 XML v3 或原有 v2；namespace 模板保留显式版本，不自动迁移不兼容的旧数学模板；AUDMAP 创建允许 v3 typed properties，但不能带入已有 UUID/Artifact 绑定。
- 嵌入式输入控件有显式版本的创建入口；Global Search 在创建查询编辑器时选择目标版本，已有查询保持自己的版本。查询、inserter 快照和预览使用自描述 native fragment；worker 不再强制 read_xml_v2，版本缺失/错配或非法片段须报告错误。
- AVD 的计数器 checkpoint 带 native XML envelope；原生 MathBlock/MathGrid 行策略参与 counter-only evaluation。已求值环境不是文档源，v3 环境指纹复用现有 AUDMAP typed document-model 编码，不发明新的树编码，也不把样式宏强塞进 v3 文档 schema。
- Artifact 拒绝列表的查询、右键加入、Preferences、提取 worker、逻辑记录 adapter 保留 fragment 版本；离线 preflight/stage 纳入 `.lst` 内的结构化名称，而不将它当作派生缓存删除。旧纯文本列表仍可读，v1/v2 指纹不被默认改成 v3。
- 新建文档的实际 AUDMAP 路径暴露出原生数学 DRD descriptor 缺少类型声明。已在 `standard_tree_labels.inc` 补齐描述符类型，保留正文的可编辑角色和 long-arrow 结构化 descriptor 的不可编辑角色；没有在节点模型里增加标签名特判。新测试覆盖 11 类原生节点、两种 arrow descriptor、grid cell 和身份分配。

本批最终验证（2026-10-16）：

- `/tmp/athena-v3-runtime-main-build.log`：主程序 `build_qt6/src/ATHENA.bin` 构建/链接成功，退出码 0；未覆盖安装二进制。DRD 对象的构建时间晚于本批源修改，ELF 首个 DT_NEEDED 仍为 `libmimalloc.so.3`。
- `/tmp/athena-v3-runtime-fifteen-tests-final.log`：15/15 CTest 套件通过，进程退出码 0。包括 `artifact_title_filter_v3_test`、`artifact_v3_runtime_test`、`hodarium_logical_database_test`、`xml_v3_test`、`native_v3_document_creation_test`、`new_buffer_test`、`namespace_database_test`、`buffer_actor_test`、`vault_search_test`、`compound_virtual_document_v3_test`、`native_v3_input_widget_test`、`native_v3_search_query_widget_test`、`native_v3_row_numbering_test`、`native_v3_export_test` 和 `document_node_model_test`。
- 输入控件测试使用真正的 Qt offscreen 控件及隔离用户目录，不开启会替换控件为空壳的 ATHENA headless 模式。验证键盘输入按 v1/v2/v3 来源分派、已有 v3 查询不被后续创建器覆盖、销毁后 buffer 移除；Global Search 验证 Vault 切换中的查询树和版本保持。AVD 测试包含计数器 checkpoint 往返、行标签/UUID、嵌套作用域和两个原生 PDF 成员的导出与合并。
- `/tmp/athena-v3-runtime-copy-preflight.log`：授权副本 `/home/felix/data/Notes (1)` 全库预检成功，832 份当前文档、261 份 `.ath` 历史版本、10592 个索引树片段、2 条已知 warning，源副本未变。
- `/tmp/athena-v3-runtime-copy-stage.log`：生成新的隔离候选 `/home/felix/tmp/athena-xml-v3-runtime-20261016`，退出码 0；转换并验证 832 份文档、261 份历史、10592 个索引树，失效 3328 个派生 hash。候选历史库 `quick_check=ok`，262 行历史、2 行 protected，另存 1 份迁移过的受保护 `.ath` 原始证据。另一个 protected 逻辑记录不作为文档数学转换。
- 新候选 manifest 的 `hodarium_protocol_cutover_verified=false`、`eligible_for_atomic_publication=false` 仍成立，原始 `/home/felix/data/Notes` 未写入。新的拒绝列表迁移路径还通过了包含结构化 legacy math 的独立 stage/失败不落盘 fixture；不能仅从真实副本的片段总数推断这个特性得到覆盖。

本批发现的运行时入口由后续批次继续处理，见 8.5。`hodarium_inventory.cpp`、`hodarium_application.cpp`、`hodarium_application_live.cpp` 和 `QTMHodarium.cpp` 的协议切换仍未完成，不能全文替换 read_xml_v2/write_xml_v2 后宣布完成。

以下为这次审计定位的表示边界；其后续修改见 8.5，不应再把此列表直接当作当前未修复清单：

- `vault_maintenance_common.cpp` 的读取结果丢弃 source format；images/persons/wikilinks/evaluation-bar passes 还调用 `tree_to_texmacs`。`vault_file_references.cpp`、`person_names.cpp`、`redundant_wikilinks.cpp`、`evaluation_bars.cpp` 的递归重建会丢掉 compound metadata。evaluation-bar 变换还合成旧 `VAR_AROUND`；v3 必须直接改写 native 节点或明确拒绝。summary 创建和 person 发现也依赖旧 markup。需要验证 no-op 字节不变、完整树/富属性保留、原子发布和 stale-source 拒绝，不只是成功重新解析。
- Actor HTML 的 `edit_typeset_rep::exec_html` 经 `convert_OTS1_symbols_to_universal_encoding`；后者重建 compound 时丢失 metadata，后续 `expand_references`/`simplify_execed` 也需审计。已通过的 converter 测试不覆盖这条 Actor 前处理路径。
- `websites.cpp` 仅保留导入树，`websites_export.cpp` 的临时 HTML 源固定写 v2；`rewrite_static_links` 未通用遍历 rich properties（例如 `math:row-tag`），不能只照顾 enunciation name。导入返回 `_ERROR` 也必须在边界拒绝，不能输出一页错误树后报告成功。
- `data_art.cpp` 创建 PDF scratch actor 时未随源快照传递存储版本。普通 website PDF 使用源 actor 与这条 DataArt 临时路径不能混为一谈。
- `edit_math.cpp::native_v3_math_context` 尚有为早期 fixture 保留的 AST 推断分支；最终应让所有 fixture/临时控件显式设置存储版本，再移除此推断，不根据内容猜测文档版本。

### 8.5 属性、维护与网站的实际入口（2026-10-16）

本批实现已进入工作树，主程序构建、13 项最终回归和 3 项实际运行时测试通过。验收范围仅为本节列出的入口，不代表完整 XML v3 生产迁移完成。

- 节点属性快照使用自描述 native envelope，明确携带源版本；属性头不伪装成缺少必需孩子的 native 数学节点。Actor 提交检查版本、节点 lease 和属性快照，富文本输入控件沿用源版本。版本比较视图按各自版本创建只读 buffer，保留 typed metadata，并将高亮操作交回 Actor。
- 维护读取保留实际源格式；无修改时保持原始字节，有修改时要求完整文档往返相等。旧 markup/Scheme 沿用其原始表示，而不是先做语义 import 再交给旧 writer。资源、人名、冗余链接、enunciation、evaluation-bar 变换不再丢弃已存 metadata/UUID。资源引用也遍历 list/dictionary 中的富文本。
- v3 evaluation-bar 直接生成 Delimited；不能无歧义改写的带脚标裸竖线、需要拆分的 attributed span 明确拒绝，不改写为旧 VAR_AROUND。person/evaluation/wikilink 的富属性本批保留但不递归修改；资源路径必须遍历富属性。summary 由结构化树生成，按目标 Vault 版本写入。
- 维护快照不长期持有每份文档的 FD；保留目录身份、文件 revision 和原始字节，提交时重新打开并校验，最终使用现有 confined 原子替换。新增低 FD 限额、目录替换、symlink 和 stale-source 用例。资源与冗余链接批次保留原先 stage/backup rollback 流程，不声称已实现整库事务或不合作写者的 CAS。
- 网站导入保留源格式并拒绝错误树；临时 HTML 文档按实际源版本编码，不通过旧 enunciation 别名来掩盖 metadata 丢失。OTS1 转换、exec_html 的引用展开/简化和链接资源重写保留富属性；v3 源不再经过旧 OTS1 转换。DataArt 快照携带 Actor 版本，临时 PDF buffer 使用显式版本替换；失败不再偷偷导出无 DataArt 的 PDF。
- 原生数学编辑仅依赖 buffer.storage_version；祖先树猜测分支已删除，旧 fixture 改为显式标记版本。
- 冷启动实测发现 v3 读取结果曾依赖先前是否加载过样式：文件 envelope 的 `style/body/...` 和已声明的 `strong` 会被当作未注册标签。envelope 在标准 DRD 中注册；随附 `.ats` 中的 literal `assign` 到 `macro/xmacro` 声明生成 build-directory 标签目录，供桌面与独立 node 初始化使用。目录不扫描待打开文档、不执行宏、不放宽 v3 reader。动态宏名、别名和自定义样式仍需要声明所有者显式注册，不能据此宣布所有扩展可冷加载；详见 `src/Data/Drd/shipped_macro_labels.md`。
- 实际只读版本比较暴露 `url_scratch` 只检查磁盘、不检查已打开 buffer 的问题；两侧快照原本获得同一名称。比较入口现检查 live buffer 重名，保留两侧独立格式/身份，而不是覆盖已有快照。

本批已确认的验证：

- `/tmp/athena-v3-boundaries-retested.log`：文档创建、维护、属性 dialog 通过。维护覆盖 832 个快照、FD 限额 64、目录替换、symlink、stale source 和各版本 summary。
- `/tmp/athena-v3-diff-scratch-test.log`：实际 Qt/Actor 属性与版本比较完整通过，包含 v2/v3 属性提交与 Undo、过期版本/lease 拒绝、富文本输入、混合版本只读比较与属性差异查看。
- `/tmp/athena-v3-boundaries-regression.log`：旧维护、enunciation、网站快照、原生数学和 UTF-8 编辑回归通过。随附标签生成器的 6 项 Python 测试通过。
- `/tmp/athena-v3-boundaries-final-build.log`：主程序已链接成功；同一次 make 随后的新增测试 target 因自动 reconfigure 前的旧目标表报 `No rule`。随后 `/tmp/athena-v3-boundaries-final-tests-build.log` 和 `/tmp/athena-v3-boundaries-main-verified-build.log` 均退出 0。最终产物仍是 `build_qt6/src/ATHENA.bin`，未覆盖安装版本；mimalloc 仍是首个 DT_NEEDED。
- `/tmp/athena-v3-boundaries-final-regression.log`：13/13 通过（两个生成器、文档创建、网站快照、native 导出、XML v3、冷标签读取、原生数学编辑、UTF-8 编辑、维护、实际属性/版本比较、属性 dialog、ROW 编号）。冷标签用例不启动 Guile 或先读 v1/v2，验证标准宏可直接读取、未知标签不能自行注册、旧数学节点仍被拒绝。
- `/tmp/athena-v3-boundaries-runtime.log`：真实网站导出、AUDMAP v3 runtime、DataArt Scheme 三项均通过，进程退出码 0。网站测试覆盖冷启动直接 Actor 加载、不同版本 HTML/native 导出、显式版本 DataArt 临时 PDF、malformed source 与不支持的 eqnarray 自定义 tag 拒绝；错误不产生页面，不改写源文件。
- `/tmp/athena-v3-website-row-roundtrip.log`：补强后的实际 Actor native 导出再读通过，检查 ROW UUID、富文本 tag 内容、unnumbered、空 tag 与缺失 tag 的区别。
- 独立 node 的 Dockerfile 已补标签生成器、随附样式资源的 COPY 和 build-stage Python 依赖声明；本轮未构建容器、未安装软件、未部署 NAS，不能把桌面测试当成独立 node 的构建/协议验收。

仍需继续的实际边界：

- `clipboard_xml.cpp` 当前仍默认 v1/v2，`edit_select.cpp`、Qt own/named clipboard 快路径和 AVD copy/paste 的来源版本没有完整贯通。必须同时处理 origin version、目标版本验证、UUID copy/move、AVD 修改前预检，不能只换一个 codec。不能在 cut 后才发现片段不合法。
- `tree_search.cpp::match` 的结构化比较尚未明确匹配 native typed math policies；版本化 query transport 已完成不等于结构化匹配语义完成。atomic/concat/document/select-region 路径可绕开普通 match，`append_content_matches` 重建 math wrapper 也丢失 wrapper 属性；fuzzy 不能绕开显式属性约束。建议数学 descriptor 按位置/值、数学属性按类型比较且忽略 UUID，表达式正文保留既有 pattern 搜索。通用非数学属性是全图相等还是 query 子集、rich tag 是否支持 pattern 仍须确认，未擅自实现。
- 维护的 live Actor/Undo 调度仍须端到端验收；本批的磁盘/离线变换用例不能代替它。
- 原生 LaTeX 导入、完整 native 编辑路径、Hodarium 格式 epoch/握手与旧 revision DAG 保留、候选原子发布/回滚仍待完成。生产安全闸保持关闭。

### 8.6 剪贴板来源版本与修改前预检（2026-10-16）

本批已进入工作树，集中构建和测试尚未完成，以下不是验收通过声明。

- `clipboard_xml` 保持既有 texmacs/content/mode/language/可选 move tuple。运行时 header 中的 typed origin marker 随 named/own-PID 快路径传递，写 XML 前去除，读取时由实际 XML envelope 版本恢复。输入中的伪造 header、冲突版本、源内容中的 origin marker 明确拒绝。旧无标记片段只按原 v1/v2 规则处理，绝不从数学 AST 猜成 v3。
- 来源与目标均须 codec 往返相等。v1/v2 泛型 XML 虽然可以拼写 v3 节点名，但剪贴板语义校验拒绝这种伪装，也检查 rich/list/dictionary 内的数学结构及 typed math 属性。共同可表达的内容允许跨版本，不自动迁移旧数学。
- `selection_validate_paste` 在普通编辑器和 AVD prepare 阶段共用，检查目标格式、只读和模式；在 cut、插入、UUID move claim 之前失败。graphics cut 先 copy/publish 再删除；失败的剪贴板发布不会继续 cut。临时 Paste From/Copy To 格式在异常时恢复。
- Qt 原生 MIME 不向纯文本备用缓存降级；内外部快路径使用同一 origin 合约。AVD 按 Actor storage version 编码各片段，合并之前验证共同目标版本；不兼容数学、重复身份等在 cut 前报错。
- 完整的原生显示网格选择提升到实际 MathBlock，保留 inherited numbering 和对象身份，而不是拼一个带重复 UUID 的包壳。部分 ROW/CELL 的上下文尚不能完整搬运时明确拒绝，不能把这说成完整的原生矩形选区支持。
- `insert_tree` 不再把 attributed DOCUMENT 当成无属性的段落列表摊平；它作为块保留容器。源根选择仅去掉根 UUID，保留其它属性。AVD 合并同样只展开无属性 DOCUMENT。
- 新测试目标：`clipboard_origin_version_test`、`compound_clipboard_v3_test`；既有 `document_node_copy_test`、`qt_utilities_test`、`utf8_editor_test` 增加/调整了相关断言。可选真实副本冷读入口是给 `clipboard_origin_version_test isolatedCorpusColdRoundtrip` 设置 `ATHENA_V3_CLIPBOARD_CORPUS`，只读取指定目录，不写文件。
- 当前集中构建日志：`/tmp/athena-v3-clipboard-build.log`。须先核对最终状态和真实测试结果，生产闸仍关闭。

### 8.7 结构化数学搜索：typed policy 和匹配路径（2026-10-16）

- `src/Data/Tree/tree_search.cpp`：结构化查询对显式指定的 typed properties 比较类型和值，忽略被搜索节点及 rich-text 内部节点的源 UUID；列表/字典比较其精确结构。所有 `math:*` 属性为严格的双向约束：缺失的行编号策略表示 inherit，不能匹配显式 numbered/unnumbered；脚标 placement、网格 kind、公式编号等同理。
- v3 数学节点不走可跳孩子的 injective 匹配；DELIMITED、BIG_OPERATOR 等 descriptor 槽位按原生结构内容比较，不能在左右括号或 sizing 描述符上按子串匹配。原生脚标等节点的空内容槽位也不能被全局 blank-match 开关当作通配符。MathVariant 只有第一个槽位是 descriptor，第二个是正文。
- 原子查询、带属性的 CONCAT/DOCUMENT 快速路径、SELECT_REGION 容器和 `append_content_matches` 保持同一约束语义。临时 math/select-region 包装保留原 math 查询属性；`select-region` 自身仅作为搜索操作，不误比较为具有 math 属性的持久节点。带属性原子查询不允许 fuzzy 路径绕开显式约束。
- `tests/Plugins/Qt/vault_search_test.cpp::nativeV3StructuredSearchHonorsPolicies` 新增 source UUID、脚标策略/空槽位、定界字形/sizing、MathGrid kind/ROW 编号/富标签、math 包装属性和原子 fuzzy 拒绝的断言。**这些新断言尚未构建和运行；主程序构建结果与测试验证要分开报告。**
- 最终 `cmake --build build_qt6 --target ATHENA.bin -j20` 编译/链接成功，退出码 0，日志 `/tmp/athena-v3-structured-search-verified-build-20261016.log`；`git diff --check` 通过。构建产物经 `install` 到 `.new`、`cmp`、`mv` 和部署后 `cmp` 校验，已部署到 `ATHENA/bin/ATHENA.bin`；正在运行的进程不会因此热更新。未运行测试目标、未修改生产 Vault、未启用 XML v3 发布。
- 此批修改仅约束结构化匹配，未启用 v3 生产 Vault；也不代表完成了结构化查询的所有产品级语义（尤其是多节点局部匹配、select-region 嵌套与 live Actor 实际检索验收）。

### 8.8 结构化搜索补强及独立运行时验证（2026-10-16 后续）

- `tree_search.cpp` 中原生 `MATH_GRID` 的 ROW/CELL 结构现在必须逐行逐格按位置、列数和 typed metadata 比对；不能利用旧版 `allow-injective-match` 偏好跳过网格单元。空 CELL body 也不再作为 blank-match 通配符。普通非 MathGrid 的文字 TABLE 仍走原有搜索流程。
- 空 `CONCAT` 查询不再访问不存在的 `what[0]`；空字符串原子查询的字节游标在零长度命中后推进到下一个 grapheme，避免循环停滞。
- 新增 `tests/scheme/native-v3-structured-search-runtime-test.scm`，并在 `tests/CMakeLists.txt` 注册 `native_v3_structured_search_runtime_test`（使用既有隔离运行器及 application runtime resource lock）。该测试走完整 ATHENA.bin → 生成 Scheme glue → C++ 搜索路径，覆盖普通文本基线、空 CONCAT、脚标策略、网格多余 CELL、空 CELL 和不同 grid kind。
- 使用已有 `build_qt6/src/ATHENA.bin` 与私有 `/tmp` 测试 Profile 实际运行该脚本，`ATHENA-SCHEME-RUNTIME-PASS` 且进程退出码 0。**这不替代尚未构建的 Qt `vault_search_test.cpp` 新测试**；测试文件新增的细粒度 cases 后续仍要在被明确授权的测试目标构建中执行。
- CMake 更新后的正式 `cmake --build build_qt6 --target ATHENA.bin -j20` 已完成，退出码 0，日志 `/tmp/athena-v3-structured-search-20261016-final.log`。只运行注册的单项 `ctest --test-dir build_qt6 -R '^native_v3_structured_search_runtime_test$' --output-on-failure`，结果为 **1/1 PASS、退出码 0**，日志 `/tmp/athena-v3-structured-search-ctest-20261016.log`。部署通过 `.new` 安装、`cmp`、原子 `mv` 和部署后 `cmp` 校验；未启动/重启用户当前进程，未触碰真实 Vault 或 Hodarium，未解禁 XML v3 生产发布。

### 8.9 含选择区域的原生数学结构查询（2026-10-16 晚）

- 再次沿已注册的隔离 Scheme→C++ runtime 搜索入口写下先失败再修复的断言，证实原生 MathGrid 在 `select-region` 下绕过了 ROW/CELL 的完整位置及列数检查：短一列的 grid 查询会命中两列来源。`src/Data/Tree/tree_search.cpp::select_direct` 现在对 native math 按位置校验完整槽位、math descriptor、空值、typed property 和其余子表达式，并对 MATH_GRID 严格校验 ROW/CELL 结构；只在全部约束匹配后发布选区，避免部分命中泄漏。普通非数学的自由选择顺序保持原有行为。
- 第二项先失败后修复的回归表明，Vault 对 math 查询自动添加 `select-region` 后，已有内层选区会因进入普通 `search` 而被当作无效标签。嵌套选区现在重新进入 `select` 分派；无内层选区时保留以前的 math 局部文本搜索。另覆盖单侧不可见定界符不能匹配可见端点、MathScripts placement、MathVariant kind、普通 inline 选择，以及外层 MathGrid 行列数约束。
- `tests/scheme/native-v3-structured-search-runtime-test.scm` 新增上述完整入口断言；最终新增用例通过 `ctest --test-dir build_qt6 -R '^native_v3_structured_search_runtime_test$' --output-on-failure`，结果 1/1 PASS、退出码 0，日志 `/tmp/athena-v3-select-coverage-ctest-20261016.log`。主程序指定命令 `cmake --build build_qt6 --target ATHENA.bin -j20` 构建成功，退出码 0，日志 `/tmp/athena-v3-select-nested-build-20261016.log`。已有 `vault_search_test.cpp` 加入真实 Qt Vault math 包装路径的细粒度断言，但**这次没有构建或运行该 Qt 测试目标**，不能把 Scheme runtime 的成功说成 Qt suite 通过。
- 实际格式 cutover、整库 release/publish 与 Hodarium wire protocol 仍保持禁用和隔离；这批修复不改变 `.ath` 的存储版本、迁移状态或任何线上服务。

### 8.10 数学描述符与 Vault 搜索文本的所有权（2026-10-16 晚，后续批次）

- `tree_search.cpp::is_accessible_for_search` 之前在 `DRD_ACCESS_SOURCE` 下访问了原生 DELIMITED 的物理 sizing、括号端点及 MATH_VARIANT 的 kind，导致用户文本查询误命中并非表达式正文的 AST 字段。使用隔离真实 ATHENA→Scheme→C++ 搜索入口先复现 `a` 意外命中 DELIMITED 自动尺寸策略，再阻止纯文本搜索穿过隐藏的 descriptor 槽位。**完整结构化数学查询**仍精确比较相应 descriptor，不降级为文本搜索。
- 统一 AST 描述符所属规则于 `src/Data/Tree/math_tree_v3.hpp/.cpp`：`descriptor_slot` 标识结构比较时须精确匹配的物理槽位；`hidden_search_descriptor_slot` 指出不能当作独立可见文本的子槽位。特别区分用户可见的 BIG_OPERATOR、MATH_MIDDLE 及 Unicode MathLongArrow 字形；这些虽属 descriptor 但应保留文本搜索能力。DELIMITED 端点、大小策略、MathVariant kind、BigOperator upright 布尔值、MathSymbol 语义注册 ID 等不被直接当作正文文本。
- `QTMVaultSearch.cpp` 的 fuzzy 原子遍历、标题/可用页面的补充原子搜索、`tree_has_visible_text`、`visible_tree_text` 与前导粗体作用域搜寻共用隐藏描述符规则，避免 fuzzy/预览线路绕过底层精确搜索约束。新增 Qt `nativeV3SearchExcludesMathDescriptorsFromTextAndFuzzy` 回归，验证 `1024` 手动尺寸不能作为正文或近似 `1023` 命中，而正文中真正出现的数字仍可命中。**该 Qt 测试目标尚未单独构建/执行，不声称 Qt fuzzy 路径已得到运行时验收。**
- 已注册的隔离 Scheme 运行时回归增加 source-mode、手动 `1024`、MathVariant kind、可见 `∑`/`|`/`→` 与内部 upright policy 的正反用例。`cmake --build build_qt6 --target ATHENA.bin -j20` 编译链接退出码 0，日志 `/tmp/athena-v3-math-visible-slots-build-20261016.log`；`ctest --test-dir build_qt6 -R '^native_v3_structured_search_runtime_test$' --output-on-failure` 为 **1/1 PASS、退出码 0**，日志 `/tmp/athena-v3-math-visible-slots-ctest-20261016.log`。未运行全套测试，未访问生产 Vault，未改变 XML v3 迁移/发布安全闸。
- 已依项目要求通过 `ATHENA/bin/ATHENA.bin.new` 临时安装、`cmp`、原子 `mv` 与部署后 `cmp` 验证发布 `ATHENA/bin/ATHENA.bin`；没有重启正在运行的进程，也没有自动提交或推送。正常 Vault 入口仍拒绝 `node_model_version >= 2` 的候选版本。

### 8.11 单段 DOCUMENT 查询越界及 typed metadata 局部匹配（2026-10-16）

- 检查 `tree_search.cpp::search_document` 时发现：原搜索代码在查询 DOCUMENT 仅有一个子节点、源 DOCUMENT 有两个以上子节点的情况下，会进入多段遍历并访问 `what[1]`。新增真实隔离 ATHENA→Scheme glue→C++ 搜索用例 `(document "alpha" "beta")` 对 `(document "alpha")`，先复现 **SIGSEGV、进程退出 -11**（`/tmp/athena-v3-single-document-red-20261016.log`），再为单子节点查询走逐段搜索分支。对单段负例、双段正负例及空 DOCUMENT 查询也补充用例，防止修复单段时破坏多段搜索。
- 随后发现带属性的单段 DOCUMENT 查询在属性相同的来源中仍然无法局部命中：旧快速路径仅凭“查询是否带属性”决定退回严格整树匹配，导致长度不等时无结果。CONCAT 查询同样存在这个问题。`search(range_set&, tree, tree, path)` 现在**先验证来源满足查询的 typed property 约束**，满足才进入原来的 DOCUMENT/CONCAT 局部搜索；不满足时仍走普通结构搜索，不允许缺失属性的来源伪装命中。UUID 不成为查询条件，数学策略的双向属性约束保留。
- 新增隔离 Scheme 回归覆盖有/无匹配属性的 DOCUMENT 与 CONCAT 局部检索；随后 `cmake --build build_qt6 --target ATHENA.bin -j20` 成功、退出码 0，日志 `/tmp/athena-v3-scoped-structure-build-20261016.log`。`ctest --test-dir build_qt6 -R '^native_v3_structured_search_runtime_test$' --output-on-failure` 结果 **1/1 PASS、退出码 0**，日志 `/tmp/athena-v3-scoped-structure-ctest-20261016.log`。只构建主程序，没有单独构建尚未运行的 Qt 测试目标或运行全套测试。
- 本次没有对生产 Notes、Hodarium 或 XML v3 发布闸进行任何写入或切换；本节是运行时搜索安全性修复，不代表整体格式迁移完成。
- 最终产物已通过 `ATHENA/bin/ATHENA.bin.new` 临时安装、`cmp`、原子 `mv`、部署后 `cmp` 校验安装到 `ATHENA/bin/ATHENA.bin`；没有自动重启用户正在运行的实例，也没有 commit/push。`git diff --check` 通过，普通运行时对 `node_model_version >= 2` 的 Vault 拒绝仍存在。

### 8.12 结构化查询的嵌套属性边界、精确结果范围与命中上限（2026-10-16 夜）

- 在已有隔离 ATHENA→Scheme glue→C++ 运行时回归中先复现 `match_cascaded` 的 metadata 绕过：带 `query:scope` typed 属性的嵌套 CONCAT 查询可以当作纯文本与未标记的 source atom 匹配，进而让随后的 `select-region` 错误产生结果。`src/Data/Tree/tree_search.cpp::match` 现将**带属性的查询必须有相同源节点类型且其属性必须满足约束**放在旧版 blank/wildcard 快路径之前。普通无属性的 `CONCAT`→原子字符串匹配保留；真正带相同属性的 source CONCAT 也能匹配。先失败的测试日志 `/tmp/athena-v3-select-metadata-red-20261016.log`，修改后主程序构建通过，隔离 CTest 1/1 PASS；日志 `/tmp/athena-v3-match-metadata-build-20261016.log`、`/tmp/athena-v3-match-metadata-ctest-20261016.log`。
- 用实际运行时输出核对并固定**精确范围**：结构化数学 `DELIMITED` 中选中 `xy` 返回 `(0 5 0)` 到 `(0 5 2)`，Unicode `β` 返回两字节范围 `(0 5 2)` 到 `(0 5 4)`；连续两段 DOCUMENT 命中返回 `(0 0)` 到 `(1 4)`。新的断言防止数学描述符被错误计入正文偏移，或混淆 UTF-8 字节数与字符数。
- 增加实际 native typed 结构用例：`MATH_GRID` 的 ROW `math:row-numbering` 在 inherit/numbered/unnumbered 间严格区分；`math:row-tag` 的 rich-text 比较忽略原节点与查询富文本的不同 UUID，但不能忽略富文本内容；CELL 的 `math:cell-format` 字典要求类型与键值一致，缺失属性不能匹配显式属性。Scheme→C++ 单项 CTest 实际运行均通过。
- 另找到全局 `search_max_hits` 的计量错误：原代码把 `range_set` 中每个命中的两条起止路径当作两个命中计数，请求 20 个结果时只有约 11 个。先失败的 `/tmp/athena-v3-hit-limit-red-20261016.log` 证明此误差，随后统一按 `N(sel)/2` 计算，并在 `merge()` 处维护全局上限。新增 20/1/0 上限、跨 DOCUMENT 与 CONCAT 的断言。指定主程序命令 `cmake --build build_qt6 --target ATHENA.bin -j20` 已成功，日志 `/tmp/athena-v3-hit-limit-build-20261016.log`，定向 CTest 1/1 PASS（`/tmp/athena-v3-hit-limits-extended-20261016.log`）。
- 完整性抽样：只运行 `node_data_art_scheme_test`、`node_model_bridge_scheme_test`、`native_v3_structured_search_runtime_test` 这三个隔离应用运行时目标，**3/3 PASS、退出码 0**，日志 `/tmp/athena-v3-search-bridge-retest-20261016.log`。未编译/运行 Qt `vault_search_test.cpp` 的新增 target、未运行全套 suite，不将单项 Scheme 成功等同于真实 Vault/Qt 搜索 GUI 全链路验收；未触碰生产 Notes、Hodarium，也未开启 v3 生产格式发布。
- 主程序构建产物已按既定步骤通过 `ATHENA/bin/ATHENA.bin.new`、`cmp`、原子 `mv`、部署后 `cmp` 发布至 `ATHENA/bin/ATHENA.bin`，但没有重启正在使用的 ATHENA 进程。工作区暂未 commit/push，保留候选 Vault 拒绝打开的代码和全部发布安全闸。

### 8.13 XML v3 数学校验器的畸形子节点与描述符身份安全（2026-10-16 继续）

- 实际独立 ATHENA 导出入口 `latex-export-prepare-v3-source` 复现 `MATH_LONG_ARROW(MATH_SYMBOL(CONCAT("differential-d")), "", "")` 在 `math_tree_v3.cpp::valid_native_math` 中直接读取非原子 `->label`，产生 **SIGSEGV / 退出 -11**（日志 `/tmp/athena-v3-malformed-arrow-probe-20261016.log`）。现要求复合 MATH_SYMBOL 的唯一子节点先满足 `is_atomic`，之后才用权威 registry 解析；无效结构经已有异常边界明确拒绝。实际隔离 `native-v3-export-runtime-test.scm` 已验证此畸形输入不崩溃、拒绝后再次导出正常。
- 另有独立身份丢失证据：给 `BIG_OPERATOR` Unicode 字形子节点赋 UUID，原生 v3 导出校验旧行为仍接受（日志 `/tmp/athena-v3-descriptor-metadata-probe-20261016.log`），但紧凑 XML v3 的 `<o k=... u=.../>` 只保留父节点 metadata，不可能保存描述符子节点的 UUID。相同问题涉及 DELIMITED 的五个 `<d>` 属性与 MATH_MIDDLE 的 `<m k=.../>`。`valid_delimited`、`valid_big_operator` 和 `valid_native_math(MATH_MIDDLE)` 现 fail closed 拒绝子 descriptor 上所有 UUID/typed properties；保留正文子树的属性和 ID。在 `athena_document_xml_v3.cpp` 中 MATH_MIDDLE 的专用序列化分支也改为调用完整 `valid_native_math` 校验，避免只有单独的 native validator 严格、真正 XML writer 却绕过它。
- 给 `tests/scheme/native-v3-export-runtime-test.scm` 加入上述畸形箭头与 DELIMITED/BIG_OPERATOR/MATH_MIDDLE 所有紧凑描述符的身份拒绝断言，并检查失败后可重新成功导出。**已用完整 ATHENA.bin + 隔离运行器执行，进程退出码 0**，日志 `/tmp/athena-v3-descriptor-identity-final-runtime-20261016.log`。补充 `tests/Data/Convert/Xml/xml_v3_test.cpp` 对畸形箭头的 fragment/document 拒绝、每种携带身份的描述符拒绝、有效箭头和正文带 UUID 的合法 roundtrip 的直接 codec 断言；**这些 Qt/C++ 测试尚未构建与运行**，不能声明 XML writer roundtrip 测试通过。
- 主程序依规范 `cmake --build build_qt6 --target ATHENA.bin -j20` 构建/链接成功，退出码 0，日志 `/tmp/athena-v3-descriptor-identity-verified-build-20261016.log`。其余已注册隔离运行时 `node_data_art_scheme_test`、`node_model_bridge_scheme_test`、`native_v3_structured_search_runtime_test` 经定向 CTest **3/3 PASS、退出码 0**，日志 `/tmp/athena-v3-validator-final-smoke-20261016.log`。没有重新配置、构建测试 target、运行整个 suite、触碰生产 Vault/Hodarium 或解禁 XML v3 publish。
- 本批最终二进制已经通过 `.new` 临时安装、`cmp`、原子 `mv` 和部署后 `cmp` 校验更新至 `ATHENA/bin/ATHENA.bin`；运行中的程序不会因此热更新。修改继续留在 dirty worktree，无 Git commit/push；正常 Vault 入口仍拒绝打开 XML v3 候选。

### 8.14 原生 MathGrid 单元格高度模式的严格性与剩余发布门槛（2026-10-17）

- 进一步核对 `src/Typeset/Table/cell.cpp`、`QTMTablePropertiesPane.cpp` 和 `edit/Interface/table_commands.cpp` 的权威高度模式：`auto`、`exact`、`min`、`max`。`math_tree_v3.cpp::valid_grid_row` 此前只验证 `math:cell-format.height-mode` 是字符串，任意伪造模式都会合法地写入 XML v3，却在原生 Typesetter 中默默表现为默认模式，造成布局语义失真。现在只允许四个已有实际实现的枚举值，其余在 v3 原生模型/codec 验证时 fail closed。
- 先向 `tests/scheme/native-v3-export-runtime-test.scm` 加入未知模式 `invented` 必须由 **native math validator** 拒绝的运行时断言，隔离 ATHENA 中验证其原先错误落入 LaTeX 的“不支持 height-mode 导出”分支，未能在原生模型层拒绝（`/tmp/athena-v3-cell-mode-red-20261017.log`）。修改后完整主程序 `cmake --build build_qt6 --target ATHENA.bin -j20` 构建/链接退出 0（`/tmp/athena-v3-cell-mode-build-20261017.log`）；隔离原生 v3 export 测试进程退出 0（`/tmp/athena-v3-cell-mode-runtime-20261017.log`）。四个合法高度模式仍是可接受的 native 模型，但当前 LaTeX 对它们明确报不支持，绝不自动降级；这也是该运行时回归的正向对照。
- `tests/Data/Convert/Xml/native_v3_grid_model_test.cpp` 增加 `cellHeightModeRejectsUnknownValues`：直接验证四个合法值的严格 XML v3 往返，以及未知值被 writer 拒绝。**本次没有得到构建单独 Qt 测试目标的授权，该新 C++ 测试尚未编译和运行。**
- 验证额外基线：上一轮已编译的 `native_v3_export_test`、`native_v3_mathml_test`、`xml_v3_test` 共 **3/3 PASS**，日志 `/tmp/athena-v3-prebuilt-native-regression-20261017.log`，但这些旧测试二进制不包含新 C++ 断言；最新主程序隔离 `native-v3-mathml-routing-test.scm` 通过（`/tmp/athena-v3-mathml-latest-20261017.log`），`node_data_art_scheme_test`、`node_model_bridge_scheme_test`、`native_v3_structured_search_runtime_test` 再次 **3/3 PASS**（`/tmp/athena-v3-model-runtime-latest-20261017.log`）。
- 开发计划已更新为真实阶段状态：严格 XML v3 codec/数学 AST 与断开式离线 check/stage 候选已实现，生产 v3 未激活。**离彻底完成还有独立的：** 原生编辑/排版/导入导出/Actor+Qt 全面端到端验证；Hodarium 新格式 epoch、设备协商、受保护旧 revisions DAG 清算；跨目录及外部同步事务的 quiescence/prepare/commit/rollback/restart；离线完整副本、多设备故障注入、生产迁移显式授权。不能以当前局部成功或预编译测试替代这些证据，也不能解禁 manifest 的 `hodarium_protocol_cutover_verified=false` / `eligible_for_atomic_publication=false`。
- 最终 Debug 主程序已通过临时 `.new` 安装、构建产物 `cmp`、原子 `mv`、部署后 `cmp` 更新到 `ATHENA/bin/ATHENA.bin`（2026-10-17 00:44 +08）；`git diff --check` 通过。没有改动生产 Notes、不自动重启用户 ATHENA、没有 Git commit/push。代码中普通 Vault 入口对 `node_model_version >= 2` 的明确拒绝仍存在。

### 8.15 Hodarium 旧格式同步的 XML 版本与 Vault epoch 安全闸（2026-10-17）

- **发现旧协议的两个隐含版本错误：** `hodarium_worker.cpp::sync_worker::saved` 对任意保存字节直接签名/登记为 `format=ath-xml-v2, semantic_version=3`，未验实际 XML envelope 和 document body UUID。另有 `hodarium_application.cpp::validate_document_application_source` 虽固定校验旧 revision 元数据，却直接用 `read_xml_v2` 读取字节；该 API 按设计还接受 XML v1，不能作为签名 revision 精确格式的证明。与 XML v3 候选目录共享旧 Hodarium binding 时，这些路径可能制造误标 revision、错误 apply 或虚假的删除。
- 保留旧 Hodarium 协议合同不变：只有 XML **v2 envelope**、合法的完整 native v2 文档结构、body UUID 等于 revision object ID 才能作为 `ath-xml-v2/semantic_version=3` 非删除 revision。由 `detail::validate_document_application_source` 同时用于远端 apply 和本地 `saved()` **在 `revision_store` 创建/写入前**完成校验，禁止把 v1/v3 字节装进旧 content-addressed DAG。v3 独立 revision 格式和新 protocol epoch 此批**未定义或启用**。
- `detail::require_v2_document_vault` 统一读取/核实真实 Vaultfile 的 `node_model_version==1`，阻断 node model 0 / v3 candidate 2。校验接入本地保存、关闭/打开文档的应用入口、写入前 publication mutex gate，及 Hodarium worker 的应用排程、资源/逻辑 DB 后台捕获、启用复制的 Vault 列表、capture-enabled 通知、恢复 pending intents、删除 tombstone 生成；tombstone 于 source publication 回调内再次确认，防止版本切换发生在 inventory 与签名删除之间。同步错误保持显式拒绝/暂停，没有旧格式自动 fallback，更没有修改既有 revision ID、parents、payload 或签名。
- `tests/ATHENA/Server/buffer_actor_test.cpp` 补上 v2 专属 Vaultfile fixture，增加 `hodariumV2WireBoundaryRejectsMislabeledDocuments`，直接覆盖 v2 正例、v1/v3 envelope 伪装、错误 body identity、错误 format/semantic version、node-model 0/2 拒绝和恢复到 1。按 repo 约束没有构建 Qt 测试目标；对该测试源码用既有 `build_qt6/compile_commands.json` 中的编译参数执行**单文件 `-fsyntax-only` 检查（退出 0）**，它只证明语法编译，不代表 MOC、链接或 Qt 测试通过。日志 `/tmp/athena-v3-hodarium-buffer-actor-syntax-20261017.log`。
- 指定主程序 `cmake --build build_qt6 --target ATHENA.bin -j20` 构建退出码 0，日志 `/tmp/athena-v3-hodarium-epoch-gates-full-build-20261017.log`；定向执行预编译的 `hodarium_revisions_test`、`hodarium_logical_database_test`、`buffer_actor_test` 均通过（3/3，退出 0），日志 `/tmp/athena-v3-hodarium-legacy-ctest-20261017.log`。这些测试二进制早于新 guard 源码，**只验证旧测试基线**。真实两设备 handshake、远端 v3 revision epoch、旧 DAG preservation、错误恢复、跨目录 commit/rollback 及正式 publish **仍未实现和验收**，不得提高任何生产发布标志。
- 增补最后的竞态保护：删除 tombstone 在 source publication 回调内重检 Vaultfile；启动恢复 pending applications 时即拒绝不兼容 Vault；只有版本适配的 Vault 才会触发 capture-enabled，防止改成 v3 候选时旧 worker 继续报备。最终指定 Debug 主程序构建/链接 **退出码 0**（`/tmp/athena-v3-hodarium-guard-final-build-20261017.log`），定向旧 Hodarium/Actor + 新 Scheme 搜索共 **4/4 PASS**（`/tmp/athena-v3-hodarium-epoch-final-regression-20261017.log`）；前三项 Qt 测试仍是先前构建的二进制，不包含本次新断言。通过 `ATHENA/bin/ATHENA.bin.new`、`cmp`、原子 `mv` 及部署后 `cmp` 校验安全更新安装二进制（2026-10-17 08:43 +08），未重启用户进程、未触碰生产 Vault、未提交或推送。`git diff --check` 通过。

### 8.16 Hodarium 共享 group 全量离线审计：DAG、格式与同一快照（2026-10-17）

- 以完整共享 group 为本批阶段验收范围，不再仅修一个局部入口。原 `vault_xml_v3_hodarium.cpp::audit_external_hodarium_v3` 在同一 group 同时绑定两个 Vault 时为每个绑定重扫整组 `revisions-*.sqlite`，导致 revision、incoming、resolution_approvals 和 fingerprint 的基础统计重复。现先收集 group→有序 Vault 集合，再每组只审计一次物理日志；对 bound Vault 的 `ath-xml-v2` 和 applied/pending 分别统计，仍要求 group 级所有 pending incoming/approvals 都可见。严格校验 group 为 43 字符 base64url handle，Vault scope 非空且长度受限，禁止恶意 group 前缀匹配其他日志文件。
- 仅有 `seal_revision` 重算 digest 并不能证明外部 SQLite 的因果 DAG 保持原样：`parents` 关系表仍可能增删一条指向真实 revision 的索引边，`foreign_key_check` 不会检测 descriptor 与索引不一致。现在为每个已封存 revision 读取 `parents` 物理边，要求与 digest 保护的、排序后的 `parents` 完全一致。额外检查 `applied` 记录的 `(vault,object)` 必须等于其引用的 revision 的所有者；防止不同 Vault 或不同对象的合法 digest 被挂在错误已应用指针下。相关跨 Vault 的 SQL 注入/篡改负例已添加至 `xml_v3_test.cpp`。
- 对 bound Vault 的旧 `ath-xml-v2/semantic_version=3` 非 tombstone revision，要求真实 XML **v2 envelope** 并通过严格 v2 reader。一个自洽的旧 SHA-256 digest 并不能证明它不是被误标的 XML v1/v3；新 C++ 负例用真实封存的、但带 v3 XML 的旧格式 revision 确认审计必须失败。
- 审计结束时不仅对刚读完的日志单独 `verify_unchanged`，还必须再次遍历全部已读 `device.sqlite` 与 group journal 文件，核查它们仍是非 symlink 的普通文件、完整 SHA-256 一致且不存在新增非空 WAL。这样多个组顺序扫描时，较早读完的日志若在后续扫描时变化，最终审计不会把彼此不在同一状态的快照混成一个可信指纹。这仍不是跨设备原子冻结或远端 handshake，只是使本地只读审计 fail-closed。
- 新增可重复的 `tests/scheme/native-v3-hodarium-shared-audit-test.py` 并正式注册为 `native_v3_hodarium_shared_audit_test`：在临时独立目录创建 **两个 Vault binding、一份共享 revision.sqlite 和一份真实 XML v2 source**；走正式 `ATHENA.bin --upgrade-vault-xml-v3 --check --hodarium-state`，验证统计 `2 bindings / 1 journal`、没有改源字节/mtime、没有产生 SQLite WAL/SHM；再禁用一个绑定，证明旧因果绑定仍需保留；随后在另一个新目录实际运行 `--stage`，检查生成候选 `node_model_version=2`、manifest 两个发布安全闸仍为 false、原始 Vault/Hodarium 输入完全不变；伪造不符合 43 位 group 语法的额外 binding 后，验证 fail-closed。**新测试通过 CTest 1/1、退出码 0**（`/tmp/athena-v3-hodarium-shared-ctest-20261017.log`）。之前的独立 CLI 实验也已验证同样的正例（`/tmp/athena-v3-shared-hodarium-audit-cli-20261017.log`）。
- `tests/Data/Convert/Xml/xml_v3_test.cpp::externalHodariumSharedGroupJournalsCountOnce` 新增有两个不同真实 sealed revision 的共享 group fixture，覆盖物理唯一 revision 数、应用数、恶意 `parents` 索引边、错误 applied 指针和**伪装成旧 v2 格式的已封存 v3 payload**；这四份累计 Qt/C++ 搜索、codec、grid、Actor 测试源码已按现有 `compile_commands.json` 逐个执行 `-fsyntax-only`，全部退出码 0（`/tmp/athena-v3-qt-new-regression-syntax-20261017.log`）。**受仓库单独测试 target 构建限制，新增 Qt 用例仍未重新 MOC/链接/运行**，不得以该语法检查或旧预编译 suite 代替实际 Qt 运行验收。
- 正式 `cmake --build build_qt6 --target ATHENA.bin -j20` 已退出 0（初次因 CMake/日期依赖变化触发完整依赖重编译，`/tmp/athena-v3-hodarium-audit-final-build-20261017.log`；后续源码增量 `postfix` 复建 `/tmp/athena-v3-hodarium-audit-postfix-build-20261017.log` 亦退出 0）。最后定向执行新 CLI、Scheme 搜索/节点模型及**旧编译的** Hodarium/Actor/MathML/codec/Vault Search 共 **12/12 PASS**，日志 `/tmp/athena-v3-hodarium-milestone-regression-20261017.log`；另分别运行最新主程序真实原生 v3 export 与 MathML 导入路由 Scheme 测试，均退出 0，日志 `/tmp/athena-v3-hodarium-milestone-export-scheme-20261017.log`、`/tmp/athena-v3-hodarium-milestone-mathml-scheme-20261017.log`。明确区分被新构建的 ATHENA.bin 实际使用的 CLI/Scheme 测试与旧 Qt 测试 binary 的旧行为验证。
- **仍未完成**：签名 revision 新 epoch、远端两设备握手、旧 DAG/受保护证据与新链的双向可恢复关联、跨 Vault/外部 DB prepare/commit/crash rollback、生产或全量真实副本的发行级验收。`hodarium_protocol_cutover_verified=false` 和 `eligible_for_atomic_publication=false` 继续保持，未修改生产 Notes，也未自动 commit/push。

### 8.17 真实离线 Notes 副本完整 stage、独立历史/索引恢复与 Hodarium 协议边界（2026-10-17）

- 对用户**已有且授权的非生产离线拷贝** `/home/felix/data/Notes (1)` 通过当前 `ATHENA.bin` 的 `--upgrade-vault-xml-v3 --check` 运行完整只读 source、历史、Artifact 索引预检：**832 个现有文档、125550 个定界符、231146 个脚标、16637 个大算符、28068 个 MathBlock、8028 个 MathGrid、261 个历史文档版本、10592 个索引树片段、原有 2 条语义警告，退出 0**，工具最终报告 `Original Vault unchanged`，日志 `/tmp/athena-v3-authorized-copy-readonly-check-20261017.log`。没有对真实生产 Notes 路径运行任何命令。
- 按可用磁盘空间和现有隔离环境，在唯一新目录 `/home/felix/tmp/athena-xml-v3-offline-20261017-1039` 使用同一离线副本运行 `--stage`，退出 0（`/tmp/athena-v3-authorized-copy-private-stage-20261017.log`），生成完整断开式 candidate。manifest 明确 `source_node_model_version=1`、`candidate_node_model_version=2`、`documents=832`、`history_versions=261`、`protected_history_versions=1`、`index_trees=10592`、`invalidated_cache_hashes=3328`。因为**未提供任何外部 Hodarium state**，receipt 的 `hodarium_external_state_audited=false`、`hodarium_existing_bindings=0` 不证明现实中没有绑定；`hodarium_protocol_cutover_verified=false`、`eligible_for_atomic_publication=false` 没有变化。候选仅用于离线阅读/校验，**不能交给普通 ATHENA 编辑和同步**。
- 新增独立的 `tests/scheme/native-v3-staged-history-verify.py`，使用 `sqlite3` immutable+read-only 与独立 Python Fossil delta/checksum 实现恢复**全部 262 行历史**，不是简单调用 C++ 迁移器重新确认其自身输出。其中 237 条 delta、25 条完整快照；261 个活动文档历史的真实来源为 XML v1 **6 个**、XML v2 **255 个**；另有一个非 `.ath` 历史。每行完整 payload 都通过 SHA-256、长度、路径、trigger、ID 校验；保护引用 **2 条**和 preserved original v2 evidence **1 个**完整匹配。实际执行退出 0（`/tmp/athena-v3-full-history-independent-20261017.log`）。不把合法历史 v1 来源误报为当前 Vault v2 不合格。
- 新增独立的 `tests/scheme/native-v3-staged-index-verify.py`：用 Python XML 标准库逐个解析原始和候选中 **832 个活动 `.ath` 文档**以及 **6400 个 Artifact 名称树、4192 个 BoldText 结构化树**；逐行比较除指定 tree column 外的所有索引列/rowid，核实 stage 产生 canonical v3 XML；按 manifest 独立核对失效的 **3328 个哈希**与清空 **4165 行范围缓存**。还对 staged history/artifacts/enunciations/bold-text/namespaces DB 以 SQLite 只读执行 `quick_check` 和 `foreign_key_check`，已检查 DB 均通过。程序退出 0，日志 `/tmp/athena-v3-full-index-independent-20261017.log`。
- 额外用真实隔离 ATHENA Scheme→C++ `vault-load-with-ns` 试图打开**该 detached candidate**，结果明确拒绝 `XML v3 Vaults cannot yet be opened`（`/tmp/athena-v3-disconnected-runtime-gate-20261017.log`），而非自动编辑/再保存。测试成为私有 Hodarium stage CLI 内的常规步骤（`tests/scheme/native-v3-disconnected-candidate-runtime-test.scm`），stage 与普通 runtime 必须区别对待。
- Hodarium v1 `revision_sender` 和 `revision_receiver` 的收发 offer 前加入 legacy document epoch fail-closed：凡 `ath-xml-*` 文档必须明确为 `ath-xml-v2`、`semantic_version=3`，防止未协商的 `ath-xml-v3` 或错误语义版本混入旧 wire。`tests/hodarium/revisions_test.cpp` 中包含将来 v3 合法封存 revision 的 sender/receiver 拒绝断言，已通过单独编译参数的 `-fsyntax-only`；因为仓库规则未授权单独构建/执行新 Qt/C++ 测试 target，这一测试仅为待运行断言，不声称已通过新 wire 运行时。资源与逻辑对象的独立 format 合同不变。
- 新 `native_v3_hodarium_shared_audit_test` 的隔离数据由 Python 按**真实 Hodarium v1 CBOR descriptor + SHA-256 domain**计算 3 条合法**内容寻址** revision（2 个不同真实 Vault 根、同一个 group journal、一条因果 parent、两个 applied 指针），而非空 SQLite 表。再分别制造缺失/额外 parent 索引、错绑 `revisions.vault/object`、跨 Vault applied、非法 eligibility、伪造 approval/intent、损坏缓存 fingerprint、有效 digest 但错误 XML envelope/body UUID、未协商 v3 revision、非空 WAL、symlink 绑定、非法 group 或重复 Vault root、membership sidecar 内容修改及 WAL 等负例；保证每次失败后恢复再读可成功。合法恢复代际日志 `revisions-GROUP-GENERATION.sqlite` 与旧代际保留一起被完整审计，同组实际物理库数不会漏记，伪造的短 generation filename 则直接拒绝。当前 `audit_external_hodarium_v3` 还把组内 membership/conflicts/decisions 各代际 SQLite 加入最终 SHA-256 fingerprint，不等于已验证远端成员签名或协议 cutover。最后单项隔离 CTest 通过（`/tmp/athena-v3-hodarium-generation-journals-ctest-20261017.log`）。
- 跨设备新协议 epoch、冻结提案、保留旧内容寻址 DAG、commit/rollback/restart 的**非发布设计草案**见 `notes/athena-xml-v3-hodarium-epoch-protocol-draft.md`。这仍是待冻结的设计与实施任务，既不代表远端 handshake 已实现，也不授权迁移生产 Notes、自动启用格式 v3、修改真实密钥/同步配置或删除旧 revision。

### 8.18 恢复代际文件发现、跨对象因果及完整 eligibility 审计（2026-10-17 午后）

- `audit_external_hodarium_v3` 现在要求与所绑定 group 相关的 `revisions-GROUP-GENERATION.sqlite` 文件名有**正好 43 个 base64url 代际字符**，保留旧 `revisions-GROUP.sqlite` 作为合法 legacy path；对属于同 group 但伪造代际长度/字符的 SQLite 日志直接 fail closed，而非跳过或混入其他 group。同样对 `membership-GROUP-GENERATION.sqlite`、`conflicts-GROUP-GENERATION.sqlite` 与 `decisions-GROUP-GENERATION.sqlite` 严格检查，防止旧受保护成员/决议资料从 state fingerprint 中静默消失。新增私有 CLI fixture 将合法旧代际日志复制为独立只读历史，确认组库个数与 revision 数全部累加；无效 revision/sidecar 文件名必须拒绝。**不会删除任何旧代际文件，也没有创建新的 wire epoch。**
- SQLite `foreign_key_check`、seal hash、`parents` 索引一致均不能证明父 revision 属于**同一个 `(vault, object)`**。增加物理 `parents` 表相连的两个 revisions scope 一致检查，隔离测试刻意创建自洽 SHA-256/descriptor/index、但指向另一个 Vault 的合法封存 revision，必须拒绝（`/tmp/athena-v3-cross-scope-parents-ctest-20261017.log`）。
- 另外保护 `applied` 与 `apply_intents` 的 `eligible` 关联：两者来自运行时通过 eligibility/冲突裁决后允许应用的 revision；不能因为一个完整且 content-addressed 的 head 失去了 eligible 记录便把它当成可恢复的因果证据。增加对已应用 head 和尚未应用但持有 pending intent 的独立负例；两者均须 fail closed，隔离 CTest **1/1 PASS**（`/tmp/athena-v3-applied-intent-eligibility-ctest-20261017.log`）。
- 强化“相同大小+原 mtime 被手动恢复”的数据库修改故障注入：测试真实 `device.sqlite` 对应的 membership sidecar 即使在 mtime/文件大小看起来未变时，依然因为内容 SHA-256 变化使外部 state fingerprint 改变，断开的 v3 stage 不会接受旧结果（`/tmp/athena-v3-hodarium-restored-mtime-ctest-20261017.log`）。
- 独立索引核验器对全部 **10592** 个迁移后 XML v3 Artifact/BoldText 片段重读后，增加只对可比较的**非类型化数学结构**比对旧/新原子文本的强断言：**10475 个原子文本投影完全一致**；剩余 **117 个**使用 typed delimiter/scripts/MathSymbol 等结构，不能简单依 XML 文本节点数量判断数学语义，交由原生 AST 等价与 v3 codec 回归覆盖，绝不声称这 117 项有额外的独立语义等价证明。独立索引工具退出码 0（`/tmp/athena-v3-index-atomic-text-verified-20261017.log`）。另外对私有历史数据库的**临时复制品**破坏 Fossil delta，独立校验器明确拒绝（无触碰真实或来源副本数据库）。
- **身份认证与完整性边界澄清**：`seal_revision` 的 v1 SHA-256 是 digest/内容寻址 ID，并**不是**每条 revision 的单独数字签名；`origin_member` 字段在 digest 中，也不证明消息由该私钥拥有者发出。真正 peer 会话认证、group membership、决议证书及未来 epoch 签署还需额外验证，不以审计中的“sealed hash 一致”宣称远端签名已经认证。设计草案也明确了这一点。
- 指定 `cmake --build build_qt6 --target ATHENA.bin -j20` 已成功（`/tmp/athena-v3-applied-intent-eligibility-build-20261017.log`），加入后再次通过了正式隔离 `native_v3_hodarium_shared_audit_test` 1/1（`/tmp/athena-v3-applied-intent-eligibility-ctest-20261017.log`）。现有 Qt 新断言只完成语法检查，未按仓库限制擅自重建测试 target 或执行全套 suite。真实生产 v3 format epoch、跨设备 handshake 和原子发布依然未实现、未授权。
- 为避免独立验证工具本身退化，新增无需 ATHENA 启动的正式 `native_v3_staged_verifier_selftest`（`tests/scheme/native-v3-staged-verifier-selftest.py`）：临时私有 v2/v3 Vault 各一份、两条小型历史（含一个 Fossil delta 和原保护证据）、Artifact/BoldText 两个 XML 树；先确认两个独立核验器均接受有效来源，再注入非法 Fossil delta、恶意 Base64 编码、非数学文本内容变化，必须分别明确拒绝。注册至 `tests/CMakeLists.txt`，与真正调用最新 ATHENA.bin 的 `native_v3_hodarium_shared_audit_test` 一起**2/2 PASS**（`/tmp/athena-v3-staged-and-hodarium-ctest-20261017.log`），没有用生产数据作夹具。因为修改 CMake 注册，按约束仅运行 `cmake --build build_qt6 --target ATHENA.bin -j20`，完成可执行程序构建，退出 0（`/tmp/athena-v3-verifier-selftest-build-20261017.log`），未执行默认 ALL。
- 随后又用独立 Python 核验器重新核对授权的**离线复制品**及已生成的不可发布候选，262 条历史及 832 文档/10592 树均再度核对通过（`/tmp/athena-v3-latest-detached-history-20261017.log`、`/tmp/athena-v3-latest-detached-indexes-20261017.log`）。新旧相关测试共**定向 13/13 PASS**（`/tmp/athena-v3-20261017-final-13-targeted-ctest.log`）；特别注意 Qt/C++ 测试 target 仍沿用**旧编译二进制**，只有新 CLI/Scheme/Python 测试真正用了最新 app/build 输入，新增 QTest 断言并未获单独 target 编译/执行授权。没有假装本地只读审计已完成远端签名验证或 Hodarium 跨设备原子 cutover。
- 最终产物按既定部署手续经过 `ATHENA/bin/ATHENA.bin.new` 暂存、部署前 `cmp`、原子 `mv`、部署后 `cmp` 验证一致，2026-10-17 12:57 (+08:00) 已安装到 `ATHENA/bin/ATHENA.bin`；`git diff --check` 通过。没有重启运行中实例、没有对生产 Notes 使用任何迁移/写入命令，也没有 Git commit/push。最终普通 Vault 入口仍拒绝 `node_model_version >= 2`，manifest 的 `hodarium_protocol_cutover_verified=false`、`eligible_for_atomic_publication=false` 保持关闭。新格式 **仍未进入生产可写状态**。

### 8.19 Hodarium 旧 epoch 三类 revision 的真实格式合同复用（2026-10-17 晚）

- 从实际 `ATHENA.bin --upgrade-vault-xml-v3 --check --hodarium-state` 隔离 fixture 连续找到并验证三个旧协议离线审计绕过：**(1)** 内容寻址 ID 自洽的 `ath-resource-style-v1` revision 能伪装成 `.ath` 文档、内部隐藏文件或任意字符串 UUID；**(2)** `athena-namespace-v1` 内容哈希正确，却可声明 `../Namespace.json` 等并非 runtime 从对象键生成的路径；**(3)** 派生 Artifact/RAG 结果可伪造 payload SHA-256 后缀、删除语义，以及 Artifact 拒绝列表可伪造固定对象身份。新的私有 CLI 负例先证明审计原来对 (1)/(2)/(3) 中的身份约束放行，后修改并用同一 CTest GREEN；日志 `/tmp/athena-v3-hodarium-resource-path-red-20261017.log`、`/tmp/athena-v3-logical-revision-red-20261017.log`、`/tmp/athena-v3-derived-logical-red-20261017.log` 与 `/tmp/athena-v3-derived-logical-ctest-20261017.log`。
- 采用**原有运行时源头规则**，而不是另外复制一套“看起来相似”的路径算法：把资源应用内部的 `validate_revision` 通过纯元数据入口 `validate_resource_revision_metadata` 暴露给 `vault_xml_v3_hodarium.cpp`，校验 resource type/实际路径扩展名/隐藏目录/对象 canonical UUID 等；把逻辑应用内部的 `history_path(object)`、derived payload digest+不可变性、Artifact rejection 身份整理为 `validate_logical_revision_metadata`，由普通旧协议 `apply_logical_revision` 和只读 v3 外部审计**共同调用**。这样 `--check` 在旧 runtime 本来也不能安全应用的 revision 上会 fail closed；未来无需两套规则各自漂移。
- 正例同样覆盖：真实 `.ts` style resource **及其 pending apply intent**，合法固定路径的 namespace revision、有效 `athena-artifact-range-v1` 派生结果都必须通过。对未提供 Materials 声明的自定义附件扩展名，`validate_resource_revision_metadata` **明确 fail closed**，不假定未知扩展名被授权；这仍不是自定义附件跨设备恢复的最终合同，后续必须把经过权限验证的 Materials 声明传入审计，才能进入全量产品验收。禁止仅凭本地内容寻址哈希替代资源权限/语义证明。
- 旧文档端还有独立的路径表达 bug：通用历史路径校验会归一化 `folder/../Alias.ath` 或 `folder//Alias.ath`，旧 Hodarium 应用却可能按未归一化的 revision 路径进行源 identity/replay。现在通过共用的 `validate_document_application_path` 要求确切规范路径、无反斜线、`.ath` 后缀并禁止内部 Vault 路径；用实际 CTest 的 `folder/../`、`folder/./`、双斜线及 Windows 风格反斜线负例先 RED 后 GREEN（`/tmp/athena-v3-hodarium-noncanonical-path-red-20261017.log`、`/tmp/athena-v3-document-canonical-path-ctest-20261017.log`），不改变普通文档读取器的历史格式兼容政策。
- 所有本轮测试只在随机临时根目录中构造 revision v1 CBOR+SHA-256、两个不同 Vault 同组独立原件和独立 SQLite，未访问生产 Notes；直接正反例经**最新主程序**运行的定向隔离 CTest **1/1 PASS**（`/tmp/athena-v3-derived-logical-ctest-20261017.log`）。指定 `cmake --build build_qt6 --target ATHENA.bin -j20` 编译链接退出 0（`/tmp/athena-v3-document-canonical-path-build-20261017.log`、`/tmp/athena-v3-derived-logical-build-20261017.log`）。不声称已重新编译 Qt/C++ 测试二进制，更不能由此启用新 Hodarium 协议 epoch 或生产格式 v3。

### 8.20 旧 Hodarium revision 的文档／资源／逻辑格式源头校验与版本误读防护（2026-10-17 夜）

- 本轮审查发现离线 journal 审计与真正 Hodarium 应用层之间存在**跨格式身份和路径约束的可复现偏差**，采用顺序：在私有临时两 Vault／共享 group v1 SHA-256+CBOR revision 数据库造出 hash 正确但语义错误的记录，先运行真实 ATHENA.bin `--upgrade-vault-xml-v3 --check` 证明原来会误判，再修源头并重跑同一 CTest。所有测试只读工具对临时造假数据库操作；没有生产 Notes 数据参与。
- **资源 revision**：`ath-resource-style-v1` 可以带 `.ath` 或不允许的隐藏目录路径，或任意非 canonical UUID，旧离线审计仍误报完整有效。`hodarium_resources.cpp` 的权威 `validate_revision` 改由纯元数据公开入口 `validate_resource_revision_metadata` 提供给正常旧协议和 `vault_xml_v3_hodarium.cpp` 共用，检查 format↔path kind、相对路径、隐藏/内部文件及 QUuid 身份；可接受正常 `.ts` 样式及其 pending intent。红灯日志 `/tmp/athena-v3-hodarium-resource-path-red-20261017.log`，最新版构建和测试后 GREEN `/tmp/athena-v3-resource-metadata-contract-final-ctest-20261017.log`。**自定义 Materials 已声明附件扩展名**没有从隔离 group 审计里自动获取相关授权，缺少显式声明时 fail closed，不能由此声称已全面验收自定义附件跨设备回放。
- **逻辑 revision**：`athena-namespace-v1` 可以被伪装为 `../Namespace.json`，派生 RAG／Artifact 记录可丢失 SHA-256 身份后缀或伪装成可删除，Artifact rejection 可冒用错误的对象键。现在 `hodarium_logical_sync.cpp` 将旧运行时的对象键决定的历史路径、derived 无 parent/不可删除、payload digest identity 和拒绝列表固定身份封装在 `validate_logical_revision_metadata`，由普通 `apply_logical_revision` 及严格只读审计共同调用。通过 namespace 正例、derived Artifact 正例及多个身份/路径负例；红灯 `/tmp/athena-v3-logical-revision-red-20261017.log`、`/tmp/athena-v3-derived-logical-red-20261017.log`，绿灯 `/tmp/athena-v3-derived-logical-ctest-20261017.log`。
- **Document 路径**：`history::valid_relative_document_path` 会将 `folder/../Alias.ath` 先归一化，使旧 apply 路径和真实文件身份可能不一致；现在共享 `validate_document_application_path` 要求 lexical canonical 路径，不含反斜线、`/./`、`/../`、双斜线或隐藏 Vault 内部组件。真实隔离负例先失败、修后通过（`/tmp/athena-v3-hodarium-noncanonical-path-red-20261017.log`、`/tmp/athena-v3-document-canonical-path-ctest-20261017.log`）；保持 XML v1/v2 历史读取只能在迁移入口的政策不变。
- **验证集合**：本轮最终 `cmake --build build_qt6 --target ATHENA.bin -j20` 构建/链接成功（`/tmp/athena-v3-document-canonical-path-build-20261017.log`、`/tmp/athena-v3-derived-logical-build-20261017.log`）。对新代码与现存 CTest 作具体过滤后 **13/13 PASS**（`/tmp/athena-v3-hodarium-three-contracts-13-ctest-20261017.log`）；最新版 ATHENA.bin 在隔离 profile 下直接执行原生 v3 export/MathML 两个 Scheme 测试均退出 0（`/tmp/athena-v3-20261017-new-hodarium-export-runtime.log`、`/tmp/athena-v3-20261017-new-hodarium-mathml-runtime.log`）。未构建 Qt 测试 targets，不将已编译旧 Qt binary 的成功误当作新增 QTest 通过。
- 进一步重新运行最新 ATHENA.bin 的 `--upgrade-vault-xml-v3 --check '/home/felix/data/Notes (1)'`，仅限此前已授权的**离线非生产副本**，独立临时 HOME/profile，退出 0：832 当前文档、125550 delimiters、231146 scripts、16637 operators、28068 MathBlock、8028 grids，261 历史文档版本、10592 索引树、2 条原有警告，完成时明确 `Original Vault unchanged`（`/tmp/athena-v3-evening-authorized-copy-full-check-20261017.log`）。未访问生产 Notes，未执行 v3 生产 cutover；Hodarium v3 设备协商/bridge/两阶段提交与崩溃回滚仍未完成。

### 8.21 外部 Hodarium state 目录清单的并发一致性与临时日志出现／消失（2026-10-17 晚）

- 只对已发现的数据库逐个 `SHA-256` 校验仍有一个不可忽略的时序窗口：对 group A 扫描期间，同步写者可能新建 `revisions-GROUP-GENERATION.sqlite` 或删除保留的旧代际日志；旧逻辑可对剩余已发现文件重哈希全部成功，但得到的因果集合不是真实的同一快照。`vault_xml_v3_hodarium.cpp` 现在在打开 `device.sqlite` **之前**拍摄外部 state 目录的 `(文件名, symlink_status.type)` 完整有序清单，最后重新枚举并要求清单一致；新增或消失的数据库、WAL/SHM、sidecar/其他条目都使离线审计 fail closed。原有每个文件 SHA-256/WAL 检查仍保留，目录清单比对**不替代**停写、远端握手或真正跨文件系统事务。
- 为保证不是纸面测试，`tests/scheme/native-v3-hodarium-shared-audit-test.py` 在 Linux 私有临时 SQLite 夹具上使用标准 C library inotify `IN_OPEN(device.sqlite)` 确定**入口完成目录枚举之后的时刻**，此时使用 `os.link` 原子加入一个内容完整、文件名合法的另一代际 revision 库，然后断言真实 ATHENA.bin `--check --hodarium-state` 以 `External Hodarium database directory changed during immutable audit` 明确拒绝；同样在打开 `device.sqlite` 后删除初始已存在的合法旧代际日志，也必须拒绝。两种注入均不修改原 revision 字节、IDs、SQLite rows 或生产文件，清理临时 hardlink 后可重跑正例；隔离 CTest **1/1 PASS**，日志 `/tmp/athena-v3-hodarium-directory-add-remove-ctest-20261017.log`。在非 Linux 平台该 inotify 专项自然跳过，不用伪造一个无效通过信号。
- 指定 `cmake --build build_qt6 --target ATHENA.bin -j20` 已成功（`/tmp/athena-v3-hodarium-directory-snapshot-build-20261017.log`），此前的三类资源/文档/逻辑格式负例与运行时正例继续通过。本检查只能发现开始/结束文件清单不一致，仍不能在任意外部写者恶意恢复同一文件内容和目录集合时证明分布式静止；产品发布需要另行定义并实现各设备真实 freeze/prepare/commit/rollback/restore，候选仍不可发布。

### 8.22 原生 v3 数学键盘/工具栏：长箭头、大算符、字母变体与固定槽位删除（2026-10-17 夜）

- 按用户最新要求，停止把工作重点放在受信任设备 Hodarium 的恶意 intent 构造，撤回上一轮新加且未完成的恶意 `source_fingerprint`/`protected_history_version` 探针。接下来以正常用户键盘、工具栏、光标、数学编辑和导出功能为主，不再用大批对抗性 Hodarium fixture 代替编辑体验。此前已通过、支持历史兼容/离线候选的代码和测试保持不动。
- `edit_math.cpp::back_in_long_arrow` 原有旧 `LONG_ARROW` 删除逻辑会在上标为空时删除子树并改变元数；而 XML v3 原生 `MATH_LONG_ARROW` 必须永远恰有 Unicode/MathSymbol、下注释、上注释三个物理子节点。新增原生分支按当前编辑槽位导航，不删除空上/下槽，也不改变节点 UUID 或 typed metadata。`tests/Edit/Modify/native_v3_math_editor_test.cpp::nativeLongArrowDeletionKeepsThreeSlots` 涵盖前/后删除、固定 arity、identity 和 v3 codec roundtrip。
- **两条实际入口一致性**：`native_math_keyboard.cpp` 和 `native_editor_actions.cpp` 的 `insert-long-arrow` 原先无条件构建旧 `LONG_ARROW` 2/3 子节点。现在共用 `athena::auto_close::insert_long_arrow(editor_rep*,tree,bool)`：v3 创建固定三槽位 `MATH_LONG_ARROW`、将光标引向指定的 upper/lower annotation；v2 继续保持旧构造和光标位置。支持 JSON 键盘配置中的真实 `NAMED_SYMBOL` 箭头；先经原生 registry 验证，再将 `texmacs:longhookrightarrow` 等旧 renderer spelling 映射到 `MATH_SYMBOL(long-hookrightarrow)` 等规范语义 ID，不匹配的命名符号明确提示，而不是引入旧 v2 AST。新增编辑器源码回归 `longArrowKeyboardAndToolbarActionsUseNativeV3`。
- 两条入口的 `insert-big` 也原先无条件创建旧 `BIG`，即便键盘配置使用 `(with math-big-upright true math-big-limits true "∫")`。新增共享 `insert_big_operator` 把真实 JSON `WITH` flags 转换为 v3 `BIG_OPERATOR` 的 upright 字形位与 `MATH_SCRIPTS` 的 typed `math:scripts-policy=d`，处理工具栏普通 Unicode 大算符、选择内容后的大算符插入；v2 仍创建旧 `BIG`，无法无损编码的单独命名算符 fail closed。新增编辑器源码回归 `bigOperatorKeyboardAndToolbarActionsKeepV3Policies`。
- 工具栏 `insert-alphabet` 现共用 `insert_alphabet`：v3 对 `math-alpha-bold/bbb/cal` 分别插入 `MATH_VARIANT(bf/bb/cal,字母)`，保留数学变体身份；v2 不改。原生 v3 grammar/type setter/LaTeX exporter 尚未支持 `math-alpha-frak` 与 `math-alpha-bold-up` 的**独立字体语义**，v3 目前明确提示未支持，不擅自将其近似替换成 cal/bf；新增编辑器源码回归 `mathAlphabetToolbarUsesNativeV3Variants`。
- **验证证据严格区分**：两条 GUI 命令入口涉及 `editor` 包装类型，首次 main 构建发现与 `editor_rep*` 参数不一致（失败日志 `/tmp/athena-v3-long-arrow-actions-build-20261017.log`），已改用 `.operator->()` 后连续正常构建，最终 `cmake --build build_qt6 --target ATHENA.bin -j20` **退出码 0**（`/tmp/athena-v3-long-arrow-named-id-build-20261017.log`）。新增三组编辑器 QTest 用例源码在原 `build_qt6/compile_commands.json` 的实际编译选项下执行 `-fsyntax-only` **退出码 0**（`/tmp/athena-v3-long-arrow-named-id-test-syntax-20261017.log`），**尚未 MOC/重新链接/运行新增用例**，不等于新 GUI 动作交互已验收。当前仅对先前编译的旧编辑/MathML/导出/搜索 Qt 5 项定向 CTest **5/5 PASS**（`/tmp/athena-v3-20261017-final-math-ui-five-ctest.log`），它们没有本轮新增代码的完整编辑器断言。明确需要后续独立构建目标授权与实际 UI 验收。
- 最新主程序实际 Guile→C++ LaTeX 转换回归（`tests/scheme/native-v3-export-runtime-test.scm`）已验证原生三槽长箭头 `\xrightarrow[i]{n}`、空标注 `\xrightarrow{}`、大算符 `\sum` 以及数学字母 `\mathbf{A}`、`\mathbb{A}`、`\mathcal{A}`，隔离进程 **退出 0**（`/tmp/athena-v3-variant-keyboard-export-20261017.log`）。
- **另发现但不应凭空“修复”**：现有 `parse-xml` 把原始 `<mtext>&Sum;</mtext>` 和 `<mtext>&amp;Sum;</mtext>` 都转成完全相同的 SXML 字符串 `"&Sum;"`；随后当前 v3 importer 又按命名实体展开成 `∑`。在已丢失原始转义方式的 parsed SXML 上，无法判断用户原本要“求和符号”还是字面 `&Sum;`，只修改后级 `native_mathml::token()` 将破坏另一种合法输入。隔离实际入口证据 `/tmp/athena-v3-mathml-entities-inspection.log`。后续应在 MathML 原始 XML→SXML **词法/实体保留边界**设计无损导入方案并覆盖两个合法来源；当前仍是未完成的用户可见兼容性问题，不能声称全部 MathML 保真。
- 本轮没有 commit/push，没有改变生产 Notes、v3 Vault 打开限制或两个候选发布安全闸；仅以 v2/v3 兼容行为回归与原生数学编辑体验为实施重点。
- 本批最终主程序 `cmake --build build_qt6 --target ATHENA.bin -j20` 以退出码 0 链接完成（`/tmp/athena-v3-long-arrow-named-id-build-20261017.log`），编辑器新增 Qt 回归源码经 `-fsyntax-only` 退出码 0（`/tmp/athena-v3-long-arrow-named-id-test-syntax-20261017.log`），定向既有 Qt/运行时/索引验证 CTest **6/6 PASS**（`/tmp/athena-v3-math-editor-milestone-6-ctest-20261017.log`；其中 Qt test binary 仍旧，不能代表新增槽位已执行）。直接用最新主程序隔离执行 `native-v3-export-runtime-test.scm` 和 `native-v3-mathml-routing-test.scm` **两项退出 0**，日志 `/tmp/athena-v3-20261017-final-math-export-scheme.log` 和 `/tmp/athena-v3-20261017-final-mathml-route-scheme.log`。Debug 主程序已通过 `ATHENA/bin/ATHENA.bin.new` 临时安装、构建产物 `cmp`、原子 `mv`、部署后 `cmp` 于 **2026-10-17 22:34 +08:00** 更新到 `ATHENA/bin/ATHENA.bin`。`git diff --check` PASS；全部修改保留在 dirty worktree（40 个修改/新增文件），没有提交、推送或重启现有 ATHENA。生产 Vault v3 读写拒绝、两个候选发布标志 false 均未改变。

### 8.23 原生数学字母完整五入口：数学粗体、直立粗体、Fraktur（2026-10-17 继续）

- 先对照权威 `ATHENA/packages/standard/std-math.ats` 中历史 `math-alpha-*` 数学字体宏，确认前一轮工具栏 `math-alpha-bold → MATH_VARIANT(bf)` **不等价**：`bf` 代表历史文本模式粗体（`math-bf`），而数学粗体必须设置数学字体的 `MATH_FONT_SERIES=bold` 且保留 math shape；`math-alpha-bold-up` 还要求 `MATH_FONT_SHAPE=right`，`math-alpha-frak` 要求 `MATH_FONT_SHAPE=frak`。修正为三个**不同**的原生类型 `mbf`、`mbfu`、`frak`；原来已有 `bf`、`bb`、`cal` 的含义不变。`math_tree_v3.cpp::valid_native_math` 严格接受这三个新规范种类，XML v3 codec 仍保存 typed kind 与 body 节点 UUID，不复制旧 v2 WITH/style wrapper。
- 原生排版入口 `Typeset/Concat/concat_math.cpp::typeset_v3_variant` 根据真实 std-math 定义使用 `env->local_begin/end` 设置 `MATH_FONT_SERIES`/`MATH_FONT_SHAPE`，且 `mbf/mbfu/frak` 保持**数学模式**，不能套 `mode=text`。工具栏 `auto_close.cpp::insert_alphabet` 已由“错误的 `bf` 或拒绝”改为分别写入 `mbf/mbfu/frak`，键盘/工具栏原有 v2 source 不改。先前用户未允许的操作（全 Vault format publish / 生产同步切换）仍受安全闸保护。
- `math_v2_to_v3.cpp` 的严格 v2→v3 walker 识别五类历史 `math-alpha-bold`、`math-alpha-bold-up`、`math-alpha-frak`、`math-alpha-cal`、`math-alpha-bbb`，其中 `math-bf` 仍显式迁移为文字粗体 `bf`，保留旧源节点 ID。原生 MathML importer 把 `mathvariant=bold → mbfu`（Unicode 数学粗体直立）、`bold-italic → mbf`、`fraktur → frak`，不修改原 v2 HTML/MathML 自动路由；先前错误采用的非标准 `bold-normal` 选项没有保留。新 Qt 源码 `native_v3_mathml_test.cpp`、`xml_v3_test.cpp`、`native_v3_math_editor_test.cpp` 加入归属/roundtrip/ID 回归；三份均通过从现有 `compile_commands.json` 重放编译选项的 **`-fsyntax-only` 检查**（`/tmp/athena-v3-math-variant-qt-syntax-20261017.log`），但**新 Qt 测试尚未重新链接、运行**。
- LaTeX 真实导出分别使用 `mbf → \boldsymbol{A}`、`mbfu → \mathbf{A}`、`frak → \mathfrak{A}`。在 native v3 exporter 中为纯 LaTeX 加入 `amsmath`（数学粗体）及 `amsfonts`（Fraktur/数学黑板粗体）的准确依赖，MathJax 路由保留自身能力。通过真实 Scheme `latex-export-latex-extra-packages` 运行时直接检查，`mbf` 的结果含 `amsmath`，`frak` 的结果含 `amsfonts`，不只是比对字符串的 `\\mathfrak` 外观；运行证据 `/tmp/athena-v3-math-font-packages-probe.log`。持久化 Scheme `native-v3-export-runtime-test.scm` 新增导出命令及包依赖回归。
- 新增 `tests/scheme/native-v3-math-alphabet-stage-test.py`，使用**完全临时**的 XML v2 Vault（单文档、6 个实际历史变体、每个都有 UUID），调用最新 ATHENA 的真实 `--upgrade-vault-xml-v3 --check` 与 `--stage`，然后独立用 XML parser 检查紧凑 v3 的 typed variant、原文 `id` 到 v3 `i` 的 ID 完全保留、候选发布闸 false、源文件内容哈希与 mtime 不变。运行退出 0，日志 `/tmp/athena-v3-font-private-stage-20261017.log`。该测试与此前单独跑的 `native-v3-export-runtime-test.scm`、`native-v3-mathml-routing-test.scm` 都已登记为正式 CTest，可重复运行，无真实 Hodarium / 用户数据参与。
- 因 CMake 注册新测试触发较多源文件重新编译，仍依仓库约束**只构建主程序目标** `cmake --build build_qt6 --target ATHENA.bin -j20`，退出 0（`/tmp/athena-v3-font-all-entry-build-20261017.log`）；新 CTest 连同原生旧 Qt keyboard/editor/codec/MathML 等定向 **11/11 PASS**（`/tmp/athena-v3-math-font-final-targeted-ctest-20261017.log`），其中 Qt 程序是旧测试 binary、不能替代本轮新增的 QTest 执行。之后用最新主程序对用户此前授权的**非生产离线 Notes 副本** `/home/felix/data/Notes (1)` 重新跑完整 `--check`：**832 当前文档、125550 delimiters、231146 scripts、16637 operators、28068 MathBlock、8028 grids、261 历史版本、10592 索引树、原有 2 warnings，退出码 0**，工具明确 `Original Vault unchanged`，日志 `/tmp/athena-v3-variant-real-offline-copy-check-20261017.log`。这不是对生产 Notes 的读写检查。
- MathML 的原始 `<mtext>&Sum;</mtext>` 与 `<mtext>&amp;Sum;</mtext>` 在通用 `parse_xml` 阶段被合并为完全相同的 parsed SXML 字符串 `&Sum;`（真实证据 `/tmp/athena-v3-mathml-entities-inspection.log`）。解析后的 importer 不可能恢复已丢失的转义写法，因此目前**未修复该独立词法保真问题**；后续必须在字节→XML token 的来源层提供不丢失 amp/entity 来源的机制，不能仅把所有 `&Sum;` 变为字面 amp 或无条件数学符号造成另一方向回归。此工作不影响本批 Fraktur/数学粗体的明确成果。

### 8.24 MathML v3 源实体和 CDATA 保真、数学字母真实排版（2026-10-18）

- **最早出错层已证明并在显式 v3 来源入口修复**：旧 `parse_xml` 宽容 XML 解析器在处理字符数据时递归展开实体，把源 XML `<mtext>&Sum;</mtext>` 和 `<mtext>&amp;Sum;</mtext>` 变成相同的已解析 SXML `"&Sum;"`，后续 v3 importer 因此将**用户原本要的字面 `&Sum;`**错误当成求和符号。新 `parse_xml_mathml_v3` 仅供**显式** `mathml-v3-snippet`、`mathml-v3-document` 原始 XML source string 使用；新增专用 `mathml_xml_to_tree_v3`/Scheme glue，并将 `ATHENA/progs/convert/mathml/init-mathml.scm` 的原始 source converter 接到该路径。原有通用 `parse_xml`、HTML v2 自动 MathML 以及已经解析好的 `mathml-v3-stree` API 不改，后者若来源已丢失转义写法就不能追溯恢复。
- 在 XML 词法层对真实 `&amp;` 及 U+0026 的十进制/十六进制字符引用（支持前导零）、CDATA 中的每个字面 `&` 保留内部 SXML `*LITERAL-AMP*` 事件；只有合法 MathML token `mi/mn/mo/mtext` 可以消费此事件。Importer 严格校验事件位置并在组装 token 时恢复字面 `&`，而**未转义的原始 `&Sum;` 仍按照标准数学实体展开为 `∑`**。这防止了在 token 下游二次递归展开，从而同时保留两种来源的真实含义，不是全局禁用 MathML entity 转换。
- 以真实 `ATHENA.bin` 注册的 `native_v3_mathml_routing_runtime_test` 做**先红后绿**回归：原始 `<![CDATA[&Sum;]]>` 测试在修复前明确报 `MathML CDATA incorrectly expanded a named entity`，日志 `/tmp/athena-v3-mathml-cdata-red-20261018.log`；词法层 CDATA 保留修复后 `native_v3_mathml_routing_runtime_test` 退出码 0，日志 `/tmp/athena-v3-mathml-cdata-green-20261018.log`。之后扩充正反例：`&amp;`、`&#38;`、`&#038;`、`&#00038;`、`&#x26;`、`&#x0026;`、`&#X26;`，CDATA 内 `&Sum;`、`&amp;Sum;`、`A&&B`，同一 `<mtext>` 中**同时出现**字面 amp/正常 `&Sum;`，以及双重转义 `&amp;amp;`；全部通过正式 Scheme converter，最后日志 `/tmp/athena-v3-mathml-escaped-amp-final-20261018.log`。`tests/Data/Convert/Xml/native_v3_mathml_test.cpp` 同步补同层 C++ 调用断言，按旧测试编译参数 `-fsyntax-only` **退出 0**（`/tmp/athena-v3-mathml-cdata-qt-syntax-20261018.log`）；Qt 新测试仍未重建运行，不能与最新 Scheme 主程序测试混淆。
- **真排版验证**：新增 `tests/scheme/native-v3-math-variant-typeset-test.scm`，在完全私有 anonymous editor 中插入 `mbf`、`mbfu`、`frak`、`bb`、`cal`、`bf` 六种原生 MATH_VARIANT，经过正常编辑器排版并 `print-to-file`，得到 A4 单页、27 KB 级别的合法 PDF。实际用 `pdfinfo`、`pdftotext` 确认页数/字符，并以 `pdftoppm` 渲染 PNG 由模型目视核查字体分化和非空字形，没有空框或缺字；验证截图及 PDF 只放在 `/tmp/athena-v3-math-variant-visual-check/`，不触碰用户 Vault。通过已有 `evaluation-bar-test.py` + `anonymous-editor-owner-setup.scm` 注册为新 `native_v3_math_variant_typeset_editor_test`，**真实 CTest 通过**，支持的排版路径不再只凭 AST/TeX 文本猜测。
- 由于修改 CMake 测试登记与 Scheme glue 后触发全量依赖重编译，仍只按仓库约束执行 `cmake --build build_qt6 --target ATHENA.bin -j20`，**构建/链接退出码 0**，日志 `/tmp/athena-v3-mathml-and-pdf-render-build-20261018.log`。最新主程序连同旧 Qt/codec 等过滤后的相关 **13/13 定向 CTest 通过**（`/tmp/athena-v3-math-lexical-final-13-ctest-20261018.log`）；其中新增 PDF typesetter、显式 MathML v3、数学字母离线 check/stage、原生搜索/导出都是最新版 ATHENA.bin 的真实执行，新 Qt test binary 仍是之前编译版本，不能据此声称新 Qt 用例已全部执行。两组重要生产边界仍保持不变：普通 Vault runtime 拒绝 `node_model_version >= 2`，断开式候选 `hodarium_protocol_cutover_verified=false` / `eligible_for_atomic_publication=false`；没有自行提交/推送或对生产 Notes 开启升级。
- 在**上述最终构建后**，用单独临时 HOME/profile 和最新 ATHENA.bin 对此前明确允许的**非生产离线 Notes 副本** `/home/felix/data/Notes (1)` 又执行一次完整 `--upgrade-vault-xml-v3 --check`：**832 current documents / 125550 delimiters / 231146 scripts / 16637 operators / 28068 math blocks / 8028 grids / 261 history / 10592 index trees / 原有 2 warnings**，退出码 0，命令末尾 `Original Vault unchanged`（`/tmp/athena-v3-mathml-final-authorized-copy-check-20261018.log`），仍未执行生产 Vault 写入或任何 v3 publish。
- 最终 Debug 主程序使用 `ATHENA/bin/ATHENA.bin.new` 临时安装、`cmp` 核验构建产物、原子 `mv`、安装后二次 `cmp` 校验，**2026-10-18 09:48:45 +08** 成功部署至 `ATHENA/bin/ATHENA.bin`（大小 599532712 字节）。没有重启正在运行的 ATHENA，没有对生产 Notes 做任何修改，`git diff --check` 通过；全部修改保留在未提交工作区（本时点约 52 个修改/新增文件），没有自动 Git commit/push。真正完整的原生编辑器新 Qt 用例运行验收和跨设备格式切换仍是待完成阶段。

## 9. 完全未完成或待冻结的产品级决策

下列是**尚不能以当前代码行为代替用户确认**的细节，应先搜证、提出方案并征得用户同意，而不是擅自决定：

1. **MathGrid 行级编号/label 数据模型已确认**：采用第 3 节的 ROW typed policy 与可选 rich tag，保留 UUID/显式 LABEL。缺少属性唯一表示 inherit；不增加 row subtype 或另一套编号节点。后续工作是完成各入口和交互验收，不再把模型选择当作未决问题。
2. **跨设备 Hodarium wire protocol cutover**：新 format/semantic version、旧 DAG preservation、remote handshake、两阶段/多阶段提交、双设备版本错配恢复、完整 protected evidence 与 conflict approval 清算。
3. **导出失败策略已确认**：默认明确报错，不自动有损导出。当前不提供隐式图片 fallback、保留对象占位或偷偷替换数学布局；需要有损输出时必须另行设计明确的用户选择，不能把失败转为看似成功的结果。
4. **端到端编辑稳定性**：所有 v3 键盘、光标、删除、Undo、片段/clipboard、跨文件引用与定界符大小调整仍需要按真实入口验收。退出阶段的分配器链接问题已修正，但这不代表全部编辑路径都已 v3 化。
5. **生产启用前的权限和备份策略**：停止写入/同步的具体机制、备份路径及历史上交换的 RENAME_EXCHANGE 失败恢复、崩溃后自动检测/继续或回滚，特别是外部状态不是本地 Vault 目录的子目录。

## 10. 推荐的本地 Codex 继续实施顺序与验收门槛

**第一阶段：稳定代码与基线。** 先读 AGENTS.md、本文和全量 git status；只针对当前未提交 v3 文件动手，不修改既有无关的 notes/native-menu-command-loss-handoff.md。先重跑 §7.2 的 7 组与 812 corpus；若失败先定位表示边界，不堆叠补丁。补上独立的规范样例、negative cases、UUID 全链路测试。尤其区分 main 进程能 build、Qt QTest 内部 PASS、和进程 exit=0。

**第二阶段：完成原生 runtime**。依序打通：所有 math bracket/script/big-op 输入与 clipboard/deletion/cursor、MathGrid 行级编号和引用、复杂 typed cell-format 视觉/身份、TeX/MathML 直接 import 及各个 export 边界。坚持 **v3 Reader 直达原生 AST**，不得增加 v2 假兼容。

**第三阶段：全面导出与副本验收**。将 LaTeX 的大算符、脚本、对齐、图片/布局转换做高覆盖测试；完成 HTML/MathML 产品决策。用不同字体、多行高分式、左右非标准括号、单侧竖线、前置脚标做像素/box/path 对照。真实库仍只读；所有写入只能针对用户授权的隔离克隆。

**第四阶段：受保护历史/Artifact/Hodarium 整体事务**。明确外部同步切换协议，制造可复验的远端与多设备 fixture，覆盖历史所有版本和 protected snapshots 的实际恢复。完成跨目录/DB/DAG 的 prepare/commit/rollback/restart；不得先移除安全闸。

**第五阶段：发布验收**。在真实 Vault 的离线*独立拷贝*上跑全量 stage 和重读，进行新应用打开→编辑→保存→重启→历史恢复→Artifact 搜索/引用→Hodarium 多设备同步→失败回滚闭环；审计日志、签名、内容 hash、UUID、路径、所有远端 revision DAG；最后由用户明确批准并打开生产 publish。**目前没有达到这一阶段。**

## 11. 接手时必须执行的“不做”清单

- 不读到本文中 `812/812` 就直接对真实 /home/felix/data/Notes/Notes Root/ 执行 --stage 或任何写入命令。
- 不在已有 v3 Reader 插入旧 around/rsup/eqnarray 的别名识别器、自动升级器、猜测“标准括号”的默认表。
- 不将 A/B、集合差符号反斜线当成数学中间定界符；不把单独的 ^{op} 当成必须有前置基底的错误。
- 不为已确认无用户引用的 20 个无意义公式 DOCUMENT UUID 添加永久兼容 alias；但**也不能**推广成所有身份都允许删除。
- 不丢弃或重用旧 Hodarium revision ID，不在原 signed/content-addressed revision payload 上覆盖字节，不把单个本地历史恢复成功当成外部同步成功。
- 不把非 MathJax HTML 的 v3 数学降成随便输出的文字；不把未知带 metadata 的 v3 数学序列化成无法恢复它的 Scheme/TeX 占位。
- 不移除 v3 候选的生产打开拒绝、protocol_cutover_verified=false、eligible_for_atomic_publication=false，除非完整跨设备切换协议和产品回归通过，并得到用户授权。
- 不触碰用户真实 Notes、不修改无关未提交文件、不自动 commit/push。应先向用户汇报完成/未完成项与对应测试证据。

---

**最后状态：2026-10-16。** 已实现的源代码保留在 ATHENA dirty worktree；ROW 编号模型、原生编号切换、富文本 tag 绑定及 fail-closed 导出已有测试证据（见 7.1.1）。完整原生编辑、全部格式导入导出、整库历史/索引验收和 Hodarium 跨设备切换仍未全部完成。生产打开与发布安全闸保留。本文件仅写入仓库 notes/，不代表生产迁移或 Git commit。
