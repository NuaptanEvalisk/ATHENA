# ATHENA 跨设备同步：最终数学模型与命名任务

## 给接手者的任务

请为下述数学结构及其软件实现取一个共同的名字。你不需要了解 ATHENA 代码，不需要写代码，也不要重新设计同步协议或替换数学模型。

ATHENA 是一个自由软件的结构化文档与知识工作环境。这里要命名的功能用于同一个人在多台设备间持续工作，例如出门用 iPad，回家继续用 Linux 工作站。它不是以多人协作为中心的产品。

以前暂称 ATHENA Groupoid，但最终模型不是群胚，因此这个名字已经放弃。不要继续沿用。

用户希望产品名称和数学结构名称相同：

- 产品叫 **ATHENA X**。
- 数学上可以说 **an X** 或 **an X structure**，用它命名下面完整的数据与公理，而不只是其中的底层图。
- 用户可以“创建 X”“加入 X”“从 X 移除设备”。
- 两个独立服务程序可以叫 **X Server** 和 **X Relay**。

## 1. 网络语义与研究范围

设备是节点；一条单步连接可以是直接 P2P，也可以经一个 Relay。多个 Relay 可以并存，同一对设备之间可以存在不同通路。

单步 Relay 连接 A -> B 在物理上是 A -> Relay -> B。Relay 不是这里的设备节点，也不是参与同步的成员。首版不做 Relay 到 Relay 的直接转发；多步通路可以经参与同步的设备接续。

Server 与 Relay 必须区分：Server 是中心化的成员资格与 epoch 权威，Relay 只负责传输。它们不是同一种服务器。多个设备仍可经多个 Relay 或 P2P 同步；数据端到端加密。

数学模型描述一个固定网络状态下的可用通信通路。生成图在该状态下有限；随着上下线等事件，实际系统会更新这个状态。有限生成不意味着路径总数有限。

## 2. 底层生成图

取有限有向多重图（quiver）

\[
Q=(V,E,s,t).
\]

- V 是设备集合。
- E 是可用的单步通信连接集合。
- s、t 分别给出连接的起点和终点。
- 允许平行边，也允许自环。
- 每条生成边具有自己的身份；端点和颜色相同不强制它们是同一条边。

令 R 为实际 Relay 的集合，并令

\[
\Sigma=\{p\}\sqcup R,
\]

其中 p 是 P2P 标签。生成边带有标签映射

\[
c_0:E\longrightarrow\Sigma.
\]

这不是要求相邻边颜色不同的 proper graph coloring。

## 3. 自由路径范畴

取 Q 的自由路径范畴

\[
\mathcal P=\operatorname{Path}(Q).
\]

对象是设备，态射是有限的、端点可接续的生成边序列。恒等态射是空路径，复合是路径串接。

全文使用通常的范畴复合记号：g ◦ f 表示先走 f，再走 g；路径的书写序列则按实际行进顺序排列。

**不施加任何把不同边序列等同起来的路径关系。** 尤其：

- 不把往返通信消去为恒等态射。
- 不要求三角形或其他不同路径构成交换图。
- 具有相同端点和相同颜色词的路径仍然可能是不同态射。
- 复合要求中间设备匹配；标签能够串接，不意味着网络中存在对应的可接续路径。

每条态射具有唯一的生成边序列表示，无需像自由群胚那样先约化。

## 4. Dagger Axiom：指定的反向通信

在生成边上指定反向对合 e -> e^dagger，满足

\[
s(e^\dagger)=t(e),\qquad
t(e^\dagger)=s(e),\qquad
(e^\dagger)^\dagger=e,\qquad
c_0(e^\dagger)=c_0(e).
\]

将其延拓到路径：反转边的排列，并把每条边换成其指定的反向边。这给出恒等于对象的反变对合

\[
(-)^\dagger:\mathcal P^{op}\longrightarrow\mathcal P,
\]

满足

\[
1_A^\dagger=1_A,\qquad
(f^\dagger)^\dagger=f,\qquad
(g\circ f)^\dagger=f^\dagger\circ g^\dagger.
\]

