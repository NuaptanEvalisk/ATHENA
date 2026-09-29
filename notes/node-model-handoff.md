# 节点模型设计备忘录

更新：2026-09-29。

这份文件曾用于节点模型改造的跨会话交接。该 handoff 阶段已经结束；现在保留它作为
ATHENA 节点身份、属性、引用、持久化与迁移语义的长期设计备忘录。

本文不是待办清单，也不表示源码必须永远保持这里记录的具体实现方式。它记录的是已经
确立、后续修改不应无意破坏的模型边界和设计理由。行为的最终依据仍是源码和格式版本。

逐批迁移历史见 `notes/node-model-migration.md`。早期日志中的 WIP、缺口和验证状态只用于
追溯当时的实现过程，不应被解释为要求重做保存、恢复、AUDMAP、剪贴板或已经完成的 migration。

## 架构核心

旧架构曾使用 enunciation 两侧的 generated anchors 定位对象，Wikilink、Transclusion、
Artifact 又依赖 `map.sqlite` 中的映射和 file/anchor hints。这样会让可重建的定位索引
事实上承担引用语义，一旦索引损坏就需要脆弱的 repair。

新模型把身份放回源文档：

```text
源 tree 节点：可选 UUID + typed properties + 原有正文
                     |
          原生节点定位服务 / 可重建缓存
             /          |           \
        Wikilink    Transclusion    Artifact 来源

外部客户端 -> AUDMAP -> owner actor 操作文档
内部导航、排版、hover -> 原生服务 / tmfs，不经过 AUDMAP
```

长期原则：

- persistent identity 属于源 tree，不属于定位数据库、文件路径、buffer handle 或内容 hash。
- 定位缓存可以删除并重建；删除缓存后重新扫描文档应恢复同一个 source target。
- 安全重命名的恢复日志属于操作事务记录，不是定位缓存，不能随 locator/map 清理一起删除。
- 未迁移 legacy vault 的兼容读取可以继续依赖旧 map 语义；迁移后的身份真相是 source UUID。
- 任何“修复身份”的逻辑都必须有明确证据，不能用内容相似度、当前路径或展示文本猜测。

## tree、UUID 与 typed properties

ATHENA 保留原生 `tree`，不引入 identity wrapper 节点，也不把属性伪装成普通 children。
因此已有正文 child index 保持稳定，atomic 与 compound 节点共享同一套惰性 metadata 能力。

节点 metadata 分成两类：

1. 专用 source UUID；
2. 命名、带类型的 properties。

UUID 不是普通可任意写 property。它表示 persistent source identity。

properties 的持久类型集合为：

- UTF-8 string
- bool
- int64
- finite double
- list
- dictionary
- UUID reference
- rich-text（原生 tree）

禁止循环值、不透明 runtime 对象和非有限浮点。未知 namespaced property 必须尽量保留。
rich-text property 的存在不赋予宏执行权限。

源角色规划要求正文根、段落、标题、enunciation 自动拥有身份，包括嵌套正文。段落按内容
角色而不是单纯标签识别，因此 atomic node 也可能是一个 source paragraph。数学 atom、
图片等节点可以拥有 UUID，但不会仅因存在于源文档中就自动分配。

样式、宏定义、展开结果和展示副本不是 source object，不自动获得源身份。

UUID 在 vault 语义上唯一。detached subtree 内部验证只能证明局部无重复，不能代替 vault
全局唯一性判断。外部复制制造同一 UUID 多处出现时必须报告 conflict，不能任选其中一个。

## copy、duplicate 与对象连续性

以下两个操作必须严格区分：

- `copy()` / `tree-copy`：制作快照，保留 source UUID；metadata 深层独立，不共享可变树。
- `duplicate` / `tree-duplicate-source`：制造新 source object；已有 UUID 必须重新分配，
  并重映射复制范围内的 UUID references。

跨 actor 不能共享可变 native tree，也不能共享 rich-text property tree。

source object 的 identity continuity 规则：

- 修改正文不改变身份。
- 删除后根据相同内容重新创建，是新对象。
- Split：第一个非空 fragment 保留身份；若两边都空，前 fragment 保留；另一 fragment 新 ID。
- Join：前段身份保留，后段身份消失；指向后段的引用随之失效。
- Undo/redo 重放当时记录的实际 UUID，不重新推导身份。

