#ifndef PAGE_H
#define PAGE_H

#include "Inode.h"
#include "PageTable.h"

/*描述每个物理页框的使用状态*/
struct page{
    unsigned int pageNo;    //物理页框号，在初始化设定后不允许修改
    page*next;      //在空闲页缓存中，指向下一个空闲页
    unsigned int inode_id;
    unsigned int pageOffset;  //文件中的页框偏移量(不是绝对偏移量B，单位是4096B)
};//20B

struct FreeList{
    page*head;
    page*tail;
};

/*盘交换区的每个页面*/
struct SwapPage{
    unsigned int pageNo;    //物理页框号
    PageTableEntry* pte;    //指向对应进程的页表项
    SwapPage*next;
};

struct FreeSwapList{
    SwapPage*head;
    SwapPage*tail;
};

#endif