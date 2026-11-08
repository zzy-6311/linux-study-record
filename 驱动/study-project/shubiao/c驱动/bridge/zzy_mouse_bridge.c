// SPDX-License-Identifier: GPL-2.0
/* USB HID input -> kernel queue -> USB Gadget IN endpoint.
 * Python only uses control ioctls, never transports physical mouse events.
 * Queue submission and completion use one request; no sleeping in input callbacks.
 */
#include <linux/module.h>
#include <linux/input.h>
#include <linux/usb/composite.h>
#include <linux/hid.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/workqueue.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include "zzy_bridge_uapi.h"

struct mouse_report {
    u8 buttons;
    __le16 x, y;
    s8 wheel, pan;
} __packed;
static DEFINE_SPINLOCK(lock);
static DECLARE_KFIFO(queue, struct mouse_report, 1024);
static struct zzy_bridge_stats counters;
static struct input_handle *source;
static struct usb_ep *in_ep;
static struct usb_request *in_req;
static bool configured, enabled, busy, connected, dirty, bound;
static u8 buttons;
static s32 axes[4];
static struct work_struct tx_work;
static struct delayed_work click_release_work;
static struct file *ai_owner;
static bool ai_active;
static u8 ai_buttons;
static u32 ai_generation, ai_commands, ai_clicks;
static char *udc_name = "fc000000.usb";
module_param(udc_name, charp, 0444);
MODULE_PARM_DESC(udc_name, "USB device controller to bind (never USB Host)");

static const u8 report_desc[] = {
    0x05,0x01,0x09,0x02,0xa1,0x01,0x09,0x01,0xa1,0x00,
    0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x05,0x81,0x02,0x75,0x03,0x95,0x01,0x81,0x03,
    0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x01,0x80,0x26,0xff,0x7f,
    0x75,0x10,0x95,0x02,0x81,0x06,
    0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,
    0x05,0x0c,0x0a,0x38,0x02,0x15,0x81,0x25,0x7f,
    0x75,0x08,0x95,0x01,0x81,0x06,0xc0,0xc0
};
static struct usb_interface_descriptor interface_desc = {
    .bLength = sizeof(interface_desc), .bDescriptorType = USB_DT_INTERFACE,
    .bNumEndpoints = 1, .bInterfaceClass = USB_CLASS_HID,
};
static struct hid_descriptor hid_desc = {
    .bLength = sizeof(hid_desc), .bDescriptorType = HID_DT_HID,
    .bcdHID = cpu_to_le16(0x0111), .bNumDescriptors = 1,
    .rpt_desc = { .bDescriptorType = HID_DT_REPORT,
                  .wDescriptorLength = cpu_to_le16(sizeof(report_desc)) },
};
static struct usb_endpoint_descriptor fs_ep = {
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = USB_DIR_IN, .bmAttributes = USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize = cpu_to_le16(7), .bInterval = 1,
};
static struct usb_endpoint_descriptor hs_ep = {
    .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = USB_DIR_IN, .bmAttributes = USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize = cpu_to_le16(7), .bInterval = 4,
};
static struct usb_descriptor_header *fs_descs[] = {
    (void *)&interface_desc, (void *)&hid_desc, (void *)&fs_ep, NULL,
};
static struct usb_descriptor_header *hs_descs[] = {
    (void *)&interface_desc, (void *)&hid_desc, (void *)&hs_ep, NULL,
};

/* All *_locked helpers require lock. A fault stops forwarding and queues a
 * neutral report when USB is still configured, preventing a stuck button.
 */
static void reset_motion_locked(void)
{
    memset(axes, 0, sizeof(axes));
    dirty = false;
}
static void halt_locked(int error, bool neutral)
{
    struct mouse_report release = {};
    enabled = false;
    ai_active = false;
    ai_buttons = 0;
    ai_generation++;
    cancel_delayed_work(&click_release_work);
    counters.last_error = error;
    counters.dropped += kfifo_len(&queue);
    kfifo_reset(&queue);
    reset_motion_locked();
    if (neutral && configured) {
        kfifo_in(&queue, &release, 1);
        schedule_work(&tx_work);
    }
}
static bool enqueue_locked(const struct mouse_report *report)
{
    if (kfifo_is_full(&queue)) {
        counters.dropped++; /* also count the rejected report */
        halt_locked(-ENOSPC, true);
        return false;
    }
    kfifo_in(&queue, report, 1);
    schedule_work(&tx_work);
    return true;
}
static bool emit_locked(s32 dx, s32 dy, s32 wheel, s32 pan)
{
    do {
        s32 x = clamp(dx, -32767, 32767), y = clamp(dy, -32767, 32767);
        s32 w = clamp(wheel, -127, 127), p = clamp(pan, -127, 127);
        struct mouse_report r = {
            .buttons = (buttons & ~(ai_owner ? BIT(3) : 0)) | ai_buttons,
            .x = cpu_to_le16((u16)x),
            .y = cpu_to_le16((u16)y), .wheel = w, .pan = p,
        };
        if (!enqueue_locked(&r))
            return false;
        dx -= x; dy -= y; wheel -= w; pan -= p;
    } while (dx || dy || wheel || pan);
    return true;
}
static void flush_locked(void)
{
    if (dirty && enabled && configured)
        emit_locked(axes[0], axes[1], axes[2], axes[3]);
    reset_motion_locked();
}

