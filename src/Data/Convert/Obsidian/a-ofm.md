# Spec: Academic Obsidian-Flavored Markdown (A-OFM)
## 概述
1. Obsidian-Flavored Markdown (OFM) 是一种 Markdown 方言, 稍加裁剪和扩展后, 我们称作 Academic OFM (AOFM). 本文档定义了这种语言.
2. 必须注意的是, 它是一种 vendor-specific 的方言, 和通常的 CommonMark 或者 GitHub-Flavored Markdown **不兼容**, 也**不能**看作它们的扩展. 此文档是你关于 A-OFM 的唯一知识来源.
3. 为了和代码块分别开, 这个文档中我们用这样的写法表达例子:
@@@@@@@@@@@@@@
这是例子的内容.
@@@@@@@@@@@@@@
很明显, 这种写法**仅限于**本文档, 它不是 OFM 的组成部分.

## 语法
### 段落
空行用来分别段落. 例如
@@@@@@@@@@@@@@
This is a paragraph.
Still within this paragraph.

This is another paragraph.
@@@@@@@@@@@@@@

### 标题
采用基本的 Markdown 标题语法, 不要求标题前后空行. 例如
@@@@@@@@@@@@@@
# This is a heading 1
## This is a heading 2
### This is a heading 3
#### This is a heading 4
##### This is a heading 5
###### This is a heading 6
@@@@@@@@@@@@@@
在 A-OFM 中, 文件名通常被当做 "0级标题", 也就是最高级的标题. 因此 # Heading 相当于二级节点, 如果最高级的文件名算做一级节点的话.

### 基础标记
例子:
@@@@@@@@@@@@@@
**bold text**
*italic text*
***bold and italic text***
~~striked out text~~
==highlighted text==
**bold text and _nested italic_ text**
**bold text and *nested italic* text**
@@@@@@@@@@@@@@
不支持其它写法. 我们用 \* 表示 * 的转义字符.

