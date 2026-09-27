# 节点身份、属性与引用改造交接

更新：2026-09-28。接手者熟悉 ATHENA，但不应假定熟悉本次新文档模型。

## 先读结论

原交接停在 native tree-set-diff 完成处；用户随后要求按功能块继续。
源生命周期、normal XML v2 persistence/activation、AUDMAP document-model v3、
Artifact source binding/revision、cut/move credential + 跨 actor undo/redo、
普通新建文档 born-v2、独立 offline node-model vault migration 与 migrated-vault bare
wikilink source-UUID cutover 均已分别完成并提交。当前 worktree 删除了旧 generated-anchor
生产系统（manual save auto-anchor、`Anchor enunciations`、maintenance `anchor-structures`、
confirmation UI 与 generator 本体）并通过集中验证，正待本次独立提交。
**核心 source/runtime/migration/reference 主链已经闭环；主要剩余工作集中在少数
tree-bearing persistence 边界、migration-time generated-anchor 清理 / anchorless transclusion UI、
以及最终数据保全/启用验收。**

当前最后代码提交：`78c6d7aa3 improve: cut over migrated wikilinks to source identities`。
此前相邻集成提交：`c465b4605`（offline node-model migration）、
`727bea0cb`（born-v2 ordinary source）、
`e5fa5aae6`（source move identity）、
`70da9708a`（Artifact source identity）、
`417efe87e`（AUDMAP document-model v3）、
`6f2937262`（normal XML v2 persistence/activation）、
`6b965f3c9`（source identity lifecycle）、`7e9b23a9b`（native tree diff）、
`f8db49cf3`（格式/preamble）、`440f069d5`（DataArt）、
`4617f4219`（增量身份事务）、`411f2ea0a`（动态引用导出）、`6325a84f1`（headless 导出）。

旧 UTF-8/XML 项目与本项目不同：这里是在已有 UTF-8/XML 基础上增加节点元数据、
XML v2、AUDMAP document-model v3 和新引用语义。已有
`--upgrade-vault-format` 只负责 legacy Cork/S-expression -> UTF-8/XML；
node-model 离线迁移的输入必须是已经为 UTF-8 XML 的 vault，并使用独立 CLI 参数。

详细历史记录在 `notes/node-model-migration.md`。它是逐批追加的日志，早期章节里的
“尚未做”可能已被后续章节完成；本文件是暂停时的汇总，源码仍是行为的最终依据。

## 新架构为什么这样设计

旧架构用 enunciation 两侧的生成 anchor 定位对象，Wikilink、Transclusion、Artifact
又依赖 map.sqlite 的映射和 URI 文件/anchor hints。定位库损坏可能丢失引用语义，
需要脆弱的 repair。新的设计将身份放回文档本身：

```text
源 tree 节点：可选 UUID + 可选 typed properties + 原有正文
                     |
          原生节点定位服务 / 可重建缓存
             /          |           \
        Wikilink    Transclusion    Artifact 来源

外部客户端 -> AUDMAP -> owner actor 操作文档
内部导航、排版、hover -> 原生服务 / tmfs，不经过 AUDMAP
```

定位数据库不拥有引用语义；删除缓存后，扫描文档应恢复同一个目标。
安全重命名的恢复日志是操作日志，不能因为定位缓存可删除就一起删除。
不能在迁移前盲删旧 map/anchors；未迁移 legacy vault 的读取兼容仍可能依赖它们。
但新的 runtime 已不再生产 generated identity anchors，迁移后的身份真相是 source UUID。

## 已确定的模型契约

### tree、UUID 与 params

- 保留现有原生 `tree`；原子和复合节点的共同表示支持惰性元数据。
  不使用 wrapper 节点，不把属性塞进普通 children，不改变正文 child index。
- 用户口中的 params 是命名、有类型的 properties，不是另一份位置参数列表。
  UUID 是专用字段，不是任意可写属性，也不是临时路径、buffer handle 或内容 hash。
