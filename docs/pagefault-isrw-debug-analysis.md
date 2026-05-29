# Shell 启动后持续缺页（`PageFault` → `isRW` 分支）排查建议

## 现象摘要

- 1# 进程已完成 `Exec`，且加载目标为 `/Shell.exe`。
- 终端未出现预期的 `[路径]#` 提示符。
- 调试确认：缺页反复进入 `Exception::PageFault` 的 **`isRW` 分支**（约 `Exception.cpp` 271–297 行），而非 `SStack()` 或 `SIGSEGV` 路径。

## `isRW` 分支在做什么

```251:297:src/interrupt/Exception.cpp
void Exception::PageFault(struct pt_regs* regs, struct pte_context* context)
{
	// ...
	bool isRW=error_code&(1UL<<1);
	isRW=isRW&&(cr2>=md.m_DataStartAddress&&cr2<=md.m_DataStartAddress+md.m_DataSize
		||cr2>=MemoryDescriptor::USER_SPACE_SIZE-md.m_StackSize&&cr2<=MemoryDescriptor::USER_SPACE_SIZE);
	if(isRW)
	{
		// COW：refcount>1 时复制页框，然后置 m_ReadWriter=1
		// ...
		pUserPageTable->m_Entrys[pageIdx].m_ReadWriter=1;
		FlushPageDirectory(...);
		return;
	}
	// 否则：尝试 SStack() 或 SIGSEGV
}
```

含义（结合 Intel 缺页错误码）：

| 条件 | 含义 |
|------|------|
| `error_code & 2` | 本次访问是**写** |
| 地址落在 `[m_DataStartAddress, m_DataStartAddress+m_DataSize]` 或栈区 `[8M-m_StackSize, 8M]` | 被当作**数据/栈写缺页** |
| 进入 `isRW` | 按 **COW（写时复制）** 处理：共享且只读页 → 复制；最后把对应 PTE 标为可写 |

因此：**反复进入 `isRW`** 通常表示：对数据/栈的写访问一直无法满足——要么 PTE 仍不可写，要么改错了 PTE/页表，要么页根本未建立映射。

---

## 高优先级怀疑点（建议按顺序验证）

### 1. `Exec` 释放栈映射时，把 `fakeStack` 一并拆掉，且之后未重建栈（★★★）

`ProcessManager::Exec` 中的顺序：

1. 分配 `fakeStack`，映射到 **1# 用户页表最后一项**（`ENTRY_CNT-1`，即 `0x7FF000` 一带）。
2. 在 `esp` 从 `USER_SPACE_SIZE` 向下构造 `argc/argv`（写用户栈）。
3. **按新的 `m_StackSize` 释放旧栈**，循环 `m_Entrys[1023-i]` 并 `m_Present=0`。
4. `EstablishUserPageTable` **只建立 text/data**，**不建立 stack**。
5. 原先用于把参数拷回用户栈的 `MemCopy(fakeStack → USER_SPACE_SIZE - StackSize)` **已被注释**。

关键代码位置：

```645:731:src/proc/ProcessManager.cpp
unsigned long fakeStack = userPgMgr.AllocMemory(...);
userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1].m_Present=1;
// ... 构造 esp/argv ...
for(unsigned int i=0;i<u.u_MemoryDescriptor.m_StackSize/userPgMgr.PAGE_SIZE;i++)
{
	userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1-i].m_Present=0;  // 含 fakeStack
}
```

```799:806:src/proc/ProcessManager.cpp
u.u_MemoryDescriptor.EstablishUserPageTable(...);  // 无 MapStackEntrys / 无栈页分配
```

**推断**：`Exec` 返回用户态后，`runtime` / `Shell` 的 `printf`/`gets` 等会写栈；栈页 **Present=0** 时，写访问仍会带 `error_code` 的写位 → 满足 `isRW` 的“写”条件，且 `cr2` 落在栈区 → 进入 `isRW`。但 handler **只设 `m_ReadWriter=1`，不设 `m_Present`、不 `AllocMemory`** → **同一 CR2 无限缺页**。这与“一直打在 `isRW`”高度吻合。