Cut/move 使用进程内一次性移动凭据。同一 vault 中第一次成功 paste 可以保留 source UUID；
再次 paste、跨 vault、凭据失效或无法验证的外部 clipboard 一律按 duplicate 处理。

clipboard bytes 本身不是“移动授权”。

跨文档 move 的 source/target history entry 共享同一 marker。跨 actor undo/redo 只有在两侧
该 marker 都是下一可执行项时才能协调执行；执行顺序必须避免任何瞬间出现两份同一 UUID。
若 peer document 存在更晚编辑，则拒绝跨过该 move，而不是尝试猜测合并。

## equality、hash 与 projection

完整 tree equality/hash 包含 metadata。

消费者如果只关心内容，必须显式选择 projection；不能依赖旧时代“metadata 不参与相等”的
偶然行为。

`content_projection` 去除 source UUID，但保留 properties 和引用目标。它是语义内容投影，
不是 embedding fingerprint，也不是所有 cache 的统一 key。

不同缓存应分别定义自己的输入契约，例如：

- storage bytes fingerprint
- Artifact extraction content
- source identity / semantic revision
- range-model input fingerprint

不能因为 UUID 或 reserved binding 写入发生变化，就无条件让与这些字段无关的模型缓存失效。

## 统一 enunciation 模型

canonical source structure：

```text
node: enunciation
id: <source UUID>
properties:
  kind: string("theorem")
  numbered: boolean(true)
  name: rich-text(...)                   # optional
  attribution: list(rich-text(...), ...) # optional
  year: string("19XX")                   # optional
  target: reference(<UUID>)              # e.g. proof target, optional
  variant: ...                           # optional
  legacy-tag: ...                        # optional
children:
  0: document(...)
```

enunciation 只有一个正文 child，index 为 0。theorem、lemma、proof 等类别通过 `kind`
property 表达，不再通过不同 source labels 表示。

`kind` 和 `numbered` 是 canonical enunciation 的必需属性。未知 kind 必须保留内容并使用
通用呈现，而不是拒绝或猜成已知类型。

名称、年份、署名不会被迁移器回写到正文。旧正文中用户手写的括号、人名、年份保持原样。
migration 只从明确的结构槽提取 metadata，不从自然语言猜测。

唯一类型声明是 `ATHENA/misc/enunciations.json`。显示名、样式、编号组、类别、旧标签映射和
legacy extraction policy 都应从 native registry 消费。搜索、统计、UI、Artifact 等模块
不应重新维护一份 theorem/lemma 硬编码列表。

## source ownership 与 identity lifecycle

persistent source identity 的运行时 owner 是 BufferActor。

完整身份基线由 source identity state 接管；接管意味着当前文档已经满足 source role 的
identity completeness 与唯一性要求。接管不是“顺便迁移旧文档”的入口。

已接管文档的关键约束：

- 普通编辑事务内按需生成 UUID。
- transaction commit 后增量维护 owner identity index。
- rollback/replacement 后索引必须与 owner document 重新一致。
- `replace_document` / `replace_body` 必须先验证新基线；验证失败不能污染旧 document、
  identity index 或 save state。
- structural correction、normalization、wrapper/unwrap 必须显式决定 header 如何传播，
  不能先丢 metadata 再靠路径猜测补回。
- source tokenizer 与只读/展示 tokenizer 的语义不能混为一谈。

透明 WITH-like 宏的内容角色由已有 DRD `with_like` 契约判断，不按标签名猜。未知或含糊角色
不能被随意认作新 source paragraph。

完整 source snapshot 除正文外还要保留 envelope、标准字段、collection、association、
key 和未知字段的 metadata。空 COLLECTION 也是合法对象，不能因 arity 检查写错而丢 header。

## XML v2 persistence

XML v2 是能够持久化 source UUID 与 typed properties 的文档格式。

核心要求：

- atomic 和 compound metadata 都能往返。
- reader/writer 有明确资源预算与冲突诊断。
- v1 writer 遇到 metadata 必须拒绝，而不是静默丢字段。
- v2 reader 可接受 v1/v2，但“能读 v1”不代表 v1 自动升级。
- normal v2 load 会验证完整 identity baseline 并激活 owner identity index。
- v2 normal save / Save As 保持 UUID 与 properties。
- v2 autosave/recovery 必须直接走 v2-capable native persistence，不能经过会丢 metadata 的
  legacy serializer。
