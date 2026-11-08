# RK3588 鼠标转接项目

## 2026-11-07：接入 Aimlabs 识别控制（驱动 0.4）

保留原来的纯内核鼠标透传及 ABI 1 位移接口，新增单个文件持有的 AI 控制会话。
仅当识别程序申请该会话时，`BTN_SIDE`（后退侧键）改作自动控制开关，不再传给电脑。
首次申请会话默认关闭；按一次开启，再按一次关闭。另一个侧键继续透传。

- 新接口位于 `c驱动/bridge/zzy_bridge_uapi.h`，支持会话、状态、带代次校验的移动/左键报告及取消。
- 开关关闭、手动移动导致旧结果失效；关闭开关清理未发送队列，已经提交到 USB 的报告可能已被主机接收。
- 自动按下左键后，用户态会显式释放；内核另设 120ms 释放机制。进程关闭文件也取消自动控制并释放合成按键。
- 原物理鼠标移动、点击和滚轮仍由驱动接收并转发；Python 不搬运普通鼠标报告。
- `dropped` 也包含开关/会话取消时清掉的排队报告，需结合 `usb_errors` 判读。

AI 程序的权威源码位于：

```text
G:\python-project\linux-project\project\linux-hdmi-preview\pt\hdmi_pt.py
G:\python-project\linux-project\project\linux-hdmi-preview\pt\aim_control.py
```

板上部署在 `/root/linux-hdmi-preview/pt/`，使用原始 `best.pt`、PyTorch CPU 推理。
启用鼠标联动的入口为 `python3 hdmi_pt.py --mouse-control`，不带此参数时只显示识别结果。
选择离画面中心准星最近且置信度不低于 0.65 的小球，顺序执行识别、相对移动、左键按下/释放，再获取新帧。
像素通过透视投影转换为转角，再用每计数转角计算鼠标位移。
初始参数取自本机 Aimlabs 保存的视野角及灵敏度；改变游戏配置后应调整 `--vfov`、`--yaw`、`--pitch`。
此算法针对全屏第一人称训练场，非桌面绝对光标定位，不适用于视频播放画面的交互。

当前验证：SDK 构建 `W=1` 无警告，板上模块加载/USB 枚举成功；6 项用户态控制逻辑测试通过；
真实驱动确认 OFF 时拒绝自动移动/点击、第二控制会话被拒绝、关闭后重新打开仍为 OFF。
实际侧键交互与游戏命中效果待上板操作确认，不据此宣称已经准确自动瞄准。

整理日期：2026-11-06。

**当前使用：内核转发版本，纯透传和 200ms 位移注入均已上板验证。**
源码位于 `c驱动/bridge/`，控制程序为 `python/driver_control.py`。
这个版本把输入采集、HID 报告编码和 USB 输出全部放在内核中，Python 仅发控制 ioctl。
已通过本机 Linux 6.12.69 源码的目标文件编译检查（W=1）及核心逻辑测试。
现已通过官方 SDK 正式构建，模块版本 0.3，vermagic 为 `6.12.69 SMP mod_unload aarch64`。
板上模块加载成功，Windows 识别为 `HID-compliant mouse`，序列号 `ZZY-KERNEL-MOUSE-001`。
用户已确认纯内核鼠标控制正常，并看到第二轮定时注入按节奏移动、在约五秒后停止。
以下原 Python 版本仍作为已验证的回退版本保留。两种转发方式不能同时运行。

### 2026-11-06 上板验收结果

| 项目 | 结果 |
|---|---|
| SDK 正式构建 | W=1，无编译警告，模块 0.3 |
| Windows 识别 | USB 输入设备和 HID-compliant mouse 均为 OK |
| 纯驱动透传 | 用户确认可控制；首次误以为异常是物理鼠标休眠 |
| 物理输入计数 | 10733 条事件；当时已完成 3889 个 USB 报告（含初始化释放报告） |
| 物理位移核对 | X=1172、Y=-423，输入和 USB 完成总量一致 |
| 定时注入 | 两轮，每轮 25 次，每次 X=+4、Y=0，目标间隔 200ms |
| 注入总量核对 | 每轮 injected 增加 25，USB completed 增加 25，完成位移 X 增加 100 |
| 第二轮视觉确认 | 用户确认光标按节奏移动，随后停止 |
| 错误及丢弃 | 两轮均无新增 usb_errors 和 dropped |
| 注入结束后 | Python 退出；enabled=1，纯内核透传继续 |

这验证的是当前鼠标、当前板子及当前 Windows 主机上的两种功能。
没有据此宣称低于原 Python 方案的延迟，也没有完成热插拔、长时间压力、所有 HID 兼容性或开机自动运行验收。

