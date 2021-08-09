#include <linux/cdev.h>
#include <linux/ftrace.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#include "kallsyms.h"
#include "ksyms.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");

enum RETURN_CODE { SUCCESS };

struct ftrace_hook {
    const char *name;
    void *func, *orig;
    unsigned long address;
    struct ftrace_ops ops;
};

static int hook_resolve_addr(struct ftrace_hook *hook)
{
    hook->address = kallsyms_lookup_name(hook->name);
    if (!hook->address) {
        printk("unresolved symbol: %s\n", hook->name);
        return -ENOENT;
    }
    *((unsigned long *) hook->orig) = hook->address;
    return 0;
}

static void notrace hook_ftrace_thunk(unsigned long ip,
                                      unsigned long parent_ip,
                                      struct ftrace_ops *ops,
                                      struct pt_regs *regs)
{
    struct ftrace_hook *hook = container_of(ops, struct ftrace_hook, ops);
    if (!within_module(parent_ip, THIS_MODULE))
        regs->ip = (unsigned long) hook->func;
}

static int hook_install(struct ftrace_hook *hook)
{
    int err = hook_resolve_addr(hook);
    if (err)
        return err;

    hook->ops.func = hook_ftrace_thunk;
    hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_RECURSION_SAFE |
                      FTRACE_OPS_FL_IPMODIFY;

    err = ftrace_set_filter_ip(&hook->ops, hook->address, 0, 0);
    if (err) {
        printk("ftrace_set_filter_ip() failed: %d\n", err);
        return err;
    }

    err = register_ftrace_function(&hook->ops);
    if (err) {
        printk("register_ftrace_function() failed: %d\n", err);
        ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
        return err;
    }
    return 0;
}

void hook_remove(struct ftrace_hook *hook)
{
    int err = unregister_ftrace_function(&hook->ops);
    if (err)
        printk("unregister_ftrace_function() failed: %d\n", err);
    err = ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
    if (err)
        printk("ftrace_set_filter_ip() failed: %d\n", err);
}

typedef struct {
    pid_t id;
    struct list_head list_node;
} pid_node_t;

LIST_HEAD(hidden_proc);

typedef struct pid *(*find_ge_pid_func)(int nr, struct pid_namespace *ns);
static find_ge_pid_func real_find_ge_pid;

static struct ftrace_hook hook;

static bool is_hidden_proc(pid_t pid)
{
    pid_node_t *proc, *tmp_proc;
    list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
        if (proc->id == pid)
            return true;
    }
    return false;
}

static struct pid *hook_find_ge_pid(int nr, struct pid_namespace *ns)
{
    struct pid *pid = real_find_ge_pid(nr, ns);
    while (pid && is_hidden_proc(pid->numbers->nr))
        pid = real_find_ge_pid(pid->numbers->nr + 1, ns);
    return pid;
}

static void init_hook(void)
{
    real_find_ge_pid = (find_ge_pid_func) kallsyms_lookup_name("find_ge_pid");
    hook.name = "find_ge_pid";
    hook.func = hook_find_ge_pid;
    hook.orig = &real_find_ge_pid;
    hook_install(&hook);
}

static int hide_process(pid_t pid)
{
    pid_node_t *proc, *tmp_proc;
    bool pid_exist = false;
    list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
        if (proc->id == pid) {
            pid_exist = true;
            break;
        }
    }

    if (!pid_exist) {
        proc = kvmalloc(sizeof(pid_node_t), GFP_KERNEL);
        proc->id = pid;

        list_add_tail(&proc->list_node, &hidden_proc);
    }
    return SUCCESS;
}

static int unhide_process(pid_t pid)
{
    pid_node_t *proc, *tmp_proc;
    list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
        if (proc->id == pid) {
            list_del(&proc->list_node);
            kvfree(proc);
            break;
        }
    }
    return SUCCESS;
}

#define OUTPUT_BUFFER_FORMAT "pid: %d\n"
#define MAX_MESSAGE_SIZE (sizeof(OUTPUT_BUFFER_FORMAT) + 4)

static int device_open(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

static int device_close(struct inode *inode, struct file *file)
{
    return SUCCESS;
}

static ssize_t device_read(struct file *filep,
                           char *buffer,
                           size_t len,
                           loff_t *offset)
{
    pid_node_t *proc, *tmp_proc;
    char message[MAX_MESSAGE_SIZE];
    if (*offset)
        return 0;

    list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
        memset(message, 0, MAX_MESSAGE_SIZE);
        sprintf(message, OUTPUT_BUFFER_FORMAT, proc->id);
        copy_to_user(buffer + *offset, message, strlen(message));
        *offset += strlen(message);
    }
    return *offset;
}

