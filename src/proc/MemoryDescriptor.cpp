#include "MemoryDescriptor.h"
#include "Kernel.h"
#include "PageManager.h"
#include "Machine.h"
#include "PageDirectory.h"
#include "Video.h"
#include "SwapperManager.h"

PageDirectory* MemoryDescriptor::Initialize()
{
	KernelPageManager& kernelPageManager = Kernel::Instance().GetKernelPageManager();
	
	/* m_UserPageTableArray需要把AllocMemory()返回的物理内存地址 + 0xC0000000 */
//	this->m_UserPageTableArray = (PageTable*)(kernelPageManager.AllocMemory(sizeof(PageTable) * USER_SPACE_PAGE_TABLE_CNT) + Machine::KERNEL_SPACE_START_ADDRESS);
	//为1#用户页表分配一页，并存储在m_UserPageTableArray里面
	this->m_UserPageTableArray=(PageTable*)(kernelPageManager.AllocMemory(kernelPageManager.KERNEL_PAGE_POOL_START_ADDR,kernelPageManager.KERNEL_PAGE_POOL_END_ADDR)+ Machine::KERNEL_SPACE_START_ADDRESS);
	for ( unsigned int i = 0; i < PageTable::ENTRY_CNT_PER_PAGETABLE; i++ )
	{
		PageTableEntry& entry = this->m_UserPageTableArray->m_Entrys[i];
		entry.m_Present = 0;
		entry.m_ReadWriter = 0;
		entry.m_UserSupervisor = 0;
		entry.m_WriteThrough = 0;
		entry.m_CacheDisabled = 0;
		entry.m_Accessed = 0;
		entry.m_Dirty = 0;
		entry.m_PageTableAttribueIndex = 0;
		entry.m_GlobalPage = 0;
		entry.m_ForSystemUser = 0;
		entry.m_PageBaseAddress = 0;
		entry.m_Used=0;
	}
	//分配一个页目录进行初始化，并返回页目录的起始虚地址
	const unsigned int kPageTableIdx=Machine::KERNEL_SPACE_START_ADDRESS/PageTable::SIZE_PER_PAGETABLE_MAP;
	PageDirectory* pDirectory=(PageDirectory*)(kernelPageManager.AllocMemory(kernelPageManager.KERNEL_PAGE_POOL_START_ADDR,kernelPageManager.KERNEL_PAGE_POOL_END_ADDR)+Machine::KERNEL_SPACE_START_ADDRESS);
	for ( unsigned int i = 0; i < 1024; i++ )
	{
		PageDirectoryEntry& entry = pDirectory->m_Entrys[i];
		entry.m_Present = 0;
		entry.m_ReadWriter = 0;
		entry.m_UserSupervisor = 0;
		entry.m_WriteThrough = 0;
		entry.m_CacheDisabled = 0;
		entry.m_Accessed = 0;
		entry.m_Reserved = 0;
		entry.m_PageSize = 0;
		entry.m_GlobalPage = 0;
		entry.m_ForSystemUser = 0;
		entry.m_PageTableBaseAddress = 0;
	}
	pDirectory->m_Entrys[kPageTableIdx].m_UserSupervisor=0;
	pDirectory->m_Entrys[kPageTableIdx].m_Present=1;
	pDirectory->m_Entrys[kPageTableIdx].m_ReadWriter=1;
	pDirectory->m_Entrys[kPageTableIdx].m_PageTableBaseAddress=Machine::KERNEL_PAGE_TABLE_BASE_ADDRESS>>12;
	const unsigned int u0PageTableIdx=0x0;
	pDirectory->m_Entrys[u0PageTableIdx].m_UserSupervisor=1;
	pDirectory->m_Entrys[u0PageTableIdx].m_Present=1;
	pDirectory->m_Entrys[u0PageTableIdx].m_ReadWriter=1;
	pDirectory->m_Entrys[u0PageTableIdx].m_PageTableBaseAddress=Machine::USER_ZERO_PAGE_TABLE_BASE_ADDRESS>>12;
	const unsigned int u1PageTableIdx=0x1;
	pDirectory->m_Entrys[u1PageTableIdx].m_UserSupervisor=1;
	pDirectory->m_Entrys[u1PageTableIdx].m_Present=1;
	pDirectory->m_Entrys[u1PageTableIdx].m_ReadWriter=1;
	pDirectory->m_Entrys[u1PageTableIdx].m_PageTableBaseAddress=((unsigned long)this->m_UserPageTableArray-Machine::KERNEL_SPACE_START_ADDRESS)>>12;
	return pDirectory;
}

