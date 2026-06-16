核心实现思路：请求调页式虚拟存储器，采用全局页面置换方式，系统维护空闲物理页框链表FreeList管理所有可以被分配的物理页框。页面缓存算法：启用内核堆，采用radix tree管理缓存的共享页。页面置换算法暂时使用基础的局部置换Clock。共享页被内存淘汰时，直接丢弃，后续再重新从inode读入；私有页（页表维护）被内存淘汰时，要写到盘交换区。对于请求调页式虚拟存储器来说，缺页有两种可能：一种是共享页，包括代码段和只读数据段，在可执行文件中没有读入，此时PTE为NULL，从可执行文件中读入。二是私有页，之前已经从可执行文件中读入过，但是被修改过，是脏页，被页面置换算法淘汰到磁盘上了。再次读入需要从磁盘中读入而非从可执行文件中读入

user增设vm\_list管理虚空间。指向vm\_area链表。每个逻辑段一个控制块vm\_area，同时在链表中按照起始地址递增排序。（暂时hardcode顺序。）

启用内核堆，仍然沿用最初的连续内存管理方式。这就决定了必须采用类似Linux的radix tree来缓存共享页而非hash表。



所有的修改：

D:\\UNIX V6++test\\oos\\src\\include\\Page.h，增设头文件，增设page结构体，但是不记录物理页框的引用次数count。该工作仍然由Page数组完成。增设结构体FreeList用于存储page链表的头尾指针。增设SwapPage结构体用于盘交换区上的块管理。

D:\\UNIX V6++test\\oos\\src\\include\\VMArea.h，增设头文件，增设vm\_area结构体（由于vm采用更加细化的缺页异常处理方式，单纯使用MemoryDescriptor已经不足以管理虚空间）

D:\\UNIX V6++test\\oos\\src\\include\\User.h，采用和PageManager.h相似的策略，先通过vm\_list数组把vm\_area结构所需要的空间分配好，再执行下一步

D:\\UNIX V6++test\\oos\\src\\include\\Allocator.h，依然采用32MB内存空间模型。一个page结构体20B，0x2000个就是160KB，内核静态区512KB。仍然保留之前使用的Page数组。



D:\\UNIX V6++test\\oos\\src\\include\\PageManager.h，对于内核区域，应采取急性内存分配方式；对于用户区域，即4M以上的区域，采用惰性内存分配方式，即请求调页。但0x401物理页框存放0#进程的PPDA，因此该物理页框保留不参与惰性内存分配。对于剩下的物理页框，添加到UserPageManager管理的freeList结构中。pages数组索引双向映射到每一个page结构体（page结构体保存自己对应的物理页框）。UserPageManager除了维护空闲页链表外，还维护共享页缓存radix tree，包括：保存radix tree的树根sharedPageRoot；增设pages\[freePageNum]数组，管理内存中所有可以被惰性分配的物理页框（可以被freeList管理的物理页框）。



D:\\UNIX V6++test\\oos\\src\\mm\\PageManager.cpp，改写了UserPageManager的构造函数，增加了初始化freeList的内容。重写了UserPageManager的AllocMemory和FreeMemory函数，将修改物理页框引用次数和从FreeList取/向FreeList里放空闲物理页框封装在函数中

D:\\UNIX V6++test\\oos\\src\\include\\SwapperManager.h，增设SwapperPage数组用于管理盘交换区上的每一页/块。在盘交换区上也不再使用Allocator代表的连续内存管理方式，而是增设了类似PageAllocator的SwapAllocator进行管理。

D:\\UNIX V6++test\\oos\\src\\mm\\SwapperManager.cpp，类似page数组的逻辑，在SwapperManager的构造函数中初始化SwapperPage数组。与pages数组同样的，也设置了空闲物理页链表。注意每次将物理页从内存读到磁盘块要释放内存中的物理地址，将磁盘块读到内存同理。



D:\\UNIX V6++test\\oos\\src\\proc\\ProcessManager.cpp，本系统仍然采用1张用户页表管理4M-8M区域，但是采用更加细粒度的虚空间分段方式，因此需要对user结构的vm\_list数组进行初始化。之前很多地方会在释放用户物理页框的同时通过m\_Present=0禁用映射，这在当前的设计中是错误的，需要删除。对于Exec末尾释放Inode的代码予以删除，因为在进程整个生命周期中，该Inode必须一直有效。删除对于Relocate函数的调用，真实的文件页面读入移到了缺页异常处理函数中。大幅精简0#进程执行的Sched函数。这是因为在当前设计中，所有进程图像离散从盘交换区调度到内存，都依赖缺页异常处理程序，不再依赖Sched函数。在请求调页存储器中，所有的进程的PPDA都必须放在内存中。直接删除了负责进程图像换出的XSwap函数。对NewProc内存分配一页PPDA失败的情况做出了处理：



D:\\UNIX V6++test\\oos\\src\\include\\PEParser.h，对PEParser的成员采取了更细粒度的划分方式，便于与Exec对接。但this->RDataSize=this->sectionHeaders\[RDATA\_SECTION\_IDX].Misc.VirtualSize和BBS的长度使用的是联合体的成员，可能是一个隐患。释放原进程的栈。PE文件中只存储了堆和栈的大小，没有存储位置。所以还是默认堆在BSS下面，栈紧贴着用户空间底部8M。释放原进程图像物理页时，也考虑到了要按照vm\_area释放而非MemoryDescriptor释放