- 八种值：UTF-8 string、bool、int64、有限 double、list、dictionary、UUID reference、
  rich-text（原生 tree）。禁止循环、不透明对象和非有限浮点；未知 namespaced 属性保留。
  富文本属性不因持久化而获得宏执行权限。
- 目标是正文根、段落、标题、enunciation 自动有 ID，包括嵌套正文。
  段落按内容角色识别，原子也能是段落；数学 x、图片等可以有 ID，但不自动分配。
  样式、宏定义、展开结果和展示副本不生成源身份。
- UUID 在 vault 中唯一。外部复制制造冲突时报告冲突，不能任选一个文件。
  detached 子树验证不等于 vault 全局唯一性证明。
- `copy()` / `tree-copy` 是保身份、元数据独立的快照。
  `duplicate` / `tree-duplicate-source` 是新对象复制，重新分配已有 ID，重映射范围内引用。
  二者不可混用。跨 actor 不共享可变原生树或 rich-text 属性树。
- 源节点改正文仍是同一身份；删除后重建是不同对象，不能靠内容相似度找回身份。
- Split：第一个非空片段保留身份，两边均空则前者保留；另一片段新 ID。
  Join：前段身份保留，后段消失，其引用失效。Undo/redo 重放已记录的实际 ID。
- Cut 使用进程内一次性移动凭据：同 vault 首次成功粘贴保身份；再次粘贴、跨 vault、
  凭据失效或不可验证外部剪切按复制处理。clipboard bytes 本身不构成授权。
  source/target history entry 共享同一 move marker；跨文档 undo/redo 在两侧该 marker
  都是下一可执行项时协调两个 actor，顺序避免任一时刻产生双份 source UUID。
  peer 文档存在更晚编辑时先拒绝跨过 move，要求先处理更晚历史，而不是猜测合并。
- 全量相等/hash 包含元数据。内容比较、排版和模型输入应显式选择投影。
  `content_projection` 去源 UUID 但仍保留属性和引用目标，不是现成的 embedding 指纹。

### 统一 enunciation

源结构是 `enunciation(document(...))`，只有一个正文子节点，索引为 0。
类别通过 `kind` 属性表达，而不是每个 theorem/lemma/proof 都是不同源标签。

```text
node: enunciation
id: <source UUID>
properties:
  kind: string("theorem")
  numbered: boolean(true)
  name: rich-text(...)
  attribution: list(rich-text(...), ...)
  year: string("19XX")
  target: reference(<UUID>)       # 例如 proof 的显式证明目标，可选
children:
  0: document(...)
```

当前 schema 还有可选 `variant` 和 `legacy-tag`，用来无损表达历史变体与呈现来源。
`kind`、`numbered` 是 canonical 节点必需属性。未知 kind 保内容并使用通用呈现。
名称、年份和署名不回写正文；旧正文手写的括号、人名、年份一字不动。
迁移只处理明确结构槽，不从自然语言猜 metadata，也不引入 Notes 专用规则。

唯一类型声明：`ATHENA/misc/enunciations.json`，由 native registry 消费。
显示名、样式、编号组、类别、旧标签映射及 legacy extraction policy 均从这里取。
不要在搜索、统计、UI、Artifact 等处重新硬编码 theorem/lemma 列表。

### 引用、定位与 Artifact

- 最终 Wikilink：`tmfs://wikilink/<uuid>`；仍用成熟 tmfs 基础设施。
  `Vaultfile.json` 的 `node_model_version >= 1` 是语义 cutover gate：已迁移 vault
  将第一个 UUID component 解释为 persistent source UUID；未迁移 vault 继续用
  map.sqlite 身份。不能只看 URL 形状猜新旧语义。历史 file/anchor suffix 可保留作
  显示兼容信息，但在 migrated vault 中不能覆盖、修复或替代 source UUID。
- canonical transclusion：`TRANSCLUDE(TUPLE(uuid,...))`，有序明确对象集合，
  不是两个端点之间随内容变化的范围。来源导航是 `tmfs://transclude/<uuid>`。
