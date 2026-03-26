#include <peglib.h>

// A-OFM (Academic Obsidian-Flavored Markdown) PEG Grammar
// 专为 cpp-peglib 设计，精准适配 A-OFM 的非标准特性与闭包边界。
const char* aofm_grammar = R"(
    # ------------------------------------------------------------------
    # 根节点与结构
    # ------------------------------------------------------------------
    Document        <- YAMLFrontmatter? Block* EOF
    EOF             <- !.
    
    # Block 级元素，按照优先级进行有序选择
    Block           <- BlankLine* (
                         Heading / 
                         HorizontalRule / 
                         CodeBlock / 
                         MathBlock / 
                         Callout / 
                         Blockquote / 
                         Table / 
                         List / 
                         HTMLCommentBlock /
                         AnchorBlock /
                         Paragraph
                       )

    # ------------------------------------------------------------------
    # 核心 Block 定义
    # ------------------------------------------------------------------
    YAMLFrontmatter <- '---' NL < (!'---' .)* > '---' NL
    
    Heading         <- < '######' / '#####' / '####' / '###' / '##' / '#' > ' '* < (!NL .)+ > NL
    
    HorizontalRule  <- '---' '-'* [ \t]* NL
    
    CodeBlock       <- '```' CodeLang NL < (!'```' .)* > '```' [ \t]* NL
    CodeLang        <- < [a-zA-Z0-9_+#-]* >
    
    MathBlock       <- '$$' NL < (!'$$' .)* > '$$' [ \t]* NL
    
    HTMLCommentBlock<- '<!--' < (!'-->' .)* > '-->' [ \t]* NL
    
    # 独立的锚点块 (空行后的锚点)
    AnchorBlock     <- '^' AnchorID NL

    # ------------------------------------------------------------------
    # Callout (学术扩展严格校验)
    # ------------------------------------------------------------------
    Callout         <- CalloutHeader CalloutLine*
    CalloutHeader   <- '>' [ \t]* '[!' CalloutBase ']' FoldFlag? [ \t]* CalloutExt? [ \t]* CalloutTitle? NL
    CalloutBase     <- 'abstract' / 'note' / 'summary' / 'tldr' / 'info' / 'todo' / 'tip' / 'hint' / 'important' / 
                       'success' / 'check' / 'done' / 'question' / 'help' / 'faq' / 'warning' / 'caution' / 
                       'attention' / 'failure' / 'fail' / 'missing' / 'danger' / 'error' / 'bug' / 'example' / 
                       'quote' / 'cite'
    # 严格匹配 A-OFM 允许的扩展类型
    CalloutExt      <- 'Definition' / 'Example' / 'Question' / 'Conjecture' / 'Theorem' / 'Proposition' / 
                       'Lemma' / 'Corollary' / 'Axiom' / 'Remark' / 'Alternative Proof' / 'Caution' / 
                       'Standard Steps' / 'Law' / 'Paster' / 'Disambiguation'
    FoldFlag        <- '+' / '-'
    CalloutTitle    <- < (!NL .)+ >
    CalloutLine     <- '>' [ \t]* < (!NL .)* > NL

    # ------------------------------------------------------------------
    # Blockquote (层叠捕获，内部留给 AST 后处理解析)
    # ------------------------------------------------------------------
    Blockquote      <- ('>' !([ \t]* '[!') [ \t]* < (!NL .)* > NL)+

    # ------------------------------------------------------------------
    # 列表 (支持紧随特判)
    # ------------------------------------------------------------------
    List            <- ListItem+
    ListItem        <- ListPrefix LineContent NL TightContinuation*
    
    # 紧随特判：后面是内容行，且不能以空白行或新的列表前缀开头
    TightContinuation <- !BlankLine !ListPrefix !BlockStart LineContent NL
    
    ListPrefix      <- [ \t]* ('-' / [0-9]+ '.') ([ \t]+ '[' [ x] ']')? [ \t]+

    # ------------------------------------------------------------------
    # 表格 (无需严格对齐的通用 Markdown 表格)
    # ------------------------------------------------------------------
    Table           <- TableHeader TableSeparator TableRow+
    TableHeader     <- (TableCell '|' TableCell ('|' TableCell)* / '|' TableCell ('|' TableCell)*) '|'? NL
    TableSeparator  <- [ \t]* '|'? [ \t:]* '-'+ [ \t:]* '|' ([ \t:]* '-'+ [ \t:]* '|'?)* NL
    TableRow        <- (TableCell '|' TableCell ('|' TableCell)* / '|' TableCell ('|' TableCell)*) '|'? NL
    TableCell       <- [ \t]* < (!'|' !NL .)* > [ \t]*

    # ------------------------------------------------------------------
    # 段落 (纯文本及行内元素的聚合)
    # ------------------------------------------------------------------
    Paragraph       <- ParagraphLine+
    ParagraphLine   <- !BlankLine !BlockStart Inline+ NL
    
    # 遇到这些符号意味着段落结束，新块开始
    BlockStart      <- Heading / HorizontalRule / CodeBlock / MathBlock / CalloutHeader / Blockquote / ListPrefix

    # ------------------------------------------------------------------
    # 行内元素 (Inline)
    # ------------------------------------------------------------------
    Inline          <- Image / PDF / Transclusion / WikiLink / ExtLink / 
                       InlineMath / InlineCode / 
                       TripleBoth / TripleItalicOuter / TripleStrongOuter / 
                       TripleRightItalic / TripleRightStrong / 
                       Strong / Italic / Strikethrough / Highlight / 
                       EscapeChar / InlineAnchor / Text / AnyChar

    AnyChar         <- < !NL . >

    # ------------------------------------------------------------------
    # 链接、嵌入与多媒体
    # ------------------------------------------------------------------
    Image           <- '![[' LinkTarget '.' ImageExt ('|' ImageSize)? ']]'
    ImageExt        <- 'png' / 'jpg' / 'jpeg' / 'bmp' / 'svg'
    ImageSize       <- < [0-9]+ >

    PDF             <- '![[' LinkTarget '.pdf' ('|' Alias)? ']]'

    Transclusion    <- '![[' LinkTarget ('#' SubTarget)? ('|' Alias)? ']]'
    WikiLink        <- '[[' LinkTarget ('#' SubTarget)? ('|' Alias)? ']]'
    
    LinkTarget      <- < (!'#' !'|' !']]' .)+ >
    SubTarget       <- '^' AnchorID / < (!'|' !']]' .)+ >
    Alias           <- < (!']]' .)+ >
    
    ExtLink         <- '[' < (!']' .)+ > '](' < (!')' .)+ > ')'

    # ------------------------------------------------------------------
    # 基础格式与特化重载定界符
    # ------------------------------------------------------------------
    InlineMath      <- '$' < (!'$' .)+ > '$'
    InlineCode      <- '`' < (!'`' .)+ > '`'
    Strikethrough   <- '~~' < (!'~~' .)+ > '~~'
    Highlight       <- '==' < (!'==' .)+ > '=='

    # 解决非对称嵌套问题
    TripleBoth        <- '***' < (!'***' .)+ > '***'
    TripleItalicOuter <- '***' < (!'**' .)+ > '**' < (!'*' .)+ > '*'
    TripleStrongOuter <- '***' < (!'*' .)+ > '*' < (!'**' .)+ > '**'
    TripleRightItalic <- '*' < (!'**' .)+ > '**' < (!'***' .)+ > '***'
    TripleRightStrong <- '**' < (!'*' .)+ > '*' < (!'***' .)+ > '***'
    Strong            <- '**' < (!'**' .)+ > '**'
    Italic            <- '*' < (!'*' .)+ > '*'

    # ------------------------------------------------------------------
    # 锚点、转义与兜底文本
    # ------------------------------------------------------------------
    InlineAnchor    <- ' ^' AnchorID
    AnchorID        <- < [a-zA-Z0-9\-_]+ >
    
    EscapeChar      <- '\\' < [\\*_#`|~$] >
    
    # 兜底字符：吞噬所有非特殊控制符的纯文本片段
    # 不再在此处排除 BlankLine，由 ParagraphLine 的头部判定和 NL 终止符自然界定
    Text            <- < (!BlockStart !'![[' !'[[' !'[' !'$' !'`' !'***' !'**' !'*' !'~~' !'==' !' ^' !'\\' !NL .)+ >
    LineContent     <- < (!NL .)* >
    BlankLine       <- [ \t]* NL
    NL              <- '\r\n' / '\r' / '\n'
)";
