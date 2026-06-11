#ifndef PAGE_H
#define PAGE_H

#include "Inode.h"
#include "PageTable.h"

/*描述每个物理页框的使用状态*/
struct page{
    Inode* inode;
    unsigned int filePage;  //文件中的块号（同样是等价于物理页框号）
    unsigned int pageNo;    //物理页框号
    page*next;
};//16B

struct FreeList{
    page*head;
    page*tail;
};

/*盘交换区的每个页面*/
struct SwapPage{
    unsigned int pageNo;    //物理页框号
    unsigned int swapPageNo;//盘交换区上的块号
    PageTableEntry* pte;    //指向对应进程的页表项
};

#endif