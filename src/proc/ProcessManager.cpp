#include "ProcessManager.h"
#include "Machine.h"
#include "User.h"
#include "Kernel.h"
#include "Video.h"
#include "Utility.h"
#include "PEParser.h"
#include "Regs.h"
#include "MemoryDescriptor.h"

unsigned int ProcessManager::m_NextUniquePid = 0;

ProcessManager::ProcessManager()
{
	CurPri = 0;
	RunRun = 0;
	RunIn = 0;
	RunOut = 0;
	ExeCnt = 0;
	SwtchNum = 0;
}

ProcessManager::~ProcessManager()
{
}

void ProcessManager::Initialize()
{
	//nothing to do here
}

void ProcessManager::SetupProcessZero()
{
	//初始化Process#0的Process和User结构
	Process* pProcZero = &(this->process[0]);
	pProcZero->p_stat = Process::SRUN;
	pProcZero->p_flag = Process::SLOAD | Process::SSYS;
	pProcZero->p_nice = 0;
	pProcZero->p_time = 0;
	pProcZero->p_pid = NextUniquePid();
	//除ppda区与核心栈外，进程没有用户态部分
	pProcZero->p_size = 0x1000;
	pProcZero->p_addr = PROCESS_ZERO_PPDA_ADDRESS;
	pProcZero->p_textp = NULL;

	User& u = Kernel::Instance().GetUser();
	u.u_procp = pProcZero;
	u.u_MemoryDescriptor.m_TextStartAddress = 0;
	u.u_MemoryDescriptor.m_TextSize = 0;
	u.u_MemoryDescriptor.m_DataStartAddress = 0;
	u.u_MemoryDescriptor.m_DataSize = 0;
	u.u_MemoryDescriptor.m_StackSize = 0;
	u.u_MemoryDescriptor.m_UserPageTableArray = NULL;
	u.u_MemoryDescriptor.current=0;
}

unsigned int ProcessManager::NextUniquePid()
{
	return ProcessManager::m_NextUniquePid++;
}

int ProcessManager::NewProc()
{
	Process* child = 0;
	for (int i = 0; i < ProcessManager::NPROC; i++ )
	{
		if ( process[i].p_stat == Process::SNULL )
		{
			child = &process[i];
			break;
		}
	}
	if ( !child ) 
	{
		Utility::Panic("No Proc Entry!");
	}

	User& u = Kernel::Instance().GetUser();
	Process* current = (Process*)u.u_procp;

	current->Clone(*child);

	SaveU(u.u_rsav);
	if(u.u_procp != current)
	{
		Diagnose::Write("NewProc: child resume\n");
		return 1;
	}
	Diagnose::Write("NewProc: parent continue\n");

	PageTable* pgTable = u.u_MemoryDescriptor.m_UserPageTableArray;  	// 将父进程的用户页表指针m_UserPageTableArray备份至pgTable
	u.u_procp = child;
	u.u_procp->p_pgTable=u.u_MemoryDescriptor.Initialize();  // 子进程暂时借用此memorydescriptor，执行此函数后分配一个1#用户页表和一个页目录，返回页目录

	UserPageManager& userPageManager = Kernel::Instance().GetUserPageManager();

	unsigned long srcAddress = current->p_addr;
	
	if ( !userPageManager.EnoughSpace(userPageManager.USER_PAGE_POOL_START_ADDR,userPageManager.USER_END_ADDR,PageManager::PAGE_SIZE) ) /* 不成功(没有足够空间)，将父进程图像复制到盘交换区。这块还没好 */
	{
		if(userPageManager.freeList.tail!=NULL)
		{
			Diagnose::Write("freeList remain at least one page,but Page array is full!");
			return 1;
		}
		page*swapped=u.u_MemoryDescriptor.selectVictim(pgTable);	//局部置换，选择当前进程的一私有页进行换出到交换区
		BufferManager&bfMgr=Kernel::Instance().GetBufferManager();
		SwapperManager&swpMgr=Kernel::Instance().GetSwapperManager();
		unsigned int blockno=swpMgr.AllocSwap();
		if(!blockno)	//分配失败
		{
			Diagnose::Write("Swapper Distribution fail!");
			return 1;
		}
		unsigned long virtualAddr=(u.u_MemoryDescriptor.current-1)*PageManager::PAGE_SIZE+0x400000;	//计算虚地址（因为Swap必须传入虚地址）
		bfMgr.Swap(blockno,virtualAddr,PageManager::PAGE_SIZE,Buf::B_WRITE);
		userPageManager.FreeMemory(swapped->pageNo<<12);
		swapped->next=NULL;
		pgTable->m_Entrys[u.u_MemoryDescriptor.current-1].m_Present=0;
		pgTable->m_Entrys[u.u_MemoryDescriptor.current-1].m_PageBaseAddress=blockno;
	}
	// 成功，父进程为子进程写页表，复制父进程可交换部分。由于本设计中采用COW，暂时只复制PPDA一页
	unsigned long desAddress = userPageManager.AllocMemory();   // 为子进程图像分配一页PPDA
	int n = userPageManager.PAGE_SIZE;	//复制PPDA区一页
	child->p_addr = desAddress;
	if ( NULL != pgTable )	//0#进程创建1#进程时传进来NULL
	{
		u.u_MemoryDescriptor.CopyUserPageTable(pgTable,userPageManager.m_pAllocator->Page);	//改成传入父进程的1#用户页表
	}
	else {
		KernelPageManager&kernelPageManager=Kernel::Instance().GetKernelPageManager();
		PageDirectory* pPageDirectory = u.u_procp->p_pgTable;
		if((unsigned long)pPageDirectory->m_Entrys[1].m_PageTableBaseAddress<<12!=((unsigned long)u.u_MemoryDescriptor.m_UserPageTableArray-Machine::KERNEL_SPACE_START_ADDRESS))
		{
			Diagnose::Write("pPageDirectory's UserPageTable!=MemoryDescriptor's UserPageTable!\n");
		}
		u.u_MemoryDescriptor.ClearUserPageTable();
	}
	while (n--)
	{
		Utility::CopySeg(srcAddress++, desAddress++);
	}

	u.u_procp = current;
	u.u_MemoryDescriptor.m_UserPageTableArray = pgTable;
	return 0;
}

