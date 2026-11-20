# Hodarium 节点为何涨到 23 GiB：mimalloc 3.0.1 现场分析

2026-11-20 · TrueNAS · 时间均为 UTC+8

**结论：在仍运行的节点上，确认了大量已经跨线程释放、却仍占着常驻页的小块。最能解释这一状态的是 mimalloc 3.0.1 默认开启且没有队列长度上限的“释放时接管页”策略。** 这不是把文件缓存误算成进程内存，也不能简单归因于 Hodarium 忘了调用 `free`。证据确认了内存滞留的位置；要把它唯一归因到这个开关，还差同版本、同负载的对照实验。

## 1. 调查的是哪一版

| 组件 | 现场版本或证据来源 |
|---|---|
| 节点镜像 | `localhost/athena-hodarium-node:debug-6b5817f5c-20261110` |
| ATHENA 源码 | `6b5817f5c`，不是调查时的仓库 HEAD；下文链接固定到部署提交 |
| 节点 ELF build ID | `120bb92b81f6615acfc7adf9fb4f6b12afa54ab1` |
| 分配器 | 进程映射为 `libmimalloc.so.3.0`；同标签的本地诊断镜像内，`dpkg-query -W libmimalloc3` 返回 `3.0.1+ds-2.1`（amd64） |
| 证据 | NAS 内核 OOM 日志、`diagnostics/memleak-debug.log`、运行中进程的 `smaps`、`pagemap`、分配器页元数据和缺页调用栈 |