### 外部链接
遵从如下的语法:
@@@@@@@@@@@@@@
[link text](https://link.com)
@@@@@@@@@@@@@@
链接地址中的空格用 "%20" 表示.

### Blockquote
例子: 
@@@@@@@@@@@@@@
> quote text
> in same quote

outside the blockquote.
@@@@@@@@@@@@@@
Blockquote是可以层叠的:
@@@@@@@@@@@@@@
> quote text
>> nested quote
> > still in nested quote
>
> in first-level quote
@@@@@@@@@@@@@@

### 列表
只支持 - 开头的无序列表:
@@@@@@@@@@@@@@
- First list item
- Second list item
- Third list item
@@@@@@@@@@@@@@

支持如下格式的有序列表:
@@@@@@@@@@@@@@
1. First list item
2. Second list item
3. Third list item
@@@@@@@@@@@@@@

支持任务列表:
@@@@@@@@@@@@@@
- [x] This is a completed task.
- [ ] This is an incomplete task.
@@@@@@@@@@@@@@

支持缩进控制的层叠列表:
@@@@@@@@@@@@@@
1. First list item
   1. Ordered nested list item
2. Second list item
   - Unordered nested list item
@@@@@@@@@@@@@@

A-OFM 的列表项内容判定同时支持以下两种模式：
标准缩进判定: 依靠严格的空格缩进将后续段落或代码块归属于特定列表项。
紧随特判 (无缩进续接) :当一个无缩进的段落紧跟着一个列表项, 中间无空行, 时，该段落特例判定为从属于紧邻的上方列表项. 例如:
@@@@@@@@@@@@@@
1. something
Blablabla
@@@@@@@@@@@@@@
那么 "Blablabla" 这个段落虽然没有缩进, 但也是从属于 "1. something" 的.

### 横线
只支持如下的水平横线:
@@@@@@@@@@@@@@
---
----
@@@@@@@@@@@@@@
不支持其它写法.

### 代码
行内代码用通常的反引号写法:
@@@@@@@@@@@@@@
Text inside `backticks` on a line will be formatted like code.
@@@@@@@@@@@@@@

使用3个连续的反引号来表达代码块, 不支持其它写法.
@@@@@@@@@@@@@@
```
const int width = runtime->surface->width();
```
@@@@@@@@@@@@@@

语言名称必须紧跟在反引号后面:
@@@@@@@@@@@@@@
```c++
sptr<tex::TextLayout> tex::TextLayout::create(const std::wstring &src,
                                              const sptr<tex::Font> &font)
{
  return std::make_shared<SkiaTextLayout>(src, font);
}
```
不支持代码块的嵌套. 

### 注释
支持 HTML 注释:
@@@@@@@@@@@@@@
<!-- comment -->
@@@@@@@@@@@@@@
不支持其它写法.

### 数学公式
使用 $ 包裹行内数学公式:
@@@@@@@@@@@@@@
This is an inline math expression $e^{2i\pi} = 1$.
@@@@@@@@@@@@@@

用 $$ 包裹行间数学公式:
@@@@@@@@@@@@@@
$$
\begin{align}
\tilde \phi: TU&\longrightarrow\phi(U)\times\mathbb R^n\\
v&\longmapsto(x^1(p),\cdots,x^n(p),c^1(v),\cdots,c^n(v))=(\underbrace{\overline{x}^1,\cdots,\overline{x}^n}_{\text{base point}},\underbrace{c^1,\cdots,c^n}_{\text{direction}})(v)
\end{align}
$$
@@@@@@@@@@@@@@

支持 \boxed 写法. 放在了 $$ ... $$ 中的 align 环境, 当做 aligned 处理. 

### 转义字符:
支持这些转义字符: \* \_ \# \` \| \~ \$

### 锚点
一个 A-OFM 的锚点, 是一个形如 "^" 开头的字符串. 比如, 下面列举了一些锚点:
@@@@@@@@@@@@@@
^123456
^abc123
^12-22-33-uu-X4
^Hello
@@@@@@@@@@@@@@

锚点中允许 A-Z, a-z, 0-9, 横线 "-", 下划线 "_". 段落结尾的锚点指向这个段落:
@@@@@@@@@@@@@@
paragraph not pointed to by the anchor.

paragraph pointed to by the anchor. ^123456

paragraph not pointed to by the anchor.
@@@@@@@@@@@@@@

锚点出现在一个段落下方 (空一行) 时, 指向这个段落. 比如
@@@@@@@@@@@@@@
paragraph not pointed to by the anchor.

paragraph $x^2$ pointed to by the anchor. 

^abcdef
@@@@@@@@@@@@@@

锚点出现在代码块, 行间公式下方时, 指向它们, 不指向它们上面的段落, 即使中间没有空行.
@@@@@@@@@@@@@@
paragraph not pointed to by the anchor.
$$
\text{I am pointed to by the anchor.}
$$

^cafe1234
@@@@@@@@@@@@@@

锚点出现在结构块 (callout (下面会定义), blockquote) 后, 指向这整个结构块.
@@@@@@@@@@@@@@
> block
> quote

^anchor
@@@@@@@@@@@@@@

锚点出现在标题下方, 指向这个标题, 比如:
@@@@@@@@@@@@@@
# Heading

^anchor-to-heading
@@@@@@@@@@@@@@

### 表格
支持通常的 Markdown 表格语法:
@@@@@@@@@@@@@@
| First name | Last name |
| ---------- | --------- |
| Max        | Planck    |
| Marie      | Curie     |
@@@@@@@@@@@@@@

表格不需要对齐, 比如: 
@@@@@@@@@@@@@@
First name | Last name
-- | --
Max | Planck
Marie | Curie
@@@@@@@@@@@@@@

### 内部链接
"内部" 是相对于 vault 而言的. 只支持 Wikilink 的写法. 例如:
@@@@@@@@@@@@@@
here is a [[link to]] a file.
@@@@@@@@@@@@@@
这是对 "link to.md" 这个文件的链接. 关于如何找到这个文件, 参阅本文档后面的 "A-OFM 文件组织" 部分.

如果这个文件中有 A Heading 这个标题, 无论层次如何, 
@@@@@@@@@@@@@@
here is a [[link to#a heading]].
@@@@@@@@@@@@@@
都包含对这个标题的链接. 链接不区分大小写, 同名标题则指向第一个匹配的.

@@@@@@@@@@@@@@
here is a [[link to#aheading|with ALIAS]].
@@@@@@@@@@@@@@
这还是对上述的标题的引用, 但是链接文本显示为 "with ALIAS". 当然, 这也是合法的:
@@@@@@@@@@@@@@
here is a [[link to|a file]].
@@@@@@@@@@@@@@
显示为 "a file".

要链接到锚点, 可以用这样的语法:
@@@@@@@@@@@@@@
here is a [[link to#^ab1234]] a file.
@@@@@@@@@@@@@@
这表示链接到 "link to.md" 文件中的 ab1234 锚点. 当然, 这也是合法的:
@@@@@@@@@@@@@@
here is a [[link to#^ab1234|LINK TO]] a file.
@@@@@@@@@@@@@@
链接显示为 "LINK TO". 

### Transclusion
Transclusion 表达交叉引用, 它的形式和 Wikilink 相仿:
@@@@@@@@@@@@@@
![[some file]]
![[some file#heading]]
![[some file#^anchor]]
@@@@@@@@@@@@@@
都是合法的链接. 下面的链接也是合法的, 但是"|"后面的部分被忽略.
@@@@@@@@@@@@@@
![[some file|some thing]]
![[some file#heading|some thing]]
![[some file#^anchor|some thing]]
@@@@@@@@@@@@@@

当 transclusion 出现在段落中, 比如
@@@@@@@@@@@@@@
a paragraph with a ![[transclusion]] inside.
@@@@@@@@@@@@@@
这时效果和
@@@@@@@@@@@@@@
a paragraph with a
![[transclusion]]
inside.
@@@@@@@@@@@@@@
相当. 逻辑上, ![[transclusion]] 包含在段落中, 也就是下面的锚点指向的是整个的包含transclusion的内容.
@@@@@@@@@@@@@@
this paragraph with a
![[transclusion]]
inside is pointed to by an anchor.

^some_anchor_12
@@@@@@@@@@@@@@

### Callout
Callout 是一种 A-OFM 支持的结构块. 这是一个基本的例子:
@@@@@@@@@@@@@@
> [!info] Here's a callout title
> Here's a callout block.
> It supports **Markdown**, [[Internal link|Wikilinks]]!
> ![[Engelbart.jpg]]
@@@@@@@@@@@@@@

在右中括号 "]" 后的 + - 号表示 callout 默认不被折叠和被折叠, 比如,
@@@@@@@@@@@@@@
> [!faq]- Are callouts foldable?
> Yes! In a foldable callout, the contents are hidden when the callout is collapsed.
@@@@@@@@@@@@@@

支持 callout 的嵌套:
@@@@@@@@@@@@@@
> [!question] Can callouts be nested?
> > [!todo] Yes!, they can.
> >>[!example]  You can even use multiple layers of nesting.
@@@@@@@@@@@@@@

这样的基本 callout 有如下的类型, 上面的 info, faq, question 等都是例子.
abstract
note
summary
tldr
info
todo
tip
hint
important
success
check
done
question
help
faq
warning
caution
attention
failure
fail
missing
danger
error
bug
example
quote
cite

在 A-OFM 中, 为了方便学术写作, 还支持进阶的 callout 用法. 考虑这个例子: 
@@@@@@@@@@@@@@
>[!abstract] Definition **Some definition** Cat
> callout content
@@@@@@@@@@@@@@

这里, 基本类型是 "abstract", 扩展类型是 "Definition", 实际上应该按照 "Definition" 来处理. 这里的粗体 **Some definition** 应当被理解为这个 callout 的标题, 而不是普通的粗体. 单独的 "Cat" 也是标题的一部分, 但是呈现上应和 "Some definition" 区别开来.

支持并且只支持如下的扩展 callout 类型:
>[!abstract] Definition
>[!example] Example
>[!question] Question
>[!question] Conjecture
>[!note] Theorem
>[!note] Proposition
>[!note] Lemma
>[!note] Corollary
>[!abstract] Axiom
>[!note] Remark
>[!done] Alternative Proof
>[!caution] Caution
>[!done] Standard Steps
>[!note] Law
>[!cite] Paster
>[!tip] Disambiguation

"只支持" 的意思是, >[!abstract] Theorem 是错误的写法. 

### 图片 和 PDF.
A-OFM 支持插入 png, jpg, jpeg, bmp, svg 格式的图片. 关于如何找到图片, 参阅本文档后面的 "A-OFM 文件组织" 部分. 例如:

@@@@@@@@@@@@@@
we include an ![[image.png]] here.
@@@@@@@@@@@@@@

必须写出图片的扩展名. 可以指定缩放大小, 比如:
@@@@@@@@@@@@@@
we include an ![[image.png|400]] here.
@@@@@@@@@@@@@@

这里 400 是按照比例缩放的宽度. 只支持这一种缩放方式. 

A-OFM 也支持插入 PDF. 方法和插入图片相同:
@@@@@@@@@@@@@@
we include an ![[image.pdf]] here.
@@@@@@@@@@@@@@

PDF 不能缩放. 不支持插入其它文件格式.

### YAML 数据
A-OFM 允许文档头部携带 YAML 数据, 比如:
@@@@@@@@@@@@@@
---
banner: '[[banner/CHCL 05 Chern Classes and Pontrjagin Classes.png]]'
---
@@@@@@@@@@@@@@
这里, 如果能解析为 YAML 数据, "---" 就不充当横线了. 两个 "---" 中间必须是符合 YAML 语法的 YAML. 这些数据是给外部程序用的, 本身在 A-OFM 中不起到作用. 

## 其它说明
### A-OFM 文件组织
为了清楚的说明, 假设我们有这样的文件结构:
vault/ ---- dir1/  ---- dir2/ ---- a.md
         |           |          |
         |           |          -- b.md
         |           --- a.md
         ---- b.md
         ---- c.md
上面插入 Wikilink, transclusion, 图片, PDF 用的, 都是文件字符串, 比如这样的都是文件字符串:
image.png
some.md
some
folder/some.md
folder/some
这里, .md 扩展名可以省略, 其它扩展名不能省略, 用 / 表示目录. 比如 ![[image.png|400]] 中的 "image.png" 就是一个文件字符串.

下面介绍用文件字符串如何找到文件. 首先, vault 目录 vault/ 相当于虚拟的根目录, 所以这样的文件字符串显然没有歧义:
/b.md
/dir1/dir2/b
这样的 "绝对路径", 开头的 "/" 不能省略.

如果开头没有 "/", 那就不是绝对路径. 首先设一个文件字符串仅仅包括文件名, 比如 "b" 或者 "b.md", 我们用 "从本地开始, 先向前再向后, BFS" 的方法找文件. 比如, 假设在 a.md 中出现了 [[b.md]] 这个链接, "从本地开始", 首先看当前的 dir1/ 目录, 这里没有 b.md 所以 "向前", 一级子目录 dir2/ 有 b.md 所以指的就是它. 如果向前找不到, 比如在 vault/dir1/dir2/a.md 中的 [[c.md]], 就得 "向后" 进行 BFS, 就近的匹配是 vault/c.md 这个文件. 如果在一个文件, 比如 vault/dir1/a.md 中出现 [[a.md]] 这种, 那就是它自身这个文件.

如果形如 "dir1/a.md" 这种, 首先看是不是相对本目录能够找到. 比如在 vault/dir1/a.md 中, 相对与本目录 vault/dir1, 展开后 vault/dir1/dir1/a.md 不存在, 不能找到, 于是就默认是相对于根目录 vault/ 的, vault/dir1/a.md 这个文件. 例如, 在 vault/dir1/a.md 中, dir2/a 就是 vault/dir1/dir2/a.md 这个文件.

对图片和 PDF 的寻找, 原理和上述是一样的, 只不过扩展名不能省略而已.

上面的简单描述没有包括的 corner case, 更具体的用这个算法. 以下为 pseudocode:
```
# A-OFM 内部链接解析算法的标准伪代码
# 参数 link_name: 链接中的文件名字符串 (如 "c.md" 或 "c")
# 参数 current_dir: 当前文件所在的绝对路径目录
def resolve_internal_link(link_name, current_dir):
    # 阶段 1: 向前 - 在当前目录及其子树中进行标准 BFS
    match = bfs_search(link_name, current_dir)
    if match: 
        return match

    # 阶段 2: 向后 - 逐级向上回溯到 vault 根目录，并对旁支进行 BFS
    prev_node = current_dir
    curr_node = get_parent_dir(current_dir)

    while curr_node is not null:
        # 1. 检查父目录本身
        if contains_file(curr_node, link_name):
            return join_path(curr_node, link_name)

        # 2. 对父目录下的其他旁支（排除刚刚查找过的前进分支）进行 BFS
        for sibling in get_subdirectories(curr_node):
            if sibling != prev_node:
                match = bfs_search(link_name, sibling)
                if match: 
                    return match

        # 3. 继续向上一级回溯
        prev_node = curr_node
        curr_node = get_parent_dir(curr_node)

    return null # 遍历整个 vault 仍未找到

# 辅助函数：标准的广度优先搜索
def bfs_search(link_name, start_dir):
    queue = [start_dir]
    while queue is not empty:
        node = queue.pop_front()
        if contains_file(node, link_name):
            return join_path(node, link_name)
        # 保证按目录名/系统默认顺序将子目录压入队列
        for child_dir in get_subdirectories(node):
            queue.push_back(child_dir)
    return null
```
