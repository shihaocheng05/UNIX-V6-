#ifndef TEXT_H
#define TEXT_H

#include "INode.h"
#include "PageDirectory.h"

/*
 * @comment 对应Unixv6中 struct text结构
 * 描述可执行代码正文段(code segment)的信息
 *
 *
 */
class Text
{
public:
	Text();
	~Text();

	/* 递减x_ccount的值，如果x_ccount递减至0，
	 * 表示内存中已经没有引用该共享正文段的进程，
	 * 则释放该共享正文段占据的内存。
	 */
public:
	unsigned int	x_size;		/* 代码段长度，以字节为单位 */
	Inode*			x_iptr;		/* 内存inode地址 */
	unsigned short	x_count;	/* 共享正文段的进程数 */
	PageDirectory* x_pgTable;
};

#endif

