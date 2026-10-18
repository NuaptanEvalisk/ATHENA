# ATHENA XML v3 / Hodarium：跨设备格式 Epoch 协议草案

> **2026-10-18：此兼容桥接方案已被用户撤销。** 最终只支持 v3，不支持旧格式备份和旧 Hodarium 证据，迁移后删除全部迁移工具。本文仅为历史设计记录，不应继续实现旧 DAG 桥接或将其作为发布门槛。当前目标见 `athena-xml-v3-design-migration-handoff.md` 首节；实际生产状态重置尚未执行。

> **状态：提案，2026-10-17；未冻结、未上线、不得据此解除 Vault 安全闸。**
>
> 本文件是实现/验收设计，**不是**已经存在的 wire contract，也不是用户批准的生产迁移计划。最终 format 字符串、epoch 编号、签名消息形状、授权主体、参与者集合及切换政策仍须单独确认。当前代码只实现只读审计与隔离候选；详见 `athena-xml-v3-design-migration-handoff.md` §§5、8.15–8.17。

## 1. 已确认的当前技术合同

- 旧 Hodarium revision descriptor 是 `protocol: 1`，SHA-256 的 domain 为 `ATHENA-HODARIUM-REVISION-v1`，输入是 CBOR descriptor + 原始 payload。文档 revision 使用 `format=ath-xml-v2, semantic_version=3`；其 `id` 和 `parents`、`applied`、`incoming`、`apply_intents`、`eligible`、`resolution_approvals`、受保护历史证据都属于同一因果系统。
- **身份认证边界必须分清**：`seal_revision` 的 SHA-256 domain hash 是**内容寻址完整性证明，不是单条 revision 的数字签名**；`origin_member` 是被哈希覆盖的声明字段，不自动证明该成员持有私钥或实际发送了这一 revision。旧通信使用已认证的设备会话，成员、冲突决定和恢复可能有独立签名凭据。任何新 epoch/bridge 的真实来源认证必须另行定义并验证，不能把 `sealed.id` 验证成功当成签名已验证。
- Wire exchange 使用 `ATHENA-HODARIUM-EXCHANGE-v1`，内部 revision transfer 使用 `ATHENA-HODARIUM-REVISION-v1`；`revision_receiver`/`revision_sender` 当前禁止把将来的 `ath-xml-v3` 文档通过 v1 wire 发送和接收。资源和逻辑数据库已有独立版本合同。
- XML v3 存储是 `node_model_version=2`，原生 v3 Reader 只能处理 canonical v3 AST。当前脱机 `--check` / `--stage` 会构建 v3 的**断开式候选**、迁移当前文档、所有文档历史、索引树与保留的 protected evidence。候选 manifest 始终为 `hodarium_protocol_cutover_verified=false`、`eligible_for_atomic_publication=false`。
- 正常 ATHENA 运行时仍拒绝打开 `node_model_version >= 2` 的 Vault。新的 `require_v2_document_vault` 使老 Hodarium worker 不能对 v3 候选捕获、删除、应用或复制旧格式 revision。**这不等于实现了新的跨设备协议。**
- 外部 state 不仅是 device.sqlite 和 revisions-*.sqlite，还包括同 group 的 membership、conflicts、decisions sidecars；离线审计必须发现 WAL、数据库内容修改、损坏的旧**内容寻址** DAG/index、错误 applied/eligible/intent/cache，以及同组绑定和成员年代的不一致。

## 2. 不可妥协的数据不变量

