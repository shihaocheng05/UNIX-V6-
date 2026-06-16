#ifndef MAP_NODE_H
#define MAP_NODE_H

/*
 *@comment 这个结构对应Unixv6中的map结构
 *下面给出map结构参考
 * struct map	@line 2515
 * {
	char *m_size;
	char *m_addr;
 * }
 */
struct MapNode
{
	unsigned long m_Size;
	unsigned long m_AddressIdx;	     //分配空间的起始地址
};

#endif