/* 在进程切换的过程中，根本没有用到TSS */
int ProcessManager::Swtch()
{	
	//Diagnose::Write("Start Swtch()\n");
	User& u = Kernel::Instance().GetUser();
	SaveU(u.u_rsav);

	/* 0#进程上台*/
	Process* procZero = &process[0];

	/* 
	 * 将SwtchUStruct()和RetU()作为临界区，防止被中断打断。
	 * 如果在RetU()恢复esp之后，尚未恢复ebp时，中断进入会导致
	 * esp和ebp分别指向两个不同进程的核心栈中位置。 good comment！
	 *
	 * 为什么，由0#进程承担挑选就绪进程上台的操作？
	 * 单从进程切换的角度，完全可以由下台进程挑选就绪进程上台。 但是，考虑时钟中断。
	 * 一秒末的 例行处理，最好系统idle时，其次是在执行应用程序过程中；不可以放在内核执行过程中。
	 * 如何判断？
	 * 内核idle的标志：  0#进程在睡眠态执行idle()子程序。
	 * 看 TimeInterrupt.cpp的Line 82.
	 * 如是，必须由0#进程执行select()。
	 *
	 */
	X86Assembly::CLI();
	SwtchUStruct(procZero);
	/* 原来的宏调用是这样写的   RetU(u0)，u0参数没用到，会引起歧义，删除 */
	RetU();
	X86Assembly::STI();

	/* 挑选最适合上台的进程 */
	Process* selected = Select();
	/* 恢复被保存进程的现场 */
	X86Assembly::CLI();
	SwtchUStruct(selected);
	RetU();
	X86Assembly::STI();
	//上面的是原子操作，因此我选择在开中断后再修改其他关键信息
	User& newu = Kernel::Instance().GetUser();
	selected=newu.u_procp;
	Machine::Instance().m_PageDirectory=selected->p_pgTable;
	PageTable* loadedUserPageTable = (PageTable*)((Machine::Instance().GetPageDirectory().m_Entrys[1].m_PageTableBaseAddress << 12)
		+ Machine::KERNEL_SPACE_START_ADDRESS);
	Machine::Instance().m_UserPageTable=loadedUserPageTable;
	
	/*
	 * If the new process paused because it was
	 * swapped out, set the stack level to the last call
	 * to savu(u_ssav).  This means that the return
	 * which is executed immediately after the call to aretu
	 * actually returns from the last routine which did
	 * the savu.
	 *
	 * You are not expected to understand this.
	 */
	if ( newu.u_procp->p_flag & Process::SSWAP )
	{
		newu.u_procp->p_flag &= ~Process::SSWAP;
		aRetU(newu.u_ssav);
	}
	
	/* 
	 * 被fork出的进程在上台之前会在被调度上台时返回1，
	 * 并同时返回到NewProc()执行的地址
	 */
	return 1;
}