**建议调试**：

- 在 `Exec` 返回前打印 1# 页表 **栈区**（`1024 - ceil(m_StackSize/4096)` ~ `1023`）的 `Present / RW / PFN`。
- 记录每次缺页的 **CR2**、**error_code**（区分 bit0：不存在 vs 保护违例）。
- 确认 `EstablishUserPageTable` 之后是否应调用 `MapStackEntrys`（或等价逻辑），以及是否应恢复 `MemCopy` 到 `USER_SPACE_SIZE - parser.StackSize`。

---

### 2. `isRW` 把“栈缺页”和“COW 写保护”混在同一分支（★★★）

- **COW**：页 **Present=1** 且 **RW=0**，写触发保护违例（error_code 通常 bit0=1, bit1=1）。
- **栈 demand**：页 **Present=0**，写也会带写位（bit1=1），但 bit0=0。

当前 `isRW` 对两种情况都只执行：

```cpp
pUserPageTable->m_Entrys[pageIdx].m_ReadWriter = 1;
```

对 **未映射栈页** 无效；而本应走 `Process::SStack()` 的栈扩展，却被 `isRW` 提前拦截：

```300:302:src/interrupt/Exception.cpp
if( cr2 < USER_SPACE_SIZE - md.m_StackSize && cr2 >= context->esp - 8 ... )
	current->SStack();
```

对位于 **`[8M - m_StackSize, 8M]`** 内的缺页，`cr2 < 8M - m_StackSize` 为假 → **永远不会 `SStack()`**。

**建议**：在 `isRW` 内先判断 `m_Present`；若为 0，转 `SStack()` 或单独的“栈页分配+映射”逻辑，而不是只改 RW 位。

---

### 3. `pageIdx` 未区分页目录项 0 / 1（★★☆）

用户空间 8MB 布局（`MemoryDescriptor::Initialize` / `Machine::InitPageDirectory`）：

| 线性地址 | 页目录项 | 页表 |
|----------|----------|------|
| `0x00000000` – `0x003FFFFF` | PD[0] | 共享 **0# 用户页表**（`USER_ZERO`，含 `runtime` @ 0） |
| `0x00400000` – `0x007FFFFF` | PD[1] | 进程 **`m_UserPageTableArray`** |

`PageFault` 始终：

```cpp
PageTable* pUserPageTable = md.GetUserPageTableArray();
unsigned int pageIdx = (cr2 & (0x3FF<<12)) >> 12;
```

即 **只用 PD[1] 的页表**，用 CR2 低 10 位作下标。

- 对 `cr2 = 0x00401xxx`（典型 PE `ImageBase=0x400000`）：正确。
- 对 `cr2 < 0x00400000`（如 `runtime`、低地址 `.data`）：应改 **PD[0] 对应页表**，当前会 **改错表项** → 缺页无法消除。

**建议调试**：每次缺页打印 `CR2`、`CR2>>22`（页目录索引）、当前进程 `p_pgTable` 中 PD[0]/PD[1] 的 PFN。

---

### 4. `FlushPageDirectory` 是否总在“当前进程”页目录上生效（★★☆）

```cpp
FlushPageDirectory((unsigned long)&Machine::Instance().GetPageDirectory()
    - Machine::KERNEL_SPACE_START_ADDRESS);
```

`Swtch` 会把 `Machine::m_PageDirectory` 设为 `selected->p_pgTable`，一般与 CR3 一致。但若在 **CR3 与 `m_PageDirectory` 不一致** 的路径（早期启动、未经过 `Swtch`、手动改 CR3）修 PTE，TLB 刷新会错对象。

**建议**：缺页处理里用 **`当前 CR3`（`mov %%cr3`）** 或与 `u.u_procp->p_pgTable` 对齐后再 `FlushPageDirectory`。

---

### 5. `NewProc` 遗留的 1# 页表项未在 `Exec` 中清理（★★☆）

0# 创建 1# 且 `pgTable == NULL` 时，对 **整张 1# 页表** 填：