static ssize_t device_write(struct file *filep,
                            const char *buffer,
                            size_t len,
                            loff_t *offset)
{
    int err;
    long pid, ppid;

    char *message;
    char add_message[] = "add", del_message[] = "del";
    char delim[] = " ,";

    struct pid *pid_struct = NULL;
    struct task_struct *pid_task_struct = NULL;

    if (len < sizeof(add_message) - 1 && len < sizeof(del_message) - 1)
        return -EAGAIN;

    message = kvmalloc(len + 1, GFP_KERNEL);
    memset(message, 0, len + 1);
    copy_from_user(message, buffer, len);

    if (!memcmp(message, add_message, sizeof(add_message) - 1)) {
        char *pid_ptr = message + sizeof(add_message);
        char *found = NULL;

        while ((found = strsep(&pid_ptr, delim)) != NULL) {
            err = kstrtol(found, 10, &pid);
            if (err != 0)
                return err;

            pid_struct = find_get_pid(pid);
            if (pid_struct) {
                pid_task_struct = pid_task(pid_struct, PIDTYPE_PID);
                ppid = task_ppid_nr(pid_task_struct);
                hide_process(pid);
                hide_process(ppid);
            }
        }
    } else if (!memcmp(message, del_message, sizeof(del_message) - 1)) {
        char *pid_ptr = message + sizeof(del_message);
        char *found = NULL;

        while ((found = strsep(&pid_ptr, delim)) != NULL) {
            err = kstrtol(found, 10, &pid);
            if (err != 0)
                return err;

            pid_struct = find_get_pid(pid);

            if (pid_struct) {
                pid_task_struct = pid_task(pid_struct, PIDTYPE_PID);
                ppid = task_ppid_nr(pid_task_struct);
                unhide_process(pid);
                unhide_process(ppid);
            }
        }
    } else {
        kvfree(message);
        return -EAGAIN;
    }

    *offset = len;
    kvfree(message);
    return len;
}

static struct hideporc_dev {
    struct cdev cdev;
    struct device *device;
    dev_t devt;
    struct class *class;
} hideporc_device;

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_close,
    .read = device_read,
    .write = device_write,
};

#define MINOR_VERSION 1
#define DEVICE_NAME "hideproc"

KSYMDEF(kvm_lock);
KSYMDEF(vm_list);

static int _hideproc_init(void)
{
    int err, dev_major;

    int r;

    if ((r = init_kallsyms()))
        return r;

    KSYMINIT_FAULT(kvm_lock);
    KSYMINIT_FAULT(vm_list);

    if (r)
        return r;

    printk(KERN_INFO "@ %s\n", __func__);
    err = alloc_chrdev_region(&hideporc_device.devt, 0, MINOR_VERSION,
                              DEVICE_NAME);
    dev_major = MAJOR(hideporc_device.devt);

    hideporc_device.class = class_create(THIS_MODULE, DEVICE_NAME);

    cdev_init(&hideporc_device.cdev, &fops);
    cdev_add(&hideporc_device.cdev, MKDEV(dev_major, MINOR_VERSION), 1);
    hideporc_device.device =
        device_create(hideporc_device.class, NULL,
                      MKDEV(dev_major, MINOR_VERSION), NULL, DEVICE_NAME);

    init_hook();

    return 0;
}

static void _hideproc_exit(void)
{
    pid_node_t *proc, *tmp_proc;

    printk(KERN_INFO "@ %s\n", __func__);
    /* FIXME: ensure the release of all allocated resources */

    list_for_each_entry_safe (proc, tmp_proc, &hidden_proc, list_node) {
        list_del(&proc->list_node);
        kvfree(proc);
    }

    hook_remove(&hook);
    device_destroy(hideporc_device.class,
                   MKDEV(MAJOR(hideporc_device.devt), MINOR_VERSION));
    cdev_del(&hideporc_device.cdev);
    class_destroy(hideporc_device.class);
    unregister_chrdev_region(hideporc_device.devt, 1);
}

module_init(_hideproc_init);
module_exit(_hideproc_exit);