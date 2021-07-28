# 2021q3 Homework1 (quiz1)
contributed by < `GundamBox` >

###### tags: `linux2021`

## 開發環境

- 2021/07/27 
    - CPU
        - AMD Ryzen 7 1700 (8C16T)
    - Distro
        - Ubuntu 20.04-LTS
    - Kernel
        - 5.10.16.3-microsoft-standard-WSL2
    - Host 
- 2021/07/28
    - WSL 太多問題要解，擠出一台舊電腦安裝 Linux 原生環境繼續寫作業
    - CPU
        - Intel i3-4160 (2C4T)
    - Distro
        - Ubuntu 20.04-LTS
    - Kernel
        - 5.8.0-63-generic

> 覺得文件寫清楚硬體環境比較好 debug
> 啟發自 st9540808 的[開發記錄][st9540808 開發記錄]

## 開發之前

- [作業連結][linux2021-summer-quiz1]
- [設定環境遇到的困難][where_is_linux_header]
- [設定環境遇到的困難-part2][install_ubuntu_20]

## 題目

### 1. 解釋上述程式碼運作原理，包含 [ftrace][ftrace.txt] 的使用

運作原理可以分為三塊

#### 建立裝置

`_hideproc_init` 作為核心模組進入點，做的事情主要是建立 device `hideproc`

而裝置驅動程式的四個動作 open, release, read, write 中

最重要的是 `device_write`

- "add [pid]" 就把 pid 加到 list 記下來
- "del [pid]" 就把 pid 從 list 移除

#### 建立 list 存隱藏的 pid

`LIST_HEAD` 這個巨集會建立 doubly linked list

核心模組藉此紀錄被隱藏的 pid

#### ftrace ####

- `kallsyms_lookup_name` 找出 `find_ge_pid` 符號的地址
- hook 這個 function
    - `real_find_ge_pid` 函式指標指向原本的 `find_ge_pid`
    - `hook_find_ge_pid` 是欲變更的 `find_ge_pid`
        - 在 `is_hidden_proc` 檢查 pid 有沒有在 list 裡面，有的話就跳過

### 2. 本程式僅在 Linux v5.4 測試，若你用的核心較新，請試著找出替代方案 ###
> 2020 年的變更 [Unexporting kallsyms_lookup_name()][kallsyms_lookup_name]
> [Access to kallsyms on Linux 5.7+][kallsyms-mod]

#### 替代方案的演化 ####

:::info
"file" 的翻譯是「檔案」，"document" 的翻譯是「文件」
請尊重台灣資訊科技前輩的篳路藍縷
:notes: jserv
:::

- 工程師 Ctrl C+V 大法(誤)

    直接將 [kallsyms-mod][kallsyms-mod] 的原始碼加進來，接著 failed (X)

    ```bash
    $ make -C /lib/modules/`uname -r`/build M=`pwd` modules
    CC [M]  main.o
    LD [M]  hideproc.o
    MODPOST Module.symvers
    ERROR: modpost: "kallsyms_lookup_name" [hideproc.ko] undefined!
    make[2]: *** [scripts/Makefile.modpost:111: Module.symvers] Error 1
    make[2]: *** Deleting file 'Module.symvers'
    make[1]: *** [Makefile:1705: modules] Error 2
    make: *** [Makefile:9: all] Error 2
    ```

- 改 `Makefile`，加入 `kallsyms.o`

    ```shell
    MODULENAME := hideproc
    obj-m += $(MODULENAME).o
    $(MODULENAME)-y += main.o kallsyms.o

    KERNELDIR ?= /lib/modules/`uname -r`/build
    PWD       := $(shell pwd)

    all:
        $(MAKE) -C $(KERNELDIR) M=$(PWD) modules
    clean:
        $(MAKE) -C $(KERNELDIR) M=$(PWD) clean
    ```
    
    - 從文件 [kbuild_makefiles][kbuild_makefiles] 可以知道
        - `obj-m` 是編成 module，如果把 `kallsyms.o` 加到 `obj-m` 會導致以下錯誤
        ```bash
        make -C /lib/modules/`uname -r`/build M=`pwd` modules
          CC [M]  main.o
          LD [M]  hideproc.o
          CC [M]  kallsyms.o
          MODPOST Module.symvers
        WARNING: modpost: missing MODULE_LICENSE() in kallsyms.o
        ERROR: modpost: "kallsyms_lookup_name" [hideproc.ko] undefined!
        make[2]: *** [scripts/Makefile.modpost:111: Module.symvers] Error 1
        make[2]: *** Deleting file 'Module.symvers'
        make[1]: *** [Makefile:1705: modules] Error 2
        make: *** [Makefile:9: all] Error 2
        ```
        
