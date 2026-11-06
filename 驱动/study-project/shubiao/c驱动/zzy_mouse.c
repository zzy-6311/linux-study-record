// SPDX-License-Identifier: GPL-2.0
/* 第二步：input_handler 接收鼠标，miscdevice 向单个用户态读取者提供事件。 */
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include "zzy_mouse_uapi.h"

static DECLARE_KFIFO(event_fifo, struct zzy_mouse_event, 1024);
static DEFINE_SPINLOCK(queue_lock);
static DEFINE_MUTEX(read_lock);
static DECLARE_WAIT_QUEUE_HEAD(read_wait);
static bool reader_open, connected, overflow_pending;
static u64 received, dropped;
static u32 sequence;
/* input core 串行调用 connect/disconnect；只接收一只匹配鼠标。 */
static struct input_handle *active_handle;

/* 必须持有 queue_lock；溢出时显式通知，不能悄悄丢失按键状态。 */
static void enqueue_locked(u16 type, u16 code, s32 value)
{
    struct zzy_mouse_event event = {
        .timestamp_ns = ktime_get_ns(),
        .sequence = sequence++,
        .type = type, .code = code, .value = value,
    };
    if (!reader_open)
        return;
    if (kfifo_is_full(&event_fifo)) {
        dropped += kfifo_len(&event_fifo);
        kfifo_reset(&event_fifo);
        overflow_pending = true;
    }
    kfifo_in(&event_fifo, &event, 1);
}

static bool data_ready(void)
{
    unsigned long flags;
    bool ready;
    spin_lock_irqsave(&queue_lock, flags);
    ready = overflow_pending || !kfifo_is_empty(&event_fifo);
    spin_unlock_irqrestore(&queue_lock, flags);
    return ready;
}

static void mouse_event(struct input_handle *handle, unsigned int type,
                        unsigned int code, int value)
{
    unsigned long flags;
    if (type != EV_SYN && type != EV_KEY && type != EV_REL)
        return;
    spin_lock_irqsave(&queue_lock, flags);
    received++;
    enqueue_locked(type, code, value);
    spin_unlock_irqrestore(&queue_lock, flags);
    wake_up_interruptible(&read_wait);
}

static void set_connection(bool state)
{
    unsigned long flags;
    spin_lock_irqsave(&queue_lock, flags);
    connected = state;
    enqueue_locked(ZZY_MOUSE_META, ZZY_MOUSE_CONNECTION, state);
    spin_unlock_irqrestore(&queue_lock, flags);
    wake_up_interruptible(&read_wait);
}