正式模块 SHA-256：`686bea41987dce4b08c76d5d91c75384af54c18bc2c8d68bf9b1d20ab37de771`。

## 新版本：两种测试

新增文件：

```text
c驱动/bridge/Makefile
c驱动/bridge/zzy_mouse_bridge.c
c驱动/bridge/zzy_bridge_uapi.h
c驱动/bridge/zzy_mouse_bridge.ko
python/driver_control.py
```

新模块使用 Linux 现有 USB Host/HID 层解析鼠标，通过 input_handler 接收事件，
在内核中生成七字节 HID 报告，直接向 USB Gadget IN 端点提交请求。
它不读写用户态 `/dev/hidg*`，不需要 Python 搬运鼠标数据，也不依赖旧的 `usb_f_hid.ko`。
这是保持动作含义的转发，不是接收器原始 USB 报文逐字节复制。

该模块只绑定 `fc000000.usb`，仅匹配 `248d:5b5f` 的鼠标输入接口。
USB 配置完成后仍保持透传关闭；一次性控制命令启用后，即使 Python 退出也持续工作。
输出以 USB 2.0 HID 非 Boot 协议工作，保留五键、相对 X/Y、普通垂直/水平滚轮。
绝对坐标、完整 HID idle 周期重发及产品级跨主机兼容性仍不在本次验证范围内。

### 部署前提

先用官方 SDK 构建新模块。把 `c驱动/bridge` 三个文件放进独立 ext4 工作目录，
用本文末尾相同的工具链和 SDK 内核命令编译，只将 `M=` 指向新工作目录。
编译产物应为 `zzy_mouse_bridge.ko`。不要拿本地编译检查的 `.o` 去加载。

将正式生成的模块和 `driver_control.py` 上传到板子 `/root/linux-shubiao/`。
停止旧 `mouse_bridge.py`；如果旧 `zzy_mouse` gadget 已创建，先执行旧配置程序的 stop。
再停止官方 ADB gadget，释放 USB Device 控制器，最后加载新模块：

```sh
cd /root/linux-shubiao
# 仅在旧 zzy_mouse gadget 存在时执行：
python3 gadget_setup.py stop
sh /etc/init.d/S50usb-gadget.sh stop
# 确认旧 gadget 的 UDC 为空、电脑 OTG 数据线连接正常后：
insmod ./zzy_mouse_bridge.ko
python3 driver_control.py status
```

`connected=1` 表示输入接收器就绪；`configured=1` 表示电脑已配置新的 USB 鼠标；
`enabled=0` 表示尚未启用转发。若 USB 控制器被占用，先解决占用，不要强行重复加载。

### 测试一：纯内核透传

松开所有鼠标按键，执行一次：

```sh
python3 driver_control.py start
```

命令立即退出。此后不运行 Python 转发程序，移动鼠标、使用五个按键和滚轮，
确认电脑收到正常动作。之后查询：

```sh
python3 driver_control.py status
```

应看到 `enabled=1`、`completed` 增加，且无新增 `usb_errors` 或异常丢弃。
`completed` 表示 USB 请求已完成，不能据此声称电脑应用或屏幕已显示动作。
`queued` 只统计尚未提交的报告，不包含正在 USB 上传输的一份报告；
刚提交最后一次注入时，submitted 比 completed 多 1 可能是正常瞬时状态，静止等待后再核对。

### 测试二：每 200ms 注入固定偏移

先让物理鼠标静止并松开所有按键，记录一次 status，再执行：

```sh
python3 driver_control.py inject --dx 4 --dy 0 --interval-ms 200 --count 25
sleep 1
python3 driver_control.py status
```

预期约五秒内向右分步移动：25 次 × 4 = 100 个相对计数。
电脑加速度和灵敏度会影响屏幕像素距离，因此不是保证移动 100 像素。
对比测试前后，`injected` 应增加 25、`injected_x` 增加 100、`injected_y` 不变。
无物理移动、无错误且队列排空时，`completed_x` 也应增加 100。
Python 的 sleep/调度并非硬实时；若调度延迟，程序不会追赶补发形成突发移动。

注入是新增相对移动，保留物理鼠标当前按键状态，不会主动产生点击。
因此手动按住按键时注入也可能形成拖动，首次测试必须松开按键。
Python 注入结束或 Ctrl+C 后停止新增指令，已入队的少量报告仍可能完成发送；纯内核透传继续。

停止透传：

```sh
python3 driver_control.py stop
```