- ordinary new source documents born-v2：从创建开始就有完整 source identity baseline，
  第一次保存直接写 XML v2。
- DataArt、临时派生 buffer 等匿名工作树不应因普通文档规则被误升级为 source document。

旧 `--upgrade-vault-format` 的职责仍然是 legacy Cork/S-expression -> UTF-8/XML。
node-model migration 是另一层升级，输入前提是 vault 已经处于 UTF-8/XML 世界。

## Wikilink、Transclusion 与 locator

migrated vault 的 canonical Wikilink：

```text
tmfs://wikilink/<source-uuid>
```

`Vaultfile.json` 中 `node_model_version >= 1` 是新语义的 cutover gate。不能只根据 URI 形状
判断第一个 component 是 source UUID 还是旧 map identity。

migrated vault 中，historical file/anchor suffix 最多只是显示兼容信息，不能覆盖、修复或替代
source UUID。未迁移 vault 继续保留旧 map 语义。

canonical Transclusion：

```text
TRANSCLUDE(TUPLE(uuid, uuid, ...))
```

它表示有序、明确的 source object 集合，不表示“两个 anchor 之间当前碰巧存在的内容范围”。

规则：

- UUID list 去重并保留首次出现顺序。
- 祖先/后代重叠选择拒绝。
- 缺失 source object 保留 missing placeholder，不静默缩短集合。
- source navigation 使用 `tmfs://transclude/<uuid>`。
- locator 明确区分 pending、missing、read failure、conflict、cycle 等状态。
- live owner 的一致 snapshot 优先于磁盘；未保存删除不能被旧磁盘内容“复活”。
- locator 命中后，最终消费者仍应再次验证实际节点 UUID。
- cold lookup 可以后台扫描 vault；UI/排版不能同步阻塞整个文件 census。
- 当前 locator/cache 是可重建内存服务，不应被描述成持久 O(1) identity database。

Safe Rename 的恢复日志已经独立于旧 map，位于 `.athena/safe-rename.sqlite`。migrated vault
的 rename 计划、执行和恢复不能要求 `map.sqlite` 健康存在；legacy vault 仍可维护旧 map
兼容索引。

## Artifact source binding

Artifact 的长期 identity 是：

```text
source UUID + stable extraction role
```

而不是 file path、offset、当前类别文本或内容相似度。

source tree 的 reserved property `athena:artifact-bindings` 保存：

```text
role -> artifact UUID
```

该 property 由专用内部 mutation 修改，普通 property API 不允许任意写。duplicate 为新 source
object 时必须清除 bindings。

已有 Artifact UUID 应尽量保留。migration 或首次接管旧数据库关系时，只能在证据充分时把
旧 identity 写回 source binding。

Artifact schema v4 持久化有序 `source_nodes`。段落对象集合一旦由提取结果和 source binding
确认，就不再解释为动态 offsets 范围：

- 在中间插入新 paragraph 不自动扩大集合。
- 修改正文不改变定位对象。
- source UUID 缺失、重复或冲突必须失败。
- legacy offsets 只有在源内容 revision 完全匹配时才能转换为明确 UUID list。
- 没有证据时提示重建 Artifact，不近似猜测，不启动模型。

模型 cache 必须以真实模型输入契约决定是否复用，不能让无关 identity/binding 写入造成伪 miss。

## AUDMAP document model

AUDMAP 已定义 document-model v3，用于外部客户端访问带 persistent identity 的文档树。

重要边界：

- HELLO/WELCOME 进行明确 protocol/document-model negotiation。
- document/node `get` 和相关 projection 可携带 persistent UUID + typed properties。
- connection handle、ticket handle、runtime occurrence handle 都是瞬时运行时身份，
  不能冒充 source UUID。
- `assign_id` 只能由 server 生成 UUID，并保持幂等。
- `update_properties` 复用 native schema 与 reserved-property 保护规则。
- structural edit 不能通过 wire payload 注入任意 metadata。
- replacement 要遵守 source header preservation / baseline validation 契约。

内部导航、排版与 hover 不应为了统一接口而绕道 AUDMAP；它们已有 owner/local native 路径。

## 展示副本、preview 与导出