void ProcessManager::Sched()
{
	User&u=Kernel::Instance().GetUser();
	while(true)
	{
		u.u_procp->Sleep((unsigned long)u.u_procp,ProcessManager::PWAIT);
	}
}

void ProcessManager::Wait()
{
	int i;
	bool hasChild = false;
	User& u = Kernel::Instance().GetUser();
	SwapperManager& swapperMgr = Kernel::Instance().GetSwapperManager();
	BufferManager& bufMgr = Kernel::Instance().GetBufferManager();
	KernelPageManager& kernelPgMgr=Kernel::Instance().GetKernelPageManager();
	UserPageManager&userPgMgr=Kernel::Instance().GetUserPageManager();
	
	Diagnose::Write("Process %d finding dead son. They are ",u.u_procp->p_pid);
	while(true)
	{
		for ( i = 0; i < NPROC; i++ )
		{
			if ( u.u_procp->p_pid == process[i].p_ppid )
			{
				Diagnose::Write("Process %d (Status:%d)  ",process[i].p_pid,process[i].p_stat);
				hasChild = true;
				/* 睡眠等待直至子进程结束 */
				if( Process::SZOMB == process[i].p_stat )
				{
					/* wait()系统调用返回子进程的pid */
					u.u_ar0[User::EAX] = process[i].p_pid;

					process[i].p_stat = Process::SNULL;
					process[i].p_pid = 0;
					process[i].p_ppid = -1;
					process[i].p_sig = 0;
					process[i].p_flag = 0;
					kernelPgMgr.FreeMemory((unsigned long)process[i].p_pgTable-Machine::KERNEL_SPACE_START_ADDRESS);
					process[i].p_pgTable=NULL;

					/* 读入swapper中子进程u结构副本 */
					//先在页表区内存中分配一个临时页框用于读入PPDA
					unsigned long userAddr=kernelPgMgr.AllocMemory(kernelPgMgr.KERNEL_PAGE_POOL_START_ADDR,kernelPgMgr.KERNEL_PAGE_POOL_END_ADDR)+Machine::KERNEL_SPACE_START_ADDRESS;
					unsigned blkno=process[i].p_addr;
					User* pUser = (User *)userAddr;
					for(unsigned int i=0;i<PageManager::PAGE_SIZE;i+=BufferManager::BUFFER_SIZE,blkno++)
					{
						Buf* pBuf = bufMgr.Bread(DeviceManager::ROOTDEV, blkno);
						Utility::DWordCopy((int *)pBuf->b_addr,(int *)((char*)pUser+i),BufferManager::BUFFER_SIZE / sizeof(int));
						bufMgr.Brelse(pBuf);
					}
					swapperMgr.FreeSwap(process[i].p_addr);
					/* 把子进程的时间加到父进程上 */
					u.u_cstime += pUser->u_cstime +	pUser->u_stime;
					u.u_cutime += pUser->u_cutime + pUser->u_utime;

					int* pInt = (int *)u.u_arg[0];
					/* 获取子进程exit(int status)的返回值 */
					*pInt = pUser->u_arg[0];
					kernelPgMgr.FreeMemory(userAddr-Machine::KERNEL_SPACE_START_ADDRESS);
					Diagnose::Write("end wait\n");
					return;
				}
			}
		}
		if (true == hasChild)
		{
			/* 睡眠等待直至子进程结束 */
			Diagnose::Write("wait until child process Exit! ");
			u.u_procp->Sleep((unsigned long)u.u_procp, ProcessManager::PWAIT);
			Diagnose::Write("end sleep\n");
			continue;	/* 回到外层while(true)循环 */
		}
		else
		{
			/* 不存在需要等待结束的子进程，设置出错码，wait()返回 */
			u.u_error = User::ECHILD;
			break;	/* Get out of while loop */
		}
	}
}

