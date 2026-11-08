/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef ZZY_BRIDGE_UAPI_H
#define ZZY_BRIDGE_UAPI_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define ZZY_BRIDGE_ABI 1
struct zzy_bridge_stats {
    __u32 abi, size, connected, configured, enabled, queued, buttons;
    __s32 last_error;
    __aligned_u64 input_events, submitted, completed, dropped, injected, usb_errors;
    __aligned_s64 physical_x, physical_y, injected_x, injected_y, completed_x, completed_y;
};
struct zzy_bridge_delta { __s32 dx, dy; };
#define ZZY_BRIDGE_STATUS _IOR('M', 0, struct zzy_bridge_stats)
#define ZZY_BRIDGE_ENABLE _IOW('M', 1, __u32)
#define ZZY_BRIDGE_INJECT _IOW('M', 2, struct zzy_bridge_delta)
/* AI is a single file-owned session. BTN_SIDE toggles it, initially OFF.
 * Generation prevents an inference started before OFF/ON or manual motion
 * from submitting an old command. Closing the file releases synthetic keys.
 */
struct zzy_ai_state {
    __u32 abi, size, active, generation, synthetic_buttons, side_code, commands, clicks;
};
struct zzy_ai_report {
    __u32 generation, buttons; /* only bit 0 (left) is accepted */
    __s32 dx, dy;
};
#define ZZY_AI_SESSION _IOW('M', 3, __u32)
#define ZZY_AI_STATE _IOR('M', 4, struct zzy_ai_state)
#define ZZY_AI_REPORT _IOW('M', 5, struct zzy_ai_report)
#define ZZY_AI_CANCEL _IO('M', 6)
#endif