- make 成功後，接著執行 `sudo insmod hideproc.ko` 會導致 WSL 死掉
    :::warning
    什麼叫做「死掉」？用語請精準！
    :::

    參考 [kallsyms-mod][kallsyms-mod] 的 main.c 的寫法加入 
    ```cpp
    KSYMDEF(kvm_lock);
    KSYMDEF(vm_list);
    ```
    以及在 `_hideproc_init` 加入
    ```cpp
    if ((err = init_kallsyms()))
		return err;

	KSYMINIT_FAULT(kvm_lock);
	KSYMINIT_FAULT(vm_list);

	if (err)
		return err;
    ```
    
    - 2021/07/28 修改:
        - 開發環境為 WSL + vscode
        - WSL 死掉是指:
            1. 與 WSL 連線的 vscode 會斷線重啟，實際上 WSL 發生什麼事需要找 log 出來看
            2. vscode 重新連線 WSL 後執行 `sudo lsmod` 指令，發現 module 沒有 insert 進去
        - 加上之前安裝 linux header 出現問題，認為 WSL 潛在問題太多，所以放棄使用 WSL 開發，改用原生 linux 開發

- 無法隱藏 pid
    ```shell
    $ sudo insmod hideproc.ko 
    $ pidof htop
    3453
    $ echo "add 3453" | sudo tee /dev/hideproc 
    add 3453
    $ pidof htop
    3453
    ```
    - [ ] ~~TODO: 先將 WSL 退回 kernel 4.19，回答完後面兩題再解決~~
        - 改為原生 linux 開發

:::warning
與其花心思在 WSL 上，不如把原生 Linux 裝好，這樣你才能放心地開發 Linux 核心模組和相關程式
:notes: jserv
:::

- 改為原生 linux 開發
    可以正常隱藏 pid
    ~~謎之音: WSL 肯定有偷偷做壞事(誤)~~

### 3. 本核心模組只能隱藏單一 PID，請擴充為允許其 PPID 也跟著隱藏，或允許給定一組 PID 列表，而非僅有單一 PID ###

#### 允許給定一組 PID 列表，而非僅有單一 PID ####

- 要從 message 拿出多個 pid 先想到 split 後再把每個 substring 傳入 kstrtol 獲得 pid
- 但[文件][kernel_api_ch02s02]沒有提到 `strtok`，倒是有 `strsep`，主要改動如下

    ```c=
    static ssize_t device_write(struct file *filep,
                                const char *buffer,
                                size_t len,
                                loff_t *offset)
    {
        ...

        int err;
        char delim[] = " ,";

        if (!memcmp(message, add_message, sizeof(add_message) - 1)) {
            char *pid_ptr = message + sizeof(add_message);
            char *found = NULL;

            while ((found = strsep(&pid_ptr, delim)) != NULL) {
                err = kstrtol(found, 10, &pid);
                if (err != 0)
                    return err;
                hide_process(pid);
            }
        } else if (!memcmp(message, del_message, sizeof(del_message) - 1)) {
            char *pid_ptr = message + sizeof(del_message);
            char *found = NULL;

            while ((found = strsep(&pid_ptr, delim)) != NULL) {
                err = kstrtol(found, 10, &pid);
                if (err != 0)
                    return err;
                unhide_process(pid);
            }
        } 

        ...
    }
    ```
- 執行結果
    ```bash
    $ sudo insmod hideproc.ko
    $ pidof cron
    592
    $ pidof htop
    3534
    $ echo "add 592,3534" | sudo tee /dev/hideproc
    $ pidof cron # 什麼都沒發生
    $ pidof htop # 什麼都沒發生
    $ echo "del 592,3534" | sudo tee /dev/hideproc
    $ pidof cron
    592
    $ pidof htop
    3534
    ```

#### 允許其 PPID 也跟著隱藏 ####

- [ ] TODO

### 4. 指出程式碼可改進的地方，並動手實作 ###

#### 資源釋放 ####

1. 改寫新功能時，`rmmod` 會導致當機，看起來是這段註解寫的問題，拿了資源就要記得還
    ```c
    static void _hideproc_exit(void)
    {
        printk(KERN_INFO "@ %s\n", __func__);
        /* FIXME: ensure the release of all allocated resources */
    }
    ```
    - 資源獲取的順序
        1. alloc_chrdev_region
        2. class_create
        3. cdev_add
        4. device_create
    - 所以釋放順序應該是
        1. device_destroy
        2. cdev_del
        3. class_destroy
        4. unregister_chrdev_regio
