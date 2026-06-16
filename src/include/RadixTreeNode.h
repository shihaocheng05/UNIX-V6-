#ifndef RADIX_TREE_NODE_H
#define RADIX_TREE_NODE_H

#define RADIX_TREE_MAP_SHIFT 8
#define RADIX_TREE_MAP_SIZE (1UL<<RADIX_TREE_MAP_SHIFT)

class radix_tree_node{
public:
    unsigned int height;    //距离叶子层的高度（height==0为叶子层，height>=1为非叶子层）
    unsigned int count;     //该节点有多少个槽位被使用
    void* slot[RADIX_TREE_MAP_SIZE];
    radix_tree_node();
    radix_tree_node(unsigned int height,unsigned int count);
    static void FreeRangePage(radix_tree_node*root);   /*释放以根结点开始的子树*/
    /*找到freePage直属的叶子结点，若该叶子不存在，则寻找最接近叶子的节点*/
    static radix_tree_node* FindLeafNode(radix_tree_node*root,unsigned int inode_id,unsigned int pageOffset);   
    /*RO挂radix_tree*/
    static radix_tree_node* EstablishPathToTrueLeaf(radix_tree_node*leaf_node,unsigned int inode_id,unsigned pageOffset);
};

#endif