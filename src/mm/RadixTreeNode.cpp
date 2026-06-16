#include "RadixTreeNode.h"
#include "KernelAllocator.h"
#include "Kernel.h"
#include "Page.h"

radix_tree_node::radix_tree_node(unsigned int height,unsigned int count)
{
    this->height=height;
    this->count=count;
    for(unsigned int i=0;i<RADIX_TREE_MAP_SIZE;i++)
    {
        slot[i]=NULL;
    }
}

void radix_tree_node::FreeRangePage(radix_tree_node*root)   //只在inode引用数为0时调用一次，因此，对于共享页的引用计数不应该增加（应始终为1）
{
    KernelAllocator&kAllocator=Kernel::Instance().GetKernelAllocator();
    if(root->height==0) //叶子节点
    {
        UserPageManager&userPgMgr=Kernel::Instance().GetUserPageManager();
        for(unsigned int i=0;i<RADIX_TREE_MAP_SIZE;i++)
        {
            if(root->slot[i]!=NULL) //page结构，并不由内核堆动态分配，而是一直使用在UserPageManager静态区初始化的内存。释放时，直接挂在FreeList队尾
            {
                userPgMgr.FreeMemory(((page*)root->slot[i])->pageNo<<12);
                root->slot[i]=NULL;
                root->count--;
            }
        }
    }
    else
    {
        for(unsigned int i=0;i<RADIX_TREE_MAP_SIZE;i++)
        {
            if(root->slot[i]!=NULL)
            {
                FreeRangePage((radix_tree_node*)root->slot[i]);
                root->slot[i]=NULL;
                root->count--;
            }
        }
    }
    kAllocator.FreeMemeory(sizeof(radix_tree_node),(unsigned long)root);     //传入当前节点虚地址
}

radix_tree_node* radix_tree_node::FindLeafNode(radix_tree_node*root,unsigned int inode_id,unsigned int pageOffset)
{
    KernelAllocator&kAllocator=Kernel::Instance().GetKernelAllocator();
    radix_tree_node*current=root;
    if(current->slot[inode_id]==NULL)
    {
        return current;
    }
    current=(radix_tree_node*)current->slot[inode_id];
    if(current->slot[(pageOffset&(((1UL<<8)-1)<<16))>>16]==NULL)
    {
        return current;
    }
    current=(radix_tree_node*)current->slot[(pageOffset&(((1UL<<8)-1)<<16))>>16];  //匹配第1个比特段
    if(current->slot[(pageOffset&(((1UL<<8)-1)<<8))>>8]==NULL)
    {
        return current;
    }
    current=(radix_tree_node*)current->slot[(pageOffset&(((1UL<<8)-1)<<8))>>8];   //匹配第2个比特段
    return current;
}

radix_tree_node*radix_tree_node::EstablishPathToTrueLeaf(radix_tree_node*leaf_node,unsigned int inode_id,unsigned pageOffset)
{
    KernelAllocator&kAllocator=Kernel::Instance().GetKernelAllocator();
    if(leaf_node->height==3)
	{
		radix_tree_node*inode_tree_node=(radix_tree_node*)kAllocator.AllocMemory(sizeof(radix_tree_node));
		//建立前驱后继关系
		leaf_node->count++;
		leaf_node->slot[inode_id]=inode_tree_node;
		//初始化
		inode_tree_node->count=0;
		inode_tree_node->height=2;
		for(unsigned int i=0;i<RADIX_TREE_MAP_SIZE;i++)
		{
			inode_tree_node->slot[i]=NULL;
		}
		//自然过渡到下一层逻辑
		leaf_node=inode_tree_node;
	}
	if(leaf_node->height==2)
	{
		radix_tree_node*first_tree_node=(radix_tree_node*)kAllocator.AllocMemory(sizeof(radix_tree_node));
		//建立前驱后继关系
		leaf_node->count++;
		leaf_node->slot[(pageOffset&(((1UL<<8)-1)<<16))>>16]=first_tree_node;
		//初始化
		first_tree_node->count=0;
		first_tree_node->height=1;
		for(unsigned int i=0;i<RADIX_TREE_MAP_SIZE;i++)
		{
			first_tree_node->slot[i]=NULL;
		}
		//自然过渡到下一层逻辑
		leaf_node=first_tree_node;
	}
	if(leaf_node->height==1)
	{
		radix_tree_node*second_tree_node=(radix_tree_node*)kAllocator.AllocMemory(sizeof(radix_tree_node));
		//建立前驱后继关系
		leaf_node->count++;
		leaf_node->slot[(pageOffset&(((1UL<<8)-1)<<8))>>8]=second_tree_node;
		//初始化
		second_tree_node->count=0;
		second_tree_node->height=0;
		for(unsigned int i=0;i<RADIX_TREE_MAP_SIZE;i++)
		{
			second_tree_node->slot[i]=NULL;
		}
		//自然过渡到下一层逻辑
		leaf_node=second_tree_node;
	}
    return leaf_node;
}