1. **不重写旧证据**：旧 v1 revision 的 ID、原始 payload、descriptor、parents、审批证书、签名和历史 protected bytes 永远不被新格式覆盖。改变其中任一字段会改变内容寻址身份，不能以补写 `id` 字段掩盖。
2. **不暗中改因果链**：v3 的新 revision ID 必须由其新协议 domain 和实际 canonical v3 payload 得到；不能一边用 v3 payload，一边把 v1 `id` 当成 v3 ID 或沿用没有可验证解释的 v1 parent 边。
3. **两个独立 epoch**：Vault 本地格式、Hodarium group 授权代际和文档 wire epoch 不得混为一个数字。成员 recovery/change generation 不代表全体设备已接受 v3 文档格式；普通 `Vaultfile.node_model_version` 也不是跨设备 ack。
4. **先冻结再准备**：在任一设备生成有效 cutover receipt 前，所有相关 Vault 的 live Actor 保存、后台文档/资源/逻辑数据库捕获、下载/应用和冲突决定都须有可恢复的停写边界。单个用户点击 Pause 或一个 WAL checkpoint 不代表其他设备静止。
5. **跨系统决定权**：本地 RENAME_EXCHANGE 可以原子切换一个目录名称，却不能同时原子提交外部 revision SQLite、密钥代际和远端设备。任何 commit 决策须有独立于参与者崩溃的持久来源；已 commit 的设备不允许自行重新激活旧 epoch。
6. **失败必须关闭**：旧设备收到 v3 epoch 的保存/交换请求、v3 设备收到尚未被明确 bridge 的 v1 补发 revision、签名/head 不一致、WAL 未 checkpoint、conflict 未裁决、承诺过期或重复 commit 等，均不得启用编辑或发布；不得自动转 v2 或默默排除该设备。

## 3. 建议的离线事务/协议状态机（需要产品确认）

| 状态 | 单一权限边界 | 可持久化证据 | 崩溃后的安全行为 |
| --- | --- | --- | --- |
| `legacy-active` | v1 wire + XML v2 Vault | 旧内容寻址 DAG、current applied/head、独立会话认证凭据 | 保留全部 v2 权限，禁止 v3 发布 |
| `freeze-proposed` | 组/Vault freeze 提案，尚未确认所有设备 | 各设备身份+generation、单调 nonce、审计参与者集合 | 超时撤销提案，旧状态未改变 |
| `frozen` | 授权设备和本地 Actor/worker 已停写，拒绝新的旧 revision | 所有设备 freeze ack、数据库 WAL=0、revision-head 集合、group state fingerprint | 未作 commit 时只能验证并恢复 freeze 或安全撤销 |
| `prepared` | 私有 v3 Vault、历史/索引迁移完成但未发布 | 每个候选文件/DB digest、ID mapping/bridge digest、old evidence digest、prepare receipt | 复读所有证据；冲突则丢弃候选，不能进入普通运行时 |
| `commit-decided` | 不可逆的全组持久 commit 决议 | 授权签名、多设备 ack、旧/新 epoch ID 和审计 hash | **只允许继续完成或保持隔离**；禁止独自回滚至旧可写态 |
| `activated` | 新 v3 文档协议与本地 v3 Vault 均已启用 | 每设备 durable activation ack、旧 revision 只读引用、post-commit 校验 | 使用同一决议重复修复，不能重算旧 revision |

草案要求决定参与者集合的来源必须是**可认证的当前 group membership**和所有与该 group 相关的 Vault/远端；不能仅枚举当前在线设备。离线成员、已退出成员的仍有效受保护 revision、未完成 apply/incoming 与历史设备证明，均须有明确的冻结或撤销证据。

## 4. 新 revision / 旧 DAG 的桥接（尚未决定具体编码）

- 候选方案是**另起新 revision wire epoch/domain 和新文档 format**，而不是修改 protocol=1 的 hash 函数。候选新 epoch descriptor 必须显式绑定 `group`、`vault`、`member generation`、文档格式、origin/device、旧 DAG 的不可变根证据、迁移 bridge 身份和 v3 parents；具体字段/命名/签名方案尚待评审。
- 迁移器必须对旧 revision graph **全量拓扑遍历**，包括每个历史分支、合并、tombstone、受保护先前版本；把每个旧文档源通过相同严谨 v2→v3 迁移规则转换并生成新 v3 payload，新内容 hash 与 revision ID 单独计算。对历史不可无损迁移或需要消歧的分支，应阻断 cutover，而不是只转换 heads。
- 建立明确、可核验的 `old_revision_id → new_revision_id | preserved_legacy_only` 映射；若历史行无法无损变成新格式，选择 `preserved_legacy_only` 时须定义独立的**可读恢复入口**和跨设备授权协议，不能让普通 v3 runtime 偷偷兼容读取 v2。
- 旧和新 revision 的 ancestry 应通过**已签署的 bridge receipt 或新的 genesis event**显式联系，避免混合两个 hash domain 的 parents 数组。不得将旧 revision 的签名/内容寻址身份重新解释为 v3 的签名。
- 须将所有 `applied`、head 选择、未完成传输、pending intents、冲突审批和历史受保护引用纳入 cutover mapping 或明确拒绝；仅有源 `.ath` 当前字节相等、history.sqlite quick_check=ok 不构成因果证明。

