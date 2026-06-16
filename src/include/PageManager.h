#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "MapNode.h"
#include "Allocator.h"
#include "Page.h"
#include "RadixTreeNode.h"
#include "PageTable.h"

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
	unsigned long AllocContinueMemory(unsigned long startAddr,unsigned long endAddr,unsigned long size);
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
	unsigned long FreeContinueMemory(unsigned long pgAddr,unsigned long size);
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
	static const unsigned long USER_PAGE_POOL_START_ADDR = 0x401000;		/* 用户物理内存区域起始地址4MB，但是0x400是0#进程的PPDA，应保留 */
	static const unsigned int freePageNum=(0x2000000-0x401000)/PAGE_SIZE;		/*用户区全部可以用于分配的页框数目*/
	/* static member */
	static unsigned int USER_PAGE_POOL_SIZE;		/* 用户物理内存区域大小：由内核初始化时进行设置 */
	static const unsigned long USER_END_ADDR=0x2000000;
	static FreeList freeList;
	static page pages[freePageNum];		/*全部可以被分配的用户区物理页框*/
	/*radix tree共享页缓存树根，前8个bit为inode在m_Inode数组的索引，后24个bit为文件偏移量；因此索引刚好可以用一个unsigned int来存储。
	本设计中radix_tree使用的f_offset是文件中的页框偏移量，相对于文件起始位置偏移了几个页框。
	仿照Linux radix tree固定分层，不合并存放单个page的叶子结点*/
	/*始终使用同一个Kernel::Instance().GetUserPageManager()，所以静态成员和非静态是一样的，都放在全局区*/
	radix_tree_node sharedPageRoot=radix_tree_node(3,0);	
public:
	UserPageManager(PageAllocator* pgallocator);
	int Initialize();	/* 初始化MapNode map[0]为用户物理页区起始地址、大小 */
	/*重写UserPageManager的内存分配和释放函数*/
	unsigned long AllocMemory();	//分配一页，返回起始物理地址
	unsigned long FreeMemory(unsigned long phyAddr);	//释放一页，返回值1释放成功，返回值0释放失败
};

#endif