D:\\UNIX V6++test\\oos\\src\\proc\\Text.cpp，在x\_count==0时释放盘交换区上的正文段副本，由于在盘交换区上也离散化存储，因此需要找到用户页表进行释放。

D:\\UNIX V6++test\\oos\\src\\include\\Text.h，对于Text结构，由于本设计中正文段不再在盘交换区上留存副本，所有正文段的物理页面只能在盘交换区上或者内存中二选一。删除x\_daddr字段。由于目前在盘交换区上也是用离散内存管理方式因此不再需要该字段。同理，x\_caddr字段也不再需要。旧设计中整个正文段作为一个整体管理 —— 所有物理页要么全在内存，要么全在交换区。所以需要两个粒度x\_count和x\_ccount。在新设计中，正文段是按照页为粒度存储。x\_ccount功能被Page数组逐页替代。因此，在我的设计中，去除了x\_ccount这个字段，只保留x\_count字段。彻底去除XFree函数，因此其逻辑也外移到具体的代码段中。

D:\\UNIX V6++test\\oos\\src\\proc\\MemoryDescriptor.cpp，MemoryDescriptor::EstablishUserPageTable采用缺页异常处理程序进行惰性分配，而非急性分配方式。增加void FreePhyPage(unsigned int sectionStartIdx,unsigned long sectionSize,bool isStack)用于通过内存中的页表释放所有已分配的物理页框(包括盘交换区上的)。微调MemoryDescriptor::CopyUserPageTable(PageTable\* pgTable,unsigned int Page\[])逻辑，正文段一样使用Page数组计数（因为已经把x\_ccount删了），更新了MemoryDescriptor::CheckUserSpaceSize( vm\_area vm\_list\[] )的判断逻辑和传入参数。设计了page\* MemoryDescriptor::selectVictim(PageTable\*userPageTableArray)/\*先clock选取一个RW页放在磁盘中（在COW实现中，RW页一定私有。因为若RW页被子进程复制，该pte会改为RO）\*/，存在疑点：一定要选择RW页吗？RO页可不可以？

D:\\UNIX V6++test\\oos\\src\\include\\PageTable.h，从m\_ForSystemUser中取了一位作为m\_Used，标识该页表项是否被使用（是否为NULL）。将m\_Present的语义修改为了是否在盘交换区（1在内存，0在盘交换区），将m\_PageBaseAddress在m\_Present==0时的语义修正为了相对于盘交换区起始位置的偏移块号（即与内存中的页框号一一对应）。

D:\\UNIX V6++test\\oos\\src\\include\\RadixTreeNode.h，增设头文件，初级版本的radix\_tree\_node，在本设计中没有必要使用到脏页标识。设置结构体radix\_tree\_node，2^8=256个槽位，每个节点至多有256个孩子。在radix\_tree的设计中，我决定将每个inode的下属page管理为一个循环链表。注意，在本设计中，radix tree的完整索引是一个unsigned int，其中前八位是inode\_id，即inode在m\_Inode表中的id；后24位是文件的偏移页框号，即page对应页框相对于文件起始位置的偏移页框数目（一个页框4096B），而非偏移地址（按B计算）。

D:\\UNIX V6++test\\oos\\src\\include\\INode.h，增加了到m\_Inode中的反向索引

D:\\UNIX V6++test\\oos\\src\\fs\\OpenFileManager.cpp，InodeTable::IGet(short dev, int inumber)增加对反向索引的更新，InodeTable::IPut(Inode \*pNode)增加释放共享页

D:\\UNIX V6++test\\oos\\src\\mm\\RadixTreeNode.cpp，实现函数radix\_tree\_node::FreeRangePage(radix\_tree\_node\*root)，传入根结点，对子树的释放

D:\\UNIX V6++test\\oos\\src\\proc\\Process.cpp，修改Exit，首先是适应盘交换区的页式逻辑，将user写入盘交换区时由分配512B改成分配4k。其次，对于虚拟段的释放，采用类似Exec的逻辑。修改SStack和SBreak



D:\\UNIX V6++test\\oos\\src\\interrupt\\Exception.cpp，修改逻辑：首先判断缺页异常是否处于用户态。若否，直接退出。若是，先遍历vm\_list找是否处于任意逻辑段。若不处于，堆栈扩展/发SIGSEGV信号。处于，

1. 若在代码段或只读数据段，先尝试找对应页的缓存。若找不到，该页没有缓存，需要从inode取。分配一页物理页框，修改对应PTE，从iptr读取内容，初始化page结构体并放在radix tree中。若该页有缓存，直接写PTE，Page数组相应引用次数+1
2. 若在数据段，先看m\_Used判断是否为NULL，若不为NULL，判断是否由写操作引发异常以及是否在内存中，若是，则COW；若不是则直接判断是不在内存，分配空间，从磁盘中读入数据页并释放交换区相应物理块，若是RO页则还要一并放在radix tree中。若PTE为NULL，先从radix tree找，如果找不到，从inode取。通过这两种途径让radix tree中有一个页缓存后，让这个页缓存变为RO，然后再分配一个页框挂现运行进程页表，复制页缓存内容到这个新分配页框。（出现问题：谁来释放radix tree中的共享页框？答案：在inode引用计数为0时释放所有该inode下的共享页缓存。因此共享页缓存的引用计数不应该上升，应始终保持1）
3. 若在BSS段，与数据段类似。区别：BSS段的PTE==NULL时，分配一个物理页框并刷0
4. 堆栈段初次分配，和BSS相同，只是没有刷0

