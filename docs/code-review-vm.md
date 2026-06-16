# UNIX V6++ VM 代码静态排查报告（最终版）

*2026年6月*

---

## 一、当前剩余的错误

### 1.1 `Process.cpp:173` — 指针算术错误：`(int *)(&u+i)`

```cpp
for(unsigned int i=0;i<PageManager::PAGE_SIZE;i+=BufferManager::BUFFER_SIZE,blkno++)
{
    Utility::DWordCopy((int *)(&u+i), (int *)pBuf->b_addr, BUFFER_SIZE / sizeof(int));
    //                   ^^^^^^^^^^^
    // &u 是 User*，&u+i = (char*)&u + i*sizeof(User)
    // sizeof(User)≈600B，i=512时读到 (char*)&u + 512*600 ≈ &u+300KB 处
    // 等同于从随机内核内存复制到交换区
}
```

**修正**：`(int *)((char*)&u + i)`

### 1.2 `Process.cpp:170-177` — Exit 分块写交换区：Brelse/Bwrite 顺序待确认

旧代码只在 `Bwrite` 后调用 `Brelse`（或不调用）。新代码每块都调用 Brelse。需确认 BufferManager 的 Bwrite 语义——Bwrite 是否已经隐式释放缓冲区。若 Bwrite 已释放，再次 Brelse 会导致重复释放。

---

## 二、已修复错误清单

| 编号 | 问题 | 状态 |
|------|------|------|
| 1.1 | `FreeSwapList` 缺分号 | ✅ |
| 1.2 | `current` 成员变量 | ✅ 非Bug |
| 1.3 | stackVirtualIdx 写错为 heapVirtualIdx | ✅ |
| 2.1(旧) | f_offset `<<12` 误算 | ✅ 改为 `/4096*4096` |
| 2.2(旧) | ReadI 地址用 virtualIdx 而非 cr2 | ✅ 改为 `cr2&0xFFFFF000` |
| 2.3(旧) | Victim PTE 更新缺失 | ✅ NewProc:119-120 |
| 2.5(旧) | 正文段 COW 计数泄漏 | ✅ isText 排除 |
| — | Page[0x401] 误清零 | ✅ 非Bug |
| — | rdataPageNum v_start→v_length | ✅ line 237 |
| — | FreePhyPage m_Used 设反 | ✅ line 307 → `=0` |
| — | FreePhyPage 交换区编码不一致 | ✅ 改为直接传 `m_PageBaseAddress` |
| — | selectVictim 取模顺序 | ✅ line 340 先取模再访问 |
| — | Exit 512B→4KB 分块写入 | ✅ 改为 8×512B 循环 |
| — | Radix tree root slot 未初始化 | ✅ 新增构造函数 memset |
| — | FreeRangePage 循环条件泄漏 | ✅ 删除 `&&root->count>0` |
| — | Swap 读回地址用 virtualIdx | ✅ 改为 `cr2&0xFFFFF000` |

---

## 三、确认非 Bug 的误报

| 编号 | 问题 | 原因 |
|------|------|------|
| 1.2(新) | 共享文本 Page[] 不递增 | x_count 保护：只有 x_count→0 时 FreePhyPage 才触发，FreeRangePage 的安全网 Page[] 为 0 直接返回 |
| 1.3(新) | Radix tree 命中不递增 | 缓存页仅 AllocMemory 一次、FreeRangePage 释放一次；其他进程做私有拷贝，不直接引用缓存页 |

---

## 四、调试前检查清单

- [ ] `Process.cpp:173` → `(int *)((char*)&u + i)`
- [ ] 确认 `Bwrite` 后 `Brelse` 不会重复释放
- [ ] Wait 读取时 `process[i].p_addr` 指向 8 块中的第 1 块（当前正确：`blkno-=8`）
- [ ] `FreeSwap(p_addr)` 释放整页（8块），与 `AllocSwap` 的 8 块分配匹配