- UUID 列表去重保首次顺序；祖先/后代重叠选择拒绝。缺失项保留占位，不静默缩短列表。
- 定位区分 pending、missing、read failure、conflict、cycle 等状态。
  活文档 owner 一致快照优先，未保存删除不能从磁盘“复活”。命中后实际消费再验 UUID。
- 冷定位在合并的后台扫描工作中做；排版显示正在定位，不同步等全库扫描。
  核心 locator 冷批次仍做文件 census；Qt facade 的稳定快照命中才避免文件系统工作。
  现在是内存缓存，不要声称已有持久 SQLite 定位库或所有请求都 O(1)。
- 普通展示副本清除源身份及 artifact bindings。源上下文/style/initial/preamble
  仍需保留以正确预览；展示复制不是新源对象复制。
- PDF/打印不能把 pending 占位当最终内容：先异步/离线准备冻结引用快照，再排版输出。
  动态宏生成的引用通过无输出布局探测迭代到依赖闭包，用户输出动作只执行一次。
- Artifact 最终按 source UUID + 稳定 extraction role 绑定；改类别或正文不重新猜身份。
  已有 artifact UUID 必须保留，新旧绑定都写入源属性 `athena:artifact-bindings`。
  此保留属性是 role -> artifact UUID 字典；普通属性 API 禁止改，复制为新对象时清除。
  producer/database/source persistence 已接通；v2 以 source binding 为身份真相，
  v1/legacy 仍使用旧 conservative association 兼容；offline vault migration 已将旧
  Artifact UUID/source 关系写回源 binding。

## 三色工作清单

图例：🟩 该明确范围已实现并有定向验证；🟦 已有实现但集成/验收未闭环；
🟧 尚未实现或尚未开始最终切换。绿色不代表已启用生产格式。

### 🟩 已完成的模块与局部闭环

- 🟩 原子/复合节点可选 UUID、八类属性、独立复制、完整相等/hash、内容投影。
- 🟩 原生 metadata modification、observer/history、保存 split/join 身份结果，
  已覆盖若干规范化、原子/concat 转换及独立 annotated wrapper 保护。
- 🟩 显式 XML v2 codec，包括 atomic metadata、typed properties、资源预算和冲突诊断。
  默认 v1 writer 遇 metadata 拒绝，不静默丢弃；显式 v2 reader 可读 v1/v2。
- 🟩 AUDMAP document-model v3 tree codec 与 protocol negotiation 已正式激活。
- 🟩 detached 源角色规划、schema 校验、可注入确定性分配器、重复身份检查。
- 🟩 owner 增量身份索引及事务末尾/保存前 hooks，回滚重建；按脏分支工作，不每键深拷贝。
- 🟩 新对象复制重分配已有 ID，重写 typed 引用、native HLINK 和新 transclusion 内链，
  清除 artifact 绑定；annotated 剪贴板快照可用 XML v2。
- 🟩 native 属性读写/按需赋 ID Scheme 接口、保头 `tree-rebuild`、原生 patch 传递。
- 🟩 enunciation registry、显式 detached 转换、搜索分类/筛选、统计、源颜色消费 registry。
- 🟩 canonical enunciation 原生呈现/编号、结构化标题安全投影及源码 cursor 映射。
- 🟩 Focus/context 原生属性编辑器与只读 UUID；owner lease 检查、单次可撤销提交。
- 🟩 UUID locator、live owner census、冲突/失败/循环/有序列表语义与可清缓存。
- 🟩 canonical transclusion 的异步呈现、源导航、带源样式 hover、快照失效通知代码。
- 🟩 冻结引用导出基础设施、headless coordinator、动态生成引用闭包；真实隔离 PDF 验证。
- 🟩 DataArt 源元数据保留，格式化/preamble 局部重建的身份保留及 owner undo/redo 验证。
- 🟩 当前收尾：native tree-set-diff、ICU 字素边界编辑、完整目标 header、
  wrapper/unwrap observer 保留、格式/嵌入源文本/cardlink callers、Materials 局部字段更新。
