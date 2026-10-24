// SPDX-License-Identifier: GPL-2.0
/*
 * ============================================================================
 *  mytoggle.c  ——  第一个驱动：每写一次，GPIO 电平反转一次
 * ============================================================================
 *
 * 【它干什么】
 *     P1_A2（GPIO1_A2，Linux 编号 34）配成输出。
 *     用户每往 /dev/mytoggle 写一次，引脚电平就反转一次（高→低→高→低…）。
 *
 * 【怎么用】
 *     sudo insmod mytoggle.ko                 # 加载，创建 /dev/mytoggle
 *     echo 1 | sudo tee /dev/mytoggle         # 反转一次
 *     sudo cat /sys/kernel/debug/gpio | grep mytoggle   # 看 out hi / out lo
 *     sudo rmmod mytoggle                     # 卸载
 *
 * 【完整调用链】（读代码时对着这张图看）
 *
 *   insmod
 *     └─> mytoggle_init()                驱动入口，只被调用一次
 *           ├─ gpio_request(34)          独占这个引脚
 *           ├─ gpio_direction_output()   配成输出
 *           ├─ alloc_chrdev_region()     申请设备号 510:0
 *           ├─ cdev_add()                把 fops 挂到内核
 *           └─ class_create/device_create  让 /dev/mytoggle 自动出现
 *
 *   echo 1 > /dev/mytoggle
 *     └─> VFS 查到设备号 -> 查 fops -> mytoggle_write()   每写一次调用一次
 *           └─ gpio_get_value() 读电平
 *              gpio_set_value() 写相反值
 *
 *   rmmod
 *     └─> mytoggle_exit()                驱动出口，只被调用一次，逆序释放
 *
 * 【为什么 6.12 用 gpio_* 而不是 gpiod_*】
 *     gpiod_request() 在新内核里不再对普通驱动开放（只给 provider 用），
 *     所以这里用 include/linux/gpio.h 里的整型 API，
 *     它其实是 gpiod_* 的 inline 包装，最终调的还是一套东西。
 * ============================================================================
 */

/* ---------- 头文件：每一个都是必须的，缺了编不过 ---------- */
#include <linux/module.h>        /* module_init/module_exit、MODULE_LICENSE */
#include <linux/kernel.h>        /* pr_info / pr_err（printk 家族） */
#include <linux/init.h>          /* __init / __exit 标记 */
#include <linux/fs.h>            /* struct file、struct file_operations */
#include <linux/cdev.h>          /* cdev_init / cdev_add：字符设备对象 */
#include <linux/device.h>        /* class_create / device_create：自动建 /dev 节点 */
#include <linux/uaccess.h>       /* copy_to_user / copy_from_user（本例没用到，留着） */
#include <linux/gpio.h>          /* gpio_request / gpio_direction_output / gpio_set_value */

/* 驱动名字：同时用作 设备名、/dev 节点名、GPIO 申请者标识 */
#define DRV_NAME "mytoggle"

/*
 * gpio_num：要控制哪个 GPIO 编号。
 *   默认 34 = GPIO1_A2（P1_A2，JP5 第 16 脚，1.8V 电平）。
 * module_param 会把它暴露成模块参数，加载时可以改：
 *   sudo insmod mytoggle.ko gpio_num=36     # 换成 P1_A4
 * 0444 = 在 /sys/module/mytoggle/parameters/gpio_num 里只读可见。
 */
static int gpio_num = 34;
module_param(gpio_num, int, 0444);
MODULE_PARM_DESC(gpio_num, "要控制的 GPIO 编号（默认 34 = P1_A2）");

/*
 * 三个全局变量，模块加载期间一直存在：
 *   my_devno  ：内核分配的 设备号（主+次），日志里看到的 "510, 0" 就是它
 *   my_cdev   ：内核里代表“一个字符设备”的对象
 *   my_class  ：设备类，device_create 时要用；卸载时也要用它删节点
 */
static dev_t my_devno;
static struct cdev my_cdev;
static struct class *my_class;

/*
 * ============================================================================
 * mytoggle_write —— 用户每 echo 一次，内核就调它一次
 * ============================================================================
 * 参数（内核注册的回调签名都是固定的）：
 *   filp  ：这一次打开的文件对象（可以拿 private_data 存自己的数据）
 *   ubuf  ：用户空间传进来的缓冲区地址（★ 不能直接解引用！）
 *   count ：用户想写多少字节
 *   ppos  ：文件当前偏移，一般不用管
 *
 * 返回值：必须返回“实际处理了多少字节”，否则用户程序会认为写失败。
 *          返回负数 = 出错（用户拿到 -1 和 errno）。
 *
 * 本例故意不读 ubuf 的内容：写什么都无所谓，写一次就反一次。
 * 如果想区分 "/ 0"，就用 copy_from_user 把用户数据搬进来判断。
 * ============================================================================
 */
static ssize_t mytoggle_write(struct file *filp, const char __user *ubuf,
                              size_t count, loff_t *ppos)
{
    int cur, next;

    cur  = gpio_get_value(gpio_num);   /* 读当前电平：0 或 1 */
    next = !cur;                       /* 取反 */
    gpio_set_value(gpio_num, next);    /* 写回硬件寄存器，引脚立刻变化 */

    /* pr_info 打在 dmesg 里，调试时能看到每次反转 */
    pr_info(DRV_NAME ": gpio%d %d -> %d\n", gpio_num, cur, next);

    return count;                      /* ★ 必须返回 count，不能返回 0 */
}

