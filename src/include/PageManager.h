#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "MapNode.h"
#include "Allocator.h"

class PageManager
{
public:
	/* static member */
	static unsigned int PHY_MEM_SIZE;	/* 物理内存大小，系统启动时根据物理内存大小设置 */
	
	/* static const member */
	static const unsigned int PAGE_SIZE = 0x1000;					/* 物理内存页大小 */
	static const unsigned int MEMORY_MAP_ARRAY_SIZE = 0x200;		/* 最多可分配512个对象 */
	static const unsigned int KERNEL_MEM_START_ADDR	= 0x100000;		/* 内核映像从1M物理内存开始 */
	static const unsigned int KERNEL_SIZE			= 0x80000;		/* 内核映像大小限制(一般二进制映像远不会到512K大小) */

	/* Functions */
public:
	PageManager(PageAllocator* pgallocator);
	virtual ~PageManager();
	
	/* 完成对MapNode map[]数组的初始化清零 */
	int Initialize();
	/* 
	 * 物理内存分配
	 * 
	 * size: 需分配内存大小(单位: byte)，实际分配物理内存大小以页
	 * 为单位，会根据size大小以4K为边界，向上取整至4K字节整数倍。
	 * 
	 * 返回值: 成功分配的物理内存区起始地址，返回0表示分配失败。
	 */

	//重写为传入分配区域（用户区或者核心区）的始末物理地址，每次分配一页
	unsigned long AllocMemory(unsigned long startAddr,unsigned long endAddr);
	/* 
	 * 物理内存释放
	 * 
	 * size: 需释放内存大小(单位: byte)，实际释放物理内存大小以页
	 * 为单位，会根据size大小以4K为边界，向上取整至4K字节整数倍。
	 * 
	 * 返回值: 释放物理内存操作总能成功，但通常不检查其返回值。
	 */

	//重写为每次尝试释放一个物理页框
	unsigned long FreeMemory(unsigned long pgAddr);

	//判断空间是否充足
	bool EnoughSpace(unsigned long startAddr,unsigned long endAddr,unsigned long size);

private:
	PageManager();

	/* Members */
public:
	MapNode map[PageManager::MEMORY_MAP_ARRAY_SIZE];
	PageAllocator* m_pAllocator;
private:
//	Allocator* m_pAllocator;
};


class KernelPageManager : public PageManager
{
public:
	/* 
	 * 物理地址 0x200000 被用于PageDirectory, 
	 * 物理地址 0x201000 被用于内核页表, 
	 * 物理地址 0x202000 与 0x203000 用于用户程序页表.
	 */
//	static const unsigned int KERNEL_PAGE_POOL_START_ADDR = 0x200000 + 0x2000 + 0x2000;
	static const unsigned int KERNEL_PAGE_POOL_START_ADDR = 0x200000 + 0x3000;		//核心页表和0#用户页表和0#进程页目录（原则上也给予保留）
	static const unsigned int KERNEL_PAGE_POOL_SIZE = 0x200000 - 0x4000;
	static const unsigned int KERNEL_PAGE_POOL_END_ADDR =0x400000;

public:
	KernelPageManager(PageAllocator* pgallocator);
	int Initialize();	/* 初始化MapNode map[0]为内核物理页区起始地址、大小 */
};


class UserPageManager : public PageManager
{
public:
	/* static const member */
	static const unsigned long USER_PAGE_POOL_START_ADDR = 0x400000;		/* 用户物理内存区域起始地址4MB */
	/* static member */
	static unsigned int USER_PAGE_POOL_SIZE;		/* 用户物理内存区域大小：由内核初始化时进行设置 */
	static const unsigned long USER_END_ADDR=0x2000000;
	
public:
	UserPageManager(PageAllocator* pgallocator);
	int Initialize();	/* 初始化MapNode map[0]为用户物理页区起始地址、大小 */
};

#endif