- 🟩 接续源生命周期块：完整 buffer 快照的外层/字段/collection/association/key 元数据，
  包括带元数据的空 collection；删除或更新文档字段保留外层 header。
- 🟩 显式 owner 接管完整身份基线、事务内赋 ID、split/join、撤销重做/冲突回滚，
  replacement 基线预检查及索引重建。
- 🟩 structural correction 与数学规范化的 source-aware 重建；保留独立标注的容器/子节点，
  不允许启发式修正吞掉有身份的分隔符、脚本包裹或正文节点。
- 🟩 normal XML v2 文档持久化/激活：dispatcher 识别 v2，普通 load 自动验证完整身份基线并
  启用 owner index；普通编辑事务分配 UUID，normal save/Save As 保持 v2，reopen 保持 UUID/properties。
- 🟩 v2 autosave/recovery：native texmacs autosave 直接写 v2，不经过 legacy serializer；
  recovery 通过 `buffer-import` 恢复格式身份并重新激活 owner index。v1/legacy 不自动升级。
- 🟩 AUDMAP wire protocol 2 / document-model 3 已正式启用：HELLO/WELCOME 精确协商 v3，
  document/node `get` 与属性投影携带 persistent UUID + typed properties；connection/ticket handle
  明确保持瞬时 occurrence identity，不充当源 UUID。
- 🟩 AUDMAP v3 metadata operations：`assign_id` 仅 server 生成且幂等，`update_properties`
  复用原生 schema/保护规则；结构编辑不能注入 metadata，替换保留原 source header。
  C++/Python SDK 版本、REPL 帮助和示例同步更新。
- 🟩 Artifact producer 已以 `source UUID + extraction role` 作为 XML v2 身份真相：
  专用内部 mutation 写入 reserved `athena:artifact-bindings`，旧 DB identity 可在首次接管时
  被可靠继承，之后删掉 Artifact DB 也能从源 binding 恢复同一 artifact UUID。
- 🟩 Artifact revision/cache 契约已拆分：storage bytes、Artifact extraction content、
  source identity/semantic revision 与具体 range-model input fingerprint 分开；相同模型输入
  可跨无关文档编辑复用，不因 source UUID/binding 写入本身产生伪 cache miss。
- 🟩 一次性 cut/move credential：完整 identified source object 的 cut 真正删除对象而不是
  留下带旧 UUID 的空壳；同 vault 首次 paste 可保 UUID，第二次/跨 vault/stale paste
  统一走 `duplicate_source_nodes`。source/target 用同一 history marker 协调跨 actor undo/redo，
  redo 会按 marker 精确选择 peer redo branch，不依赖 branch 0 或内容相似度。
- 🟩 新建普通文档 born-v2：不存在的用户文件与 New/New Window 的普通 source buffer
  从创建时就拥有完整 source UUID baseline 与 active owner identity index；第一次保存直接
  写 XML v2。DataArt/临时派生 buffer 仍可继续使用旧的匿名 `make_new_buffer`，不被误升级。
- 🟩 独立 offline node-model vault migration：新 CLI
  `--upgrade-vault-node-model VAULT_DIRECTORY` 只接受已经 UTF-8/XML-v1 的 vault；
  legacy Cork/S-expression 输入明确要求先运行旧 `--upgrade-vault-format`。migration 在
  private sibling snapshot 中 canonicalize enunciation、确定性分配 source UUID、优先复用可唯一
  映射的旧 map UUID、迁移 wikilink/transclude、写 Artifact source bindings，完整验证后才做
  atomic directory exchange；Vaultfile 以 `node_model_version: 1` 记录完成状态并支持重复运行 no-op。
- 🟩 migrated-vault bare wikilink cutover：runtime vault snapshot 发布 `node_model_version`；
  migrated vault 的点击导航、hover/link peek、reference graph 与 website export 都以 source UUID
  为身份真相，不再接受 map/file/anchor hints 改写目标。未迁移 vault 保持原 map.sqlite 语义。
  新 Wikilink 插入从实际 XML-v2 source object 读取 persistent UUID；选中对象没有 UUID 时拒绝
  插入，而不是生成新的 map identity。map.sqlite 暂保留作兼容/rename 历史数据。
