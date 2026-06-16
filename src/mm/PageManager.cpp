#include "PageManager.h"
#include "Allocator.h"

unsigned int PageManager::PHY_MEM_SIZE;
unsigned int UserPageManager::USER_PAGE_POOL_SIZE;
const unsigned int PageManager::PAGE_SIZE;

PageManager::PageManager(PageAllocator* pgallocator)
{
	this->m_pAllocator = pgallocator;
}

int PageManager::Initialize()
{
	for ( unsigned int i = 0; i < MEMORY_MAP_ARRAY_SIZE; i++ ) 
	{
		this->map[i].m_AddressIdx = 0;
		this->map[i].m_Size = 0;
	}
	return 0;
}

unsigned long PageManager::AllocMemory(unsigned long startAddr,unsigned long endAddr)
{
	unsigned int startIdx=startAddr/PAGE_SIZE;
	unsigned int endIdx=endAddr/PAGE_SIZE;
	return this->m_pAllocator->Alloc(startIdx,endIdx)<<12;
}

unsigned long PageManager::AllocContinueMemory(unsigned long startAddr,unsigned long endAddr,unsigned long size)
{
	if (size == 0) return 0UL; // 或者 return startAddr;

	unsigned int PageNum = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	unsigned int startIdx = startAddr / PAGE_SIZE;
	unsigned int endIdx = endAddr / PAGE_SIZE;
	unsigned int count = 0;
	int firstIdx = -1;

	for (unsigned int i = startIdx; i < endIdx; i++) {
		if (this->m_pAllocator->Page[i] == 0) {
			if (count == 0) firstIdx = i;
			this->m_pAllocator->Page[i] = 1;
			count++;
			if (count >= PageNum) {
				return (firstIdx << 12); // 返回起始页地址
			}
		} else {
			if (count > 0) {
				for (unsigned int j = 0; j < (unsigned int)count; j++)
					this->m_pAllocator->Page[firstIdx + j] = 0;
				count = 0;
				firstIdx = -1;
			}
		}
	}
	// 到这里没有找到，回滚尾部已标记的页（如果有）
	if (count > 0 && firstIdx >= 0) {
		for (unsigned int j = 0; j < (unsigned int)count; j++)
			this->m_pAllocator->Page[firstIdx + j] = 0;
	}
	return 0UL;
}

unsigned long PageManager::FreeMemory(unsigned long pgAddr)
{
	unsigned int pgIdx=pgAddr/PAGE_SIZE;
	return this->m_pAllocator->Free(pgIdx);
}

unsigned long PageManager::FreeContinueMemory(unsigned long pgAddr,unsigned long size)
{
	if(size==0) return 0UL;
	unsigned int pgIdx=pgAddr/PAGE_SIZE;
	unsigned int PageNum=(size+PAGE_SIZE-1)/PAGE_SIZE;
	for(unsigned int i=pgIdx;i<pgIdx+PageNum;i++)
	{
		this->m_pAllocator->Free(i);
	}
	return 0UL;
}

bool PageManager::EnoughSpace(unsigned long startAddr,unsigned long endAddr,unsigned long size)
{
	unsigned int page_num=(size+PAGE_SIZE-1)/PAGE_SIZE;
	unsigned int startIdx=startAddr/PAGE_SIZE;
	unsigned int endIdx=endAddr/PAGE_SIZE;
	return this->m_pAllocator->hasEnoughSpace(startIdx,endIdx,page_num);
}

PageManager::~PageManager()
{
}

KernelPageManager::KernelPageManager(PageAllocator* pgallocator)
	:PageManager(pgallocator)
{
}

int KernelPageManager::Initialize()
{
	PageManager::Initialize();
	
	this->map[0].m_AddressIdx = 
		KERNEL_PAGE_POOL_START_ADDR / PageManager::PAGE_SIZE;
	this->map[0].m_Size = 
		KERNEL_PAGE_POOL_SIZE / PageManager::PAGE_SIZE;
	return 0;
}

UserPageManager::UserPageManager(PageAllocator* pgallocator)
	:PageManager(pgallocator)
{
	unsigned int userSpaceStartIdx=USER_PAGE_POOL_START_ADDR/PAGE_SIZE;
	pgallocator->Page[userSpaceStartIdx]=0;
	unsigned int i=1;
	for(;i<freePageNum;i++)
	{
		pgallocator->Page[i+userSpaceStartIdx]=0;
		pages[i-1].next=&pages[i];
		pages[i-1].pageNo=i-1+userSpaceStartIdx;	//一一对应，每个page结构体反向映射到其在pages数组中的索引
	}
	pages[freePageNum-1].next=NULL;
	pages[freePageNum-1].pageNo=userSpaceStartIdx+freePageNum-1;
	freeList.head=&pages[0];
	freeList.tail=&pages[freePageNum-1];
}

int UserPageManager::Initialize()
{
	PageManager::Initialize();
	
	this->map[0].m_AddressIdx = 
		USER_PAGE_POOL_START_ADDR / PageManager::PAGE_SIZE;
	this->map[0].m_Size = 
		USER_PAGE_POOL_SIZE / PageManager::PAGE_SIZE;
	return 0;
}

unsigned long UserPageManager::AllocMemory()
{
	page*freePage=freeList.head;
	if(freePage!=NULL)
	{
		this->m_pAllocator->Page[freePage->pageNo]=1;
		freeList.head=freePage->next;
		freePage->next=NULL;
		return freePage->pageNo<<12;
	}
	if(freeList.head==NULL)
	{
		freeList.tail=NULL;
	}
	return 0;
}

unsigned long UserPageManager::FreeMemory(unsigned long phyAddr)
{
	if(phyAddr>=USER_PAGE_POOL_START_ADDR&&phyAddr<USER_END_ADDR)
	{
		unsigned int phyIdx=phyAddr>>12;
		if(this->m_pAllocator->Page[phyIdx]<=0) return 0;
		if(--this->m_pAllocator->Page[phyIdx]==0)
		{
			page*freePage=&pages[phyIdx-(USER_PAGE_POOL_START_ADDR>>12)];
			if(freeList.tail!=NULL)
			{
				freeList.tail->next=freePage;
				freeList.tail=freePage;
			}
			else
			{
				freeList.head=freeList.tail=freePage;
			}
			freeList.tail->next=NULL;
			return 1;
		}
		
	}
	return 0;
}