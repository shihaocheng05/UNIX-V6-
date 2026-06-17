# UNIX V6++ VM 代码静态排查报告（最终版）

*2026年6月*

---

## 一、当前剩余错误

### 1.1 `ProcessManager.cpp:264` — Wait 循环用 `i++` 应是 `i+=BUFFER_SIZE`

```cpp
for(unsigned int i=0;i<PageManager::PAGE_SIZE;i++,blkno++)
//                                               ^^^ 每次 +1 字节，应该是 +512
```

Exit 中正确地用了 `i+=BufferManager::BUFFER_SIZE`，Wait 漏了 `+=BUFFER_SIZE`。

**修正**：`for(unsigned int i=0;i<PageManager::PAGE_SIZE;i+=BufferManager::BUFFER_SIZE,blkno++)`

### 1.2 `OpenFileManager.cpp:223-224` — IPut 中 `inode_layer_node` 可能为 NULL

```cpp
radix_tree_node*inode_layer_node=(radix_tree_node*)userPgMgr.sharedPageRoot.slot[pNode->i_index];
radix_tree_node::FreeRangePage(inode_layer_node);  // 如果 inode_layer_node==NULL → 崩溃
```

若该 inode 从未触发过缺页（无 radix tree 条目），`slot[pNode->i_index]` 为 NULL。

**修正**：
```cpp
if (inode_layer_node) {
    radix_tree_node::FreeRangePage(inode_layer_node);
    userPgMgr.sharedPageRoot.slot[pNode->i_index] = NULL;
    userPgMgr.sharedPageRoot.count--;
}
```

### 1.3 `OpenFileManager.cpp:226` — `sharedPageRoot.count--` 可能下溢

若 `count` 已是 0（逻辑错误或多次释放），`count--` 使 `unsigned int` 回绕为 `0xFFFFFFFF`。

**修正**：`if (userPgMgr.sharedPageRoot.count > 0) userPgMgr.sharedPageRoot.count--;`

### 1.4 `ProcessManager.cpp:385-408` — Exec 提前返回时 `sectionHeaders` 泄漏

```cpp
parser.HeaderLoad(pInode);  // 分配 sectionHeaders

if ( totalLength > ... || !legalFile )
{
    fileMgr.m_InodeTable->IPut(pInode);  // ★ 没有 FreeMemory(sectionHeaders)!
    u.u_error = User::ENOMEM;
    return;
}
```

HeaderLoad 通过 `kpm.AllocMemory` 为 `sectionHeaders` 分配了内存。总大小合法性校验（第 404 行）失败时直接 return，未释放。

同样的泄漏路径：第 385-388 行（`HeaderLoad` 失败后 return，但 HeaderLoad 失败时 `sectionHeaders` 可能未分配——实际上 `AllocMemory` 在失败校验之前就被调用，需要确认）。

---

## 二、已修复的全部错误

| 问题 | 状态 |
|------|------|
| `FreeSwapList` 缺分号 | ✅ |
| stackVirtualIdx 写错为 heapVirtualIdx | ✅ |
| f_offset `<<12` 误算 | ✅ |
| ReadI 地址用 virtualIdx | ✅ |
| Victim PTE 更新缺失 | ✅ |
| 正文段 COW 计数泄漏 | ✅ |
| rdataPageNum v_start→v_length | ✅ |
| FreePhyPage m_Used 设反 | ✅ |
| FreePhyPage 交换区编码不一致 | ✅ |
| selectVictim 取模顺序 | ✅ |
| Exit 512B→4KB 分块写入 | ✅ |
| Exit `(char*)&u+i` 指针算术 | ✅ |
| Radix tree root slot 未初始化 | ✅ |
| FreeRangePage 循环条件泄漏 | ✅ |
| Swap 读回地址用 virtualIdx | ✅ |
| Exec 末尾 sectionHeaders 未释放 | 待用户确认 |

---

## 三、确认非 Bug 项

| 问题 | 原因 |
|------|------|
| `current` 成员变量 | 已在 MemoryDescriptor.h 声明 |
| Page[0x401] 误清零 | 该页属用户池初始空闲 |
| 共享文本 Page[] 不递增 | x_count 保护，FreeRangePage 安全网 |
| Radix tree 命中不递增 | 数据段做私有拷贝，不直接引用缓存页 |
