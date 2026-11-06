/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef ZZY_MOUSE_UAPI_H
#define ZZY_MOUSE_UAPI_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define ZZY_MOUSE_ABI_VERSION 2
#define ZZY_MOUSE_CAP_EVENTS 1
#define ZZY_MOUSE_META 0x8000
#define ZZY_MOUSE_CONNECTION 1
struct zzy_mouse_info {
    __u32 abi_version, struct_size, capabilities, connected;
    char name[32];
};
/* 单调时钟、固定 24 字节；序号允许 32 位回绕。 */
struct zzy_mouse_event {
    __aligned_u64 timestamp_ns;
    __u32 sequence;
    __u16 type, code;
    __s32 value;
    __u32 reserved;
};
struct zzy_mouse_stats {
    __aligned_u64 received;
    __aligned_u64 dropped;
    __u32 queued, connected;
};
#define ZZY_MOUSE_GET_INFO _IOR('Z', 0, struct zzy_mouse_info)
#define ZZY_MOUSE_GET_STATS _IOR('Z', 1, struct zzy_mouse_stats)
#endif