展示副本不是新 source object，也不是 source snapshot 本身。

普通 preview/display copy：

- 清除 source UUID。
- 清除 Artifact bindings。
- 保留普通 typed properties，例如 enunciation kind/name。
- 保留正确呈现需要的 source context、style、initial、preamble。

这与 `duplicate_source` 不同：duplicate 产生新的 source identity；display copy 则明确不拥有
source identity。

PDF/打印不能把 pending reference placeholder 当最终输出。输出前必须准备冻结引用快照。
动态宏生成的引用可以通过无输出 layout probe 迭代到依赖闭包，但真正用户输出动作只执行一次。

## generated anchors 已退役

新 runtime 不再生产用于 heading/enunciation identity 的 generated anchors。

已经退役的机制包括：

- `vault_anchors.cpp/.hpp`
- save-time/manual-save auto anchoring
- `Anchor enunciations` 菜单/native API
- `anchor-structures` maintenance pass
- 对应 preferences/tests

offline node-model migration 会在 reference rewrite、Artifact binding 和旧 map 定位都完成后，
只删除能由“旧 map 位置 + 当前结构 + 旧生成命名规则”共同证明是 generated 的历史 labels。
用户 label 只要证据链不完整就保留。

普通 save 和 maintenance 不执行 historical anchor cleanup。

## offline node-model migration

node-model migration 是独立 CLI：

```text
--upgrade-vault-node-model VAULT_DIRECTORY
```

输入必须已经是 UTF-8/XML vault。若仍是 Cork/S-expression，先运行旧
`--upgrade-vault-format`。

migration 的事务模型：

1. 预检查。
2. 建 private sibling snapshot/staging。
3. canonicalize source structures。
4. 确定性分配 source UUID；旧 map UUID 能唯一映射时优先复用。
5. 迁移 Wikilink/Transclusion。
6. 写回 Artifact source bindings / source node sets。
7. 在证据充分时清理 historical generated labels。
8. 完整验证。
9. 最终 atomic directory exchange 发布。

失败必须保留原 vault。重复运行已完成 migration 应为 no-op。

迁移语义：

- 旧 map 多 UUID 指向同一 source object 时合并到 canonical node identity，并重写真实引用。
- whole-file identity 对应 persistent root。
- legacy range 转成明确 source object list，不继续依赖 anchor hints。
- 原有坏链接/歧义应保留为 explicit unresolved，带原始目标和诊断；不能猜修。
- 原本有效的引用若迁移失败，应阻止最终发布。
- 不从自由文本猜 metadata。
- 保留 Artifact UUID、判断结果和可复用 vectors；无法可靠重定位时标记待处理，而不是挂错对象。

`Vaultfile.json` 的 `node_model_version: 1` 记录 migration cutover。

## native tree diff 与 source-aware rebuild

`tree-set-diff` 的语义是“把 source 更新成完整目标 tree”，因此目标 UUID/properties 也属于目标。

这意味着：

- 目标显式匿名时，被替换对象 metadata 可以被清除。
- 如果调用者是在做“内容编辑但保身份”，调用者必须传递/保留正确 header。
- native diff 正确不代表所有旧 `tree-set! ... stree` caller 自动安全。
- source-attached update 要求正确 owner context。
- 文本 diff 使用 ICU grapheme boundary，不能把 Unicode character count 与 UTF-8 byte offset 混用。
- 仅修改 label/content 时也不能遗漏 metadata 差异。
- wrapper/unwrap、format normalization、tree correction 要显式传播 header。
- 将父节点替换为其真实 descendant 时，必须使用那个 descendant 的完整 header；
  这与“普通 normalize 不得吞掉有身份结构”的规则并不冲突。

历史上 tree diff 的定向验证覆盖中文、组合重音、ZWJ emoji、旗帜、空文本、header-only、
child 增删/label 变化、匿名目标、嵌套 wrap/unwrap、undo/redo 与 Materials 局部字段修改。

## 长期审计边界

以下属于长期维护时值得继续审计的边界，但不是“重新开始 node-model migration”的清单：

