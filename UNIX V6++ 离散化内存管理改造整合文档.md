# UNIX V6\+\+ 离散化内存管理改造整合文档

## 一、核心目标

对 UNIX V6\+\+ 操作系统进行**内存管理离散化改造**，替代原有的连续内存管理机制；同时实现 \\*\\* 写时复制（COW）\\*\\* 技术，优化内存利用率与进程创建效率，保留原盘交换区 “仅内存满时换出进程” 的设计初衷。

## 二、修改文件总清单

合并两份文档的修改范围，去重并补充遗漏文件，最终涉及以下 19 个文件：

- 头文件（include）：`Allocator\.h`、`Machine\.h`、`MemoryDescriptor\.h`、`PageManager\.h`、`Process\.h`、`ProcessManager\.h`、`Assembly\.h`、`Text\.h`、`Utility\.h`

- 内存管理（mm）：`Allocator\.cpp`、`PageManager\.cpp`

- 机器层（machine）：`Machine\.cpp`

- 内核核心（kernel）：`main\.cpp`、`Utility\.cpp`

- 进程管理（proc）：`MemoryDescriptor\.cpp`、`Process\.cpp`、`ProcessManager\.cpp`、`Text\.cpp`

- 异常处理（interrupt）：`Exception\.cpp`

- PE 解析（pe）：`PEParser\.cpp`

## 三、分模块详细修改内容

### 1\. 全局页分配器模块（Allocator\.h/cpp）

**核心职责**：统一管理所有物理页框的分配与释放，替代原位示图，支持 COW 实现。

- `Allocator\.h`

    1. 增设`PageAllocator`数据结构，包含全局静态单例对象`m\_Instance`。

    2. 声明 3 个核心成员函数：`Alloc`（分配物理页）、`Free`（释放物理页）、`hasEnoughSpace`（判断空闲空间）。

    3. 定义`unsigned int Page\[PAGE\_ARRAY\_SIZE\]`数组（共 8k 个表项，对应 32KB 内存），替代位示图（位示图仅需 1KB，但无法支持 COW 的引用计数管理）。

- `Allocator\.cpp`

    1. 实现`Alloc`：传入分配范围的起始 / 结束物理页框号，返回分配的物理页框号，**一次仅分配一页**。

    2. 实现`Free`：传入待释放的物理页框号，无返回值（0 为占位），**一次仅释放一页**。

    3. 实现`hasEnoughSpace`：判断指定地址范围是否有足够空闲页框，供`ProcessManager`调用。

### 2\. 页管理模块（PageManager\.h/cpp）

**核心职责**：封装物理内存分配接口，保持对外语义不变，底层调用`PageAllocator`。

- `PageManager\.h`

    1. 重写`UserPageManager`和`KernelPageManager`的内存布局常量（如`KERNEL\_PAGE\_POOL\_START\_ADDR=0x200000\+0x2000`）。

    2. 新增`EnoughSpace`函数声明，用于预判断分配空间是否充足。

    3. 保留未使用的成员（如`MapNode map\[\]`、`Initialize`函数）但不再调用。

- `PageManager\.cpp`

    1. 重写`AllocMemory`和`FreeMemory`，改为调用`PageAllocator::Alloc\(\)`和`PageAllocator::Free\(\)`。

    2. 保持`AllocMemory`语义不变：将返回的页框号左移 12 位，转换为物理地址返回。

### 3\. 机器初始化与分页模块（Machine\.h/cpp、Assembly\.h）

**核心职责**：初始化分页机制，定义全局页表指针，支持进程切换时的页表刷新。

- `Machine\.h`：补充与分页初始化相关的函数声明。

- `Machine\.cpp`

    1. 修改`InitPageDirectory\(\)`：调整`pPageDirectory`地址；修改 0 号表项基地址，使核心页表最后一项映射到`0x400`物理页框；初始化 0\# 进程的页目录和核心页表（核心页表固定存储在`0x200`物理页框）。

    2. 修改`InitUserPageTable\(\)`：仅初始化 0\# 用户页表（固定存储在`0x201`物理页框），并更新页目录中的 0\# 表项；全局指针`m\_UserPageTable`初始指向 0\# 用户页表，进程切换后指向 1\# 用户页表。

- `Assembly\.h`：修改`FlushPageDirectory\(\)`宏，适配进程切换时的页目录刷新逻辑。

### 4\. 内存描述符模块（MemoryDescriptor\.h/cpp）

**核心职责**：管理进程的地址空间，负责页目录、用户页表的创建与复制，实现 COW 基础逻辑。

- `MemoryDescriptor\.h`

    1. 修改`PageDirectory\* Initialize\(\)`、`void EstablishUserPageTable`的函数声明。

    2. 新增`void CopyUserPageTable\(PageTable\* pgTable, unsigned int Page\[\]\)`函数声明。

    3. 注释`MapToPageTable`函数声明。