- 🟩 generated-anchor producer 已退役：删除 `vault_anchors.cpp/.hpp`、manual-save auto anchoring、
  `Anchor enunciations` 菜单/native API、Qt confirmation dialog、`anchor-structures` maintenance pass
  及其 preferences/tests。普通 save 与 maintenance 均不再创建、改名或补回 heading/enunciation
  identity anchors；维护 worker preference 已改成通用名称，只供仍存在的并行维护任务使用。

### 🟦 已起步、尚未整体完成

- 🟦 源编辑全链路审计：上述快照、tree_correct/tree_brackets 路径已修，其余 tree-set!/tree->stree 重建、
  批量插入、格式化和规范化消费者仍需逐项审查。不能用事后路径猜 ID 补洞。
- 🟦 enunciation 剩余源创建入口/消费者、属性名称在搜索结果中的呈现、证明目标选择与解析。
- 🟦 属性 UI 的真实 GUI/owner 关闭竞争及结构化编辑生命周期验收。
- 🟦 locator/watchers、异步导航、嵌套 hover 的真实 Qt/actor 生命周期验收；缓存失效仍偏粗。
- 🟦 导出：原始源动态闭包已有真实验证；DataArt/selection 等后续派生转换新增依赖、
  临时 buffer 和交互取消/关闭等还需端到端验收。未准备好的引用必须 fail closed。
- 🟦 比较/缓存契约审计：基础设施已区分元数据与内容，但尚未审完全部消费点。

### 🟧 尚未完成的关键集成/切换

- 🟧 其余 clipboard/委派及 tree-bearing persistence 边界的统一 v2 切换；normal v2
  load/save/autosave/recovery 和普通新建文档已完成，不应再作为待办重做。
- 🟧 在 offline node-model migration 的 private staging 中删除可确认已经冗余的历史 generated
  anchors，并把 migrated transclusion/Artifact 选择 UI 完全切到 source UUID/object-list；生产端已
  不再生成 anchors。随后继续把 map.sqlite 从剩余兼容/rename consumers 中降级。
- 🟧 故障注入/数据保全最终验收、用户验收、统一启用及部署。

## 接手时的代码入口

路径均相对仓库 `/home/felix/data/Software/TeXmacs/texmacs`。

| 领域 | 主要入口 |
| --- | --- |
| 元数据与 tree | `src/Kernel/Types/node_metadata.{hpp,cpp}`、`tree.*` |
| 修改与历史 | `src/Kernel/Types/modification.*`、`src/Kernel/Abstractions/observer.cpp`、`src/Data/History/commute.cpp` |
| 源角色/增量身份 | `src/ATHENA/Data/document_node_model.*`、`src/Edit/Modify/edit_modify.cpp`、`src/ATHENA/buffer_state.hpp` |
| 源快照/校正/加载 | `src/ATHENA/Data/interop_document_source.*`、`src/ATHENA/Server/buffer_actor.cpp`、`src/Data/Tree/tree_correct.cpp`、`tree_brackets.cpp`、`tree_analyze.cpp` |
| 复制/剪贴板 | `src/ATHENA/Data/document_node_copy.*`，搜索 `tree_duplicate_source` / `duplicate_source` 的调用点 |
| 源树 Scheme 接口 | `src/Scheme/Scheme/native_node_properties.cpp`、`native_tree_diff.cpp`、`src/Scheme/Glue/basic.xml` |
| 类型与转换 | `src/ATHENA/Data/enunciation_model.*`、`ATHENA/misc/enunciations.json` |
| 呈现 | `src/Typeset/Env/env_enunciation.cpp`、`src/Typeset/enunciation_presentation.hpp`、`enunciation_surround.hpp` |
| 属性 UI | `src/Subsystems/Qt/QTMNodePropertiesDialog.*` |
| 定位 | `src/ATHENA/Data/node_location.*`、`vault_node_location.*` |
| 引用/hover | `src/ATHENA/Data/node_reference.*`、`src/Subsystems/Qt/QTMNodeReferences.cpp` |
| 导出 | `src/ATHENA/Data/node_reference_export.*`、`src/Subsystems/Qt/QTMNodeReferenceExport.cpp` |
| XML/离线迁移 | `src/Data/Convert/Xml/athena_document_xml.*`、`vault_format_upgrade.*` |
| AUDMAP | `src/ATHENA/Data/interop_document_codec.*`、`interop_document_nodes.*` |
| 历史与验收记录 | `notes/node-model-migration.md` |

