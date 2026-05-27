# 离散化内存改造审查（不改代码）

## 发现的问题
- 内核页分配只标记了少量固定页，导致内核映像范围内的大量页仍可被分配；这可能把内核代码或数据页分配给用户或其他内核分配。见 [src/kernel/main.cpp](src/kernel/main.cpp#L180-L183) 和内核大小常量 [src/include/PageManager.h](src/include/PageManager.h#L16-L17)。
- `MapEntry()` 的用户页表索引没有对 4MB 取模，所以虚拟地址在 0x400000+（常见 PE 基址）时索引会超过 1023，可能越界访问单张页表。见 [src/proc/MemoryDescriptor.cpp](src/proc/MemoryDescriptor.cpp#L78-L86)。
- 设计声明用户空间 8MB、2 张用户页表，但实际只为 `m_UserPageTableArray` 分配了一张页表，导致第二个 4MB 的映射会失败。见 [src/include/MemoryDescriptor.h](src/include/MemoryDescriptor.h#L11-L12) 和 [src/proc/MemoryDescriptor.cpp](src/proc/MemoryDescriptor.cpp#L15)。
- COW：`CopyUserPageTable()` 把父进程 PTE 改成只读但没有刷新 TLB，父进程可能保留旧的可写 TLB 项，从而写入不触发缺页异常。见 [src/proc/MemoryDescriptor.cpp](src/proc/MemoryDescriptor.cpp#L206-L211)。

## 已修复
- 已在 `Wait()` 中释放子进程页目录，避免每进程泄漏一个页目录。见 [src/proc/ProcessManager.cpp](src/proc/ProcessManager.cpp#L509-L510)。
- `Exec()` 释放栈页已改为传入物理地址（`m_PageBaseAddress << 12`）。见 [src/proc/ProcessManager.cpp](src/proc/ProcessManager.cpp#L746-L747)。

## 修改建议
- 启动时把所有内核常驻物理页标记为已分配：至少要覆盖 `[KERNEL_MEM_START_ADDR, KERNEL_MEM_START_ADDR + KERNEL_SIZE)`，以及页表/页目录/PPDA 等保留结构。若可行，使用链接器符号获取内核实际结束地址。参考 [src/include/PageManager.h](src/include/PageManager.h#L16-L17) 与 [src/kernel/main.cpp](src/kernel/main.cpp#L180-L183)。
- 修正 `MapEntry()` 的索引计算：对 4MB 取模或按页目录索引分表，保持与 2 表、8MB 设计一致。参考 [src/proc/MemoryDescriptor.cpp](src/proc/MemoryDescriptor.cpp#L78-L86)。
- 让用户页表分配与 8MB 声明一致：要么分配 2 张页表并完善映射逻辑，要么把 `USER_SPACE_SIZE` 等逻辑统一为 4MB。参考 [src/include/MemoryDescriptor.h](src/include/MemoryDescriptor.h#L11-L12) 和 [src/proc/MemoryDescriptor.cpp](src/proc/MemoryDescriptor.cpp#L15)。
- 在 COW 中把父进程 PTE 改只读后，刷新 TLB（重载 CR3 或对修改页执行 INVLPG），避免父进程继续使用旧的可写项。参考 [src/proc/MemoryDescriptor.cpp](src/proc/MemoryDescriptor.cpp#L206-L211)。