2. `hidden_proc` 這個 doubly linked list 配置的資源也要記得還
    直接把 `unhide_process` 的部分拿過來用就好
    **注**: 改進的時候才發現這段程式沒有檢查要 unhide 哪個 pid，所以前面隱藏多組 pid 後解除隱藏其中一個會把全部都解除隱藏XD
3. `hook_remove` 這段原始碼被加上 `#if 0` ，非常可疑XD
    `hook_install` 有用到 `register_ftrace_function`，`hook_remove` 有用到 `unregister_ftrace_function`，看起來應該是一組的。把 `#if 0` 拿掉後在 `_hideproc_exit` 加上 `hook_remove`。

4. 最終結果
    ```C
    static void _hideproc_exit(void)
    {
        pid_node_t *proc, *tmp_proc;

        printk(KERN_INFO "@ %s\n", __func__);
        /* FIXME: ensure the release of all allocated resources */
        hook_remove(&hook);
        device_destroy(hideporc_device.class,
                       MKDEV(MAJOR(hideporc_device.devt), MINOR_VERSION));
        cdev_del(&hideporc_device.cdev);
        class_destroy(hideporc_device.class);
        unregister_chrdev_region(hideporc_device.devt, 1);

        list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
            list_del(&proc->list_node);
            kvfree(proc);
        }
    }
    ```
    - `hideporc_device` 是額外寫的 struct，翻原始碼的時候找到的寫法，覺得不錯所以有樣學樣弄一個(誤)
        ```c
        static struct hideporc_dev {
            struct cdev cdev;
            struct device *device;
            dev_t devt;
            struct class *class;
        } hideporc_device;
        ```
#### 解除隱藏 ####

```c=
list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
    list_del(&proc->list_node);
    kvfree(proc);
}
```

這寫法會解除隱藏所有 pid。如果有多組隱藏的 pid，而且只想解除隱藏一個 pid，就要檢查

```c=
list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
    if (proc->id == pid) {
        list_del(&proc->list_node);
        kvfree(proc);
        break;
    }
}
```

## 後記與心得 ##

- 找資料過程中一直跳 programmersought 的內容，有附出處的文章 87% 轉自中國文章，沒附出處的文章拿其中一段原始碼搜尋後會找到其他網站的文章，覺得這網站跟內容農場高度相似。已向[終結內容農場][content-farm-terminator]回報網域及加入黑名單
    - 偷偷在作業結尾宣傳這個專案，找資料可以避掉很多垃圾文章
- 文件會有人類迷惑行為(通常是來不及更新導致)，但原始碼不會，總之先 `git clone https://github.com/torvalds/linux.git` 壓壓驚

## 參考資訊

1. [kbuild_makefiles][kbuild_makefiles]
2. [st9540808 開發記錄][st9540808 開發記錄]
3. [kernel-api - Basic C Library Functions][kernel_api_ch02s02]
4. [Linux Kernel Repo][linux_kernel_repo]


[linux2021-summer-quiz1]: https://hackmd.io/@sysprog/linux2021-summer-quiz1
[where_is_linux_header]: http://gundambox.github.io/2021/07/22/Linux-header%E5%9C%A8%E5%93%AA%E8%A3%A1%EF%BC%9F%E7%B5%95%E5%B0%8D%E9%9B%A3%E4%B8%8D%E5%80%92%E4%BD%A0
[install_ubuntu_20]: http://gundambox.github.io/2021/07/28/%E5%AE%89%E8%A3%9D-Ubuntu-20-04-%E9%81%8E%E7%A8%8B%E9%81%87%E5%88%B0%E7%9A%84%E5%B0%8F%E5%95%8F%E9%A1%8C
[ftrace.txt]: https://www.kernel.org/doc/Documentation/trace/ftrace.txt
[kallsyms_lookup_name]: https://lwn.net/Articles/813350/
[kallsyms-mod]: https://github.com/h33p/kallsyms-mod
[kbuild_makefiles]: https://www.kernel.org/doc/Documentation/kbuild/makefiles.txt
[st9540808 開發記錄]: https://hackmd.io/@st9540808/hideproc
[kernel_api_ch02s02]: https://www.kernel.org/doc/htmldocs/kernel-api/ch02s02.html
[content-farm-terminator]: https://github.com/danny0838/content-farm-terminator
[linux_kernel_repo]: https://github.com/torvalds/linux.git