static void cancel_ai_locked(void)
{
    ai_active = false;
    ai_buttons = 0;
    ai_generation++;
    cancel_delayed_work(&click_release_work);
    counters.dropped += kfifo_len(&queue);
    kfifo_reset(&queue);
    reset_motion_locked();
    if (enabled && configured)
        emit_locked(0, 0, 0, 0);
}

static void release_click(struct work_struct *work)
{
    unsigned long flags;
    spin_lock_irqsave(&lock, flags);
    if (ai_buttons) {
        ai_buttons = 0;
        if (enabled && configured)
            emit_locked(0, 0, 0, 0);
    }
    spin_unlock_irqrestore(&lock, flags);
}

static void tx_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct mouse_report *r = req->buf;
    unsigned long flags;
    spin_lock_irqsave(&lock, flags);
    busy = false;
    if (!req->status && req->actual == sizeof(*r)) {
        counters.completed++;
        counters.completed_x += (s16)le16_to_cpu(r->x);
        counters.completed_y += (s16)le16_to_cpu(r->y);
    } else if (configured) {
        counters.usb_errors++;
        counters.dropped++;
        /* A broken endpoint must not create an endless retry loop. */
        halt_locked(req->status ? req->status : -EIO, false);
    }
    if (configured && !kfifo_is_empty(&queue))
        schedule_work(&tx_work);
    spin_unlock_irqrestore(&lock, flags);
}
static void transmit(struct work_struct *work)
{
    unsigned long flags;
    int ret;
    spin_lock_irqsave(&lock, flags);
    if (!configured || busy || !in_req || kfifo_is_empty(&queue))
        goto out;
    if (kfifo_out(&queue, (struct mouse_report *)in_req->buf, 1) != 1)
        goto out;
    in_req->length = sizeof(struct mouse_report);
    in_req->zero = 0;
    busy = true;
    /* Gadget API guarantees completion will not run inside usb_ep_queue().
     * Serialize submission against disable's configured=false transition.
     */
    ret = usb_ep_queue(in_ep, in_req, GFP_ATOMIC);
    if (ret) {
        busy = false;
        counters.usb_errors++;
        counters.dropped++;
        halt_locked(ret, false);
    } else {
        counters.submitted++;
    }
out:
    spin_unlock_irqrestore(&lock, flags);
}