## 当前 tree-set-diff 的特别说明

这是“把源更新成完整目标”，包括 UUID 和 properties，而不是猜测语义的内容替换。
目标明确匿名时会清除被替换对象的 metadata；内容编辑者若想保身份，必须显式保留 header。
因此 native diff 正确不代表所有旧 `tree-set! ... stree` callers 都已安全。

原 Scheme optimizer 有两个真实问题：字符数与 UTF-8 byte offset 混用；
子节点一样时只改标签而漏掉 metadata。新实现使用 ICU grapheme_cursor，
对已附着源要求当前 editor owner，尽量用局部 edits 保留 cursor/observers。
显式将父替换为实际子孙时记录完整 child header，与禁止丢身份的普通规范化不同。

本批正常编译已成功，随后仅运行 `tests/scheme/node-tree-diff-test.scm`。
该脚本在真实 BufferActor、隔离 headless profile 中运行，最终 exit 0，日志包含
`ATHENA-NODE-TREE-DIFF-PASS`。覆盖中文、组合重音、ZWJ emoji、旗帜、空文本、
header-only、增删子节点/标签、匿名目标、嵌套包裹解包裹及撤销重做、Materials 字段修改。

测试中曾遗漏正常命令的 `start-editing` 作者设置，造成 undo 连续回退两步。
补齐 fixture 事务入口后通过，**不要再为此修改 history 内核**。临时 debug-history 已去掉。
有一次临时诊断对 atom 调用 tree-children 导致诊断自身崩溃，已用 atomic 分支修正。
隔离目录中旧 `failure.txt` 可能仍在；它不是最新失败，判断看进程状态及最新成功日志。

本批日志（忽略目录，不是可移植交接附件）：
- `build_qt6/node-tree-diff-build-final.log`
- `build_qt6/node-tree-diff-runtime.log`
- `build_qt6/node-source-edit-runtime-ZoKSLw/`，专用测试 profile，没有生产 vault。

## 后续如何续上，不要重启项目

源编辑/加载事务的 staged 接管与快照校正块已完成，具体范围见下节。
后续继续其余源创建/批量重建消费者及格式激活边界，再做 cut/move 与 Artifact/迁移闭环。
不要继续反复打磨已经通过的 XML codec 或为每个小修改启动全部测试。
每个后续批次都明确输入、输出、owner 和持久化边界，完成一大块后做对应定向验证。

离线迁移必须补齐以下约束，不能以“能打开新文件”代替：

1. 预检查、私有快照、确定性映射、转换、验证、事务发布，失败保原件，可取消/续跑/恢复。
2. 旧 map 多 UUID 指向同一对象时合并到规范节点身份，并重写实际引用。
   整文件对应持久根；旧范围变成明确对象列表，不留 anchor hint 作为必要恢复信息。
3. 原有坏链接/歧义保存为 explicit unresolved，带原始目标、诊断和逐项日志；
   不猜修、不阻塞其他可迁移内容。原本有效引用转换失败仍必须阻止发布。
4. 只删可确认的生成 anchors，保留用户 label/ref 语义；自由文本完全不做 metadata 推测。
5. 保留 Artifact UUID、判定和 vectors，绑定持久化进文档；mtime/size 变化不自动触发推理。
   复用模型结果前验证真实模型输入契约，无法重定位标待处理，不挂错对象。
6. 使用隔离副本验证删缓存、外部 rename、UUID 冲突、未保存修改、多节点有序选择、循环、
   不可读、缺失和写入/中断故障。不自动操作 `~/data/Notes`。