```127:134:src/proc/ProcessManager.cpp
for ( i = 0; i < 1024; i++ ) {
    m_Present = 1; m_ReadWriter = 1;
    m_PageBaseAddress = 0x00000 + i + 1024;  // 物理页 1024+i，连续映射
}
```

`EstablishUserPageTable` **只覆盖 text/data 段对应项**，**中间空洞、栈区旧项** 可能仍为上述“连续假映射”，与离散化、 refcount 不一致。

**建议**：`Exec` 在 `EstablishUserPageTable` 前对 1# 页表做 **全表清零**（或 `ClearUserPageTable`），再按段映射；并核对 `Page[]` 与 PFN 是否一致（见下条）。

---

### 6. `Page[]` 引用计数与 `NewProc` 手工 PFN 不一致（★★☆）

`PageAllocator::Alloc` 在分配成功时 `Page[idx]=1`；`CopyUserPageTable` 对共享可写页 `Page[base]++` 并把父/子 PTE 标只读。

`NewProc` 中 `m_PageBaseAddress = 1024+i` **未经过 `Alloc`**，`Page[1024+i]` 可能仍为 0。

COW 分支：

```cpp
if (pgAllocator->Page[base] > 1) { /* 复制 */ }
pUserPageTable->m_Entrys[pageIdx].m_ReadWriter = 1;
```

- `Page[base]==0` 时只改 RW，可能掩盖“页框未纳入分配器”的问题。
- 若 refcount 与真实共享关系不一致，可能 **误复制** 或 **漏复制**。

**建议**：对缺页 CR2 打印 `base`、`Page[base]`、PTE 的 `Present/RW/PFN`；核对 `FreeMemory` 是否与 `Page[]` 同步递减。

---

### 7. `CopyUserPageTable` 的 COW 语义（★☆☆）

```206:212:src/proc/MemoryDescriptor.cpp
if(entry[i].m_ReadWriter) {
    Page[entry[i].m_PageBaseAddress]++;
    entry[i].m_ReadWriter=false;  // 仅改父进程页表指针 entry
}
new_entry[i].m_ReadWriter=entry[i].m_ReadWriter;
```

- 子进程 1# **未走** `CopyUserPageTable`（`pgTable==NULL`），Exec 后数据段在 `EstablishUserPageTable` 里 **直接 RW=1**，一般不应因 COW 进 `isRW`。
- 若以后从 **fork 出的 shell** 或共享正文/数据，需确认：**父表项** 与子表项是否都只读、refcount 是否正确。

---

### 8. 数据段边界 / `isRW` 范围误判（★☆☆）

`isRW` 用 `m_DataStartAddress + m_DataSize`（**不含** 对齐后的 BSS 扩展页）。若 PE 的 `.bss` 超出 `DataSize` 仍写在“数据区”外：

- 可能 **不进 `isRW`** → `SIGSEGV`；或
- 若落在 `NewProc` 遗留的 RW 假映射上 → 行为诡异而非循环缺页。

`PEParser::Relocate` 按 section 清 0，依赖页已映射；BSS 超出 `EstablishUserPageTable` 所映射页数时，会在 **未映射或错误映射** 上写。

**建议**：对照 `Shell.exe` 的 section 头，核对 `DataSize`、`StackSize`、`BSS_SECTION_IDX` 与 `EstablishUserPageTable` 的 `dataPageNum`。

---

### 9. 正文段只读与写 text（★☆☆）

`EstablishUserPageTable` 对 text 设 `m_ReadWriter=0`。若 `cr2` 落在 text 却被算进 data 范围（`m_DataStartAddress`/`m_DataSize` 解析错误），会进 `isRW` 且只改 RW，可能仍与执行权限/内容不一致。

**建议**：缺页时打印 `cr2` 是否落在 `[TextStart, TextStart+TextSize)`。

---

### 10. 用户态出口与栈指针（★☆☆）

`Exec` 末尾：

```870:876:src/proc/ProcessManager.cpp
pContext->eip = 0x00000000;   // runtime
pContext->esp = esp;          // 在 Exec 内构造的栈顶
u.u_ar0[User::EAX] = parser.EntryPointAddress;
```

