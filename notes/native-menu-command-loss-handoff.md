# ATHENA Native Menubar / Toolbar Command Loss: Implementation Handoff

## 给接手 ChatGPT 的任务

你已经了解 ATHENA。本任务是修复 Scheme 菜单迁移到 native C++ / JSON 后丢失的大量菜单命令及工具栏入口，不是重新设计整个 application shell。

用户发现 Insert 连插入文档 title 都没有，要求调查并完整修复。调查已确认不是单个命令问题：菜单迁移遗漏、按命令 ID 前缀错误归组、metadata-sensitive 空文本判断，以及 menubar 未消费声明式条件叠加。请从当前工作树继续，不要仅补一个 Title 按钮就宣告完成。

用户最后要求上一位代理停止实现，节省 quota，只写本交接。所有三个子代理均已确认停止；没有构建或提交这轮菜单修改。

## 当前状态与边界

- 仓库：`/home/felix/data/Software/TeXmacs/texmacs`，分支 `master`。
- 当前 HEAD：`a8aa27fc070edccb9865c7897c8b215af7b47fa0`，`fix: create canonical source documents from the vault explorer`。这是此前 Vault Explorer 新建文件修复，已经构建部署并提交，不属于待修菜单任务。
- 当前已写但未提交、未构建的菜单修改只有两处：
  - `src/Edit/Interface/edit_interface.cpp`：三处 `== tree("")` 改为 `== ""`。
  - 新文件 `ATHENA/misc/ui/editor-document-insert.json`：两个 title/abstract 命令与 `insert-document-header` item group。
- 新 JSON **尚未接入 loader，当前不会生效**；其 `item_groups` 也是拟议的新机制，loader 目前不支持。该草稿还缺现有扩展 schema 要求的 `toolbars: []`。请完善而非当成完成品。
- `editor-format-commands.json` 尚未创建；`native_editor_actions.cpp` 尚未修改。
- 不写用户的 `/home/felix/data/Notes`，不重启其正在使用的 ATHENA，不对其进程使用 GDB。
- 不过度测试，不跑全量测试。完成较大完整块后再统一构建及定向验证。
- 先读仓库 `AGENTS.md`。菜单数据应在 JSON；执行、类型验证及 actor ownership 在 C++，不要把菜单长列表搬进 C++，也不要把任意执行逻辑塞进 JSON。

## 已确认的迁移背景

相关历史提交：

- `aa283786a`：native mode toolbar cutover。
- `5a83cbeb6`：native Focus toolbar cutover。
- `2d9b5fe41`：2026-10-02 删除旧 Scheme 菜单，涉及 50 个文件，约 6823 行删除。

可直接读取删除前的源文件，不要恢复整个旧 Scheme 系统：

```bash
git show 2d9b5fe41^:ATHENA/progs/generic/format-menu.scm
git ls-tree -r --name-only 2d9b5fe41^ ATHENA/progs
```

现有菜单注册验证检查“声明的命令存在”，并不验证“旧功能全部迁移”。因此 native registry 能正常加载不等于菜单完整。

## 根因一：用工具栏命令目录和 ID 前缀拼装 menubar

`src/Subsystems/Qt/QTMCommandRegistry.cpp` 的 `registerBuiltins` 内有 `registerCommandListProvider`，枚举 `commands_`，按 ID 前缀归组并过滤 `state.available`。

- `menubar-insert-commands` 接受 `editor.insert.*`、`editor.text.*`、`editor.math.*`（排除 math.static）。
- `menubar-format-commands` 接受 `editor.format.*`、`editor.prog.*`。

所以已有的 `editor.math.format.*` 被放到 Insert，不在 Format。现存命令包含 inline/display、level0/1/2、spacing-normal/condensed。应复用，不能重复定义。

`ATHENA/misc/ui/application-shell.json`：

- Insert 目前主要靠上述 provider，加 Program languages、Math symbols。
- Format 主要靠 format provider，加 text/prog/math color、prominent spacing、semantic annotations。
- Focus 有自己的显式 provider 组合，不是完全没有迁移。

修复应在 JSON 中明确菜单层级与成员，菜单和工具栏引用同一 command ID。不要继续靠 C++ 前缀过滤表达完整的语义菜单。数学符号这样的动态目录仍可以保留专门 provider，不必把所有动态内容静态化。

## 根因二：Format 确实大面积遗漏

当前 JSON 中 `editor.format.*` 仅约 18 个，主要为段落对齐、margin/indent、em/strong/verbatim/sample/name、italic/bold/typewriter/sans-serif/small-caps。

历史 Format 中需逐项核对恢复的组：