static int mouse_connect(struct input_handler *handler, struct input_dev *dev,
                         const struct input_device_id *id)
{
    struct input_handle *handle;
    int ret;
    if (active_handle || !test_bit(EV_REL, dev->evbit) ||
        !test_bit(REL_X, dev->relbit) || !test_bit(REL_Y, dev->relbit) ||
        !test_bit(BTN_LEFT, dev->keybit))
        return -ENODEV;
    handle = kzalloc(sizeof(*handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;
    handle->dev = dev;
    handle->handler = handler;
    handle->name = "zzy_mouse";
    ret = input_register_handle(handle);
    if (ret)
        goto free_handle;
    set_connection(true);
    ret = input_open_device(handle);
    if (ret) {
        set_connection(false);
        input_unregister_handle(handle);
        goto free_handle;
    }
    active_handle = handle;
    pr_info("zzy_mouse: connected to %s (%04x:%04x)\n",
            dev->name, dev->id.vendor, dev->id.product);
    return 0;
free_handle:
    kfree(handle);
    return ret;
}

static void mouse_disconnect(struct input_handle *handle)
{
    input_close_device(handle);
    input_unregister_handle(handle);
    active_handle = NULL;
    set_connection(false);
    kfree(handle);
    pr_info("zzy_mouse: mouse disconnected\n");
}

static const struct input_device_id mouse_ids[] = {
    { .flags = INPUT_DEVICE_ID_MATCH_BUS | INPUT_DEVICE_ID_MATCH_VENDOR |
               INPUT_DEVICE_ID_MATCH_PRODUCT,
      .bustype = BUS_USB, .vendor = 0x248d, .product = 0x5b5f },
    { },
};
MODULE_DEVICE_TABLE(input, mouse_ids);
static struct input_handler mouse_handler = {
    .event = mouse_event, .connect = mouse_connect,
    .disconnect = mouse_disconnect, .name = "zzy_mouse",
    .id_table = mouse_ids,
};

static int mouse_open(struct inode *inode, struct file *file)
{
    unsigned long flags;
    int ret = 0;
    spin_lock_irqsave(&queue_lock, flags);
    if (reader_open) {
        ret = -EBUSY;
    } else {
        kfifo_reset(&event_fifo);
        overflow_pending = false;
        reader_open = true;
        enqueue_locked(ZZY_MOUSE_META, ZZY_MOUSE_CONNECTION, connected);
    }
    spin_unlock_irqrestore(&queue_lock, flags);
    return ret ? ret : nonseekable_open(inode, file);
}

static int mouse_release(struct inode *inode, struct file *file)
{
    unsigned long flags;
    spin_lock_irqsave(&queue_lock, flags);
    reader_open = false;
    kfifo_reset(&event_fifo);
    overflow_pending = false;
    spin_unlock_irqrestore(&queue_lock, flags);
    return 0;
}

static ssize_t mouse_read(struct file *file, char __user *buffer,
                          size_t count, loff_t *position)
{
    struct zzy_mouse_event events[32];
    unsigned long flags;
    unsigned int number, capacity;
    ssize_t ret;
    if (!count)
        return 0;
    if (count < sizeof(events[0]))
        return -EINVAL;
    capacity = min_t(size_t, count / sizeof(events[0]), ARRAY_SIZE(events));
    if (mutex_lock_interruptible(&read_lock))
        return -ERESTARTSYS;
    for (;;) {
        spin_lock_irqsave(&queue_lock, flags);
        if (overflow_pending) {
            memset(&events[0], 0, sizeof(events[0]));
            events[0].timestamp_ns = ktime_get_ns();
            events[0].sequence = sequence++;
            events[0].type = EV_SYN;
            events[0].code = SYN_DROPPED;
            overflow_pending = false;
            number = 1;
        } else {
            number = kfifo_out(&event_fifo, events, capacity);
        }
        spin_unlock_irqrestore(&queue_lock, flags);
        if (number)
            break;
        if (file->f_flags & O_NONBLOCK) {
            ret = -EAGAIN;
            goto unlock;
        }
        ret = wait_event_interruptible(read_wait, data_ready());
        if (ret)
            goto unlock;
    }
    if (copy_to_user(buffer, events, number * sizeof(events[0]))) {
        spin_lock_irqsave(&queue_lock, flags);
        dropped += number;
        overflow_pending = true;
        spin_unlock_irqrestore(&queue_lock, flags);
        wake_up_interruptible(&read_wait);
        ret = -EFAULT;
    } else {
        ret = number * sizeof(events[0]);
    }
unlock:
    mutex_unlock(&read_lock);
    return ret;
}

static __poll_t mouse_poll(struct file *file, poll_table *wait)
{
    poll_wait(file, &read_wait, wait);
    return data_ready() ? EPOLLIN | EPOLLRDNORM : 0;
}

static long mouse_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned long flags;
    struct zzy_mouse_info info = {
        .abi_version = ZZY_MOUSE_ABI_VERSION, .struct_size = sizeof(info),
        .capabilities = ZZY_MOUSE_CAP_EVENTS, .name = "zzy_mouse",
    };
    struct zzy_mouse_stats stats = {};
    switch (cmd) {
    case ZZY_MOUSE_GET_INFO:
        spin_lock_irqsave(&queue_lock, flags);
        info.connected = connected;
        spin_unlock_irqrestore(&queue_lock, flags);
        return copy_to_user((void __user *)arg, &info, sizeof(info)) ? -EFAULT : 0;
    case ZZY_MOUSE_GET_STATS:
        spin_lock_irqsave(&queue_lock, flags);
        stats.received = received;
        stats.dropped = dropped;
        stats.queued = kfifo_len(&event_fifo);
        stats.connected = connected;
        spin_unlock_irqrestore(&queue_lock, flags);
        return copy_to_user((void __user *)arg, &stats, sizeof(stats)) ? -EFAULT : 0;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations mouse_fops = {
    .owner = THIS_MODULE, .open = mouse_open, .release = mouse_release,
    .read = mouse_read, .poll = mouse_poll, .unlocked_ioctl = mouse_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = compat_ptr_ioctl,
#endif
};
static struct miscdevice mouse_device = {
    .minor = MISC_DYNAMIC_MINOR, .name = "zzy_mouse",
    .fops = &mouse_fops, .mode = 0600,
};
static int __init mouse_init(void)
{
    int ret;
    BUILD_BUG_ON(sizeof(struct zzy_mouse_event) != 24);
    INIT_KFIFO(event_fifo);
    ret = input_register_handler(&mouse_handler);
    if (ret)
        return ret;
    ret = misc_register(&mouse_device);
    if (ret) {
        input_unregister_handler(&mouse_handler);
        return ret;
    }
    pr_info("zzy_mouse: stage 2 ready, event ABI %d\n", ZZY_MOUSE_ABI_VERSION);
    return 0;
}
static void __exit mouse_exit(void)
{
    misc_deregister(&mouse_device);
    input_unregister_handler(&mouse_handler);
    pr_info("zzy_mouse: unloaded\n");
}
module_init(mouse_init);
module_exit(mouse_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZZY");
MODULE_DESCRIPTION("RK3588 mouse input handler and event character device");
MODULE_VERSION("0.2");