这是 dagger category 的结构。它比“每对 Hom 集之间存在某个双射”更明确：反向必须与恒等和复合相容。

**Dagger 不是 inverse。** 对非空路径 f，往返路径 f^dagger ◦ f 不是恒等态射。它真实经过了连接，因此不能免费取消。反向可用也不意味着两个方向的带宽、延迟或代价相同。

最精确的基础描述是“带标签及反向对合的 quiver 的自由路径范畴”。它具有上述 dagger 结构，而不是群胚。

## 5. Relay Axiom：捷径的存在，而不是路径相等

对任意实际 Relay 标签 r ∈ R，若存在生成边

\[
a:A\longrightarrow X,\qquad b:Y\longrightarrow B,
\qquad c_0(a)=c_0(b)=r,
\]

以及任意态射 T:X -> Y，则存在生成边

\[
h:A\longrightarrow B,\qquad c_0(h)=r.
\]

图示为：

```text
A --r--> X --T--> Y --r--> B
implies the existence of A --r--> B.
```

理由是两端都能通过同一 Relay 接入通信，已有中间绕路不妨碍使用该 Relay 的单步通路。这里的可用性是在同一通信权限与网络状态下讨论的。

关键约束：

- T 可以为空路径，也可以有任意纯色或混合颜色。
- h 必须是单步生成连接，而不只是某条颜色为 r...r 的路径。
- **绝不要求 h = b ◦ T ◦ a。** 新的单步连接和原多步通路是不同态射。
- 公理只对 r ∈ R 成立，不对 P2P 标签 p 成立。
- 公理约束哪些生成连接必须存在，不对自由路径范畴添加商关系。

因此，对一个固定 Relay r，仅保留 r 色生成边得到的图，其每个连通分量都是完全的（忽略方向看连通性；反向边由 dagger 提供）。一般的 P2P 图没有这个性质。

严格按上述全称公理，A=B 也包含在内：经 Relay 出去再回来的情形要求存在一个 r 色自环。它是非空生成边，不是恒等态射。这不会破坏自由性。

## 6. Color Functor：路径到颜色词

令 Sigma* 为字母表 Sigma 上的自由幺半群，单位元是空词 epsilon。**不对词施加幂等、消去或捷径关系。**

把它视为单对象范畴 W。为了与实际行进顺序一致，明确约定：对词 u、v，

\[
v\circ u:=uv.
\]

因此，“先 u 后 v”的结果书写为 uv。

生成边标签唯一延拓为函子

\[
C:\mathcal P\longrightarrow\mathcal W,
\]

将所有对象映到唯一对象，将路径映到其按行进顺序排列的颜色词。于是

\[
C(1_A)=\epsilon,\qquad
C(g\circ f)=C(f)C(g).
\]

在 W 上令 dagger 为词反转，每个单字母保持不变。这样 C 还是一个 dagger functor：

\[
C(f^\dagger)=\operatorname{reverse}(C(f)).
\]

进一步定义：

- 路径长度为颜色词长度，即生成连接数。
- 非空词只含一种字母时称为纯色，含至少两种不同字母时称为混合颜色。
- 空词单独代表不通信的恒等路径，不必把它归入纯色。

必须保持的区别：

\[
p\ne\epsilon,\qquad pp\ne p,\qquad rr\ne r.
\]

最后一个不等式与 Relay 公理没有冲突：公理提供一条较短的连接，但不等同两条不同路径。Color functor 只标记已有路径，不凭空产生路径，也不负责化简。

## 7. 整体结构与实际同步的关系

待命名的是上述完整结构：指定的生成图、反向对合、标签及其路径范畴，并满足 Relay 公理。不能只把它叫作 quiver，仿佛复合、dagger 和颜色词不存在。

这个结构描述通路，不独自保证数据一致性、冲突解决或成员认证。实现另外负责这些事情：

- 中心 Server 管理 Admit、Expel、成员状态和 epoch。
- 设备经实际通路进行端到端加密同步，Relay 不获得文档明文。
- 通路由自动测速选择，而非要求用户挑选；同色并不意味着同速。
- 设备保存同步副本；例如 A 同步给 B 后离线，B 仍可把内容同步给 C。
- 冲突由用户解决，解决决定传播；破坏性更新与 ATHENA 文件历史衔接。