- 任何新增的 tree-bearing persistence/clipboard/delegation 边界是否完整支持 v2 metadata。
- 新的 tree rebuild / normalization caller 是否错误丢 source header。
- cache key 是否明确选择 full tree equality、content projection 或自己的输入 fingerprint。
- Qt/actor 生命周期中的 owner close、vault switch、modal cancellation 是否正确取消异步 locator。
- preview/export 是否错误携带 source identity，或反过来丢失呈现所需 typed properties。
- 外部 rename、live unsaved state、conflict/missing/read-failure 的 locator 行为是否仍 fail closed。
- 新增 source role 是否进入统一 role planner/schema，而不是局部硬编码。

这些项目应作为功能修改时的 regression checklist，而不是周期性重跑一遍旧迁移工程。

## 主要代码入口

路径相对仓库根目录。

| 领域 | 主要入口 |
| --- | --- |
| 元数据与 tree | `src/Kernel/Types/node_metadata.{hpp,cpp}`、`tree.*` |
| 修改与历史 | `src/Kernel/Types/modification.*`、`src/Kernel/Abstractions/observer.cpp`、`src/Data/History/commute.cpp` |
| 源角色/增量身份 | `src/ATHENA/Data/document_node_model.*`、`src/Edit/Modify/edit_modify.cpp`、`src/ATHENA/buffer_state.hpp` |
| 源快照/校正/加载 | `src/ATHENA/Data/interop_document_source.*`、`src/ATHENA/Server/buffer_actor.cpp`、`src/Data/Tree/tree_correct.cpp`、`tree_brackets.cpp`、`tree_analyze.cpp` |
| 复制/剪贴板 | `src/ATHENA/Data/document_node_copy.*`，以及 `tree_duplicate_source` / `duplicate_source` callers |
| 源树 Scheme 接口 | `src/Scheme/Scheme/native_node_properties.cpp`、`native_tree_diff.cpp`、`src/Scheme/Glue/basic.xml` |
| enunciation | `src/ATHENA/Data/enunciation_model.*`、`ATHENA/misc/enunciations.json` |
| 呈现 | `src/Typeset/Env/env_enunciation.cpp`、`src/Typeset/enunciation_presentation.hpp`、`enunciation_surround.hpp` |
| 属性 UI | `src/Subsystems/Qt/QTMNodePropertiesDialog.*` |
| 定位 | `src/ATHENA/Data/node_location.*`、`vault_node_location.*` |
| 引用/hover | `src/ATHENA/Data/node_reference.*`、`src/Subsystems/Qt/QTMNodeReferences.cpp` |
| 导出 | `src/ATHENA/Data/node_reference_export.*`、`src/Subsystems/Qt/QTMNodeReferenceExport.cpp` |
| XML/离线迁移 | `src/Data/Convert/Xml/athena_document_xml.*`、`vault_format_upgrade.*` |
| AUDMAP | `src/ATHENA/Data/interop_document_codec.*`、`interop_document_nodes.*` |
| Safe Rename | `src/ATHENA/Data/vault_safe_rename.*`、`.athena/safe-rename.sqlite` |
| 历史实现记录 | `notes/node-model-migration.md` |

## 实现演进中的关键提交

下面这些 commit 主要用于追溯设计形成过程，不表示必须保留其内部实现：

- `7e9b23a9b` — native tree diff
- `6b965f3c9` — source identity lifecycle
- `6f2937262` — normal XML v2 persistence/activation
- `417efe87e` — AUDMAP document-model v3
- `70da9708a` — Artifact source identity
- `e5fa5aae6` — source move identity
- `727bea0cb` — ordinary source born-v2
- `c465b4605` — offline node-model migration
- `750639634` — historical generated-label retirement

更细的逐批验证、旧缺口、调试日志和当时的 WIP 状态统一留在
`notes/node-model-migration.md`，不再复制到本备忘录。

## 仓库工作流备注

正常 ATHENA 构建目标：

```sh
cmake --build build_qt6 --target ATHENA.bin -j20
```

binary 位于 `build_qt6/src/ATHENA.bin`。

Scheme native binding 先读 `src/Scheme/Glue/README.md`：binding 在 XML 声明，由 CMake 生成 glue，
实现放 native C++。不要手写 per-procedure generated wrapper/registration。

跨线程继续遵守 BufferActor ownership。生成 glue 只解决 marshalling/dispatch，不授予任意线程
GUI/editor/source-tree 访问权。

`.codex/`、GGUF、模型目录、perf artifacts 等无关 untracked 内容不应因 node-model 工作被顺手
加入或删除。
