#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

struct PageTableEntry
{
	unsigned char	m_Present : 1;
	unsigned char	m_ReadWriter : 1;
	unsigned char	m_UserSupervisor : 1;
	unsigned char	m_WriteThrough : 1;
	unsigned char	m_CacheDisabled : 1;
	unsigned char	m_Accessed : 1;
	unsigned char	m_Dirty : 1;
	unsigned char	m_PageTableAttribueIndex : 1;
	unsigned char	m_GlobalPage : 1;
	unsigned char 	m_Used:1;
	unsigned char	m_ForSystemUser : 2;
	unsigned int	m_PageBaseAddress : 20;		//P==1时是物理页框号，P==0时是盘交换区的扇区号
}__attribute__((packed));


class PageTable
{
public:
	static const unsigned int ENTRY_CNT_PER_PAGETABLE = 1024;
	static const unsigned int SIZE_PER_PAGETABLE_MAP = 0x400000;

public:
	PageTable();
	~PageTable();
		
public:
	PageTableEntry m_Entrys[ENTRY_CNT_PER_PAGETABLE];
};

#endif