用户喜欢“多个局部设备连接成连续的工作整体”的意象。Manifold 的局部拼接可以作为命名灵感，但本模型没有局部欧氏性、坐标图或拓扑粘合公理，不要声称它就是一种流形。

## 8. 命名要求与禁区

### 硬性要求

1. 同一个词同时作为软件产品名和上述数学结构的新名称。
2. 不叫 Groupoid；不要用名称暗示本模型具有非平凡态射的可逆性。
3. 候选不能挪用他人的商标，也不能与已有项目容易混淆。大小写差异、一个字母差异、同音或近似拼写都需要考虑，尤其是同步、备份、开发工具和笔记软件领域。
4. 必须进行实际检索，不能凭“好像没听过”声称名字没人用。不要把 ATHENA 前缀当作解决撞名的充分手段。
5. 喜欢有数学意味的造词，不喜欢过于企业化、泛泛的 “Sync Service” 一类命名。不要求一定用 -oid 后缀。
6. 新词可以由我们明确赋予数学定义，但不能伪称已有标准数学术语。

### 已讨论过的候选

- **ATHENA Groupoid**：放弃，数学结构不符。
- **Synchoid**：与已有的 ZFS 同步工具 **Syncoid** 拼写和用途过近，不推荐。
- **Synquiver**：只强调底层 quiver，用户追问它是否准确描述完整模型；不要未经解释就重新推荐。
- **Networkoid**：讨论过，尚未选定。
- **Handoffoid**：讨论过，尚未选定。意象来自设备间继续工作，但本产品不限于一次交接。
- **Continuoid**：讨论过，尚未选定。初步网页检索没发现明显同类软件不等于商标可用。
- **Syncfold**：用户喜欢 sync + manifold 的拼接意象，但已经发现同名安卓文件夹同步项目，因此不推荐。它也作为 syncFold 出现在程序设计研究中。

不要仅回复“任何名字都有可能撞车”，也不要未经检索继续抛出大量名字。请筛选出少量真正值得考虑的候选。

### 检索与交付

对最终候选检查精确拼写、近似拼写、读音，以及 GitHub、常见软件包仓库和公开商标检索结果。商标检索注明数据库、司法辖区、类别或查询范围及日期。

**“不能是他人的商标”是筛选要求，不是允许你作出无根据的法律保证。** 无法完成全球商标排查时，明确说明覆盖范围；不要把网页无结果写成“没有商标”。如果有明显冲突，淘汰并寻找下一候选，而不是把核查全部推给用户。

最终给出 3–5 个经过筛选的候选，每个包含：

- 拼写、建议读音、构词来源。
- 为什么同时适合产品和数学结构。
- “ATHENA X”“an X”“X Server”“X Relay”的实际用法。
- 同名和近名检索结果、可复查的来源链接、仍未核实的范围。
- 明确的排序与首选理由，不替用户自动定名。

## 9. 参考入口

- Dagger 与自由构造：[DisCoPy 文档](https://docs.discopy.org/en/interaction-readme/notebooks/diagrams.html)。
- 路径范畴与词幺半群：[Graphs are to categories as lists are to monoids](https://alhassy.com/PathCat)。
- Colored category 相关研究：[A topos associated with a colored category](https://arxiv.org/abs/1611.07246)。这个宽泛术语不替代本文的具体定义。
- Syncoid 撞名来源：[Sanoid / Syncoid 项目](https://github.com/jimsalterjrs/sanoid)。
- SyncFold 同领域撞名来源：[安卓文件夹同步项目介绍](https://forum.trae.cn/t/topic/39636)。
- syncFold 既有研究用法：[Iterating on multiple collections in synchrony](https://www.cambridge.org/core/journals/journal-of-functional-programming/article/iterating-on-multiple-collections-in-synchrony/E1E868AA95761C15F9896238442D790B)。

本文是供命名任务使用的数学与产品契约。不要因为旧设计文件仍含 Groupoid 等历史措辞而覆盖这里的最终模型，也不要修改仓库或重新生成设计 PDF。
