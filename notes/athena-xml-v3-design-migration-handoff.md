# ATHENA XML v3：规范、原生数学 AST、Vault 迁移与实现交接

> **状态：2026-10-16；面向后续本地 Codex 开发。**
>
> **重要：这是一份“已确定的设计 + 当前工作树真实实现状态 + 待办”文档，不是已发布格式的上线公告。** 仓库中 XML v3 的修改**尚未提交**。本文描述的是当前工作树，不是 HEAD commit（当前 HEAD 是 79f10e55d）。不要仅看 git log 就以为这些代码不存在。
>
> **安全约束：不得写入 /home/felix/data/Notes/Notes Root/；不得对真实 Notes Vault 执行写入式升级；不得未经用户允许提交或推送。** ATHENA 仓库内的 notes/ 是本交接文档的目标，与用户真实的 Notes Vault 完全不同。已有的无关文件 notes/native-menu-command-loss-handoff.md 必须保留。

## 0. 一页结论

ATHENA 是由 TeXmacs 演进出的 C++/Qt 编辑器。本项目不是给 XML v2 换几个较短的 XML tag，而是要同步重构**持久化文档表示、原生数学 AST、编辑/排版/导入导出以及整库迁移和历史同步协议**。

已经确定的核心方向：

1. **唯一规范格式 XML v3；旧格式交给明确调用的离线 v2 → v3 migrator。** v3 Reader 必须直接生成 v3 原生 AST，不用运行时兼容映射，不先生成 around / rsub 等旧树再升级。v2 Reader 作为离线升级器的输入工具独立存在。
2. **物理 XML 结构严格、无重复拼写、无同一概念的两套编码。** 文本始终写为 `<t><v>...</v></t>`；文档、片段和数学结构都有固定形状。
3. **数学公式按词法 token 保存，但不强制完整语义解析。** 比如 A/B 是 A、/、B 三个数学 token，斜线不能被误变为“成对定界符”或自动改为分式。真正上下排列的分式继续保留 FRAC 语义。
4. **自动括号配对是 v3 编辑不变量。** 不持久化孤立 LEFT/RIGHT。定界表达式始终是一棵 DELIMITED，显式给出左右字形与自动/自然/手动尺寸策略。
5. **大算符本身只是 BIG_OPERATOR 的操作数字形**，其上下限由统一五槽 MathScripts 表达，**没有独立 Big Operator 带上下限的另一套结构**。
6. **所有 display formula 都是 MathBlock**；原 eqnarray 应为 MathBlock 的子 MathGrid（eqnarray kind）；公式中多余的单孩子 DOCUMENT 从**原生 AST** 中消失；真正多段正文/enunciation 的 DOCUMENT 不动。
7. **历史和同步属于迁移事务的一部分。** 必须完整迁移历史版本而保留正常历史恢复；保留 Hodarium 受保护快照原始证据，保证外部同步修订、父因果关系和重试/冲突日志一致；不能只改当前 .ath 就宣布整库成功。
8. **普通迁移遇确实有歧义的东西发 warning，--strict 把 warning 当错误。** 严格格式错误、结构丢失、破坏身份/引用的情况本来就直接失败，不能通过宽松模式吞掉。

**目前有成果但不具备上线条件：** 812/812 份 Notes .ath 文件已完成只读的 v2 → v3 → Reader 回读，0 份失败，仅 2 条历史上确实有歧义的 SCRIPT_WITHOUT_SEQUENCE_BASE warning。隔离候选 Vault、历史 Fossil-delta、部分数据库索引转换、原生编辑/排版、LaTeX 与 MathJax HTML 已有代码和测试。**正式 Vault 发布/原子交换仍被硬性禁用，普通 ATHENA 运行时也拒绝打开候选 v3 Vault**；完整 Hodarium 跨设备协议切换尚未实现。

## 1. 项目与设计约束

### 1.1 代码位置

- 项目：/home/felix/data/Software/TeXmacs/texmacs
- 分支：master；当前变更在 dirty worktree，尚未 commit。
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
