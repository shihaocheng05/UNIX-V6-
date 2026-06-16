#ifndef SWAPPER_MANAGER_H
#define SWAPPER_MANAGER_H

#include "MapNode.h"
#include "Allocator.h"
#include "Page.h"

class SwapperManager
{
public:
	/* static non-const member */
	/* !hard code!设定磁盘从18000#开始的2000个扇区作为交换区 */
	static unsigned int SWAPPER_ZONE_START_BLOCK;
	static unsigned int SWAPPER_ZONE_SIZE;
	static const unsigned int SWAPPER_PAGE_NUM=2000/8;	//8个扇区8*512==4096B，一页

	/* static const member */
	static const unsigned int SWAPPER_MAP_ARRAY_SIZE = 0x200;
	static const unsigned int BLOCK_SIZE = 512;

	/* Functions */
public:
	SwapperManager(Allocator* pAllocator);
	~SwapperManager();

	/* 初始化MapNode map[0]为磁盘交换区起始地址、大小 */
	int Initialize();
	/* 
	 * 交换区空间分配
	 * 
	 * size:  请求分配交换区大小(单位: byte)，实际分配的交换区大小以
	 * 磁盘块为单位，根据参数size大小，向上取整至磁盘块大小的整数倍。
	 * 
	 * 返回值: 分配到的交换区起始盘块号，返回0表示分配失败。
	 */
	unsigned int AllocSwap();
	/* 
	 * 交换区空间释放
	 * 
	 * size:  释放交换区大小，每次一个物理页框，实际释放的交换区大小以
	 * 磁盘块为单位，根据参数size大小，向上取整至磁盘块大小的整数倍。
	 * startBlock: 释放交换区起始盘块号
	 * 
	 * 返回值: 释放交换区操作总能成功，返回0，但通常忽略其返回值。
	 */
	unsigned int FreeSwap(unsigned int startBlock);

private:
	SwapperManager();

	/* Members */
public:
	MapNode map[SwapperManager::SWAPPER_MAP_ARRAY_SIZE];
	/*需要维护SwapPage数组*/
	SwapPage SwapperPage[SWAPPER_PAGE_NUM];
	FreeSwapList freeSwapList;
private:
	Allocator* m_pAllocator;
};

#endif