void ProcessManager::Fork()
{
	User& u = Kernel::Instance().GetUser();
	Process* child = NULL;;

	/* 寻找空闲的process项，作为子进程的进程控制块 */
	for ( int i = 0; i < ProcessManager::NPROC; i++ )
	{
		if ( this->process[i].p_stat == Process::SNULL )
		{
			child = &this->process[i];
			break;
		}
	}
	if ( child == NULL )
	{
		/* 没有空闲process表项，返回 */
		u.u_error = User::EAGAIN;
		return;
	}

	if ( this->NewProc() )	/* 子进程返回1，父进程返回0 */
	{
		/* 子进程fork()系统调用返回0 */
		u.u_MemoryDescriptor.DisplayPageTable();
		u.u_ar0[User::EAX] = 0;
		u.u_cstime = 0;
		u.u_stime = 0;
		u.u_cutime = 0;
		u.u_utime = 0;
	}
	else
	{
		/* 父进程进程fork()系统调用返回子进程PID */
		u.u_ar0[User::EAX] = child->p_pid;
	}

	return;
}

extern "C" void runtime();
extern "C" void ExecShell();

/* 终于敢称为 V6 的 exec实现。缺憾：不支持 ISUID 比特 */
void ProcessManager::Exec()
{
	Inode* pInode;
	Text* pText;
	User& u = Kernel::Instance().GetUser();
	FileManager& fileMgr = Kernel::Instance().GetFileManager();
	UserPageManager& userPgMgr = Kernel::Instance().GetUserPageManager();
	KernelPageManager& kernelPgMgr = Kernel::Instance().GetKernelPageManager();
	BufferManager& bufMgr = Kernel::Instance().GetBufferManager();
	SwapperManager& swpMgr=Kernel::Instance().GetSwapperManager();
	(void)kernelPgMgr;

	Diagnose::Write("Process %d execing\n",u.u_procp->p_pid);
	pInode = fileMgr.NameI(FileManager::NextChar, FileManager::OPEN);
	if ( NULL == pInode )	//搜索目录失败
	{
		return;
	}

	/* 如果同时进行图像改换的进程数超出限制，则先进入睡眠 */
	while( this->ExeCnt >= NEXEC )
	{
		u.u_procp->Sleep((unsigned long)&ExeCnt, ProcessManager::EXPRI);
	}
	this->ExeCnt++;

	/* 进程必需拥有可执行文件的执行权限，且被执行的只能是一般文件。 */
	if ( fileMgr.Access(pInode, Inode::IEXEC) || (pInode->i_mode & Inode::IFMT) != 0 )
	{
		fileMgr.m_InodeTable->IPut(pInode);
		if ( this->ExeCnt >= NEXEC )
		{
			WakeUpAll((unsigned long)&ExeCnt);
		}
		this->ExeCnt--;
		return;
	}

	PEParser parser;

    if ( parser.HeaderLoad(pInode)==false )
    {
        fileMgr.m_InodeTable->IPut(pInode);
        return;
    }

	//新的文件合法性判断逻辑
	bool legalFile=1;
	unsigned int totalLength=parser.HeapSize+parser.StackSize;
	for(unsigned int i=0;i<parser.BSS_SECTION_IDX;i++)
	{
		if(parser.sectionHeaders[i].VirtualAddress+parser.sectionHeaders[i].Misc.VirtualSize>parser.sectionHeaders[i+1].VirtualAddress)
		{
			legalFile=0;
		}
		totalLength+=parser.sectionHeaders[i].Misc.VirtualSize;
	}
	totalLength+=parser.sectionHeaders[parser.BSS_SECTION_IDX].Misc.VirtualSize;

	if(totalLength+PageManager::PAGE_SIZE>MemoryDescriptor::USER_SPACE_SIZE-parser.TextAddress||!legalFile)
	{
		fileMgr.m_InodeTable->IPut(pInode);
		KernelPageManager& kpm = Kernel::Instance().GetKernelPageManager();
		kpm.FreeMemory((unsigned long)parser.sectionHeaders - Machine::KERNEL_SPACE_START_ADDRESS);
		u.u_error = User::ENOMEM;
		return;
	}

	/* 
	 * 分配内存用于存放用户程序运行需要的参数argc，argv[]，这些参数由exec()系统调用传入，
	 * 位于进程图像改换前的用户栈中，将参数备份到fakeStack中，然后可以释放原进程图像，
	 * 分配好新进程图像之后，再将fakeStack中的备份参数拷贝到新进程的用户栈中。
	 */
	/*释放原进程栈段*/
	PageTable*userPageTableArray=u.u_MemoryDescriptor.GetUserPageTableArray();
	u.u_MemoryDescriptor.FreePhyPage(0,u.u_MemoryDescriptor.m_StackSize,1);

	unsigned int stackPhyIdx=userPgMgr.AllocMemory()>>12;
	if(stackPhyIdx==0)
	{
		u.u_error = User::ENOMEM;
		return;
	}
	userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1].m_Present=1;
	userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1].m_ReadWriter=1;
	userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1].m_UserSupervisor=1;
	userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1].m_PageBaseAddress=stackPhyIdx;
	userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1].m_Used=1;

	int argc = u.u_arg[1];
	char** argv = (char **)u.u_arg[2];

	/* esp定位到栈底虚地址 */
	unsigned int esp = MemoryDescriptor::USER_SPACE_SIZE;
	int length;

	/* 复制argv[]指针数组指向的命令行参数字符串 */
	for (int i = 0; i < argc; i++ )
	{
		length = 0;
		/* 计算参数字符串长度，length不含'\0' */
		while( NULL != argv[i][length] )
		{
			length++;
		}
		esp = esp - (length + 1);
		/* 拷贝时将'\0'一起拷贝过去 */
		Utility::MemCopy((unsigned long)argv[i], esp, length + 1);
		/* 将参数字符串在新进程图像用户栈中的起始位置存入argv[i]，用户栈位于进程逻辑地址空间0x800000的底部 */
		argv[i] = (char *)esp;
	}

	/* 后续存放的是int型数值，这里以16字节边界对齐 */
	esp = esp & 0xFFFFFFF0;

	/* 复制argc和argv[] */
	int endValue = 0;
	esp -= sizeof(endValue);
	/* 向用户栈中写入endValue作为argv[]的结束 */
	Utility::MemCopy((unsigned long)&endValue, esp, sizeof(endValue));

	esp -= argc * sizeof(int);
	/* 写入argv[]的内容 */
	Utility::MemCopy((unsigned long)argv, esp, argc * sizeof(int));

	/* 令endValue指向当前栈中argv[]的起始地址，即argv[]入栈完毕后当前栈顶地址 */
	endValue = esp;
	esp -= sizeof(int);
	Utility::MemCopy((unsigned long)&endValue, esp, sizeof(int));

	/* 最后入栈argc */
	esp -= sizeof(int);
	Utility::MemCopy((unsigned long)&argc, esp, sizeof(int));	/* Done! */

	/* 释放原进程图像的共享正文段，数据段，只读数据段，BSS段，堆段 */
	if ( u.u_procp->p_textp != NULL )
	{
		if ( --u.u_procp->p_textp->x_count == 0 )
		{
			//通过内存中的页表释放所有已分配的物理页框(包括盘交换区上的)
			PageTable*userPageTableArray=(PageTable*)((u.u_procp->p_textp->x_pgTable->m_Entrys[1].m_PageTableBaseAddress<<12)+Machine::KERNEL_SPACE_START_ADDRESS);
			unsigned int textStartIdx=u.vm_list[u.TEXT_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP/PageManager::PAGE_SIZE;
			unsigned int rdataStartIdx=(u.vm_list[u.RDATA_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
			u.u_MemoryDescriptor.FreePhyPage(textStartIdx,u.u_procp->p_textp->x_size,0);
			u.u_MemoryDescriptor.FreePhyPage(rdataStartIdx,u.vm_list[u.RDATA_IDX].v_length,0);
			Kernel::Instance().GetFileManager().m_InodeTable->IPut(u.u_procp->p_textp->x_iptr);
			u.u_procp->p_textp->x_iptr = NULL;
		}
		else
		{
			ProcessManager&processMgr=Kernel::Instance().GetProcessManager();
			u.u_procp->p_textp->x_pgTable=NULL;
			for(int i=0;i<processMgr.NPROC;i++)
			{
				if(processMgr.process[i].p_textp==u.u_procp->p_textp&&processMgr.process[i].p_stat!=Process::SSTOP)
				{
					u.u_procp->p_textp->x_pgTable=processMgr.process[i].p_pgTable;
					break;
				}
			}
		}
		u.u_procp->p_textp = NULL;
	}

	unsigned int dataStartIdx=u.u_MemoryDescriptor.m_DataStartAddress%PageTable::SIZE_PER_PAGETABLE_MAP;
	dataStartIdx/=PageManager::PAGE_SIZE;
	u.u_MemoryDescriptor.FreePhyPage(dataStartIdx,u.u_MemoryDescriptor.m_DataSize,0);
	unsigned int bssStartIdx=(u.vm_list[u.BSS_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int heapStartIdx=(u.vm_list[u.HEAP_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	u.u_MemoryDescriptor.FreePhyPage(bssStartIdx,u.vm_list[u.BSS_IDX].v_length,0);
	u.u_MemoryDescriptor.FreePhyPage(heapStartIdx,u.vm_list[u.HEAP_IDX].v_length,0);

	//读取PE文件
	/*先hardcode vm_area的顺序，代码段，数据段，只读数据段，BSS段初始化*/
	for(unsigned int i=0;i<parser.IDATA_SECTION_IDX;i++)
	{
		u.vm_list[i].v_start=(parser.sectionHeaders[i].VirtualAddress+parser.ntHeader.OptionalHeader.ImageBase+PageManager::PAGE_SIZE-1)>>12<<12;
		u.vm_list[i].v_length=(parser.sectionHeaders[i].Misc.VirtualSize+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE*PageManager::PAGE_SIZE;		//虚空间内，要求按页对齐
		u.vm_list[i].f_offset=parser.sectionHeaders[i].PointerToRawData;	//文件中偏移量
		u.vm_list[i].f_length=parser.sectionHeaders[i].Misc.VirtualSize;	//文件中实际长度，实际上暂时没有用到
		u.vm_list[i].v_Present=1;
		u.vm_list[i].v_UserSupervisor=1;
		u.vm_list[i].next=&u.vm_list[i+1];
	}
	/*BSS区别*/
	if(u.vm_list[u.BSS_IDX].v_length==0)
	{
		u.vm_list[u.BSS_IDX].v_length=parser.ntHeader.OptionalHeader.SizeOfUninitializedData;
	}
	u.vm_list[u.BSS_IDX].f_length=0;
	/*虚拟段个性化*/
	u.vm_list[u.TEXT_IDX].v_ReadWriter=0;
	u.vm_list[u.DATA_IDX].v_ReadWriter=1;
	u.vm_list[u.RDATA_IDX].v_ReadWriter=0;
	u.vm_list[u.BSS_IDX].v_ReadWriter=1;
	/* 堆栈段vm_area初始化 */
	u.vm_list[u.HEAP_IDX].v_start=(u.vm_list[parser.BSS_SECTION_IDX].v_start+u.vm_list[parser.BSS_SECTION_IDX].v_length+PageManager::PAGE_SIZE-1)>>12<<12;//堆默认在BSS后面跟着
	u.vm_list[u.HEAP_IDX].v_length=(parser.HeapSize+PageManager::PAGE_SIZE-1)>>12<<12;
	u.vm_list[u.HEAP_IDX].f_offset=0;
	u.vm_list[u.HEAP_IDX].f_length=0;
	u.vm_list[u.HEAP_IDX].v_Present=1;
	u.vm_list[u.HEAP_IDX].v_ReadWriter=1;
	u.vm_list[u.HEAP_IDX].v_UserSupervisor=1;
	u.vm_list[u.HEAP_IDX].next=&u.vm_list[u.STACK_IDX];

	u.vm_list[u.STACK_IDX].v_length=(parser.StackSize+PageManager::PAGE_SIZE-1)>>12<<12;	//栈位置默认接壤用户空间底部8M
	u.vm_list[u.STACK_IDX].v_start=u.u_MemoryDescriptor.USER_SPACE_SIZE-u.vm_list[u.STACK_IDX].v_length;
	u.u_MemoryDescriptor.m_StackSize=u.vm_list[u.STACK_IDX].v_length;
	u.vm_list[u.STACK_IDX].f_length=0;
	u.vm_list[u.STACK_IDX].f_offset=0;
	u.vm_list[u.STACK_IDX].v_Present=1;
	u.vm_list[u.STACK_IDX].v_ReadWriter=1;
	u.vm_list[u.STACK_IDX].v_UserSupervisor=1;
	u.vm_list[u.STACK_IDX].next=NULL;

	/* 获取分析PE头结构得到正文段的起始地址、长度 */
	u.u_MemoryDescriptor.m_TextStartAddress = u.vm_list[u.TEXT_IDX].v_start;
	u.u_MemoryDescriptor.m_TextSize = u.vm_list[u.TEXT_IDX].v_length;

	/* 数据段的起始地址、长度 */
	u.u_MemoryDescriptor.m_DataStartAddress = u.vm_list[u.DATA_IDX].v_start;
	u.u_MemoryDescriptor.m_DataSize = u.vm_list[u.DATA_IDX].v_length;

	Process::ProcessState p_stat=u.u_procp->p_stat;
	u.u_procp->p_stat=Process::SSTOP;
	pText = NULL;
	/* 分配一个空闲Text结构，或者和其它进程共享同一正文段 */
	for ( int i = 0; i < ProcessManager::NTEXT; i++ )
	{
		if ( NULL == this->text[i].x_iptr )     /* 记下找到的第一个空闲text结构 */
		{
			if ( NULL == pText )
			{
				pText = &(this->text[i]);
			}
		}
		else if ( pInode == this->text[i].x_iptr )		/* 如果，这不是一个空闲text结构，看一下text结构指向的可执行文件是exec系统调用要执行的应用程序吗？ */
		{
			this->text[i].x_count++;
			u.u_procp->p_textp = &(this->text[i]);
			pText = NULL;	/* 与其它进程共享同一正文段，则pText重新清零，否则指向一空闲Text结构 */
			break;
		}
	}
	u.u_procp->p_stat=p_stat;

	int sharedText = 0;

	/* 没有可共享的现成Text结构，进行相应初始化 */
	if ( NULL != pText )
	{
		/* 
		 * 此处i_count++用于平衡XFree()函数中的IPut(x_iptr)；倘若只有Exec()开始处
		 * 调用NameI()函数中IGet()，以及Exec()结尾处IPut()释放exe文件的Inode回到空闲Inode表，
		 * 极端情况下：若后续进程很快也Exec()，获取空闲Inode恰好是之前加载的exe文件释放的Inode，
		 * 则会错误地判断：pInode (当前exe对应Inode) == this->text[i].x_iptr(之前exe文件Inode)，
		 * 导致和之前进程共享同一Text结构，即同一正文段，而实际上本该是两个独立的程序。
		 */
		pInode->i_count++;
		pText->x_count = 1;
		pText->x_iptr = pInode;
		pText->x_size = u.u_MemoryDescriptor.m_TextSize;
		/* 为正文段分配内存的步骤在vm版本不再需要，采用惰性分配方式 */
		pText->x_pgTable=u.u_procp->p_pgTable;
		/* 建立u区和Text结构的勾连关系 */
		u.u_procp->p_textp = pText;
	}
	else
	{
		pText = u.u_procp->p_textp;
		sharedText = 1;
	}

	Diagnose::Write("Process %x, p_addr %x, p_size %x, x_size %x\n",
			u.u_procp->p_pid,u.u_procp->p_addr,u.u_procp->p_size,u.u_procp->p_textp->x_size);
	
	if(sharedText)
	{
		u.u_MemoryDescriptor.EstablishUserPageTable(u.vm_list,1,pText->x_pgTable);
	}
	else
	{
		u.u_MemoryDescriptor.EstablishUserPageTable(u.vm_list,0,NULL);
	}

	/* 减少ExeCnt计数值 */
	if ( this->ExeCnt >= NEXEC )
	{
		WakeUpAll((unsigned long)&ExeCnt);
	}
	this->ExeCnt--;

	/* 用默认的方式处理信号  */
	for (int i = 0; i < u.NSIG ; i++)
	{
		u.u_signal[i] = 0;
	}

	/* 清0所有通用寄存器  */
	for (int i = User::EAX - 4; i < User::EAX - 4*7 ; i = i - 4)
	{
		u.u_ar0[i] = 0;     /* 下标写成  User::EAX + i 可读性要强一些，但是运算速度慢了。就小抠，追求速度吧 */
	}

	KernelPageManager& kpm = Kernel::Instance().GetKernelPageManager();
	kpm.FreeMemory((unsigned long)parser.sectionHeaders - Machine::KERNEL_SPACE_START_ADDRESS);

	/* 将exe程序的入口地址放入核心栈现场保护区中的EAX作为系统调用返回值，这个是runtime要用  */
	u.u_ar0[User::EAX] = parser.EntryPointAddress;
	/* 构造出Exec()系统调用的退出环境，使之退出到ring3时，开始执行user code */
	struct pt_context* pContext = (struct pt_context *)u.u_arg[4];
	pContext->eip = 0x00000000;	/* 退出到ring3特权级下从线性地址0x00000000处runtime()开始执行 */
	//pContext->eip = parser.EntryPointAddress;
	pContext->xcs = Machine::USER_CODE_SEGMENT_SELECTOR;
	pContext->eflags = 0x200;	/* 此项是否篡改无关紧要 */
	pContext->esp = esp;
	pContext->xss = Machine::USER_DATA_SEGMENT_SELECTOR;
	Diagnose::Write("Exec exit!\n");
}

Process* ProcessManager::Select ()
{
	/* 前一次选中上台进程 */
	static int lastSelect = 0;
	
	while (true)
	{
		int priority = 256;
		int best = -1;	/* 本轮搜索找到的最合适上台进程 */

		this->RunRun = 0;

		/* 搜索优先级最高的可运行进程 */
		for ( int count = 0; count < NPROC ; count++ )
		{
			/* 从上一次被选中进程的下一个开始回环扫描，而不是每次从0#进程开始，保证各进程机会均等 */
			int i = (lastSelect + 1 + count) % NPROC;
			if ( Process::SRUN == process[i].p_stat && (process[i].p_flag & Process::SLOAD) != 0 )
			{
				if ( process[i].p_pri < priority )
				{
					best = i;
					priority = process[i].p_pri;
				}
			}
		}
		if ( -1 == best )
		{
			__asm__ __volatile__("hlt");
			continue;
		}

		SwtchNum++;
		if ( SwtchNum & 0x80000000 ) 
		{
			SwtchNum = 0;	/* 计数溢出变为负数后，重置为零 */
		}
		/* 如果选出优先级最高的可运行进程 */
		this->CurPri = priority;
		lastSelect = best;
		return &process[best];

	}
}

void ProcessManager::Kill()
{
	User& u = Kernel::Instance().GetUser();
	int pid = u.u_arg[0];
	int signal = u.u_arg[1];
	bool flag = false;

	for ( int i = 0; i < ProcessManager::NPROC; i++ )
	{
		/* 不允许发送信号给进程自身 */
		if ( u.u_procp == &process[i] )
		{
			continue;
		}
		/* 不是信号的接收方目标进程，继续搜寻 */
		if ( pid != 0 && process[i].p_pid != pid)
		{
			continue;
		}
		/* pid为0，则将信号发送至与发送进程同一终端的所有进程，0#进程不包括在内 */
		if ( pid == 0 && (process[i].p_ttyp != u.u_procp->p_ttyp || i == 0 ) )
		{
			continue;
		}
		/* 除非是超级用户，否则要求发送、接收进程u.uid相同，即不可给其它用户进程发送信号 */
		if ( u.u_uid != 0 && u.u_uid != process[i].p_uid )
		{
			continue;
		}
		flag = true;
		/* 信号发送给满足条件的目标进程 */
		process[i].PSignal(signal);
	}
	if ( false == flag )
	{
		u.u_error = User::ESRCH;
	}
}

void ProcessManager::WakeUpAll(unsigned long chan)
{
	/* 唤醒系统中所有因chan而进入睡眠的进程 */
	for(int i = 0; i < ProcessManager::NPROC; i++)
	{
		if( this->process[i].IsSleepOn(chan) )
		{
			this->process[i].SetRun();
		}
	}
}

void ProcessManager::Signal( TTy* pTTy, int signal )
{
	for ( int i = 0; i < ProcessManager::NPROC; i++ )
	{
		if ( this->process[i].p_ttyp == pTTy )
		{
			this->process[i].PSignal(signal);
		}
	}
}
