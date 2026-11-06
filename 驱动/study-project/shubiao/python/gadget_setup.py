"""配置临时 USB 鼠标。start 临时停 ADB；stop 恢复 ADB。需在板上以 root 运行。"""
import argparse
from pathlib import Path
import subprocess
import time

BASE = Path("/sys/kernel/config/usb_gadget")
G = BASE / "zzy_mouse"
UDC = "fc000000.usb"
SERVICE = "/etc/init.d/S50usb-gadget.sh"
# 5 按键 + 3 位填充、16 位相对 X/Y、8 位垂直滚轮、8 位水平滚轮。
REPORT = bytes.fromhex(
    "05 01 09 02 a1 01 09 01 a1 00 "
    "05 09 19 01 29 05 15 00 25 01 75 01 95 05 81 02 "
    "75 03 95 01 81 03 "
    "05 01 09 30 09 31 16 01 80 26 ff 7f 75 10 95 02 81 06 "
    "09 38 15 81 25 7f 75 08 95 01 81 06 "
    "05 0c 0a 38 02 15 81 25 7f 75 08 95 01 81 06 c0 c0"
)


def put(path, value):
    path.write_text(str(value))


def cleanup():
    # 只删除本程序创建的 configfs 项，不能递归删除 configfs。
    if not G.exists():
        return
    if (G / "UDC").read_text().strip():
        put(G / "UDC", "\n")
    link = G / "configs/c.1/hid.usb0"
    if link.is_symlink():
        link.unlink()
    for part in ("configs/c.1/strings/0x409", "configs/c.1",
                 "functions/hid.usb0", "strings/0x409"):
        path = G / part
        if path.exists():
            path.rmdir()
    G.rmdir()


def start():
    if G.exists():
        raise RuntimeError("zzy_mouse 已存在，请先执行 stop，不覆盖已有配置。")
    for gadget in BASE.iterdir():
        if gadget.name != "rockchip" and (gadget / "UDC").read_text().strip() == UDC:
            raise RuntimeError("控制器被其他 gadget 占用，停止操作。")
    if not Path("/sys/module/usb_f_hid").exists():
        subprocess.run(["insmod", "/lib/modules/6.12.69/extra/usb_f_hid.ko"], check=True)
    subprocess.run(["sh", SERVICE, "stop"], check=True, timeout=20)
    try:
        for _ in range(50):
            if not (BASE / "rockchip/UDC").read_text().strip():
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("ADB 尚未释放 USB 控制器。")
        G.mkdir()
        # Linux gadget 示例标识，仅用于本机开发验证，非产品 VID/PID。
        for name, value in {"idVendor": "0x1d6b", "idProduct": "0x0104",
                            "bcdDevice": "0x0100", "bcdUSB": "0x0200"}.items():
            put(G / name, value)
        strings = G / "strings/0x409"
        strings.mkdir()
        for name, value in {"serialnumber": "ZZY-MOUSE-DEMO-001",
                            "manufacturer": "ZZY Lab",
                            "product": "RK3588 Mouse Demo"}.items():
            put(strings / name, value)
        config = G / "configs/c.1"
        config.mkdir()
        put(config / "bmAttributes", "0xc0")  # 板子有独立电源。
        put(config / "MaxPower", "2")
        function = G / "functions/hid.usb0"
        function.mkdir()
        put(function / "subclass", "0")
        put(function / "protocol", "0")
        put(function / "report_length", "7")
        put(function / "no_out_endpoint", "1")
        (function / "report_desc").write_bytes(REPORT)
        (config / "hid.usb0").symlink_to(function)
        put(G / "UDC", UDC)
        print("HID 已绑定。设备号：" + (function / "dev").read_text().strip())
        print("在 /dev/hidg* 查看对应节点。当前没有发送任何移动或点击。")
    except Exception:
        cleanup()
        subprocess.run(["sh", SERVICE, "start"], check=False, timeout=20)
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["start", "stop"])
    args = parser.parse_args()
    if args.action == "start":
        start()
    else:
        cleanup()
        subprocess.run(["sh", SERVICE, "start"], check=True, timeout=20)
        print("已移除模拟鼠标，并请求恢复官方 ADB。")