`main.cpp` 在 **`Exec` 之前** 把 `runtime` 拷到物理 0；`Exec` 内 **不再拷贝**（已注释）。需保证 PD[0] 的 `0` 页仍映射且可执行。

若 `esp` 指向 **已释放的 fakeStack 区域** 而栈页未重建 → 与第 1 点一致。

---

## 建议的调试检查表（不改代码即可做）

| # | 操作 | 期望 / 异常 |
|---|------|-------------|
| 1 | `Exec` 返回前 dump 1# 页表项 0、text 区、data 区、**栈区**（高索引） | 栈区 `Present=1` 且 PFN 合法 |
| 2 | 每次 `isRW` 打印：`CR2`、`error_code`（十六进制）、`Present`、`RW`、`PFN`、`Page[PFN]` | `Present=0` 却进 COW 分支 → 第 2 点成立 |
| 3 | 对比 `CR2>>22` 为 0 还是 1 | 为 0 时必须改 PD[0] 页表，不是 `m_UserPageTableArray` |
| 4 | 确认 `Shell.exe` 的 `ImageBase`、`SizeOfStackCommit`、各段 `VirtualAddress` | 与 `m_*` 字段一致 |
| 5 | 在 `EstablishUserPageTable` 后、`Relocate` 后各 dump 一次页表 | 中间空洞不应残留 `1024+i` 连续映射 |
| 6 | 单步过 `runtime` 第一条 `push`/`mov` 栈指令 | 对应 `CR2` 是否落在未映射栈页 |

---

## 与 `main.cpp` 启动链的关系（简要）

```229:243:src/kernel/main.cpp
int pid = NewProc();           // 0# 创建 1#
// 1#: MoveToUserStack → ExecShell → int 0x80 execv("/Shell.exe")
```

- 1# 的初始 1# 页表来自 `NewProc` 的特殊分支（连续 PFN），与 **离散 `EstablishUserPageTable`** 混用，是 Exec 后页表“脏项”的根源之一。
- `runtime` / `ExecShell` 在低地址；`Exec` 后从 `eip=0` 进入 `runtime`，再 `call *%eax` 到 Shell 入口——栈与 **PD[0]/PD[1]** 都必须正确。

---

## 修复方向（仅供后续改代码时参考，本文档不实施修改）

1. **Exec**：在释放 `fakeStack` 之后，为 `parser.StackSize` **显式映射栈页**（`MapStackEntrys` 或循环 `AllocMemory` + 填 PTE），并恢复 argv 到真实用户栈的拷贝。
2. **PageFault**：`isRW` 内区分 **Present==0（栈/按需分配）** 与 **Present==1 && !RW（COW）**；栈区 Present==0 时调用 `SStack()` 或同类逻辑。
3. **PageFault**：按 `CR2` 选择 PD[0] 或 `m_UserPageTableArray` 再算 `pageIdx`。
4. **Exec**：`EstablishUserPageTable` 前 **清空** 进程 1# 用户页表，避免 `NewProc` 残留项。
5. **统一** 所有 PFN 与 `Page[]`：映射、COW、释放均走同一套 refcount 规则。

---

## 相关源文件索引

| 文件 | 内容 |
|------|------|
| `src/interrupt/Exception.cpp` | `PageFault` / `isRW` / COW |
| `src/proc/ProcessManager.cpp` | `Exec`、`NewProc` |
| `src/proc/MemoryDescriptor.cpp` | `EstablishUserPageTable`、`CopyUserPageTable`、`Map*` |
| `src/proc/Process.cpp` | `SStack` |
| `src/pe/PEParser.cpp` | `Relocate`、段地址 |
| `src/kernel/main.cpp` | 0#→1#、`runtime`、ExecShell |
| `src/machine/Machine.cpp` | PD[0]/PD[1] 初始布局 |
| `src/include/Assembly.h` | `FlushPageDirectory` |

---

*文档生成目的：在暂不修改代码的前提下，集中列出与“Exec 后 Shell 无法显示提示符、持续进入 `PageFault::isRW`”相关的可验证假设与调试步骤。*