//释放核心态内存也需要物理地址
void MemoryDescriptor::Release()
{
	KernelPageManager& kernelPageManager = Kernel::Instance().GetKernelPageManager();
	if ( this->m_UserPageTableArray )
	{
		kernelPageManager.FreeMemory((unsigned long)this->m_UserPageTableArray - Machine::KERNEL_SPACE_START_ADDRESS);
		this->m_UserPageTableArray = NULL;
	}
}


unsigned int MemoryDescriptor::MapEntry(unsigned long virtualAddress, unsigned int size, unsigned long phyPageIdx, bool isReadWrite)
{	
	unsigned long address = virtualAddress - USER_SPACE_START_ADDRESS;
	
	//计算从pagetable的哪一个地址开始映射
	unsigned long startIdx = address >> 12;
	unsigned long cnt = ( size + (PageManager::PAGE_SIZE - 1) )/ PageManager::PAGE_SIZE;	//页数向上取整

	PageTableEntry* entrys = (PageTableEntry*)this->m_UserPageTableArray;		//已经是用户页表了，virtualAddress应该始终大于4M（无论代码段还是数据段）
	for ( unsigned int i = startIdx; i < startIdx + cnt; i++, phyPageIdx++ )
	{
		entrys[i].m_Present = 0x1;
		entrys[i].m_UserSupervisor = 0x1;
		entrys[i].m_ReadWriter = isReadWrite;
		entrys[i].m_PageBaseAddress = phyPageIdx;
	}
	return phyPageIdx;
}

void MemoryDescriptor::MapTextEntrys(unsigned long textStartAddress, unsigned long textSize, unsigned long textPageIdx)
{
	this->MapEntry(textStartAddress, textSize, textPageIdx, false);
}
void MemoryDescriptor::MapDataEntrys(unsigned long dataStartAddress, unsigned long dataSize, unsigned long dataPageIdx)
{
	this->MapEntry(dataStartAddress, dataSize, dataPageIdx, true);
}

void MemoryDescriptor::MapStackEntrys(unsigned long stackSize, unsigned long stackPageIdx)
{
	unsigned long stackStartAddress = (USER_SPACE_START_ADDRESS + USER_SPACE_SIZE - stackSize) & 0xFFFFF000;
	this->MapEntry(stackStartAddress, stackSize, stackPageIdx, true);
}

PageTable* MemoryDescriptor::GetUserPageTableArray()
{
	return this->m_UserPageTableArray;
}
unsigned long MemoryDescriptor::GetTextStartAddress()
{
	return this->m_TextStartAddress;
}
unsigned long MemoryDescriptor::GetTextSize()
{
	return this->m_TextSize;
}
unsigned long MemoryDescriptor::GetDataStartAddress()
{
	return this->m_DataStartAddress;
}
unsigned long MemoryDescriptor::GetDataSize()
{
	return this->m_DataSize;
}
unsigned long MemoryDescriptor::GetStackSize()
{
	return this->m_StackSize;
}

