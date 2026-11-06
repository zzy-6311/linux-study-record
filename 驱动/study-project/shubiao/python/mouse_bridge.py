"""把 XC TECH 接收器的鼠标事件转发给电脑；Ctrl+C 停止。
板上运行：python3 -u mouse_bridge.py
启动时先松开鼠标按键。相对移动和五键原样转发，滚轮采用普通刻度。
"""
import argparse
import fcntl
import os
from pathlib import Path
import select
import signal
import stat
import struct
import sys
import time

DEVICE = "/dev/input/by-id/usb-XC_TECH_Wireless-Receiver-event-mouse"
# 64 位 Linux input_event；不依赖旧的观察/单次移动测试脚本。
EVENT = struct.Struct("@llHHi")
GADGET = Path("/sys/kernel/config/usb_gadget/zzy_mouse")


def reports(buttons, dx, dy, wheel, pan):
    """超出单个报告范围时拆包，保留移动总量和方向。"""
    while True:
        values = [max(-limit, min(limit, value))
                  for value, limit in zip((dx, dy, wheel, pan), (32767, 32767, 127, 127))]
        yield struct.pack("<Bhhbb", buttons, *values)
        dx -= values[0]
        dy -= values[1]
        wheel -= values[2]
        pan -= values[3]
        if not any((dx, dy, wheel, pan)):
            break


class Translator:
    def __init__(self):
        self.buttons = 0
        self.axes = [0, 0, 0, 0]
        self.changed = False

    def flush(self):
        if not self.changed:
            return []
        result = list(reports(self.buttons, *self.axes))
        self.axes = [0, 0, 0, 0]
        self.changed = False
        return result

    def feed(self, kind, code, value):
        if kind == 0 and code == 3:
            raise RuntimeError("输入事件丢失，停止转发并释放按键，请重新启动。")
        if kind == 0 and code == 0:
            return self.flush()
        # 普通、高精度滚轮是同一动作的两种表示，不能叠加。
        # 此接收器已确认报告普通滚轮，所以这里忽略代码 11/12。
        if kind == 2 and code in (0, 1, 8, 6):
            self.axes[{0: 0, 1: 1, 8: 2, 6: 3}[code]] += value
            self.changed = True
        elif kind == 1 and 272 <= code <= 276 and value in (0, 1):
            bit = 1 << (code - 272)
            new_buttons = self.buttons | bit if value else self.buttons & ~bit
            if new_buttons != self.buttons:
                # 保留每一次按下和松开的顺序，即使两者出现在同一帧。
                result = self.flush()
                self.buttons = new_buttons
                self.changed = True
                return result + self.flush()
        return []


def output_device():
    udc = (GADGET / "UDC").read_text().strip()
    if not udc or (Path("/sys/class/udc") / udc / "state").read_text().strip() != "configured":
        raise RuntimeError("USB 鼠标尚未连接电脑，请先配置 gadget。")
    major, minor = map(int, (GADGET / "functions/hid.usb0/dev").read_text().split(":"))
    for path in Path("/dev").glob("hidg*"):
        info = path.stat()
        if stat.S_ISCHR(info.st_mode) and info.st_rdev == os.makedev(major, minor):
            return path
    raise RuntimeError("未找到本项目的 HID 输出节点。")


def send(fd, report, timeout=1.0):
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([], [fd], [], remaining)[1]:
            raise TimeoutError("USB 输出超时")
        try:
            written = os.write(fd, report)
        except BlockingIOError:
            continue
        if written != len(report):
            raise RuntimeError("HID 报告写入不完整")
        return


def stop_signal(signum, frame):
    raise KeyboardInterrupt


def bridge(dry_run, seconds):
    if sys.platform != "linux" or struct.calcsize("@l") != 8:
        raise RuntimeError("请在板子的 64 位 Linux Python 中运行。")
    source = Path(DEVICE).resolve(strict=True)
    identity = Path("/sys/class/input") / source.name / "device/id"
    if (identity / "vendor").read_text().strip().lower() != "248d" or (
            identity / "product").read_text().strip().lower() != "5b5f":
        raise RuntimeError("输入设备不是已确认的接收器，拒绝读取。")
    target = output_device() if not dry_run else None
    # 与单次移动程序使用同一个设备锁，避免两个程序同时发报告。
    fd = os.open(target, os.O_WRONLY | os.O_NONBLOCK) if target else None
    locked = False
    count = 0
    try:
        if fd is not None:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
            send(fd, bytes(7))
        translator = Translator()
        deadline = time.monotonic() + seconds if seconds else None
        with open(source, "rb", buffering=0) as mouse:
            print(f"输入：{source}；输出：{target or '仅检查，不发送'}", flush=True)
            print("转发已开始，Ctrl+C 停止。", flush=True)
            while True:
                remaining = None if deadline is None else deadline - time.monotonic()
                if remaining is not None and remaining <= 0:
                    break
                if not select.select([mouse], [], [], remaining)[0]:
                    break
                data = os.read(mouse.fileno(), EVENT.size * 64)
                if not data or len(data) % EVENT.size:
                    raise RuntimeError("鼠标断开或事件数据不完整。")
                for _, _, kind, code, value in EVENT.iter_unpack(data):
                    for report in translator.feed(kind, code, value):
                        if fd is not None:
                            send(fd, report)
                        count += 1
    finally:
        if fd is not None:
            try:
                if locked:
                    send(fd, bytes(7), timeout=0.5)
            except OSError as error:
                print(f"未能发送按键释放报告（请检查 USB 连接）：{error}", file=sys.stderr)
            finally:
                os.close(fd)
        print(f"结束，共处理 {count} 个输出报告。", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="只检查输入，不向电脑发送")
    parser.add_argument("--seconds", type=float, default=0)
    args = parser.parse_args()
    if args.seconds < 0:
        parser.error("--seconds 不能为负数")
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, stop_signal)
    try:
        bridge(args.dry_run, args.seconds)
    except KeyboardInterrupt:
        print("已停止。")
    except (OSError, RuntimeError) as error:
        print(f"转发失败：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