- 水平空白：stretchable、rigid、rigid box、tab/custom tab；段前段后垂直空白。
- 段前/段后 indentation yes/no。
- 换行：next-line、line-break、no-break、new-line。
- 页前/页后分页和禁止分页；hgroup/vgroup。
- opacity 预设及自定义。
- Adjust：Move、Shift、Resize、Extend、Clip、Smash、Reduce、Inflate。
- Transform：Rotate、Dilate、Skew、Linear2d。
- Specific output：texmacs、latex、html、screen、printer、image、even、odd。
- Group/rigid、phantom、superpose、repeat、datoms。
- 字体效果：embold、embbb、slanted、hmagnified、vmagnified、condensed、extended、monospaced、degraded、distorted、gnawed。
- 图形效果：blur、outline、thicken、erode、shadow、engrave、emboss、shadowed-raise、outlined-engrave、outlined-emboss、degrade、distort、gnaw、make-transparent、make-opaque、recolor、skin。
- hyphenate selection（底层能力仍在）。

很多执行能力已经在 native `format_commands.*`、geometry 等实现中，不需要重新写格式算法。应从 `src/Edit/Interface/native_editor_actions.cpp` 的有限 action validator/dispatcher 接入，再在 JSON 声明。禁止通过任意 Scheme eval 逃避原生接口。

Font、Paragraph、Page、Cell/Table 的部分功能已经进入原生属性面板。不要把“菜单布局变化”误报为“能力删除”，也不要恢复已经有明确产品删除决策的旧 UI。

## 根因三：Title / Abstract 与源 UUID 的空文本比较

`src/Edit/Interface/edit_interface.cpp::editor_rep::focus_toolbar_state_snapshot()` 原先约 1233 行：

```cpp
const bool cursorEmpty =
  has_subtree(root, cursorParent) &&
  subtree(root, cursorParent) == tree("");
```

tree-to-tree equality 包含 node metadata。有 UUID 的空 atom 不等于无 metadata 的 `tree("")`，导致标题/摘要入口错误隐藏。文本内容比较 `== ""` 不比较 metadata；工作树已改此处及约 1798/1803 行两个 slide 空文本判断。

已有 Focus provider：`QTMFocusSpecialCommands.cpp` 中 `editor-focus-document-extras`，包含 `document-insert-title`、`document-insert-abstract` 等。它挂在 Focus 菜单和 Focus toolbar。**不是整个系统没有标题能力，而是普通 Insert 缺入口，Focus 又受错误空文本判断影响。**

title 可用条件目前还包括空光标位置、无 doc-data、非 beamer；abstract 包括空光标位置、有 doc-data、无 abstract-data。普通 Insert 和 Focus 的可用条件不必完全相同，但应避免重复标题和不合适上下文。

新 `editor-document-insert.json` 草稿使用现有原生 `make-doc-data`、`make-abstract-data`，需要接入并审核能力条件。原生和旧 Scheme 标题插入都用了 `(0,0,0)` caret path；此前怀疑路径但没有证明错误，**不要未经验证擅自修改**。可参考 `native_latex_commands` 中现有 doc-data 插入实现。

## 根因四：menubar 没有消费已解析的 item 条件

这是阅读代码确认的另一处，尚未修改：

- `QTMCommandPresentation.cpp` 解析 `requires` / `forbids` / `any` 和 Focus 对应 flags，存入 `QTMCommandMenuItem`。
- `QTMEditorToolbarPresenter` 会执行这些 presentation conditions。
- `QTMApplicationMenuPresenter` 的 `menu_entry` 不保存这些条件，`build_menu` / `refresh_menu` 不检查它们。

如果只把 toolbar 的条件菜单移入 menubar，会出现不合上下文的子菜单和 provider。要补正确的通用条件消费，尤其 provider 不可用时必须隐藏此前生成的 dynamic actions/menus，避免 stale 可见项。

建议复用 snapshot flags 的判断逻辑，而不是在两套 presenter 中堆两份稍有差别的规则。仍经 `editor_proxy_for_context(context)` 获取当前 editor/AVD 的 snapshot，不能跨 actor 直接访问树。`whenMainToolbarHidden` 的 toolbar-specific 语义需单独处理，不应无脑搬到 menubar。

另：menubar `build_menu` 创建 submenu 时未使用 submenu icon，若恢复的声明含 icon，检查其呈现。这不是此次大量命令缺失的主因。

## 其他菜单审计发现（静态结果，需继续核验）