bool MemoryDescriptor::CheckUserSpaceSize( vm_area vm_list[] )
{
	User&u=Kernel::Instance().GetUser();
	bool legalFile=1;
	unsigned int totalLength=vm_list[u.HEAP_IDX].v_length+vm_list[u.STACK_IDX].v_length;
	for(unsigned int i=0;i<u.BSS_IDX;i++)
	{
		if(vm_list[i].v_start+vm_list[i].v_length>vm_list[i+1].v_start)
		{
			legalFile=0;
		}
		totalLength+=vm_list[i].v_length;
	}
	totalLength+=vm_list[u.BSS_IDX].v_length;

	if(totalLength+PageManager::PAGE_SIZE>MemoryDescriptor::USER_SPACE_SIZE-0x400000||!legalFile)
	{
		u.u_error = User::ENOMEM;
		Diagnose::Write("u.u_error = %d\n",u.u_error);
		return false;
	}
	return true;
}

/* 因为需要供Newproc调用，所以只写页表，不建立地址映射关系 */
/*改：只会被Exec调用，并既写页表且建立映射关系*/
/*二改：vm版本中，不再需要为正文段急性分配内存，而使用缺页异常处理程序进行惰性分配。此时EstablishUserPageTable函数只对正文段初始化或者对所有短进行置NULL操作*/
void MemoryDescriptor::EstablishUserPageTable(vm_area vm_list[],int shared,PageDirectory*p_pgTable)
{
	User& u = Kernel::Instance().GetUser();
	PageTable*userPageTableArray=u.u_MemoryDescriptor.m_UserPageTableArray;
	UserPageManager&userPageManager=Kernel::Instance().GetUserPageManager();
	//MemoryDescriptor和vm_list应当解耦，所以这里没有使用简单的循环来直接进行页表置NULL
	unsigned int textVirtualIdx=(vm_list[u.TEXT_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int dataVirtualIdx=(vm_list[u.DATA_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int rdataVirtualIdx=(vm_list[u.RDATA_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int bssVirtualIdx=(vm_list[u.BSS_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int heapVirtualIdx=(vm_list[u.HEAP_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int stackVirtualIdx=(vm_list[u.STACK_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int textPageNum=(vm_list[u.TEXT_IDX].v_length+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	unsigned int dataPageNum=(vm_list[u.DATA_IDX].v_length+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	unsigned int rdataPageNum=(vm_list[u.RDATA_IDX].v_length+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	unsigned int bssPageNum=(vm_list[u.BSS_IDX].v_length+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	unsigned int heapPageNum=(vm_list[u.HEAP_IDX].v_length+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	unsigned int stackPageNum=(vm_list[u.STACK_IDX].v_length+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	if(shared)
	{
		PageTable*oldUserPageTableArray=(PageTable*)((p_pgTable->m_Entrys[1].m_PageTableBaseAddress<<12)+Machine::KERNEL_SPACE_START_ADDRESS);
		for(unsigned int i=0;i<textPageNum;i++)
		{
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_Present=oldUserPageTableArray->m_Entrys[textVirtualIdx+i].m_Present;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_ReadWriter=0;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_UserSupervisor=1;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_PageBaseAddress=oldUserPageTableArray->m_Entrys[textVirtualIdx+i].m_PageBaseAddress;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_Used=oldUserPageTableArray->m_Entrys[textVirtualIdx+i].m_Used;
		}
	}
	else
	{
		//若当前进程的正文段不被共享，则将页表对应的项置为NULL
		for(unsigned int i=0;i<textPageNum;i++)
		{
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_Used=0;
		}
	}
	for(unsigned int i=0;i<dataPageNum;i++)
	{
		userPageTableArray->m_Entrys[dataVirtualIdx+i].m_Used=0;
	}
	for(unsigned int i=0;i<rdataPageNum;i++)
	{
		userPageTableArray->m_Entrys[rdataVirtualIdx+i].m_Used=0;
	}
	for(unsigned int i=0;i<bssPageNum;i++)
	{
		userPageTableArray->m_Entrys[bssVirtualIdx+i].m_Used=0;
	}
	for(unsigned int i=0;i<heapPageNum;i++)
	{
		userPageTableArray->m_Entrys[heapVirtualIdx+i].m_Used=0;
	}
	//stack是特例，最后一页fakestack已经映射完毕
	for(unsigned int i=0;i<stackPageNum-1;i++)
	{
		userPageTableArray->m_Entrys[stackVirtualIdx+i].m_Used=0;
	}
}

//改写为通过父进程的1#用户页表m_UserPageTableArray写子进程的1#用户页表
void MemoryDescriptor::CopyUserPageTable(PageTable* pgTable,unsigned int Page[])
{
	ClearUserPageTable();
	User& u=Kernel::Instance().GetUser();
	PageTableEntry*entry=pgTable->m_Entrys;
	PageTableEntry*new_entry=(PageTableEntry*)this->m_UserPageTableArray;                                                                                    
    unsigned int textStartIdx=(u.vm_list[u.TEXT_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;                
    unsigned int textPageNum=(u.vm_list[u.TEXT_IDX].v_length+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;   
	unsigned int rdataStartIdx=(u.vm_list[u.RDATA_IDX].v_start%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int rdataPageNum=(u.vm_list[u.RDATA_IDX].v_length+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	//完全复制父进程PTE
	for(unsigned i=0;i<PageTable::ENTRY_CNT_PER_PAGETABLE;i++)
	{
		new_entry[i].m_Present=entry[i].m_Present;
		new_entry[i].m_UserSupervisor=entry[i].m_UserSupervisor;
		bool isText=(i>=textStartIdx&&i<(textStartIdx+textPageNum))||(i>=rdataStartIdx&&i<(rdataStartIdx+rdataPageNum));
		if(entry[i].m_Present&&!isText)//若相应物理页在内存，COW
		{
			Page[entry[i].m_PageBaseAddress]++;
			entry[i].m_ReadWriter=false;
		}
		new_entry[i].m_ReadWriter=entry[i].m_ReadWriter;
		new_entry[i].m_PageBaseAddress=entry[i].m_PageBaseAddress;
		new_entry[i].m_Used=entry[i].m_Used;
	}
	FlushPageDirectory((unsigned long)&Machine::Instance().GetPageDirectory()-Machine::KERNEL_SPACE_START_ADDRESS);
}

void MemoryDescriptor::DisplayPageTable()
{
	unsigned int i,j;

	Diagnose::Write("Process PT:");
	for ( j = 0; j < PageTable::ENTRY_CNT_PER_PAGETABLE; j++)
			if ( 1 == this->m_UserPageTableArray->m_Entrys[j].m_Present )
				Diagnose::Write("<%d,%x>  ",j,this->m_UserPageTableArray->m_Entrys[j].m_PageBaseAddress);
	Diagnose::Write("\n");

	Diagnose::Write("<PPDA,%x>  ",Machine::Instance().GetKernelPageTable().m_Entrys[1023].m_PageBaseAddress);
}

void MemoryDescriptor::ClearUserPageTable()
{
	User& u = Kernel::Instance().GetUser();
	PageTable* pUserPageTable = u.u_MemoryDescriptor.m_UserPageTableArray;

	unsigned int j ;

	for (j = 0; j < PageTable::ENTRY_CNT_PER_PAGETABLE; j++ )
	{
		pUserPageTable->m_Entrys[j].m_Present = 0;
		pUserPageTable->m_Entrys[j].m_ReadWriter = 0;
		pUserPageTable->m_Entrys[j].m_UserSupervisor = 1;
		pUserPageTable->m_Entrys[j].m_PageBaseAddress = 0;
		pUserPageTable->m_Entrys[j].m_Used=0;
	}

}

void MemoryDescriptor::FreePhyPage(unsigned int sectionStartIdx,unsigned long sectionSize,bool isStack)
{
	PageTable*userPageTableArray=this->GetUserPageTableArray();
	UserPageManager&userPgMgr=Kernel::Instance().GetUserPageManager();
	SwapperManager&swpMgr=Kernel::Instance().GetSwapperManager();
	if(!isStack)
	{
		for(unsigned int i=0;i<(sectionSize+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;i++)
		{
			if(userPageTableArray->m_Entrys[sectionStartIdx+i].m_Used==1)
			{
				if(userPageTableArray->m_Entrys[sectionStartIdx+i].m_Present==1)
				{
					unsigned long phyAddr=(unsigned long)(userPageTableArray->m_Entrys[sectionStartIdx+i].m_PageBaseAddress<<12);
					userPgMgr.FreeMemory(phyAddr);
				}
				else
				{
					swpMgr.FreeSwap(userPageTableArray->m_Entrys[sectionStartIdx+i].m_PageBaseAddress);
				}
				userPageTableArray->m_Entrys[sectionStartIdx+i].m_Used=0;
			}
		}
	}
	else
	{
		for(unsigned int i=0;i<(sectionSize+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;i++)
		{
			if(userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1-i].m_Used==1)
			{
				if(userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1-i].m_Present==1)
				{
					unsigned long phyIdx=(unsigned long)userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1-i].m_PageBaseAddress<<12;
					userPgMgr.FreeMemory(phyIdx);
				}
				else
				{
					swpMgr.FreeSwap(userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1-i].m_PageBaseAddress);
				}
				userPageTableArray->m_Entrys[PageTable::ENTRY_CNT_PER_PAGETABLE-1-i].m_Used=0;	//表示NULL
			}
		}
	}
}

page* MemoryDescriptor::selectVictim(PageTable*userPageTableArray)/*先clock选取一个RW页放在磁盘中（在COW实现中，RW页一定私有。因为若RW页被子进程复制，该pte会改为RO）*/
{
	UserPageManager&userPgMgr=Kernel::Instance().GetUserPageManager();
	PageAllocator*pAllocator=userPgMgr.m_pAllocator;
	page*selected=NULL;
	//先找A==0和D==0的
	for(unsigned int i=0;i<PageTable::ENTRY_CNT_PER_PAGETABLE;i++,current++)
	{
		current%=PageTable::ENTRY_CNT_PER_PAGETABLE;
		if(pAllocator->Page[userPageTableArray->m_Entrys[current].m_PageBaseAddress]>1) continue;
		if(userPageTableArray->m_Entrys[current].m_Used==1&&userPageTableArray->m_Entrys[current].m_Present==1&&userPageTableArray->m_Entrys[current].m_ReadWriter==1)
		{
			if(userPageTableArray->m_Entrys[current].m_Accessed==0&&userPageTableArray->m_Entrys[current].m_Dirty==0)
			{
				unsigned int pagesIdx=((userPageTableArray->m_Entrys[current].m_PageBaseAddress<<12)-userPgMgr.USER_PAGE_POOL_START_ADDR)>>12;
				selected=&userPgMgr.pages[pagesIdx];
				current++;
				current%=PageTable::ENTRY_CNT_PER_PAGETABLE;
				return selected;
			}
		}
	}
	//找不到，找A==0的，总能找到
	while(selected==NULL)
	{
		current%=PageTable::ENTRY_CNT_PER_PAGETABLE;
		if(pAllocator->Page[userPageTableArray->m_Entrys[current].m_PageBaseAddress]>1)		//缺陷：没有考虑整个页表的物理页框引用次数都大于1的情况，此时会导致无法退出。
		{
			current++;
			continue;
		}
		if(userPageTableArray->m_Entrys[current].m_Used==1&&userPageTableArray->m_Entrys[current].m_Present==1&&userPageTableArray->m_Entrys[current].m_ReadWriter==1)
		{
			if(userPageTableArray->m_Entrys[current].m_Accessed==1)
			{
				userPageTableArray->m_Entrys[current].m_Accessed=0;
			}
			else
			{
				unsigned int pagesIdx=((userPageTableArray->m_Entrys[current].m_PageBaseAddress<<12)-userPgMgr.USER_PAGE_POOL_START_ADDR)>>12;
				selected=&userPgMgr.pages[pagesIdx];
				current++;
				current%=PageTable::ENTRY_CNT_PER_PAGETABLE;
				return selected;
			}
		}
		current++;
	}
	
	return selected;
}
