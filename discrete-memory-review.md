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

## Exec 后诊断清单
### 1. 先确认 Exec 构造的返回现场
- 在 [src/proc/ProcessManager.cpp](src/proc/ProcessManager.cpp) 的 `Exec()` 末尾记录 `pContext->eip`、`pContext->esp`、`pContext->xcs`、`pContext->xss`、`pContext->eflags`。
- 期望值是：`eip = 0x0`，`xcs = USER_CODE_SEGMENT_SELECTOR`，`xss = USER_DATA_SEGMENT_SELECTOR`，`esp` 落在新用户栈顶附近。
- 如果这里就不对，问题在 `Exec()` 构造现场，不必继续看 shell。

### 2. 再确认系统调用返回前的页表状态
- 在 [src/interrupt/SystemCall.cpp](src/interrupt/SystemCall.cpp) 的 `InterruptReturn()` 前打印当前 `u.u_procp->p_pid`、`u.u_procp->p_pgTable`、`Machine::Instance().GetPageDirectory()`、`Machine::Instance().GetUserPageTable()`。
- 重点确认“当前进程的页目录”和“机器当前使用的页目录”是一致的。
- 如果不一致，`iret` 以后用户态第一条指令很可能直接缺页。

### 3. 重点看 0 地址的 runtime 是否真的可执行
- 在 `Exec()` 里构造完 `runtime` 拷贝后，打印 0 地址对应页的页表项：present、rw、us、page base address。
- 如果 `eip = 0x0` 但该页未映射，或者没有用户态执行权限，`iret` 后会立刻异常。
- 这是最符合“看到 Exec exit!，但看不到 `[%s]#`”的故障点。

### 4. 查页故障和通用保护异常
- 在 [src/interrupt/Exception.cpp](src/interrupt/Exception.cpp) 的 `PageFault`、`GeneralProtection`、`StackSegmentError` 处理函数里加日志。
- 至少记录：`CR2`、故障时 `EIP`、`CS`、`SS`、错误码、当前进程 PID。
- 判断标准：
	- `CR2 = 0x0`：大概率是 `runtime` 代码页没映射或不可执行。
	- `CR2` 落在高地址：大概率是用户栈或参数区问题。
	- `#GP` / `#SS`：大概率是段寄存器或栈切换不合法。

### 5. 验证 shell 是否真的进入 main1()
- 在 [src/shell/main.c](src/shell/main.c) 的 `main1()` 第一行 `printf("[%s]#", curPath);` 前后各打一条日志。
- 若没有进入这里，问题就不在 shell 逻辑，而在 `runtime -> ExecShell -> execv -> 返回用户态` 的链路上。

### 6. 验证 ExecShell 到 shell.exe 的跳转链
- 在 [src/kernel/main.cpp](src/kernel/main.cpp) 中确认 `runtime` 被复制到 0 地址后，`ExecShell` 的地址没有被误用。
- 在 `runtime()` 里确认 `call *%eax` 的 `eax` 确实是 `Shell.exe` 的入口地址。
- 如果 `eax` 正确但仍无提示符，优先查 `execv` 返回现场与页表映射。

### 7. 看调度器是否把 1# 进程真正切回前台
- 在 [src/proc/ProcessManager.cpp](src/proc/ProcessManager.cpp) 的 `Swtch()` 和 `Select()` 中打印被选中的 PID、`p_stat`、`p_flag`、`p_pgTable`。
- 如果 1# 进程没被选中，或者被选中后没有对应的 `SLOAD`，就会停在内核态而不是 shell。

### 8. 推荐最小日志顺序
- `Exec` 末尾：现场构造值。
- `SystemCall::InterruptReturn()` 前：当前页目录和用户页表。
- 异常处理入口：`CR2` 和故障 EIP。
- `main1()` 入口：`[%s]#` 前后。

### 9. 你现在最可能的结论
- `Exec exit!` 已出现，说明 `Exec()` 逻辑本身大概率完成。
- 如果随后没有 `[%s]#`，最可疑的是 `iret` 返回用户态后第一条指令缺页，优先查 0 地址 `runtime` 页和用户栈页。
