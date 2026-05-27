#include "MemoryDescriptor.h"
#include "Kernel.h"
#include "PageManager.h"
#include "Machine.h"
#include "PageDirectory.h"
#include "Video.h"

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

bool MemoryDescriptor::CheckUserSpaceSize( unsigned long textVirtualAddress, unsigned long textSize, unsigned long dataVirtualAddress, unsigned long dataSize, unsigned long stackSize )
{
	User& u = Kernel::Instance().GetUser();

	/* 如果超出允许的用户程序最大8M的地址空间限制 */
	if ( textSize + dataSize + stackSize  + PageManager::PAGE_SIZE > USER_SPACE_SIZE - textVirtualAddress)
	{
		u.u_error = User::ENOMEM;
		Diagnose::Write("u.u_error = %d\n",u.u_error);
		return false;
	}
	return true;
}

/* 因为需要供Newproc调用，所以只写页表，不建立地址映射关系 */
/*改：只会被Exec调用，并既写页表且建立映射关系*/
void MemoryDescriptor::EstablishUserPageTable( unsigned long textVirtualAddress, unsigned long textSize, unsigned long dataVirtualAddress, unsigned long dataSize,int shared,PageDirectory*p_pgTable)
{
	User& u = Kernel::Instance().GetUser();
	PageTable*userPageTableArray=u.u_MemoryDescriptor.m_UserPageTableArray;
	UserPageManager&userPageManager=Kernel::Instance().GetUserPageManager();
	unsigned int textVirtualIdx=(textVirtualAddress%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned int dataVirtualIdx=(dataVirtualAddress%PageTable::SIZE_PER_PAGETABLE_MAP)/PageManager::PAGE_SIZE;
	unsigned long textPageNum=(textSize+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	unsigned long dataPageNum=(dataSize+PageManager::PAGE_SIZE-1)/PageManager::PAGE_SIZE;
	if(shared)
	{
		PageTable*oldUserPageTableArray=(PageTable*)((p_pgTable->m_Entrys[1].m_PageTableBaseAddress<<12)+Machine::KERNEL_SPACE_START_ADDRESS);
		for(unsigned int i=0;i<textPageNum;i++)
		{
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_Present=1;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_ReadWriter=0;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_UserSupervisor=1;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_PageBaseAddress=oldUserPageTableArray->m_Entrys[textVirtualIdx+i].m_PageBaseAddress;
		}
	}
	else
	{
		for(unsigned int i=0;i<textPageNum;i++)
		{
			unsigned int textPhyIdx=userPageManager.AllocMemory(userPageManager.USER_PAGE_POOL_START_ADDR,userPageManager.USER_END_ADDR)/PageManager::PAGE_SIZE;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_Present=1;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_ReadWriter=0;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_UserSupervisor=1;
			userPageTableArray->m_Entrys[textVirtualIdx+i].m_PageBaseAddress=textPhyIdx;
		}
	}
	for(unsigned int i=0;i<dataPageNum;i++)
	{
		unsigned int dataPhyIdx=userPageManager.AllocMemory(userPageManager.USER_PAGE_POOL_START_ADDR,userPageManager.USER_END_ADDR)/PageManager::PAGE_SIZE;
		userPageTableArray->m_Entrys[dataVirtualIdx+i].m_Present=1;
		userPageTableArray->m_Entrys[dataVirtualIdx+i].m_ReadWriter=1;
		userPageTableArray->m_Entrys[dataVirtualIdx+i].m_UserSupervisor=1;
		userPageTableArray->m_Entrys[dataVirtualIdx+i].m_PageBaseAddress=dataPhyIdx;
	}
}

//改写为通过父进程的1#用户页表m_UserPageTableArray写子进程的1#用户页表
void MemoryDescriptor::CopyUserPageTable(PageTable* pgTable,unsigned int Page[])
{
	User& u=Kernel::Instance().GetUser();
	PageTableEntry*entry=pgTable->m_Entrys;
	PageTableEntry*new_entry=(PageTableEntry*)this->m_UserPageTableArray;

	
	for(unsigned i=0;i<PageTable::ENTRY_CNT_PER_PAGETABLE;i++)
	{
		new_entry[i].m_Present=entry[i].m_Present;
		new_entry[i].m_UserSupervisor=entry[i].m_UserSupervisor;
		if(entry[i].m_ReadWriter)
		{
			Page[entry[i].m_PageBaseAddress]++;
			entry[i].m_ReadWriter=false;
		}
		new_entry[i].m_ReadWriter=entry[i].m_ReadWriter;
		new_entry[i].m_PageBaseAddress=entry[i].m_PageBaseAddress;
	}
	FlushPageDirectory((unsigned long)&Machine::Instance().GetPageDirectory()-Machine::KERNEL_SPACE_START_ADDRESS);
}

void MemoryDescriptor::DisplayPageTable()
{
	unsigned int i,j;

	Diagnose::Write("Process PT:");
	for (i = 0; i < Machine::USER_PAGE_TABLE_CNT; i++)
		for ( j = 0; j < PageTable::ENTRY_CNT_PER_PAGETABLE; j++)
			if ( 1 == this->m_UserPageTableArray[i].m_Entrys[j].m_Present )
				Diagnose::Write("<%d,%x>  ",i*1024+j,this->m_UserPageTableArray[i].m_Entrys[j].m_PageBaseAddress);
	Diagnose::Write("\n");

	Diagnose::Write("<PPDA,%x>  ",Machine::Instance().GetKernelPageTable().m_Entrys[1023].m_PageBaseAddress);

	PageTable* pUserPageTable = (PageTable*)((unsigned int)(Machine::Instance().GetPageDirectory().m_Entrys[0].m_PageTableBaseAddress) << 12 | 0xC0000000);
	Diagnose::Write("User PT: %x", (unsigned int)pUserPageTable);

//	for (i = 0; i < Machine::USER_PAGE_TABLE_CNT; i++)
		for ( j = 1; j < PageTable::ENTRY_CNT_PER_PAGETABLE; j++)
			if ( 1 == pUserPageTable[1].m_Entrys[j].m_Present )
				Diagnose::Write("<%d,%x>  ",1*1024+j,pUserPageTable[1].m_Entrys[j].m_PageBaseAddress);
	Diagnose::Write("\n");
}

void MemoryDescriptor::ClearUserPageTable()
{
	User& u = Kernel::Instance().GetUser();
	PageTable* pUserPageTable = u.u_MemoryDescriptor.m_UserPageTableArray;

	unsigned int i ;
	unsigned int j ;

	for (i = 0; i < Machine::USER_PAGE_TABLE_CNT; i++)
	{
		for (j = 0; j < PageTable::ENTRY_CNT_PER_PAGETABLE; j++ )
		{
			pUserPageTable[i].m_Entrys[j].m_Present = 0;
			pUserPageTable[i].m_Entrys[j].m_ReadWriter = 0;
			pUserPageTable[i].m_Entrys[j].m_UserSupervisor = 1;
			pUserPageTable[i].m_Entrys[j].m_PageBaseAddress = 0;
		}
	}

}

/*
void MemoryDescriptor::MapToPageTable()
{
	User& u = Kernel::Instance().GetUser();

	if(u.u_MemoryDescriptor.m_UserPageTableArray == NULL)
		return;

	PageTable* pUserPageTable = Machine::Instance().GetUserPageTableArray();
	unsigned int textAddress = 0;
	if ( u.u_procp->p_textp != NULL )
	{
		textAddress = u.u_procp->p_textp->x_caddr;
	}

	for (unsigned int i = 0; i < Machine::USER_PAGE_TABLE_CNT; i++)
	{
		for ( unsigned int j = 0; j < PageTable::ENTRY_CNT_PER_PAGETABLE; j++ )
		{
			pUserPageTable[i].m_Entrys[j].m_Present = 0;   //先清0

			if ( 1 == this->m_UserPageTableArray[i].m_Entrys[j].m_Present )
			{
				pUserPageTable[i].m_Entrys[j].m_Present = 1;
				pUserPageTable[i].m_Entrys[j].m_ReadWriter = this->m_UserPageTableArray[i].m_Entrys[j].m_ReadWriter;
				pUserPageTable[i].m_Entrys[j].m_PageBaseAddress = this->m_UserPageTableArray[i].m_Entrys[j].m_PageBaseAddress;
			}
		}
	}

	FlushPageDirectory();
}*/

//void MemoryDescriptor::MapToPageTable()
//{
//	User& u = Kernel::Instance().GetUser();
//	Machine& machine = Machine::Instance();
//	unsigned long pgDirPhys = (unsigned long)(u.u_procp->p_pgTable) - Machine::KERNEL_SPACE_START_ADDRESS;
//
//    unsigned long phyFrame = (unsigned long)(u.u_MemoryDescriptor.m_UserPageTableArray);  // 相对表（现在已经是页表了）首地址，虚地址
//
//	if(phyFrame == NULL)
//		return;
//	else
//		phyFrame = (phyFrame - 0xC0000000) >> 12;   // 相对表（现在已经是页表了）物理页框号
//
//	Diagnose::Write("Start Address of Process's 1# User Page Table: %x，%x\n",(unsigned long)(u.u_MemoryDescriptor.m_UserPageTableArray),phyFrame);
//
//	Machine::Instance().GetPageDirectory().m_Entrys[0x1].m_UserSupervisor = 1;
//	Machine::Instance().GetPageDirectory().m_Entrys[0x1].m_Present = 1;
//	Machine::Instance().GetPageDirectory().m_Entrys[0x1].m_ReadWriter = 1;
//	Machine::Instance().GetPageDirectory().m_Entrys[0x1].m_PageTableBaseAddress = phyFrame;
//
//	FlushPageDirectory(pgDirPhys);
//	PageDirectory * pPageDirectory = & machine.GetPageDirectory();
//	Diagnose::Write("PageTable used by CPU %x\n", Machine::Instance().GetPageDirectory().m_Entrys[0].m_PageTableBaseAddress);
//}