- `MemoryDescriptor\.cpp`

    1. 修改`Initialize\(\)`：为 1\# 用户页表分配一页内存，将虚地址存入`m\_UserPageTableArray`；分配并初始化一个页目录，返回其起始虚地址。

    2. 修改`EstablishUserPageTable\(\)`：仅被`Exec`函数调用，不再重复初始化用户栈；新增 6 个参数（正文段起始 / 长度、数据段起始 / 长度、正文段是否共享、共享正文段的进程页表）；计算页表项索引时先对 4M 地址空间求余，再除以`PAGE\_SIZE`；共享正文段时直接复制父进程页表物理地址，否则重新分配内存。

    3. 新增`CopyUserPageTable\(\)`：供`NewProc\(\)`调用，实现 COW 核心逻辑：检测父进程 1\# 用户页表项的`m\_ReadWriter`属性，若为可写则将该页引用数 \+ 1，并将父进程对应表项改为只读（RO）。

    4. 修改`Release\(\)`实现；注释`MapToPageTable\(\)`函数。

### 5\. 进程核心模块（Process\.h/cpp）

**核心职责**：管理进程的内存资源，实现离散化的堆栈扩展与进程退出清理。

- `Process\.h`

    1. 新增成员变量`p\_pgTable`，存储进程页目录的虚地址。

    2. 注释`Expand`函数声明（离散化无需连续内存扩展）。

- `Process\.cpp`

    1. 重写`Exit\(\)`：

        - 去除`Text::XccDec\(\)`调用，将共享正文段内存释放逻辑移至`XFree\(\)`之前。

        - 释放共享正文段前，将当前进程`p\_stat`设为`SSTOP`；遍历`Process`数组，找到其他引用该正文段的进程，将`Text`的`x\_pgTable`更新为该进程的`p\_pgTable`；`XFree`返回后恢复原`p\_stat`。

    2. 重写`SBreak\(\)`：按页向上取整`newSize`；缩短数据段时，调用`FreeMemory`并将对应页表项`m\_Present`设为 0；扩展数据段时，逐页分配内存并建立页表映射。

    3. 重写`SStack\(\)`：每次调用分配一页用户区物理页框，直接在页表中建立映射。

### 6\. 进程管理与切换模块（ProcessManager\.h/cpp）

**核心职责**：实现进程创建、切换、调度与盘交换区换入换出，适配离散化内存。

- `ProcessManager\.h`：重新定义宏`SwtchUStruct\(p\)`，适配进程切换时的 U 区映射更新。

- `ProcessManager\.cpp`

    1. 修改`SetupProcessZero\(\)`：初始化 0\# 进程相关属性，指定其`p\_pgTable`成员；将 0\# 进程 PPDA 区地址改为 4M（原 4M\-0x1000）。

    2. 修改`NewProc\(\)`：调用`CopyUserPageTable\(\)`复制父进程 1\# 用户页表，实现 COW。

    3. 修改`Swtch\(\)`：增加补丁，更新 Machine 类中页表相关指针；切换 CR3 寄存器和内核页表中的 U 区映射（全局`m\_PageDirectory`等指针不随进程切换更新）。

    4. 修改`Sched\(\)`：调整换入逻辑（`found2`代码块），先换入盘交换区的进程 PPDA 获取`User`结构，再根据代码段、数据段信息逐页建立用户页表映射；磁盘读写按页偏移 8 个扇区（1 页 = 4096B=8×512B 扇区）。

    5. 修改`Wait\(\)`：适配离散化内存管理的进程等待逻辑。

    6. 修改`Exec\(\)`：调用`EstablishUserPageTable\(\)`建立新进程的用户页表。

    7. 修改`XSwap\(\)`：调整换出逻辑，根据页表逐页释放内存中可交换的进程图像，并将对应页表项`m\_Present`设为 0 禁用映射。

### 7\. 异常处理模块（Exception\.cpp）

**核心职责**：处理页错误异常，实现 COW 的写时复制触发逻辑。

- 修改`PageFault\(\)`：新增`isRW`布尔值，判断异常是否由**写操作且目标段可写**共同引发；若满足且目标页引用数 \&gt; 1，则分配新物理页，复制原页内容，更新当前进程 1\# 用户页表项，并将原页引用数 \- 1。

### 8\. 正文段管理模块（Text\.h/cpp）

**核心职责**：管理共享正文段，适配离散化的内存释放逻辑。

- `Text\.h`：注释`XccDec`函数声明；新增成员变量`x\_pgTable`，存储共享正文段对应的页表指针。

- `Text\.cpp`：修改`XFree\(\)`；注释`XccDec\(\)`函数实现。

### 9\. 工具函数模块（Utility\.h/cpp）

- `Utility\.h`：注释`CopySeg2`函数声明。

- `Utility\.cpp`：注释`CopySeg2`函数实现；兼容`FlushPageDirectory\(\)`宏的修改。

### 10\. PE 解析模块（PEParser\.cpp）

- 修改`Relocate\(Inode\* p\_inode, int sharedText\)`和`HeaderLoad\(Inode\* p\_inode\)`：仅为`sectionHeaders`分配一页内存，适配当前`AllocMemory`的语义，满足`ImageSectionHeader`的空间需求。

### 11\. 内核主函数（main\.cpp）

- `main0\(\)`：调用`machine\.InitPageDirectory\(\)`和`Machine::Instance\(\)\.InitUserPageTable\(\)`初始化页目录、核心态页表和用户态页表；调用`machine\.EnablePageProtection\(\)`开启分页模式。

- `next\(\)`：增加第 172\-180 行的`Page`数组初始化代码；初始化 0\# 进程的`p\_pgTable`成员；硬编码修改`Page`数组中 0\# 进程已分配页框的表项；1\# 进程分支不再直接写用户页表，改为调用`SwtchUStruct`。

## 四、调试中发现的问题
