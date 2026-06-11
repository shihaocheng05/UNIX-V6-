#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "MapNode.h"
#include "Page.h"

/* @comment 该类为内存分配算法类，针对使用MapNode
 * 数组标记的情况，可以用在PageManager和SwapDiskManager中
 * 其中函数在Unixv6中对应关系如下：
 * Alloc()	: malloc(mp, size)		@line 2538
 * Free()	: mfree(mp, size, aa)	@line 2556 
 */
class Allocator
{
/* Functions */
public:
	unsigned long Alloc(MapNode map[], unsigned long size);
	unsigned long Free(MapNode map[], unsigned long size, unsigned long addrIdx);

public:
	static Allocator& GetInstance();
private:
	static Allocator m_Instance;
};

class PageAllocator
{
public:
	static const unsigned int PAGE_ARRAY_SIZE=0x2000;	//32MB空间所需page数组大小（使用page数组替代bitmap）

	unsigned long Alloc(unsigned int startIdx,unsigned int endIdx);		//参数为开始分配的起始地址。比如在哪里分配。返回分配的页框号
	unsigned long Free(unsigned int pgIdx);	//仅能按页释放，给PageManager调用
	unsigned int Page[PAGE_ARRAY_SIZE];	//共32k，内核静态区共0.5M(512k)，应该装得下
	bool hasEnoughSpace(unsigned int startIdx,unsigned int endIdx,unsigned int page_num);
	static PageAllocator& GetInstance();
private:
	static PageAllocator m_Instance;
};

#endif

