# 布局窗口内的 C++ 分配

分别运行改动前保存的 Release 探针与重构后的 Release 探针，
每组 3 批 × 20 帧。合成文本覆盖 96／384 叶，DirectWrite 覆盖 96 叶；
均包含首次、无变化、缩放、叶更新、分支更新、混合更新，以及局部／全树失效。
36 组前后配对的几何 checksum 全部一致；每个版本内部各批的申请次数、
申请字节、测量次数和布局次数均通过探针的确定性断言。

## 384 叶合成文本，局部失效

| 模式 | 申请次数：前 → 后 | 申请字节：前 → 后 | 窗口峰值存活字节：前 → 后 |
|---|---:|---:|---:|
| 首次 | 21,904 → 17,157 | 5,477,342 → 9,128,761 | 2,208,227 → 2,804,996 |
| 无变化 | 0 → 0 | 0 → 0 | 0 → 0 |
| 缩放 | 838.80 → 401.25 | 485,230.40 → 469,620.60 | 237,940 → 237,924 |
| 叶更新 | 1,130.30 → 644.20 | 551,355.20 → 540,908 | 255,157 → 251,697.40 |
| 分支更新 | 1,267.70 → 698.80 | 587,996.60 → 600,927.60 | 275,883.60 → 273,599.20 |
| 混合更新 | 994.25 → 533.40 | 519,165.40 → 506,516.20 | 246,244.20 → 244,798 |

首次申请次数减少 **21.67%**，但申请字节增加 **66.66%**、窗口峰值增加 **27.02%**。
缩放／叶更新的申请次数分别减少 **52.16%／43.01%**。
DirectWrite 96 叶首次申请次数从 5,972 降至 4,720（减少 **20.96%**），
窗口峰值从 575,979B 增至 718,380B（增加 **24.72%**）。

## 统计边界

- 表中均为每帧平均值；峰值列是每批“各帧窗口峰值的平均数”的中位数。
- 探针覆盖当前线程的 C++ `new/delete`。建树、已存在缓存、其他线程、
  Windows／DirectWrite 内部的非 C++ 申请均不纳入窗口计数。
- 新分配的缓存可以活到窗口结束之后，因此“峰值存活”并不等同临时垃圾。
- 累计申请字节包含容器扩容，不能当作实际同时驻留的字节。
- 不据此宣称整个进程的内存占用下降。内部节点对象另有 +280B／节点的取舍，
  见[总报告](../README.md)。
- 插桩会影响耗时，本目录时间列仅作辅助；三方性能结论使用未插桩比较器。

## 原始证据与复现

[全部配对数据](before-after.csv)及各 `before/after-*-samples.csv` 保留全部结果，
可执行文件及探针源码摘要保存在[来源记录](../source-provenance.json)。

```powershell
build/comparison-release/axisly_css_retained_allocations.exe --iterations 20 --samples 3 --backend synthetic --output build/alloc-synthetic.csv --samples-output build/alloc-synthetic-samples.csv
build/comparison-release/axisly_css_retained_allocations.exe --iterations 20 --samples 3 --backend directwrite --output build/alloc-directwrite.csv --samples-output build/alloc-directwrite-samples.csv
```