## 5. 事务日志、幂等恢复与撤销窗口

建议独立的 durable cutover coordinator（具体存储位置尚待选定）按 `(group, vault, epoch-id, nonce)` 唯一约束保存 prepare/commit 决议、参与设备、旧/新哈希和完成阶段。每台设备的本地执行记录需要独立的 `prepare-sequence` 与 `commit-decision-id`，不得用内存中的 `paused=true` 充当持久证据。

- 在**全组 durable commit 之前**，候选目录可安全放弃；旧 Vault 保持原状。恢复时重新校验现有 frozen receipt，不可跳过设备 ack。
- 在**全组 commit 已决定之后**，如果设备于 Vault rename、外部 DB 提交或确认回传中途崩溃，它只能从 durable intent/receipt **幂等地继续完成**，或保持隔离等候另一设备恢复。不能因为某个本地换目录失败便解除 freeze 并在旧系统继续签 v1 文档 revision。
- 如果产品需要 commit 后回到 v2，必须定义新一轮有独立 revision 证据的反向 epoch 迁移；**绝不能**把本地备份目录换回来就声称远端已回滚。
- 验证每个成员具有唯一且当前有效的密钥/代际，任何 commit 阶段都要求远端 ack 的 group、vault、epoch、源 head、目标 digest 完全一致；延迟或重复 v1 消息不能被当作新 epoch 的合法载荷。

## 6. 必须通过的离线验收矩阵

| 测试场景 | 必须看到的结果 |
| --- | --- |
| 2 台设备、2 个不同 Vault 同属 1 group，历史 revision DAG 含分支和 merge | 组级 revision 只审计一次；各 Vault 身份单独统计；新旧 DAG 均可复现 |
| 一台设备离线、另一台已 prepare | 不会做永久 commit 或解除 freeze；旧 Vault 无变化 |
| 旧 v1 revision 迟到、分片下载重试/重复包、v3 offer 走 v1 wire | 没有错误提升 v1 hash 为 v3；不建立半封存 revision |
| 旧 heads 与 protected history 涉及被删除 UUID 或真实歧义脚标 | 阻断而不是损失身份，生成可定位诊断 |
| 任何节点在 freeze、prepare、rename、DB transaction、ACK 前后被强制退出 | 每个故障点都有确定的 continuation 或安全废弃；重复启动幂等 |
| 旧 `device.sqlite` 或 group 下 memberships/conflicts/decisions/revisions 的内容、WAL、mtime/指纹变化 | 当前准备证明失效，丢弃私有候选；原 Vault 与外部源数据不受写入 |
| SQLite content-addressed descriptor、parents index、applied/eligible/approval/intent、payload hash 缓存不一致 | 只读审计拒绝；不能用 SQLite FK quick_check 单独取代逻辑校验 |
| 不完整接收、未完成应用、冲突裁决审批、不同步 group membership 或签名验证失败 | 不进入 `commit-decided`，不解禁生产发布 |
| 跨设备激活后普通编辑→保存→同步→历史恢复→Artifact/引用→重启 | 所有 UUID/typed properties、因果 identity 与边、缓存语义稳定一致 |

## 7. 对当前实现的明确限制

现有 `vault_xml_v3_hodarium.cpp` 只完成**本地且只读的证据审计**，没有检查远端网络成员的当前真实性、没有冻结外部写者、没有重新签 v3 revision 或运行跨设备状态机。`vault_xml_v3_history.cpp` 保护的旧 payload 也是本地历史证据，不会自动变成远端已认可的 bridge。任何将 `eligible_for_atomic_publication` 写为 true 的实现，必须同时附上此文的全部验收证据、当前用户确认的 wire 合同和明确批准；在此之前不应修改生产 Vault 或同步配置。
