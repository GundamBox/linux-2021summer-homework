# 2021q3 Homework1 (quiz1)
###### tags: `linux2021`
contributed by < GundamBox >

~~因為會同步發在自己的Blog，中間會加點平常寫文章的梗~~

## 開發環境 ##

- CPU
    - AMD Ryzen 7 1700 (8C16T)
- Distro
    - Ubuntu 20.04-LTS
- Kernel
    - 5.10.16.3-microsoft-standard-WSL2

**注**: 
覺得文件寫清楚硬體環境比較好 debug
啟發自 st9540808 的[開發記錄][st9540808 開發記錄] - 2021/07/28 01:02
    

## 開發之前 ##

- [作業連結](https://hackmd.io/@sysprog/linux2021-summer-quiz1) 
- [設定環境遇到的困難](http://gundambox.github.io/2021/07/22/Linux-header%E5%9C%A8%E5%93%AA%E8%A3%A1%EF%BC%9F%E7%B5%95%E5%B0%8D%E9%9B%A3%E4%B8%8D%E5%80%92%E4%BD%A0/)

## 題目 ##

### 1. 解釋上述程式碼運作原理，包含 [ftrace][ftrace.txt] 的使用 ###

運作原理可以分為三塊

#### 建立 device ####

`_hideproc_init` 作為 module 進入點，做得事情主要是建立 device `hideproc`

而 device 的四個動作 open, release, read, write 中

最重要的是 `device_write`

- "add [pid]" 就把 pid 加到 list 記下來
- "del [pid]" 就把 pid 從 list 移除

#### 建立 list 存隱藏的 pid ####

LIST_HEAD 這個 macro 會建立 Doubly Linked Lists

module 就是靠這個 list 儲存被隱藏的 pid

#### ftrace ####

- kallsyms_lookup_name 找出 find_ge_pid 的 address
- hook 這個 function
    - real_find_ge_pid 這個 function pointer 是原本的 find_ge_pid
    - hook_find_ge_pid 是加料後的 find_ge_pid
        - 在 is_hidden_proc 檢查 pid 有沒有在 list 裡面，有的話就跳過

### 2. 本程式僅在 Linux v5.4 測試，若你用的核心較新，請試著找出替代方案 ###
> 2020 年的變更 [Unexporting kallsyms_lookup_name()][kallsyms_lookup_name]
> [Access to kallsyms on Linux 5.7+][kallsyms-mod]

#### 替代方案的演化 ####

1. 工程師 Ctrl C+V 大法(誤)

    直接將 [kallsyms-mod][kallsyms-mod] 的原始碼加進來，接著 failed (X)

    ```bash
    make -C /lib/modules/`uname -r`/build M=~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1 modules
    make[1]: Entering directory '/home/gundambox/WSL2-Linux-Kernel-linux-msft-wsl-5.10.16.3'
    CC [M]  ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/main.o
    LD [M]  ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/hideproc.o
    MODPOST ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/Module.symvers
    ERROR: modpost: "kallsyms_lookup_name" [~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/hideproc.ko] undefined!
    make[2]: *** [scripts/Makefile.modpost:111: ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/Module.symvers] Error 1
    make[2]: *** Deleting file '~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/Module.symvers'
    make[1]: *** [Makefile:1705: modules] Error 2
    make[1]: Leaving directory '/home/gundambox/WSL2-Linux-Kernel-linux-msft-wsl-5.10.16.3'
    make: *** [Makefile:9: all] Error 2
    ```

2. 改 Makefile，加入 `kallsyms.o`

    ```Makefile
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
        make -C /lib/modules/`uname -r`/build M=~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1 modules
        make[1]: Entering directory '/home/gundambox/WSL2-Linux-Kernel-linux-msft-wsl-5.10.16.3'
          CC [M]  ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/main.o
          LD [M]  ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/hideproc.o
          CC [M]  ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/kallsyms.o
          MODPOST ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/Module.symvers
        WARNING: modpost: missing MODULE_LICENSE() in ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/kallsyms.o
        ERROR: modpost: "kallsyms_lookup_name" [~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/hideproc.ko] undefined!
        make[2]: *** [scripts/Makefile.modpost:111: ~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/Module.symvers] Error 1
        make[2]: *** Deleting file '~/2021_jserv_liunx_kernel/2021q3_Homework1_quiz1/Module.symvers'
        make[1]: *** [Makefile:1705: modules] Error 2
        make[1]: Leaving directory '/home/gundambox/WSL2-Linux-Kernel-linux-msft-wsl-5.10.16.3'
        make: *** [Makefile:9: all] Error 2
        ```
        
3. make 成功後，直接 `sudo insmod hideproc.ko` 會導致 WSL 死掉

    參考下 [kallsyms-mod][kallsyms-mod] 的 main.c 的寫法加入 
    ```C
    KSYMDEF(kvm_lock);
    KSYMDEF(vm_list);
    ```
    以及在 `_hideproc_init` 加入
    ```C
    if ((err = init_kallsyms()))
		return err;

	KSYMINIT_FAULT(kvm_lock);
	KSYMINIT_FAULT(vm_list);

	if (err)
		return err;
    ```

4. 無法隱藏 pid
    ```bash
    $ sudo insmod hideproc.ko 
    $ pidof htop
    3453
    $ echo "add 3453" | sudo tee /dev/hideproc 
    add 3453
    $ pidof htop
    3453
    ```
    - [ ] TODO: 先將 WSL 退回 kernel 4.19，回答完後面兩題再解決

### 3. 本核心模組只能隱藏單一 PID，請擴充為允許其 PPID 也跟著隱藏，或允許給定一組 PID 列表，而非僅有單一 PID ###
### 4. 指出程式碼可改進的地方，並動手實作 ###

## 參考連結

1. [kbuild_makefiles][kbuild_makefiles]
2. [st9540808 開發記錄][st9540808 開發記錄]

[ftrace.txt]: https://www.kernel.org/doc/Documentation/trace/ftrace.txt
[kallsyms_lookup_name]: https://lwn.net/Articles/813350/
[kallsyms-mod]: https://github.com/h33p/kallsyms-mod
[kbuild_makefiles]: https://www.kernel.org/doc/Documentation/kbuild/makefiles.txt
[st9540808 開發記錄]: https://hackmd.io/@st9540808/hideproc