/*
 * file_operations = “值班表”。
 * 内核在用户调用对应系统调用时，按这张表找你的函数：
 *   open()   -> .open      （本例没写，内核用默认行为）
 *   read()   -> .read      （本例没写 -> 默认返回 -EINVAL）
 *   write()  -> .write     （★ 我们只实现这个）
 *   close()  -> .release   （本例没写）
 * .owner = THIS_MODULE：让内核知道它属于本模块，使用中不允许 rmmod。
 */
static const struct file_operations mytoggle_fops = {
    .owner = THIS_MODULE,
    .write = mytoggle_write,
};

/*
 * ============================================================================
 * mytoggle_init —— 驱动入口（insmod 时被调用，只调一次）
 * ============================================================================
 * 五步走，任何一步失败都要把前面申请的还回去（这就是下面 goto 链的意义）。
 * 顺序很重要：先申请资源，最后才创建设备节点。
 * ============================================================================
 */
static int __init mytoggle_init(void)
{
    int ret;

    /* ① 校验编号：不是合法 GPIO 就直接退出 */
    if (!gpio_is_valid(gpio_num)) {
        pr_err(DRV_NAME ": gpio %d 编号非法\n", gpio_num);
        return -EINVAL;
    }

    /* ② 申请独占：如果这个脚已经被别的驱动占了，这里就返回 -EBUSY */
    ret = gpio_request(gpio_num, DRV_NAME);
    if (ret) {
        pr_err(DRV_NAME ": gpio%d 被占用 (%d)\n", gpio_num, ret);
        return ret;                     /* 这里还没申请别的东西，直接返回即可 */
    }

    /* ③ 配成输出，初值低电平（0） */
    ret = gpio_direction_output(gpio_num, 0);
    if (ret) {
        pr_err(DRV_NAME ": gpio%d 设置方向失败 (%d)\n", gpio_num, ret);
        goto err_gpio;                  /* 失败：回到下面把 gpio 还回去 */
    }

    /* ④ 注册字符设备：申请设备号 -> 初始化 cdev -> 加入内核 */
    ret = alloc_chrdev_region(&my_devno, 0, 1, DRV_NAME);
    if (ret)
        goto err_gpio;

    cdev_init(&my_cdev, &mytoggle_fops);
    my_cdev.owner = THIS_MODULE;
    ret = cdev_add(&my_cdev, my_devno, 1);
    if (ret)
        goto err_region;                /* 失败：注销设备号 */

    /* ⑤ 让 /dev/mytoggle 自动出现（不用手动 mknod）
     *    class_create  -> 创建一个“设备类”
     *    device_create -> 在这个类下建一个设备，内核 + udev 会生成节点
     * 注意 node 的权限是 600 root root，所以用户要 sudo 才能写。
     */
    my_class = class_create(DRV_NAME);
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);        /* class_create 失败返回的是错误指针，要转换 */
        goto err_cdev;
    }
    if (IS_ERR(device_create(my_class, NULL, my_devno, NULL, DRV_NAME))) {
        ret = -ENODEV;
        goto err_class;
    }

    pr_info(DRV_NAME ": 就绪，/dev/%s 控制 gpio%d（每次写反转）\n",
            DRV_NAME, gpio_num);
    return 0;                           /* 0 = 加载成功 */

/* ---------------- 出错回滚：逆着来，一层层退 ---------------- */
err_class:
    class_destroy(my_class);            /* ④ 后半：删类 */
err_cdev:
    cdev_del(&my_cdev);                 /* ④ 前半：删 cdev */
err_region:
    unregister_chrdev_region(my_devno, 1);   /* ③：还设备号 */
err_gpio:
    gpio_set_value(gpio_num, 0);        /* ②：先把脚拉低，再还 */
    gpio_free(gpio_num);
    return ret;
}

/*
 * ============================================================================
 * mytoggle_exit —— 驱动出口（rmmod 时被调用）
 * ============================================================================
 * 规矩：和 init 完全相反的顺序释放，一个都不能漏。
 * 漏了的后果：
 *   漏 cdev_del    -> 下次 insmod 设备号冲突
 *   漏 class/device-> /dev 下留一个孤儿节点
 *   漏 gpio_free   -> 下次 insmod 报 -EBUSY
 * ============================================================================
 */
static void __exit mytoggle_exit(void)
{
    device_destroy(my_class, my_devno);      /* 删 /dev 节点 */
    class_destroy(my_class);                 /* 删设备类 */
    cdev_del(&my_cdev);                      /* 删 cdev */
    unregister_chrdev_region(my_devno, 1);   /* 还设备号 */
    gpio_set_value(gpio_num, 0);             /* 引脚恢复低电平 */
    gpio_free(gpio_num);                     /* 还引脚 */

    pr_info(DRV_NAME ": 已卸载\n");
}

/* 把上面两个函数登记成“入口/出口”。这两个宏必须写，否则内核不知道谁是谁 */
module_init(mytoggle_init);
module_exit(mytoggle_exit);

/* 许可证：GPL 必须写。原因是 gpio_* 等大量符号是 GPL-only，不写会拒绝链接 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("youyou");
MODULE_DESCRIPTION("Toggle a GPIO on every write (P1_A2 = gpio34)");