它清空尚未发出的队列并在 USB 可用时发送释放按键报告。
USB 已断开时无法保证电脑收到释放报告。断开输入设备、USB 重新配置、队列溢出或发送错误会停止透传，
排除原因并松开按键后需要重新 start，不会自动恢复带有旧状态的数据。
`dropped` 包括故障丢弃，也包括主动 stop 清掉的未发送报告，判读时需结合操作。

停止后若需恢复原 ADB：

```sh
sleep 1
rmmod zzy_mouse_bridge
sh /etc/init.d/S50usb-gadget.sh start
```

新版本尚未配置开机自动加载。参考：[Linux USB Gadget API](https://docs.kernel.org/driver-api/usb/gadget.html)。

目标：2.4G 鼠标接收器插入 RK3588 的 USB Host，由板子读取鼠标动作，再通过 USB OTG 模拟鼠标发送给电脑。

## 以下为原 Python 转发与第二阶段采集驱动的回退说明

上面新版本已经实现内核接收和发送。以下旧版本不参与本次内核转发测试，保留供对照和回退。

### 旧版本完成到哪里

- **Python 转发已经上板验证**：读取 Linux 输入事件，转成 USB HID 报告，电脑能被鼠标控制。
- **自编 C 驱动完成输入采集**：通过 input_handler 接收鼠标事件，通过 `/dev/zzy_mouse` 提供 read、poll 和 ioctl。ABI 版本为 2。
- 自编驱动与原始输入节点的一次采集对比，11018 条事件的类型、代码、值及顺序一致；该次样本是移动事件，不代表所有按键、滚轮与热插拔均已验收。用户后续读取统计为接收 48342、丢失 0。
- **当前 Python 转发仍读取原始 evdev 节点，没有改成读取自编驱动。** 自编驱动目前不会独立向电脑输出 USB 报告。

这份目录保留当前最新的运行代码与必要依赖，不是整个项目所有功能均已完成的声明。没有收录观察脚本、延迟测试、测试结果、日志和第一阶段历史代码。

## 文件

```text
shubiao/
├── README.md
├── python/
│   ├── gadget_setup.py       配置/停止 USB HID 鼠标
│   └── mouse_bridge.py       当前可用的鼠标转发程序
└── c驱动/
    ├── zzy_mouse.c          自编鼠标输入采集驱动
    ├── zzy_mouse_uapi.h     驱动与用户态之间的 ABI 定义
    ├── Makefile             外部内核模块构建入口
    ├── zzy_mouse.ko         已编译的采集模块
    └── usb_hid/
        ├── f_hid.c          SDK 原有的 HID Gadget 功能驱动
        ├── u_hid.h
        ├── Makefile
        └── usb_f_hid.ko     已编译的 USB HID 输出模块
```

`usb_hid` 是 USB 鼠标输出所需的内核依赖，不是自编采集驱动，也不是测试代码。其源码来自 SDK，保留源码中的许可证声明。两个 `.ko` 仅用于本次匹配的内核环境，不能当成任意版本 Linux 都能使用的通用驱动。

归档版 `mouse_bridge.py` 已内置输入路径、事件结构和 gadget 路径，不再依赖旧 `main.py`、`mouse_output.py`。转发逻辑没有改变。

## 硬件与软件环境

- RK3588，官方 Buildroot 镜像，板上内核 `6.12.69`，64 位 ARM。
- Python 3，使用标准库，无需 evdev Python 包。
- 接收器：`XC TECH Wireless-Receiver`，USB VID:PID 为 `248d:5b5f`。
- Python 输入路径：`/dev/input/by-id/usb-XC_TECH_Wireless-Receiver-event-mouse`。
- USB Device 控制器：`fc000000.usb`；USB Host 接鼠标，OTG 数据口接电脑。
- HID 模块安装路径：`/lib/modules/6.12.69/extra/usb_f_hid.ko`。
- 适配官方 ADB 服务 `/etc/init.d/S50usb-gadget.sh`。

代码针对以上环境编写。更换接收器、内核或系统时，需要重新核对设备匹配、事件能力、模块兼容性、控制器名称和服务路径。

## 如何运行目前的 Python 控制程序

把 `python` 中两个文件上传到板子 `/root/linux-shubiao/`。电脑本地文件与板子文件是独立副本；本次整理没有自动更新板子代码。

若是首次部署，将 `usb_hid/usb_f_hid.ko` 上传至板子上述模块安装路径。需要 root 权限，板子内核必须匹配。

在板子上执行：

```sh
cd /root/linux-shubiao
python3 gadget_setup.py start
python3 -u mouse_bridge.py
```

启动前松开鼠标所有按键。`start` 会临时停止官方 ADB，创建 USB 鼠标功能；电脑完成枚举后再启动转发。如果本项目 gadget 已存在，不要重复 start；已配置并连接电脑时直接运行转发程序。

运行中移动、点击和滚轮操作会传给电脑。程序不独占原始输入设备，因此不要同时运行另一份鼠标转发程序。

结束时先按 Ctrl+C 停止转发，再执行：

```sh
python3 gadget_setup.py stop
```

停止转发时程序尝试释放按键；USB 已断开时不能保证释放报告送达。`stop` 移除本项目 gadget 并请求恢复官方 ADB。本项目尚未配置开机自动启动。

当前支持五键、16 位相对 X/Y、8 位垂直和水平滚轮。普通滚轮和高精度滚轮是同一动作的两种表示，目前只使用普通滚轮，避免重复累加。绝对坐标、CNN 控制和自定义运动变换尚未实现。

## 自编驱动的用法和边界

将 `zzy_mouse.ko` 上传到板子，例如 `/root/linux-shubiao/zzy_mouse.ko`。尚未加载时执行：

```sh
insmod /root/linux-shubiao/zzy_mouse.ko
ls -l /dev/zzy_mouse
```

当前 Python 转发不依赖此模块。加载模块只是让自编驱动接入输入子系统，并提供事件读取接口。没有读取者时不会积压事件。

- 事件记录固定 24 字节：单调时钟纳秒、序号、类型、代码、值、保留字段。
- `read()` 读取事件，`poll()` 等待事件，两个 ioctl 查询能力及统计，具体定义见 `zzy_mouse_uapi.h`。
- 只允许一个读取者，第二个打开者得到 EBUSY。
- 打开后会先排入一条连接状态事件，所以只查询 ioctl 时可能看到队列剩余 1。
- 队列溢出通过 SYN_DROPPED 告知用户态，不能当成正常移动继续转发。
- 此驱动没有从 Linux 内核中独占鼠标，原始 evdev 仍然可用。

## 重新编译模块

编译使用保留的官方 SDK 内核树，而非前一个系统定制项目的 Linux 7.3.1。

- SDK：`/mnt/rk3588/atk_dlrk3588_linux6.12`。
- 内核源码提交：`cd386f4bb468bc8807172e71e125dbb624f56f68`。
- 工具链：Arm GNU Toolchain 14.3.Rel1，GCC 14.3.1。
- 需要 SDK 中匹配板子的 `.config`、生成头文件及 `Module.symvers`。

先挂载 SDK 虚拟盘。将 `c驱动` 顶层的 `.c`、`.h` 和 Makefile 复制到 WSL ext4 工作目录 `/mnt/rk3588/mouse-driver-build`，然后执行：

```sh
make -C /mnt/rk3588/atk_dlrk3588_linux6.12/kernel \
  M=/mnt/rk3588/mouse-driver-build ARCH=arm64 \
  CROSS_COMPILE=/mnt/rk3588/atk_dlrk3588_linux6.12/prebuilts/gcc/linux-x86/aarch64/arm-gnu-toolchain-14.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu- \
  -j4 modules
```

USB HID 模块同理：把 `usb_hid` 中的源码和 Makefile 放到独立的 `/mnt/rk3588/mouse-hid-module`，将上述 `M=` 改为该目录。不要把两个 Makefile 混在一起。不要直接在包含空格的 Windows 归档路径下进行 Kbuild 编译。

## 旧版本当时的后续计划（已被上文内核转发方案替代）

1. 增加 Python 驱动转发入口，用 `/dev/zzy_mouse` 替代原始 evdev，复用现有 HID 报告编码与输出逻辑。
2. 处理 ABI 校验、连接状态、队列溢出、停止时释放按键以及 USB 输出超时。
3. 上板验证移动、五键、滚轮、断开与重新启动，确认“自编输入驱动 → Python → USB HID → 电脑”完整链路。
4. 再讨论把哪些处理移进内核，以及绝对坐标、AI 指令接口和开机启动。用相同方法测量延迟后再判断收益，不能假定改用驱动一定更快。

原 PyCharm 工程和板上程序在此次归档中均未删除、未替换。本目录是本次指定的精简代码归档。

后续新增的内核转发模块和控制程序已上传到板子 `/root/linux-shubiao/`。
它们以独立文件部署，没有覆盖旧的 `mouse_bridge.py`；运行时已停止官方 ADB gadget，
由新模块占用 OTG 控制器。电脑上的权威源码是本目录 `c驱动/bridge` 与 `python/driver_control.py`，
原 PyCharm 路径不会自动同步这些新增文件。
