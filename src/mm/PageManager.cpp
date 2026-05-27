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

unsigned long PageManager::FreeMemory(unsigned long pgAddr)
{
	unsigned int pgIdx=pgAddr/PAGE_SIZE;
	return this->m_pAllocator->Free(pgIdx);
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

