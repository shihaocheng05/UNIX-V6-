#include "SwapperManager.h"

unsigned int SwapperManager::SWAPPER_ZONE_START_BLOCK = 18000;
unsigned int SwapperManager::SWAPPER_ZONE_SIZE = 2000;

SwapperManager::SwapperManager(Allocator *pAllocator)
{
	this->m_pAllocator = pAllocator;
	for(unsigned int i=0;i<SWAPPER_PAGE_NUM-1;i++)
	{
		SwapperPage[i].pageNo=i;	//与数组的索引一一对应
		SwapperPage[i].pte=NULL;
		SwapperPage[i].next=&SwapperPage[i+1];
	}
	SwapperPage[SWAPPER_PAGE_NUM-1].pageNo=SWAPPER_PAGE_NUM-1;
	SwapperPage[SWAPPER_PAGE_NUM-1].pte=NULL;
	SwapperPage[SWAPPER_PAGE_NUM-1].next=NULL;
	freeSwapList.head=&SwapperPage[0];
	freeSwapList.tail=&SwapperPage[SWAPPER_PAGE_NUM-1];
}

SwapperManager::SwapperManager()
{
	//nothing to do here
}

SwapperManager::~SwapperManager()
{
	//nothing to do here
}

int SwapperManager::Initialize()
{
	for ( unsigned int i = 0; i < SWAPPER_MAP_ARRAY_SIZE; i++ )
	{
		this->map[i].m_AddressIdx = 0;
		this->map[i].m_Size = 0;
	}

	this->map[0].m_AddressIdx = SWAPPER_ZONE_START_BLOCK;
	this->map[0].m_Size = SWAPPER_ZONE_SIZE;

	return 0;
}

unsigned int SwapperManager::AllocSwap()	//从空闲块队列中取队首，返回磁盘扇区号
{
	SwapPage*swapPage=freeSwapList.head;
	if(swapPage!=NULL)
	{
		freeSwapList.head=swapPage->next;
		if(freeSwapList.head==NULL) freeSwapList.tail=NULL;
		swapPage->next=NULL;
		return SWAPPER_ZONE_START_BLOCK+swapPage->pageNo*8;
	}
	return 0;
}

unsigned int SwapperManager::FreeSwap(unsigned int startBlock)	//挂回空闲块队列队尾，通常不检查其返回值
{
	unsigned int pageNo=(startBlock-SWAPPER_ZONE_START_BLOCK)/8;
	if(freeSwapList.tail!=NULL)
	{
		freeSwapList.tail->next=&SwapperPage[pageNo];
		freeSwapList.tail=freeSwapList.tail->next;
	}
	else
	{
		freeSwapList.head=freeSwapList.tail=&SwapperPage[pageNo];
		SwapperPage[pageNo].next=NULL;
	}
	return 0;
}