## 构建与提交硬约束

正常构建固定且仅使用：

```sh
cmake --build build_qt6 --target ATHENA.bin -j20
```

产物是 `build_qt6/src/ATHENA.bin`。不是 `build_qt6/ATHENA.bin`。
**禁止省略 target，禁止自作主张 -j8，禁止全量测试/TSan/utf8_editor_test，
禁止默认部署或生产迁移。** 新增定向测试源不等于已运行；分别报告编译、运行、部署、启用。
不要因为交接文档列出了已有测试，就重新全跑一次。

Scheme bindings 先读 `src/Scheme/Glue/README.md`；在 XML 声明，CMake 生成，
实现放 native cpp。禁止手写 per-procedure wrappers、注册及修改生成文件。
跨线程继续遵守 BufferActor 所有权，生成 glue 不授予 GUI/editor 访问权。

提交前读完整近期 commit bodies，按 `type: imperative summary` 加具体 bullet body。
只 stage 当前批次；`.codex/`、GGUF、模型目录等已有 untracked 不要加入或删除。
暂停前代码批次与这份交接应分开提交，接手可直接 `git log` 定位。

最后：**没有一个可以现在随手打开的总开关。** XML 默认入口、源创建、
actor identities、tmfs 语义、协议和迁移都刻意分开 gated；必须闭环后一起启用。

## 接续批次：staged 源生命周期（2026-09-27 晚）

本批还在工作树中，没有提交、部署、生产文件修改或统一新格式启用。

- `source_identity_state::initialize_complete` 验证完整正文角色身份及唯一性，不分配 ID。
  `adopt-source-node-identities` 在当前 owner 接管这个完整基线，要求旧编辑历史已清空；
  `source-node-identities-active?` 查询本 buffer 状态。不是给旧文档自动迁移的命令。
- `replace_document` / `replace_body` 在已接管的 buffer 中先验证新基线，拒绝后保持旧源、
  索引及保存状态；成功后替换索引。文件导入在 replacement 被拒绝时返回失败，
  不再继续 capture 错误的磁盘 revision 或把旧内容标作保存成功。
- 透明 WITH-like 宏的尾参数可能被 DRD 标为 TYPE_UNKNOWN（如 em 的参数直接返回）。
  此处使用已有 DRD with_like 契约识别内容透传，不按标签名猜，不把普通 inline 参数
  当成新段落；其他未知/含糊角色仍拒绝。
- 完整快照保留 source envelope、标准字段、集合/association/key 和未知字段的 header。
  仍执行 viewport/no_aux 过滤，不把被过滤的值补回。空 COLLECTION 必须比较 L(t)，
  两参数 is_func(t,COLLECTION) 要求非空，会误判并清掉空集合 metadata。
- correction 重建显式保头；source tokenizer 与只读 tokenizer 分开，带独立 metadata
  的子节点不可被拆碎或合并掉。启发式括号/脚本重写遇到独立标注结构时保留结构。

本批 normal ATHENA.bin 构建成功，集中执行 `tests/scheme/node-source-lifecycle-test.py`
及其同名 Scheme fixture，最后 exit 0，标记 `ATHENA-NODE-SOURCE-LIFECYCLE-PASS`。
覆盖真实 BufferActor 的接管拒绝/成功、格式化 inline 角色、插入自动 ID、split/join、
undo/redo UUID 稳定、重复 ID 回滚、取消后的索引可用性、基线替换、快照字段和空集合、
九条 correction 入口、附着源 native diff，以及 v1 拒绝写 metadata 后原文件 bytes 不变。
这是同一集成脚本的集中调试/验收，不是全量测试。

日志：`build_qt6/node-source-lifecycle-build.log`；最终成功隔离目录：
`build_qt6/node-source-lifecycle-check/source-lifecycle-7s839pll/`。
其他同前缀目录保留了早期调试失败，不代表当前状态。fixture 曾误用 detached tree-set-diff，
已改为只对附着源调用；不要因此放宽 native diff 的 owner/source 约束。