核对了 [Debian 源码包清单](https://deb.debian.org/debian/pool/main/m/mimalloc/mimalloc_3.0.1+ds-2.1.dsc) 中两个归档的 SHA-256。Debian 打包目录没有 `debian/patches/`；`-2.1` 关闭额外的 `-march` 优化，并修正包替换关系，没有修改以下回收路径。采集器配置及其覆盖边界见[仓库中的现场采集说明](../tools/perf/hodarium-memleak.md)。

## 2. 内存确实在增长，但采样没报出大漏点

| 时间 | 观察 |
|---|---|
| 11 月 10 日 09:39 至 11 日 09:38 | 1,440 次分钟读数。匿名 RSS 在开始、约 6、12、18、24 小时时分别为 **103、253、468、692、952 MiB**。全程没有使用 swap。 |
| 11 月 13、17、19 日 | 内核三次全机 OOM，均杀死 Hodarium。被杀时匿名 RSS 分别是 **22.7、23.0、22.9 GiB**。第一次就是此前采样的 PID 1253690，但 24 小时采样窗口已经结束。 |
| 11 月 20 日 09:18 | 替换进程 PID 1385343 已运行约 10 小时 38 分，匿名 RSS 已到 **2.89 GiB**。两段私有匿名可写映射分别驻留约 **1.91 GiB** 和 **0.97 GiB**。 |

24 小时内，BCC 探针按约 1/1024 的比例抽样分配，存活至少 60 秒的样本最多 7 个，最后一次仅 5 个、合计 265 字节。这个小数字**不是**“堆只有 265 字节”：它只统计被探针捕获、且没有观测到释放的老样本。对象一旦被应用释放，便不再属于这类样本；底层页是否仍驻留，是另一个问题。三个 OOM 的旧堆均已消失，没有把 11 月 20 日新进程的抽样倒算成旧进程的 23 GiB。

## 3. 比 RSS 更直接的证据：逐页核对远程释放链

两段大映射均匀抽样的内容中，反复出现资源路径、图文件名、材料文件名和哈希。字符串可以在释放后原样留在内存里，所以这些内容只说明**内存装过什么**，不能说明对象还活着。真正区分两种情况的是分配器元数据：

1. 从实际加载的 `mi_usable_size` 反汇编还原二级页表定位方式，以 `/proc/PID/pagemap` 区分驻留页。主要的块大小类别是 **64、80、96、128、160 字节**，不是几个 GiB 级对象。
2. 由同一二进制的 `mi_free` 核对字段：本地释放递减页头偏移 `0x10` 的 16 位计数；跨线程释放把块挂到偏移 `0x20` 的链上。低位所有权标记不属于指针地址。
3. 在两段映射里每隔 17 个 64 KiB 区间取样，仅统计页表指回本页起点、且属于上述五种小块类别的页。读取页头并在同一份页快照中沿链计数，不调用目标进程里的函数。

| 抽查结果 | 数值 |
|---|---:|
| 符合条件的分配器页 | 2,779 |
| 页头 `used × block_size` 合计 | 28,411,840 B |
| 在 `xthread_free` 链上实际数到的块字节数 | 28,312,240 B |
| 后者占前者 | **99.65%** |
| 从 `used` 扣掉链长后至多剩一个块的页 | **2,777 / 2,779** |

**99.65% 的分母是这些抽样页的 used 块字节，不是整个进程的 RSS。** 进程也没有为抽样而暂停。这不是全堆普查，但已足以排除“这些页里主要是仍被业务引用的对象”。一些页的片段显示同一个非零 owner 标识；单看该标识，不能反推出具体 C++ 线程、原始分配者或历史接管顺序。

## 4. ATHENA 为什么会产生这种工作负载

部署版本的 [`sync_worker::extra_step`](https://github.com/nuaptanEvalisk/ATHENA/blob/6b5817f5c/src/ATHENA/Data/hodarium_worker.cpp#L189-L280) 有两个不同的执行位置。以下摘录省略了权限和错误处理。异步线程枚举资源，即使文件没有变化，也会把路径和观测值放进**本轮新建的容器**：

```cpp
// 异步任务内，hodarium_worker.cpp:257、267–270
auto inventory= inventory_resources (track->binding.root, *cancelled, 100000, &track->resource_watch, declared);
// ...
if (old != previous.end () && cached != track->resource_cache.end () &&
    athena::filesystem::same_revision (cached->second, file.observed)) {
  result.resources.emplace (file.path, file.observed); continue;
}
```

而接收结果、替换旧缓存的是驱动同步的线程：

```cpp
// hodarium_worker.cpp:193–194
auto result= extra_future.get ();
extra_track->resource_cache= std::move (result.resources);
```

`move` 避免复制新容器，不代表旧容器里的路径和节点无需销毁。这给出了真实的“一个线程构建、另一个线程释放”的对象生命周期。它是触发条件的来源之一，**不是**说凭这两行就能认定每个滞留页都来自该容器。

另一项独立的工作放大出现在 [`observe`](https://github.com/nuaptanEvalisk/ATHENA/blob/6b5817f5c/src/ATHENA/Data/hodarium_logical_sync.cpp#L47-L55)：查询一条 Materials 修订时，先导出全部逻辑对象，再筛目标键。

```cpp
for (const auto& object: export_material_objects (files.root.path ()))
  if (object.key==target.object) {
    if (object.format!=target.format) throw std::invalid_argument ("Logical Material format changed");
    return object.payload;
  }
```

这不是只凭代码猜测热点。11 月 20 日的 18 秒缺页采样，每 31 次缺页取一个样本，共 214 个；其中 209 个地址落在上述两段映射内，112 个的调用栈经过 `export_material_objects`，另有栈经过 `inventory_resources`。它证明这些路径确实在触碰相关内存，不等于 112/214 的**净增长**都归 Materials 所有。优化全库导出能减少压力，但不能代替对分配器回收机制的解释。

## 5. mimalloc 3.0.1 的关键选择：谁 free，谁可能接管整页

这里的 **reclaim** 指“把页接管进某个线程的堆”，不是“把内存还给操作系统”。**abandoned** 指页不再属于原堆，页中仍可能有活块。

**第一步：线程退出并不会直接丢下自己的页。** 非主线程的 backing heap 在退出时调用 `_mi_heap_collect_abandon`。它遍历本堆页，归集释放链；全空的页释放，还有活块的页交还为 abandoned。因此，“短命线程一退出就没人再收这些页”不是完整解释。见 [init.c](https://github.com/microsoft/mimalloc/blob/v3.0.1/src/init.c#L450-L477) 和 [heap.c](https://github.com/microsoft/mimalloc/blob/v3.0.1/src/heap.c#L81-L130)。

**第二步：一次远程 free 是否立即结算，取决于页的所有权。** [free.c 的实际分支](https://github.com/microsoft/mimalloc/blob/v3.0.1/src/free.c#L67-L80)（摘录）：

```c
const bool is_owned_now = !mi_tf_is_owned(tf_old);
if (is_owned_now) {
  mi_assert_internal(mi_page_is_abandoned(page));
  mi_free_try_collect_mt(page);
}
```

执行到这里时，当前释放的块已原子地挂上 `xthread_free`。如果这次取得了原本无主的页，就可以归集；如果页已有 owner，本次通常到入链为止。页头 `used` 由稍后遍历链表的代码扣减，核心语句是 `page->used -= (uint16_t)count;`。这正是现场有意义的比较：页头计数减去远程释放链长。见 [page.c](https://github.com/microsoft/mimalloc/blob/v3.0.1/src/page.c#L141-L184)。

**第三步：尚未全空的页，可以转归释放者。** `mi_free_try_collect_mt` 先处理已经全空的页；下述策略只涉及**还有活块**的页。3.0.1 的 `reclaim_on_free` 默认是 1。在页不太满、堆标签和 arena 兼容、允许接管等条件满足时，最后一道大小类别检查是：

```c
if (mi_page_queue(tagheap, page->block_size)->first != NULL) {
  // ... 将页从 abandoned 集合移除 ...
  _mi_heap_page_reclaim(tagheap, page);
  // ...
}
```

检查的是**接收方用不用这个块大小**，不是**它是否还缺这种页**。有一页与已有几千页都满足条件。被接管的页由 `mi_page_queue_push_at_end` 加到队尾。分配时则先试队首；队首有空间就返回，不必访问队尾。于是，某个长寿命线程可以不断接收页，却长期不归集那些队尾页上的远程释放。此时页仍归它所有，其他线程也不能把这些页当作无主页直接接管。见 [接管条件](https://github.com/microsoft/mimalloc/blob/v3.0.1/src/free.c#L197-L259)、[加入队尾](https://github.com/microsoft/mimalloc/blob/v3.0.1/src/page.c#L232-L245)、[分配时优先队首](https://github.com/microsoft/mimalloc/blob/v3.0.1/src/page.c#L795-L821)。

**这条解释有一个必要区别：** 如果 B 接管了页，又是 B 自己释放剩余所有块，后面的释放是本地释放，不构成这里观察到的大量远程积压。能解释现场的链条是：**B 接管页 → 后续释放来自 B 之外的线程 → B 没有及时归集该页**。现场能看到这条链的结果，单次快照不能证明第一步的历史。这也是为什么不能仅凭“使用了 `std::async`”就宣布归因完成。

## 6. 上游改了什么，如何验证归因

上游后来针对的正是这个策略，而不只是调整向 OS 归还页的延时：

- [5aa679c](https://github.com/microsoft/mimalloc/commit/5aa679cdee122f59a6ceac9aae8cbd4181379ef1)：改变默认策略，区分接管回来源堆与仅因别的线程释放而跨堆接管。
- [89d6293](https://github.com/microsoft/mimalloc/commit/89d629317f986d2ef7605ced9fa5ec011adc1594)：接管前检查同大小类别的页队列长度，不再只判断非空。
- [v3.2.8 的实现与设计注释](https://github.com/microsoft/mimalloc/blob/v3.2.8/src/free.c#L268-L315) 明确说明，过于积极的跨线程接管会增加内存持有量，并妨碍线程间复用。这支持机制解释，不是对 Hodarium 工作负载的实测。

**最有区分力的下一步，是只切一个开关。** 在隔离环境用相同 3.0.1 镜像、相同输入和相同任务轮数，对照默认配置与启动时设置 `mimalloc_reclaim_on_free=0` 的情况。此处使用的是 **3.0.1 的选项名**；它禁止因一次 free 而把尚未全空的页接管进当前堆，不禁止跨线程 free，也不禁止归集后释放全空页。不要同时换版本、改同步算法或改重试周期，否则实验无法区分原因。

比较相同已完成工作量下的三项量：业务仍保留几份资源缓存、匿名 RSS 曲线、页头 `used` 中已有多少块在远程释放链上。默认组持续堆积、关闭组稳定，才是对该策略的因果支持；若两组同样堆积，就应追踪这些页是否从一开始就属于长期不归集的 owner，而不是继续假定它们一定经过 free 时接管。无需为了验证而等待下一次生产 OOM。

**截至本报告：** 已完成现场观测、部署源码及 Debian 打包核对、上游变更对照。未执行开关对照，未把升级或关闭开关宣称为已验证的生产修复。