1. Dynamic：`menubar-dynamic-commands` 主要收集 Fold 插入项与 presentation 导航；漏旧 `dynamic-operate-on-buffer` 的 fold/unfold、compress/expand 和 `dynamic-filter-buffer`。
2. Table：`QTMTablePropertiesPane.cpp` 的表格/单元格面板没有 cell background、直接行列 span、表格水平基线及指定基线。尺寸、边框、padding、换行已迁入面板；合并、拆分、子表已有 `editor-focus-special-actions`，不能误报整个 Table 缺失。
3. Insert Macro：`editor-personal-macros` provider 已实现并有 toolbar 入口，Insert menubar 未引用。
4. Source：已有 activate/compact/stretch；未找到旧 `make-mod-active` 的 active、inactive*、inactive、style-only*、style-only 对应入口。
5. Focus：旧 `display-link-as-card` / `display-card-as-link` 的 native 实现与绑定还在，未找到当前菜单/provider 调用，是漏接候选。
6. 排除误报：Edit redo 分支、剪贴板格式和 AI，Document 大部分设置，Focus 主题/自定义参数已迁移。旧宏编辑器、拼写 UI 等有明确删除历史，不应自动恢复。

这些不是“全部菜单已审计”的结论。接手者应记录每组是遗漏、已移位还是有意删除，不能无差别复活所有旧菜单。

## 拟议的 JSON 接入方案（未实现，可按现有代码调整）

为菜单与工具栏共享内容，考虑增加可复用 item groups：

```json
{
  "version": 1,
  "commands": [],
  "toolbars": [],
  "item_groups": {
    "format-menu": [
      {"command": "existing.command.id"}
    ]
  }
}
```

调用处 `{"include":"format-menu"}`。这是方案，不是当前受支持语法。

`QTMCommandPresentation.cpp::loadPresentation` 现状：

- 主文件 `application-shell.json` 为 version 3，要求 commands/menus/toolbars 数组。
- 扩展目前硬列 editor-mode-toolbar、editor-focus-toolbar、editor-user-toolbar 三个 version 1 文件，要求 commands 和 toolbars 数组，只合并这两个数组。
- `parseMenuItems` 只接受 separator/command/submenu/provider 四种之一。
- submenu/menu/toolbar IDs 在同一全局集合检查重复。

若实现 include：需要正确加载所有扩展的 group 再展开，拒绝未知 group 和循环 include；同一组用于菜单及工具栏时应有稳定的 occurrence-scoped submenu ID，不能触发全局重复 ID，也不要抹掉原有校验。条件、palette 行为、命令引用验证均应保留。

预计新增 `editor-format-commands.json` 声明 Format 执行和 `format-menu`，另用一个 `editor-menus.json` 或现有合适文件声明 Insert 层级。复用 toolbar 内容时保持其既有行为，不随意重复定义 command。

不强制新 abstraction。如果显式菜单声明已足够清晰，可采用更简单方案，但必须相应修正当前尚未加载的草稿，不能留下看似实现的死配置。

## 建议实施顺序与验收

1. 阅读当前 diff 与历史菜单，形成按组的覆盖清单；确认没有他人并行改动被覆盖。
2. 完成声明式菜单加载/条件判断，补齐 Insert title/abstract/macro，并正确归属已有数学格式命令。
3. 成组接入缺失 Format 原生动作和 JSON；核验 Dynamic、Source、Table、Focus 上述实际遗漏并修复。不要先一遍遍构建微小改动。
4. 检查 JSON 语法、命令唯一性、引用、group 展开及真实 registry 加载；静态 JSON parse 成功不等于注册可用。
5. 定向检查：带 UUID 的空段落能获得适当标题入口；普通文本/数学/只读/无 buffer 时菜单条件正确；Format 操作通过 actor 可撤销；恢复命令能从合适菜单或 command palette 到达；现有 toolbar 未被破坏。
6. 不必测试每个旧格式算法；优先验证新的 dispatcher 边界与菜单层级。需要 GUI 时使用隔离 ATHENA instance，不碰用户 Notes 和运行进程。
7. 正常构建：

```bash
cmake --build build_qt6 --target ATHENA.bin -j20
```

8. 构建成功后自动覆盖部署到 `ATHENA/bin/ATHENA.bin`：先 install 到同目录临时文件，再原子 mv 替换。不要直接覆盖正在运行的文件 inode，不重启用户进程。
9. 如提交，先读最近完整 commit bodies；格式 `fix: ...` 加具体 bullet body。分合理的大块，勿混入 unrelated 修改。按用户需要汇报未完成覆盖，不宣称所有历史菜单完全恢复，除非确有清单支持。

之前 `utf8_editor_test` 曾出现测试完成后 teardown exit 139 的既有问题；不要拿它的输出冒充这轮成功验证，也不要顺带扩大范围去修。

## 停止时的准确结论

调查已找到多项具体根因，只有三处空文本判断及一个未接入的 JSON 草稿写入工作树。没有构建、没有菜单修复提交、没有生产 Notes 写入。下一位需要完成实现，而不是只做最终验收。