static void receive_event(struct input_handle *handle, unsigned int type,
                          unsigned int code, int value)
{
    unsigned long flags;
    int axis = -1;
    spin_lock_irqsave(&lock, flags);
    counters.input_events++;
    if (type == EV_KEY && code >= BTN_LEFT && code <= BTN_EXTRA &&
        (value == 0 || value == 1)) {
        u8 bit = BIT(code - BTN_LEFT);
        u8 next = value ? buttons | bit : buttons & ~bit;
        if (next != buttons) {
            flush_locked();
            buttons = next;
            if (ai_owner && code == BTN_SIDE) {
                if (value) {
                    if (ai_active)
                        cancel_ai_locked();
                    else if (enabled && configured && !(buttons & ~BIT(3))) {
                        ai_active = true;
                        ai_generation++;
                    }
                }
                goto out_event; /* reserved switch never reaches the PC */
            }
            if (ai_owner)
                ai_generation++; /* physical button invalidates old inference */
            if (enabled && configured)
                emit_locked(0, 0, 0, 0);
        }
    } else if (type == EV_SYN && code == SYN_DROPPED) {
        halt_locked(-EOVERFLOW, true);
    } else if (enabled && configured) {
        if (type == EV_SYN && code == SYN_REPORT)
            flush_locked();
        if (type == EV_REL) {
            if (ai_owner && value && (code == REL_X || code == REL_Y))
                ai_generation++;
            switch (code) {
            case REL_X: axis = 0; counters.physical_x += value; break;
            case REL_Y: axis = 1; counters.physical_y += value; break;
            case REL_WHEEL: axis = 2; break;
            case REL_HWHEEL: axis = 3; break;
            default: break; /* do not double count HI_RES wheel */
            }
            if (axis >= 0) {
                if (check_add_overflow(axes[axis], value, &axes[axis]))
                    halt_locked(-EOVERFLOW, true);
                else
                    dirty = true;
            }
        }
    }
out_event:
    spin_unlock_irqrestore(&lock, flags);
}
static int connect_mouse(struct input_handler *handler, struct input_dev *dev,
                          const struct input_device_id *id)
{
    struct input_handle *handle;
    unsigned long flags;
    int ret, i;
    if (source || !test_bit(EV_REL, dev->evbit) ||
        !test_bit(REL_X, dev->relbit) || !test_bit(REL_Y, dev->relbit) ||
        !test_bit(BTN_LEFT, dev->keybit))
        return -ENODEV;
    handle = kzalloc(sizeof(*handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;
    handle->dev = dev; handle->handler = handler; handle->name = "zzy_mouse_bridge";
    ret = input_register_handle(handle);
    if (ret)
        goto fail;
    /* Same lock order as the input callback: event_lock then bridge lock. */
    spin_lock_irqsave(&dev->event_lock, flags);
    spin_lock(&lock);
    buttons = 0;
    for (i = 0; i < 5; i++)
        if (test_bit(BTN_LEFT + i, dev->key))
            buttons |= BIT(i);
    connected = true;
    spin_unlock(&lock);
    spin_unlock_irqrestore(&dev->event_lock, flags);
    ret = input_open_device(handle);
    if (ret) {
        spin_lock_irqsave(&lock, flags);
        connected = false;
        spin_unlock_irqrestore(&lock, flags);
        input_unregister_handle(handle);
        goto fail;
    }
    source = handle;
    pr_info("zzy_mouse_bridge: input %s\n", dev->name);
    return 0;
fail:
    kfree(handle);
    return ret;
}
static void disconnect_mouse(struct input_handle *handle)
{
    unsigned long flags;
    input_close_device(handle);
    input_unregister_handle(handle);
    spin_lock_irqsave(&lock, flags);
    connected = false; buttons = 0;
    halt_locked(-ENODEV, true);
    spin_unlock_irqrestore(&lock, flags);
    source = NULL;
    kfree(handle);
}
static const struct input_device_id ids[] = {
    { .flags = INPUT_DEVICE_ID_MATCH_BUS | INPUT_DEVICE_ID_MATCH_VENDOR |
               INPUT_DEVICE_ID_MATCH_PRODUCT,
      .bustype = BUS_USB, .vendor = 0x248d, .product = 0x5b5f }, { }
};
MODULE_DEVICE_TABLE(input, ids);
static struct input_handler handler = {
    .event = receive_event, .connect = connect_mouse, .disconnect = disconnect_mouse,
    .name = "zzy_mouse_bridge", .id_table = ids,
};

static long control_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    void __user *user = (void __user *)arg;
    struct zzy_bridge_stats stats;
    struct zzy_bridge_delta delta;
    struct zzy_ai_state ai_state;
    struct zzy_ai_report ai_report;
    unsigned long flags;
    u32 value;
    int ret = 0;
    switch (cmd) {
    case ZZY_AI_SESSION:
        if (copy_from_user(&value, user, sizeof(value)))
            return -EFAULT;
        if (value > 1)
            return -EINVAL;
        spin_lock_irqsave(&lock, flags);
        if (ai_owner && ai_owner != file) {
            ret = -EBUSY;
        } else if (value) {
            if (!connected || !configured || !enabled)
                ret = -ENODEV;
            else if (buttons)
                ret = -EBUSY;
            else {
                ai_owner = file;
                cancel_ai_locked();
            }
        } else if (ai_owner == file) {
            cancel_ai_locked();
            ai_owner = NULL;
            if (enabled && configured)
                emit_locked(0, 0, 0, 0);
        }
        spin_unlock_irqrestore(&lock, flags);
        return ret;
    case ZZY_AI_STATE:
        spin_lock_irqsave(&lock, flags);
        ai_state = (struct zzy_ai_state) {
            .abi = 1, .size = sizeof(ai_state), .active = ai_active,
            .generation = ai_generation, .synthetic_buttons = ai_buttons,
            .side_code = BTN_SIDE, .commands = ai_commands, .clicks = ai_clicks,
        };
        spin_unlock_irqrestore(&lock, flags);
        return copy_to_user(user, &ai_state, sizeof(ai_state)) ? -EFAULT : 0;
    case ZZY_AI_CANCEL:
        spin_lock_irqsave(&lock, flags);
        if (ai_owner != file)
            ret = -EPERM;
        else
            cancel_ai_locked();
        spin_unlock_irqrestore(&lock, flags);
        return ret;
    case ZZY_AI_REPORT:
        if (copy_from_user(&ai_report, user, sizeof(ai_report)))
            return -EFAULT;
        if (ai_report.buttons > 1 || ai_report.dx < -2000 || ai_report.dx > 2000 ||
            ai_report.dy < -2000 || ai_report.dy > 2000)
            return -ERANGE;
        spin_lock_irqsave(&lock, flags);
        if (ai_owner != file) {
            ret = -EPERM;
        } else if (!ai_report.buttons && !ai_report.dx && !ai_report.dy) {
            ai_buttons = 0; /* release is allowed even after OFF or epoch change */
            cancel_delayed_work(&click_release_work);
            if (enabled && configured)
                emit_locked(0, 0, 0, 0);
        } else if (!ai_active || !connected || !configured || !enabled) {
            ret = -EACCES;
        } else if (ai_report.generation != ai_generation) {
            ret = -ESTALE;
        } else if (buttons & ~BIT(3)) {
            ret = -EBUSY;
        } else {
            u8 previous = ai_buttons;
            flush_locked();
            ai_buttons = ai_report.buttons;
            if (!enabled || !emit_locked(ai_report.dx, ai_report.dy, 0, 0)) {
                ai_buttons = 0;
                ret = -ENOSPC;
            } else {
                counters.injected++;
                counters.injected_x += ai_report.dx;
                counters.injected_y += ai_report.dy;
                ai_commands++;
                if (ai_buttons && !previous)
                    ai_clicks++;
                if (ai_buttons)
                    mod_delayed_work(system_wq, &click_release_work, msecs_to_jiffies(120));
                else
                    cancel_delayed_work(&click_release_work);
            }
        }
        spin_unlock_irqrestore(&lock, flags);
        return ret;
    case ZZY_BRIDGE_STATUS:
        spin_lock_irqsave(&lock, flags);
        stats = counters;
        stats.abi = ZZY_BRIDGE_ABI; stats.size = sizeof(stats);
        stats.connected = connected; stats.configured = configured;
        stats.enabled = enabled; stats.queued = kfifo_len(&queue);
        stats.buttons = buttons;
        spin_unlock_irqrestore(&lock, flags);
        return copy_to_user(user, &stats, sizeof(stats)) ? -EFAULT : 0;
    case ZZY_BRIDGE_ENABLE:
        if (copy_from_user(&value, user, sizeof(value)))
            return -EFAULT;
        if (value > 1)
            return -EINVAL;
        spin_lock_irqsave(&lock, flags);
        if (!value) {
            halt_locked(0, true);
        } else if (enabled) {
            ret = 0;
        } else if (!connected || !configured) {
            ret = -ENODEV;
        } else if (buttons || busy || !kfifo_is_empty(&queue)) {
            ret = -EBUSY;
        } else {
            reset_motion_locked();
            counters.last_error = 0;
            enabled = true;
            emit_locked(0, 0, 0, 0);
        }
        spin_unlock_irqrestore(&lock, flags);
        return ret;
    case ZZY_BRIDGE_INJECT:
        if (copy_from_user(&delta, user, sizeof(delta)))
            return -EFAULT;
        if (delta.dx < -32767 || delta.dx > 32767 ||
            delta.dy < -32767 || delta.dy > 32767)
            return -ERANGE;
        spin_lock_irqsave(&lock, flags);
        if (ai_owner) {
            ret = -EBUSY;
        } else if (!connected || !configured || !enabled) {
            ret = -ENODEV;
        } else {
            flush_locked();
            if (!enabled || !emit_locked(delta.dx, delta.dy, 0, 0)) {
                ret = -ENOSPC;
            } else {
                counters.injected++;
                counters.injected_x += delta.dx;
                counters.injected_y += delta.dy;
            }
        }
        spin_unlock_irqrestore(&lock, flags);
        return ret;
    default:
        return -ENOTTY;
    }
}
static int control_release(struct inode *inode, struct file *file)
{
    unsigned long flags;
    spin_lock_irqsave(&lock, flags);
    if (ai_owner == file) {
        cancel_ai_locked();
        ai_owner = NULL;
        if (enabled && configured)
            emit_locked(0, 0, 0, 0);
    }
    spin_unlock_irqrestore(&lock, flags);
    return 0;
}
static const struct file_operations control_fops = {
    .owner = THIS_MODULE, .unlocked_ioctl = control_ioctl,
    .release = control_release,
#ifdef CONFIG_COMPAT
    .compat_ioctl = compat_ptr_ioctl,
#endif
};
static struct miscdevice control = {
    .minor = MISC_DYNAMIC_MINOR, .name = "zzy_mouse_bridge",
    .mode = 0600, .fops = &control_fops,
};

static void function_disable(struct usb_function *f)
{
    unsigned long flags;
    spin_lock_irqsave(&lock, flags);
    configured = false;
    halt_locked(-ESHUTDOWN, false);
    spin_unlock_irqrestore(&lock, flags);
    /* Never hold bridge lock here: disable completes pending requests. */
    usb_ep_disable(in_ep);
}
static int function_alt(struct usb_function *f, unsigned int intf, unsigned int alt)
{
    unsigned long flags;
    struct mouse_report release = {};
    int ret;
    if (alt)
        return -EINVAL;
    function_disable(f);
    ret = config_ep_by_speed(f->config->cdev->gadget, f, in_ep);
    if (ret)
        return ret;
    ret = usb_ep_enable(in_ep);
    if (ret)
        return ret;
    spin_lock_irqsave(&lock, flags);
    busy = false;
    configured = true;
    counters.last_error = 0;
    enqueue_locked(&release);
    spin_unlock_irqrestore(&lock, flags);
    return 0;
}
static int function_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
    struct usb_composite_dev *cdev = f->config->cdev;
    struct usb_request *req = cdev->req;
    u16 value = le16_to_cpu(ctrl->wValue), length = le16_to_cpu(ctrl->wLength);
    u16 index = le16_to_cpu(ctrl->wIndex);
    static u8 idle;
    unsigned long flags;
    struct mouse_report r = {};
    const void *data = NULL;
    unsigned int size = 0;
    if (index != interface_desc.bInterfaceNumber)
        return -EOPNOTSUPP;
    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
    case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8) | USB_REQ_GET_DESCRIPTOR:
        if ((value >> 8) == HID_DT_REPORT) {
            data = report_desc; size = sizeof(report_desc);
        } else if ((value >> 8) == HID_DT_HID) {
            data = &hid_desc; size = sizeof(hid_desc);
        } else {
            return -EOPNOTSUPP;
        }
        break;
    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8) | HID_REQ_GET_REPORT:
        if (value != (1 << 8)) /* input report, no report ID */
            return -EOPNOTSUPP;
        spin_lock_irqsave(&lock, flags);
        r.buttons = enabled ? (buttons & ~(ai_owner ? BIT(3) : 0)) | ai_buttons : 0;
        spin_unlock_irqrestore(&lock, flags);
        data = &r; size = sizeof(r);
        break;
    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8) | HID_REQ_GET_IDLE:
        data = &idle; size = 1;
        break;
    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8) | HID_REQ_SET_IDLE:
        if (length || (value & 0xff))
            return -EOPNOTSUPP;
        idle = value >> 8;
        break;
    default:
        return -EOPNOTSUPP;
    }
    req->length = min_t(unsigned int, length, size);
    req->zero = req->length < length;
    if (req->length)
        memcpy(req->buf, data, req->length);
    return usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
}
static int function_bind(struct usb_configuration *c, struct usb_function *f)
{
    int ret = usb_interface_id(c, f);
    if (ret < 0)
        return ret;
    interface_desc.bInterfaceNumber = ret;
    in_ep = usb_ep_autoconfig(c->cdev->gadget, &fs_ep);
    if (!in_ep)
        return -ENODEV;
    hs_ep.bEndpointAddress = fs_ep.bEndpointAddress;
    ret = usb_assign_descriptors(f, fs_descs, hs_descs, NULL, NULL);
    if (ret)
        return ret;
    in_req = usb_ep_alloc_request(in_ep, GFP_KERNEL);
    if (!in_req) {
        ret = -ENOMEM;
        goto free_desc;
    }
    in_req->buf = kzalloc(sizeof(struct mouse_report), GFP_KERNEL);
    if (!in_req->buf) {
        usb_ep_free_request(in_ep, in_req);
        in_req = NULL;
        ret = -ENOMEM;
        goto free_desc;
    }
    in_req->complete = tx_complete;
    return 0;
free_desc:
    usb_free_all_descriptors(f);
    return ret;
}
static void function_unbind(struct usb_configuration *c, struct usb_function *f)
{
    function_disable(f);
    cancel_work_sync(&tx_work);
    kfree(in_req->buf);
    usb_ep_free_request(in_ep, in_req);
    in_req = NULL;
    usb_free_all_descriptors(f);
}
static struct usb_function function = {
    .name = "zzy_mouse", .bind = function_bind, .unbind = function_unbind,
    .set_alt = function_alt, .disable = function_disable, .setup = function_setup,
};
static struct usb_configuration configuration = {
    .label = "Kernel Mouse Bridge", .bConfigurationValue = 1,
    .bmAttributes = USB_CONFIG_ATT_SELFPOWER, .MaxPower = 2,
};
static int add_function(struct usb_configuration *c)
{
    return usb_add_function(c, &function);
}
static struct usb_device_descriptor device_desc = {
    .bLength = sizeof(device_desc), .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = cpu_to_le16(0x0200), .bDeviceClass = USB_CLASS_PER_INTERFACE,
    /* Development-only gadget IDs, not a commercial product allocation. */
    .idVendor = cpu_to_le16(0x1d6b), .idProduct = cpu_to_le16(0x0104),
    .bcdDevice = cpu_to_le16(0x0300), .bNumConfigurations = 1,
};
static struct usb_string strings[] = {
    { .s = "ZZY Lab" }, { .s = "RK3588 Kernel Mouse Bridge" },
    { .s = "ZZY-KERNEL-MOUSE-001" }, { }
};
static struct usb_gadget_strings language = { .language = 0x0409, .strings = strings };
static struct usb_gadget_strings *all_strings[] = { &language, NULL };
static int composite_bind(struct usb_composite_dev *cdev)
{
    int ret;
    /* 6.12 usb_composite_probe overwrites gadget_driver, including udc_name.
     * Reject every other controller in bind instead of silently binding it.
     */
    if (!cdev->gadget->dev.parent ||
        strcmp(dev_name(cdev->gadget->dev.parent), udc_name))
        return -ENODEV;
    ret = usb_string_ids_tab(cdev, strings);
    if (ret < 0)
        return ret;
    device_desc.iManufacturer = strings[0].id;
    device_desc.iProduct = strings[1].id;
    device_desc.iSerialNumber = strings[2].id;
    ret = usb_add_config(cdev, &configuration, add_function);
    if (!ret)
        bound = true;
    return ret;
}
static struct usb_composite_driver composite = {
    .name = "zzy_mouse_bridge", .dev = &device_desc, .strings = all_strings,
    .max_speed = USB_SPEED_HIGH, .bind = composite_bind,
};
static int __init bridge_init(void)
{
    int ret;
    BUILD_BUG_ON(sizeof(struct mouse_report) != 7);
    BUILD_BUG_ON(sizeof(struct zzy_bridge_stats) != 128);
    INIT_KFIFO(queue);
    INIT_WORK(&tx_work, transmit);
    INIT_DELAYED_WORK(&click_release_work, release_click);
    ret = usb_composite_probe(&composite);
    if (ret)
        return ret;
    if (!bound) {
        ret = -ENODEV;
        goto unregister_usb;
    }
    ret = input_register_handler(&handler);
    if (ret)
        goto unregister_usb;
    ret = misc_register(&control);
    if (ret) {
        input_unregister_handler(&handler);
        goto unregister_usb;
    }
    pr_info("zzy_mouse_bridge: ready; control ioctl enables kernel forwarding\n");
    return 0;
unregister_usb:
    usb_composite_unregister(&composite);
    cancel_delayed_work_sync(&click_release_work);
    cancel_work_sync(&tx_work);
    return ret;
}
static void __exit bridge_exit(void)
{
    misc_deregister(&control);
    input_unregister_handler(&handler);
    usb_composite_unregister(&composite);
    cancel_delayed_work_sync(&click_release_work);
    cancel_work_sync(&tx_work);
}
module_init(bridge_init);
module_exit(bridge_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ZZY");
MODULE_DESCRIPTION("In-kernel USB mouse bridge with relative injection control");
MODULE_VERSION("0.4");
