# 2021q3 Homework2 (hp)
contributed by < `GundamBox` >

###### tags: `linux2021`

## 開發之前

- [作業連結][linux2021-summer-homework2]

## 題目

### 1. 解釋上述程式碼運作原理

#### main

- `pthread_create` 建立 `thread` 並依照奇偶數分配動作 `list_insert` 與 `list_delete`

#### list_insert

```mermaid
stateDiagram-v2
    [*] --> 檢查`key`是否在`list`中
    
    state if_exist <<choice>>
    檢查`key`是否在`list`中 --> if_exist
    if_exist --> 將找到的`node`釋放，並將該`thread`的`hazard_pointers`狀態清除: 有
    if_exist --> 建立新的`node`並加到`list`的尾端 : 沒有

    將找到的`node`釋放，並將該`thread`的`hazard_pointers`狀態清除 --> 失敗
    建立新的`node`並加到`list`的尾端 --> `atomic`操作是否完成
    
    state if_atomic <<choice>>
    `atomic`操作是否完成 --> if_atomic
    if_atomic --> 清除該`thread`的`hazard_pointer`的狀態: 完成
    if_atomic --> 檢查`key`是否在`list`中: 未完成
    清除該`thread`的`hazard_pointer`的狀態 --> 成功
```

#### list_delete

```mermaid
stateDiagram-v2
    [*] --> 檢查`key`是否在`list`中

    state if_exist <<choice>>
    檢查`key`是否在`list`中 --> if_exist
    if_exist --> 檢查找到的`node`下一個節點是否被改變: 有
    if_exist --> 將找到的`node`釋放，並將該`thread`的`hazard_pointers`狀態清除 : 沒有
    
    將找到的`node`釋放，並將該`thread`的`hazard_pointers`狀態清除 --> 失敗

    state if_next_changed <<choice>>
    檢查找到的`node`下一個節點是否被改變 --> if_next_changed
    if_next_changed --> 檢查`key`是否在`list`中: 被改變
    if_next_changed --> 檢查`curr`是否被改變: 未改變

    state if_curr_changed <<choice>>
    檢查`curr`是否被改變 --> if_curr_changed
    if_curr_changed --> 將該`thread`的`hazard_pointers`狀態清除: 被改變
    if_curr_changed --> 將該`thread`的`hazard_pointers`狀態清除並執行GC: 未改變

    將該`thread`的`hazard_pointers`狀態清除 --> 成功
    將該`thread`的`hazard_pointers`狀態清除並執行GC --> 成功
```

#### 細節

- 避免 `cacheline` 影響結果
    - `aligned_alloc`
    - `CLPAD`
    - `alignas(128)`
    
#### 執行結果解釋

```bash
$ gcc -Wall -o list list.c -lpthread -g -fsanitize=thread 
$ ./list 
inserts = 4098, deletes = 4098
```

- insert
    - `list_new` 的 `head` 跟 `tail` 會各做一次 insert
    - `N_THREADS` 有一半做 `insert_thread`，共 `N_ELEMENTS` * (`N_THREADS` / 2) 次的 insert
    - 加總起來為 2 + 128 * 32 = 2 + 4096 = 4098
- delete
    - `head` 跟 `tail` 會各做一次 delete
    - `N_THREADS` 有一半做 `delete_thread`，共 `N_ELEMENTS` * (`N_THREADS` / 2) 次的 delete
    - 加總起來為 2 + 128 * 32 = 2 + 4096 = 4098

### 2. 指出改進空間並著手實作

### 3. 對比 rcu_list，解釋同為 lock-free 演算法，跟上述 Hazard pointer 手法有何異同？能否指出 rcu_list 實作缺陷並改進？

## 參考資料

[linux2021-summer-homework2]: https://hackmd.io/@sysprog/linux2021-summer-homework2