# RK3588 全流程自定制：U-Boot + Linux 内核 + rootfs

> **目标**：独立构建最小板级系统 —— 烧上板子后能**串口配 WiFi**、或**提前把 WiFi 写进镜像**，然后通过 **SSH** 登录并操作文件系统
> **原则**：**全流程自己定制**，不使用正点原子现成的资料 ✗ —— **只用芯片厂提供的初始化代码**（rkbin 的 DDR/BL31 二进制），其余全部从各官网自己完成
> **范围**：U-Boot 板级启动 + Linux 内核搭建 + rootfs 配置

---

## 📋 目录

- [0. 路线总览](#0-路线总览)
- [一、U-Boot 移植](#一uboot-移植)
- [二、Linux 内核](#二linux-内核)
- [三、rootfs（Buildroot）](#三rootfsbuildroot)
- [四、boot 镜像与 extlinux](#四boot-镜像与-extlinux)
- [五、烧录（三次波折）](#五烧录三次波折)
- [六、WiFi 驱动（RTL8733BU）](#六wifi-驱动rtl8733bu)
- [七、集成收尾（自动启动 + WiFi + SSH）](#七集成收尾自动启动--wifi--ssh)
- [八、最终成果](#八最终成果)
- [九、问题总结（三大教训）](#九问题总结三大教训)
- [附录：术语与缩写](#附录术语与缩写)

---

## 0. 路线总览

> rootfs 的编译前面的 **Debian** 已经是完全自定义了 ✓
> 这次用 **Buildroot** ✓，但流程大体相同 ✓ → **重点放在 U-Boot 和内核** ✓✓

```text
① U-Boot 2026.10   → 板级设备树 + defconfig → rk3588_loader + u-boot-rockchip.bin
② Linux 7.3.1      → 精简 config + 板级设备树 → Image + dtb
③ Buildroot 2026.08.1 → musl + BusyBox → rootfs.cpio.gz（后续改 256MB ext4）
④ boot 镜像（ext4 64MB）+ extlinux.conf
⑤ 烧录 → 上板 → 串口验证 → 手动 booti 验证 → 补自动启动
⑥ WiFi 驱动（RTL8733BU）+ iw/wpa_supplicant/udhcpc/Dropbear
⑦ 集成 → 完整 update.img → 上板验证通过
```

**版本一览**：

| 组件 | 版本 |
|---|---|
| U-Boot | **2026.10** |
| Linux 内核 | **7.3.1** |
| Buildroot | **2026.08.1** |
| rkbin | commit `3e288fe` |
| 芯片 | RK3588（8 GB LPDDR4X + 64 GB eMMC）|

---

# 一、U-Boot 移植

## 1.1 挂载工作盘

```bash
sudo mkdir -p /mnt/rk3588-task-e
sudo mount -t ext4 -o loop \
  /mnt/g/linux-study/linux/rk3588-task-e/rk3588-task-e.ext4 \
  /mnt/rk3588-task-e

# 确认挂载
findmnt --mountpoint /mnt/rk3588-task-e -o TARGET,SOURCE,FSTYPE,UUID
```

> **作用**：让 Linux 把 G 盘这个**镜像文件当作磁盘**使用 ✓，把里面的 ext4 文件系统接到 `/mnt/rk3588-task-e` ✓

**盘内已有的源码**：AI 帮忙下载的 **U-Boot 2026.10** ✓（里面**直接带有 RK3588 驱动支持** ✓ —— 但**不等于直接支持这块板子** ✗✓）

| 资源 | 地址 |
|---|---|
| U-Boot 源码 | https://github.com/u-boot/u-boot |
| U-Boot 编译文档 | https://docs.u-boot.org/en/stable/build/source.html |

## 1.2 生成参考配置（EVB 板）

```bash
cd /mnt/rk3588-task-e/src/u-boot
mkdir -p /mnt/rk3588-task-e/tmp
export TMPDIR=/mnt/rk3588-task-e/tmp

make O=/mnt/rk3588-task-e/out/uboot-reference \
  CROSS_COMPILE=aarch64-linux-gnu- \
  evb-rk3588_defconfig
```

> 这一步会**编译少量运行在电脑上的配置工具** ✓，再生成 `out/uboot-reference/.config` ✓
> **`.config` 就是后续构建 U-Boot 的配置清单** ✓ —— 记录目标芯片、设备树、串口、启用的功能 ✓

**检查关键结果**：

```bash
grep -E '^(CONFIG_ROCKCHIP_RK3588|CONFIG_DEFAULT_DEVICE_TREE|CONFIG_BAUDRATE)=' \
  /mnt/rk3588-task-e/out/uboot-reference/.config
```

```text
CONFIG_DEFAULT_DEVICE_TREE="rockchip/rk3588-evb1-v10"   ← 当前用瑞芯微 EVB 参考板设备树
CONFIG_ROCKCHIP_RK3588=y                                ← 已启用 RK3588 芯片支持
CONFIG_BAUDRATE=1500000                                 ← 串口默认波特率（串口工具要匹配）
```

## 1.3 读设备树，搞清板级差异

> **设备树的作用**：告诉 U-Boot 这块板上的**串口、存储、电源等硬件是怎样连接和配置的** ✓
> 现在还是**参考板**的 ✗ → **尚未适配正点原子板卡** ✗✓

```bash
# 看参考板设备树开头（板名、引用的公共描述、默认串口、供电配置）
sed -n '1,180p' \
  dts/upstream/src/arm64/rockchip/rk3588-evb1-v10.dts

# 找 U-Boot 专用的设备树补充文件
find arch/arm/dts -maxdepth 1 \
  -name '*rk3588*' -name '*u-boot*'
```

**设备树源码开头**：

```dts
#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/input/input.h>
#include <dt-bindings/pinctrl/rockchip.h>
#include <dt-bindings/soc/rockchip,vop2.h>
#include <dt-bindings/usb/pd.h>
#include "rk3588.dtsi"
```

| 文件/节点 | 含义 |
|---|---|
| `rk3588.dtsi` | **芯片级描述** ✓ —— RK3588 有哪些控制器、寄存器、中断、时钟（**可复用**）|
| `model` / `compatible` | **板名** ✓ —— `"Rockchip RK3588 EVB1 V10 Board"` / `"rockchip,rk3588-evb1-v10"`、`"rockchip,rk3588"` |

> **说明**：EVB 板**使用了哪些控制器**、外部器件/供电/引脚**如何连接** ✗ —— 这些**不一定和正点的板子一样** ✗✓ → **需要修改** ✓

**关键节点**：

```dts
aliases {
    ethernet0 = &gmac0;
    mmc0 = &sdhci;      /* 给 sdhci 控制器指定 MMC 设备别名 */
};

chosen {
    stdout-path = "serial2:1500000n8";   /* 默认控制台：UART2，1500000-8-N-1 */
};
```

> `mmc0 = &sdhci` 只是给存储设备一个**编号** ✓，**不等于后续实际功能** ✓
> **串口配置是我们首先关心的** ✓ —— 重点注意 **EVB 与我们的板子串口编号是否相同** ✓

## 1.4 追查启动早期的设备需求

```bash
cat arch/arm/dts/rk3588-evb1-v10-u-boot.dtsi
sed -n '1,220p' arch/arm/dts/rk3588-u-boot.dtsi
sed -n '1,240p' arch/arm/dts/rk3588s-u-boot.dtsi
```

> 这两个文件都很短 ✓，它们通过**逐层引用**把公共配置复用起来 ✓ —— 所以**配置不一定直接写在当前文件里** ✓

**关键发现**：

```dts
u-boot,spl-boot-order = "same-as-spl", &sdhci;
```

> **含义**：优先尝试**从加载 SPL 自身的启动设备**继续加载后续阶段 ✓；如果失败，再尝试 `sdhci` 对应的存储设备 ✓
> ⚠️ 它控制的是 **SPL 从哪里加载后续启动阶段** ✓，**不是 Linux 的根文件系统在哪里** ✗✓

**`bootph` 标记**：

```dts
&uart2 {
    bootph-all;
    clock-frequency = <24000000>;
};
```

> `bootph` 用来**说明一个设备在哪些启动阶段需要** ✓
> 因为启动早期的程序要尽量小 ✓ → **不会把所有设备都保留** ✓
> `bootph-all` = **UART2 在各启动阶段都需要** ✓，并指定 **24 MHz** 时钟 ✓

**EVB 的 UART2 / eMMC 配置**：

```bash
sed -n '/^&uart2 {/,/^};/p' \
  dts/upstream/src/arm64/rockchip/rk3588-evb1-v10.dts

sed -n '/^&sdhci {/,/^};/p' \
  dts/upstream/src/arm64/rockchip/rk3588-evb1-v10.dts
```

```dts
&uart2 {
    pinctrl-0 = <&uart2m0_xfer>;   /* 选择 UART2 的 M0 收发引脚组 */
    status = "okay";                /* 启用 UART2 */
};

&sdhci {
    bus-width = <8>;                     /* 8 位数据总线，板上需相应接线 */
    no-sdio;                             /* 不按 SDIO 设备探测 */
    no-sd;                               /* 不按 SD 存储卡探测 */
    non-removable;                       /* 固定连接、不可拔插 */
    mmc-hs400-1_8v;                      /* 支持 1.8V 信号的 HS400 */
    mmc-hs400-enhanced-strobe;           /* 支持 HS400 增强选通 */
    status = "okay";                     /* 启用该控制器 */
};
```

> **结论**：参考板启用了 **UART2 的 M0 引脚组** ✓，并把 **sdhci 配置为连接板载 eMMC** ✓

## 1.5 板级差异核对（对照正点原子原理图）

> ⚠️ **坦白说**：这部分我实在不太懂 ✓ → 交给 AI 主力阅读 ✓，我只做解释 ✓

| 项目 | 参考板（EVB）| 正点原子板 | 处理 |
|---|---|---|---|
| **串口** | UART2 M0 | **可以沿用 UART2 M0** ✓，引脚配置相同 | ✅ 直接复用 |
| **内存** | — | RK3588 + **LPDDR4X** ✓，**容量不一定** ✗ | ⚠️ 不能照搬，**不写死** |
| **eMMC** | 8 位 HS400 | 焊在核心板上，**没找到资料** ✗ | ⚠️ 先保守配置，**上板后自己读** |
| **供电** | EVB 方案 | 不能整套复制 ✗ | ⚠️ 自己适配 |
| **Wi-Fi** | — | 后续应该走 **USB 驱动**路线 ✓ | ⚠️ 后面再说 |

## 1.6 装编译依赖

```bash
sudo apt-get update &&
sudo apt-get install libgnutls28-dev python3-pyelftools
```

## 1.7 创建自己的 defconfig

```bash
cd /mnt/rk3588-task-e/src/u-boot &&
cp --update=none configs/evb-rk3588_defconfig configs/zzy-rk3588_defconfig

# 给文件写标识（编译时给个名字）
sed -i '/^CONFIG_LOCALVERSION=/d' configs/zzy-rk3588_defconfig
printf '%s\n' 'CONFIG_LOCALVERSION="-zzy-rk3588"' \
  >> configs/zzy-rk3588_defconfig
```

**生成独立构建目录**：

```bash
mkdir -p /mnt/rk3588-task-e/tmp
export TMPDIR=/mnt/rk3588-task-e/tmp

make O=/mnt/rk3588-task-e/out/uboot-zzy \
  CROSS_COMPILE=aarch64-linux-gnu- \
  zzy-rk3588_defconfig
```

> **现在的做法**：把参考配置**复制下来** ✓，创建一个新的编译工程 ✓，后面再改其中不一样的地方 ✓

## 1.8 下载 rkbin（DDR/BL31 二进制）

> **为什么必须用官方二进制？** RK 官方**不提供 DDR 初始化的源码** ✗，只给二进制 ✓ → 只能借助他们的 ✓

```bash
cd /mnt/rk3588-task-e/src &&
git init rkbin &&
cd rkbin &&
git remote add origin https://github.com/rockchip-linux/rkbin.git &&
git fetch --depth 1 origin 3e288fe814e059dd06833495f845cab04ac20a5c &&
git checkout --detach FETCH_HEAD
```

**确认挂载正确后解压**：

```bash
findmnt --mountpoint /mnt/rk3588-task-e -o TARGET,UUID
# 期望看到：f0c827b8-89bf-4b52-b620-d6b51c803569

mkdir -p /mnt/rk3588-task-e/src/rkbin-3e288fe &&
tar -xzf \
  /mnt/g/linux-study/linux/rk3588-task-e/downloads/rkbin-3e288fe.tar.gz \
  -C /mnt/rk3588-task-e/src/rkbin-3e288fe \
  --strip-components=1
```

**校验关键文件**：

```bash
cd /mnt/rk3588-task-e/src/rkbin-3e288fe &&
sha256sum \
  bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin \
  bin/rk35/rk3588_bl31_v1.56.elf \
  | tee /mnt/rk3588-task-e/logs/rkbin-input-sha256.txt
```

```text
2853a0da7ab895af43d50615af73eccf0694115dd0202483ee1c93c9071a42c3  bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin
2864c2fc36333e03e7e3fe6459225d320d310850b4ac7620d1f3601efb7d4a9b  bin/rk35/rk3588_bl31_v1.56.elf
```

**导出 DDR 参数，看是否匹配我们的需求**：

```bash
cd /mnt/rk3588-task-e/src/rkbin-3e288fe &&
python3 tools/ddrbin_tool.py rk3588 \
  -g /mnt/rk3588-task-e/logs/ddr-original-params.txt \
  bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin

grep -niE 'freq|uart|baud|channel|rank|capacity|size|stride' \
  /mnt/rk3588-task-e/logs/ddr-original-params.txt
```

## 1.9 派生编译，先跑通全流程

```bash
cd /mnt/rk3588-task-e/src/u-boot
export TMPDIR=/mnt/rk3588-task-e/tmp
export ROCKCHIP_TPL=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin
export BL31=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_bl31_v1.56.elf

set -o pipefail
make O=/mnt/rk3588-task-e/out/uboot-zzy \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/uboot-zzy-build-01.log
```

> `ROCKCHIP_TPL` / `BL31` 告诉编译器**哪里获取 DDR 初始化固件和 BL31** ✓

**检查产物**：

```bash
ls -lh \
  /mnt/rk3588-task-e/out/uboot-zzy/u-boot \
  /mnt/rk3588-task-e/out/uboot-zzy/spl/u-boot-spl.bin \
  /mnt/rk3588-task-e/out/uboot-zzy/u-boot-rockchip.bin
```

```text
-rw-r--r-- 1 zzy zzy 121K Nov  1 12:39 .../spl/u-boot-spl.bin
-rwxr-xr-x 1 zzy zzy 7.6M Nov  1 12:38 .../u-boot
-rw-r--r-- 1 zzy zzy 9.2M Nov  1 12:39 .../u-boot-rockchip.bin
```

## 1.10 写自己的板级设备树

**初始版本策略**：

| 项目 | 策略 |
|---|---|
| UART2 M0 | ✅ 保留（已核实）|
| eMMC | ✅ 保留，但**首次只用 1 位数据总线、最高 25 MHz**（降低验证复杂度）|
| SD 卡 | ❌ 关闭 |
| USB 下载 | ✅ 保留基础支持 |
| 内存 / eMMC 容量 | ❌ **不写死** |

**主设备树**：

```bash
cat > arch/arm/dts/rk3588-zzy-minimal.dts <<'EOF'
// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
#include "rk3588-generic.dts"

/ {
	model = "ZZY ATK-DLRK3588 Minimal Bring-up";
};

&sdhci {
	bus-width = <1>;
	max-frequency = <25000000>;
	/delete-property/ cap-mmc-highspeed;
	/delete-property/ mmc-hs200-1_8v;
};

&sdmmc {
	status = "disabled";
};
EOF
```

> **思路**：**引用上游最小设备树** ✓，再**覆盖需要调整的部分** ✓ → 串口和 USB 的公共配置因此可以直接复用 ✓

**U-Boot 专用补充文件**：

```bash
cat > arch/arm/dts/rk3588-zzy-minimal-u-boot.dtsi <<'EOF'
// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
#include "rk3588-generic-u-boot.dtsi"

/ {
	chosen {
		u-boot,spl-boot-order = "same-as-spl", &sdhci;
	};
};
EOF
```

> 复用**早期启动节点标记** ✓，并让 SPL 优先**沿用自身启动设备**，再尝试 eMMC ✓

## 1.11 自己的 defconfig（从 generic 派生）

```bash
cp --update=none \
  configs/generic-rk3588_defconfig \
  configs/zzy-rk3588-minimal_defconfig

sed -i \
  -e 's/^CONFIG_DEFAULT_DEVICE_TREE=.*/CONFIG_DEFAULT_DEVICE_TREE="rk3588-zzy-minimal"/' \
  -e 's/^CONFIG_DEFAULT_FDT_FILE=.*/CONFIG_DEFAULT_FDT_FILE="rockchip\/rk3588-zzy-minimal.dtb"/' \
  -e '/^CONFIG_LOCALVERSION=/d' \
  configs/zzy-rk3588-minimal_defconfig

printf '%s\n' 'CONFIG_LOCALVERSION="-zzy-minimal"' \
  >> configs/zzy-rk3588-minimal_defconfig
```

> ⚠️ **两个配置的区别**（容易搞混 ✓）：
> - **`CONFIG_DEFAULT_DEVICE_TREE`** = **U-Boot 自己使用的设备树** ✓
> - **`CONFIG_DEFAULT_FDT_FILE`** = 后续**加载 Linux 设备树时用的默认文件名** ✓ —— **不会自动生成 Linux 的设备树** ✗✓

**生成配置并编译**：

```bash
export TMPDIR=/mnt/rk3588-task-e/tmp
export ROCKCHIP_TPL=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin
export BL31=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_bl31_v1.56.elf

make O=/mnt/rk3588-task-e/out/uboot-minimal \
  CROSS_COMPILE=aarch64-linux-gnu- \
  zzy-rk3588-minimal_defconfig

set -o pipefail
make O=/mnt/rk3588-task-e/out/uboot-minimal \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/uboot-minimal-build-01.log
```

**这一版的规格**：

| 项目 | 值 |
|---|---|
| 版本 | `2026.10-zzy-minimal` |
| 板名 | `ZZY ATK-DLRK3588 Minimal Bring-up` |
| 串口 | UART2、1500000 |
| eMMC | **1 位、最高 25 MHz**、已启用 |
| SD 卡 | 已禁用 |
| SPL 加载顺序 | 沿用启动设备 → 再尝试 eMMC |

## 1.12 查真实板子的硬件信息

> ⚠️ **手上没有板子的内部资料** ✗ → 只能用**现有的 Linux 系统**查 ✓
> （正规公司里这是"直接查内部硬件资料" ✓ → 所以算正常定制 ✓）

```bash
uname -a
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINTS
cat /proc/partitions
```

```text
alientek@ATK-DLRK3588:~$ uname -a
Linux ATK-DLRK3588 6.12.69 #3 SMP Sun Oct 25 12:02:53 CST 2026 aarch64 GNU/Linux

alientek@ATK-DLRK3588:~$ lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINTS
NAME          SIZE TYPE FSTYPE MOUNTPOINTS
mmcblk0      57.6G disk
|-mmcblk0p1     4M part
|-mmcblk0p2     4M part
|-mmcblk0p3    64M part
|-mmcblk0p4   128M part
|-mmcblk0p5    32M part
|-mmcblk0p6   128M part ext4   /oem
|-mmcblk0p7     4G part ext4   /userdata
`-mmcblk0p8  53.3G part ext4   /
mmcblk0boot0    4M disk
mmcblk0boot1    4M disk
zram0           0B disk
```

> **关键信息**：板子从 **eMMC** 运行 ✓，Linux 识别为 **`/dev/mmcblk0`** ✓，根文件系统在**第 8 个分区** ✓

**再查分区名称、起始位置、前部预留空间**：

```bash
lsblk -o NAME,SIZE,START,PARTLABEL,PARTUUID
sudo fdisk -l /dev/mmcblk0
cat /sys/class/block/mmcblk0/device/type
cat /sys/class/block/mmcblk0/device/name
grep MemTotal /proc/meminfo
```

```text
NAME          SIZE   START PARTLABEL PARTUUID
mmcblk0      57.6G
|-mmcblk0p1     4M   16384 uboot     c8460000-0000-4b51-8000-239a000018d3
|-mmcblk0p2     4M   24576 misc      78590000-0000-4530-8000-26ac00001272
|-mmcblk0p3    64M   32768 boot      7a3f0000-0000-446a-8000-702f00006273
|-mmcblk0p4   128M  163840 recovery  dd200000-0000-4364-8000-70f500005eaa
|-mmcblk0p5    32M  425984 backup    e2680000-0000-480b-8000-587b00002d93
|-mmcblk0p6   128M  491520 oem       a90e0000-0000-4c14-8000-0ccc000036e4
|-mmcblk0p7     4G  753664 userdata  d96f0000-0000-4659-8000-709100006ac6
`-mmcblk0p8  53.3G 9142272 rootfs    614e0000-0000-4b53-8000-1d28000054a9

Disk /dev/mmcblk0: 57.62 GiB, 61865984000 bytes, 120832000 sectors
Units: sectors of 1 * 512 = 512 bytes
Disklabel type: gpt
Disk identifier: 39650000-0000-412E-8000-2EB200005FFC

Device           Start       End   Sectors  Size Type
/dev/mmcblk0p1   16384     24575      8192    4M unknown
/dev/mmcblk0p2   24576     32767      8192    4M unknown
/dev/mmcblk0p3   32768    163839    131072   64M unknown
/dev/mmcblk0p4  163840    425983    262144  128M unknown
/dev/mmcblk0p5  425984    491519     65536   32M unknown
/dev/mmcblk0p6  491520    753663    262144  128M unknown
/dev/mmcblk0p7  753664   9142271   8388608    4G unknown
/dev/mmcblk0p8 9142272 120831935 111689664 53.3G unknown

MMC
A3A561
MemTotal:        8106268 kB
```

**结论**：

| 项目 | 数据 |
|---|---|
| **eMMC** | `/dev/mmcblk0`，**61,865,984,000 字节** ✓（符合标称 64 GB 等级）|
| **内存** | Linux 可用约 **7.73 GiB** ✓（8 GB 板子预留部分内存后的情况 ✓）|
| **分区表** | **GPT** ✓，每扇区 **512 字节** ✓ |
| **eMMC 型号** | `A3A561` ✓ |

**磁盘位置**：

```text
前 8 MiB      : 主 GPT + 分区前预留空间（早期加载程序位置需结合启动格式判断）
8 ~ 12 MiB    : uboot 分区
12 ~ 16 MiB   : misc 分区
16 MiB 开始   : boot 分区，后面还有 recovery、rootfs 等
```

## 1.13 自启动策略

> **目标**：等待 **3 秒** ✓，然后**只从 eMMC** 查找 `extlinux.conf` ✓，按文件中的配置加载 Linux ✓

```bash
cd /mnt/rk3588-task-e/src/u-boot

# 清除可能已有的同名设置
sed -i \
  -e '/^CONFIG_BOOTDELAY=/d' \
  -e '/^CONFIG_BOOTCOMMAND=/d' \
  configs/zzy-rk3588-minimal_defconfig

# 写入启动策略
cat >> configs/zzy-rk3588-minimal_defconfig <<'EOF'
CONFIG_BOOTDELAY=3
CONFIG_BOOTCOMMAND="setenv boot_targets mmc0; setenv bootmeths extlinux; bootflow scan -lbG"
EOF
```

| 设置 | 作用 |
|---|---|
| `BOOTDELAY=3` | 等待 3 秒，留出**通过串口中断自动启动**的机会 ✓ |
| `boot_targets mmc0` | 把启动搜索设备限定为 **mmc0**（对应设备树中的 eMMC）✓ |
| `bootmeths extlinux` | 只用 **extlinux** 启动方式，跳过全局启动方法 ✓ |
| `bootflow scan -lbG` | 找到有效配置后尝试启动 ✓ |

**重新生成并编译**：

```bash
make O=/mnt/rk3588-task-e/out/uboot-minimal \
  CROSS_COMPILE=aarch64-linux-gnu- \
  zzy-rk3588-minimal_defconfig

grep -E '^CONFIG_(BOOTDELAY|BOOTCOMMAND|BOOTMETH_EXTLINUX|FS_EXT4)=' \
  /mnt/rk3588-task-e/out/uboot-minimal/.config
```

```text
CONFIG_BOOTMETH_EXTLINUX=y
CONFIG_BOOTDELAY=3
CONFIG_BOOTCOMMAND="setenv boot_targets mmc0; setenv bootmeths extlinux; bootflow scan -lbG"
CONFIG_FS_EXT4=y
```

```bash
export TMPDIR=/mnt/rk3588-task-e/tmp
export ROCKCHIP_TPL=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin
export BL31=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_bl31_v1.56.elf

set -o pipefail
make O=/mnt/rk3588-task-e/out/uboot-minimal \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/uboot-minimal-build-02.log
```

> 💡 **定制 U-Boot 的本质其实很简单** ✓：
> ① 下载 U-Boot 源码 → ② 先用它给的目标板适配（EVB）→ ③ **查清我们板子哪里不一样** → ④ **一样的复用、不一样的替换** → ⑤ **重写一份迷你设备树** → ⑥ 编译 ✓

## 1.14 烧录验证

**准备 loader**：

```bash
cd /mnt/rk3588-task-e/src/rkbin-3e288fe &&
./tools/boot_merger RKBOOT/RK3588MINIALL.ini

mkdir -p /mnt/g/linux-study/linux/rk3588-task-e/artifacts/uboot-test
cp rk3588_loader_v1.24.114.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/uboot-test/ &&
cp /mnt/rk3588-task-e/out/uboot-minimal/u-boot-rockchip.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/uboot-test/ &&
cmp /mnt/rk3588-task-e/out/uboot-minimal/u-boot-rockchip.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/uboot-test/u-boot-rockchip.bin &&
echo "下载文件准备完成"
```

→ 文件在 `G:\linux-study\linux\rk3588-task-e\artifacts\uboot-test`

**用 RKDevTool 烧录**：

| 设置 | 值 |
|---|---|
| 左侧勾选框 | **只勾第 3 行**（其他全不勾）|
| 存储 | 能选就选 **EMMC** |
| 地址 | **0x00000040**（双击修改）|
| 名字 | 可保留 `Uboot` |
| 路径 | 点该行最右单元格 → 选 `u-boot-rockchip.bin` |
| 页面下方 | ✅ **勾选"强制按地址写"** |

> ⚠️ **第一下我忘了勾"强制按地址写"** ✗ → 记住这个坑 ✓

**上板验证**：串口会看到 **3、2、1 倒数** ✓ → **至少 U-Boot 编译成功了** ✓

**命令行检测**：

```text
version
bdinfo
mmc list
mmc dev 0
mmc info
part list mmc 0
```

**结果（交给 AI 读取后结论）**：

| 检查项 | 结果 |
|---|---|
| 自编译版本 | **`2026.10-zzy-minimal`** ✓ |
| 内存识别 | **8 GiB** ✓（尚不代表完成稳定性测试）|
| eMMC 型号与容量 | `A3A561`、**57.6 GiB** ✓（与原系统一致）|
| 分区表读取 | 八个分区的位置、名称、UUID 与刷写前**一致** ✓ |
| 实际存储时钟 | **25 MHz**（保守设置）|

**再测驱动与内存读写**：

```text
ext4ls mmc 0:8 /
ext4load mmc 0:8 ${kernel_addr_r} /etc/os-release
md.b ${kernel_addr_r} 100
```

| 命令 | 结果 |
|---|---|
| `ext4ls` | ✅ 能识别 ext4 并列出目录 |
| `ext4load` | ✅ 成功把 **478 字节**读入内存 |
| `md.b` | ✅ 在 **0x02000000** 看到正确文件内容 |

→ **自己的 U-Boot → eMMC → GPT 分区 → ext4 文件 → 内存** ✓✓ 全链路通 ✓

> ⚠️ **发现的问题**：为了第一版能用 ✓，eMMC 用了**单线 + 低频**（很保守 ✓）→ 但后来发现 **我们的 eMMC 是 4 线的** ✗ → 后续需要修改 ✓

> **目前确认的是这块 8+64 板子的基础启动和读取能力** ✓
> 还没做：全内存稳定性、其他容量型号、Linux 启动验证 ✓（后面再研究 ✓）

---

# 二、Linux 内核

> **策略**：直接用**最新内核** ✓，分两部分：
> **第一部分**：先做一个**最简单的镜像**在板子上跑起来 ✓，用串口命令行查询 ✓
> **第二部分**：在跑起来的内核上**查外设问题**（eMMC、WiFi）✓

## 2.1 下载内核 7.3.1

```bash
cd G:\linux-study\linux\rk3588-task-e\downloads

curl.exe --fail --location --retry 3 --proxy http://127.0.0.1:7890 \
  --output linux-7.3.1.tar.xz \
  https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.3.1.tar.xz

# 下载校验文件
curl.exe --fail --location --retry 3 --proxy http://127.0.0.1:7890 \
  --output "G:\linux-study\linux\rk3588-task-e\downloads\sha256sums.asc" \
  https://cdn.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc
```

| 资源 | 地址 |
|---|---|
| 内核官网 | https://www.kernel.org/ |
| 内核 GitHub | https://github.com/torvalds/linux |

## 2.2 解压

```bash
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
tar -xJf \
  /mnt/g/linux-study/linux/rk3588-task-e/downloads/linux-7.3.1.tar.xz \
  -C /mnt/rk3588-task-e/src
```

**确认版本**：

```bash
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
head -n 5 Makefile
```

```text
# SPDX-License-Identifier: GPL-2.0
VERSION = 7
PATCHLEVEL = 3
SUBLEVEL = 1
EXTRAVERSION =
```

## 2.3 生成基础配置

> 类似 U-Boot 的 defconfig ✓ —— 告诉构建系统**目标是 ARM64** ✓，先生成一份 `.config` ✓
> **采用上游 ARM64 通用配置作为起点** ✓ → 先看有哪些可参考 ✓ → 再加我们板子的特殊性 ✓（**综合着节约工作量** ✓）

```bash
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
mkdir -p /mnt/rk3588-task-e/out/linux-zzy /mnt/rk3588-task-e/tmp &&
export TMPDIR=/mnt/rk3588-task-e/tmp &&
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  defconfig
```

```bash
grep -E '^CONFIG_(ARM64|ARCH_ROCKCHIP|SERIAL_8250_DW|MMC_SDHCI_OF_DWCMSHC|EXT4_FS)=' \
  /mnt/rk3588-task-e/out/linux-zzy/.config
```

```text
CONFIG_ARM64=y
CONFIG_ARCH_ROCKCHIP=y
CONFIG_SERIAL_8250_DW=y              ← 设计算器串口
CONFIG_MMC_SDHCI_OF_DWCMSHC=y        ← eMMC 控制器
CONFIG_EXT4_FS=y
```

## 2.4 加自己的标识

```bash
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
./scripts/config --file /mnt/rk3588-task-e/out/linux-zzy/.config \
  --set-str LOCALVERSION "-zzy-minimal" \
  --disable LOCALVERSION_AUTO \
  --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT \
  --enable DEBUG_INFO_NONE

make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  olddefconfig
```

> **作用**：内核名变成 **`7.3.1-zzy-minimal`** ✓，并**关闭 DWARF 调试信息**（省空间 ✓）

```bash
grep -E '^(CONFIG_LOCALVERSION=|CONFIG_DEBUG_INFO_NONE=|# CONFIG_LOCALVERSION_AUTO)' \
  /mnt/rk3588-task-e/out/linux-zzy/.config
```

```text
CONFIG_LOCALVERSION="-zzy-minimal"
# CONFIG_LOCALVERSION_AUTO is not set
CONFIG_DEBUG_INFO_NONE=y
```

## 2.5 写内核设备树

**这一版要描述的**：

| 设备 | 配置 |
|---|---|
| **串口** | UART2 M0、1500000（与硬件核对一致）|
| **eMMC** | **4 位**、最高 25 MHz |
| **USB** | 启用板载 WiFi 所接的 **USB2 HOST0** 通路 |
| **温度监测** | 芯片温度传感器 + 内部热关断通路 |
| **内存容量** | 由启动链传递，**不写死 8 GB** ✓ |

```bash
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
test ! -e arch/arm64/boot/dts/rockchip/rk3588-zzy-minimal.dts &&
cat > arch/arm64/boot/dts/rockchip/rk3588-zzy-minimal.dts <<'EOF'
// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/dts-v1/;

/* Reuse SoC hardware definitions without CPU OPP tables for now. */
#include "rk3588-extra.dtsi"

/ {
    model = "ZZY ATK-DLRK3588 Minimal Linux";
    compatible = "zzy,atk-dlrk3588-minimal", "rockchip,rk3588";

    aliases {
        mmc0 = &sdhci;
    };

    chosen {
        stdout-path = "serial2:1500000n8";
    };
};

&uart2 {
    pinctrl-0 = <&uart2m0_xfer>;
    /delete-property/ dmas;
    /delete-property/ dma-names;
    status = "okay";
};

&sdhci {
    bus-width = <4>;
    max-frequency = <25000000>;
    no-sd;
    no-sdio;
    non-removable;
    /delete-property/ supports-cqe;
    status = "okay";
};

&u2phy2 {
    status = "okay";
};

&u2phy2_host {
    status = "okay";
};

&usb_host0_ehci {
    status = "okay";
};

&usb_host0_ohci {
    status = "okay";
};

/* Internal thermal shutdown, without a PMIC shutdown GPIO. */
&tsadc {
    /delete-property/ pinctrl-0;
    /delete-property/ pinctrl-1;
    /delete-property/ pinctrl-names;
    rockchip,hw-tshut-mode = <0>;
    status = "okay";
};
EOF
```

> 💡 **eMMC 从 1 位改成了 4 位** ✓（上板验证时发现了真实总线宽度 ✓）

**列入构建列表**：

```bash
grep -qF 'rk3588-zzy-minimal.dtb' arch/arm64/boot/dts/rockchip/Makefile ||
printf '\ndtb-$(CONFIG_ARCH_ROCKCHIP) += rk3588-zzy-minimal.dtb\n' \
  >> arch/arm64/boot/dts/rockchip/Makefile

# 只编译这一份设备树
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  rockchip/rk3588-zzy-minimal.dtb

ls -lh /mnt/rk3588-task-e/out/linux-zzy/arch/arm64/boot/dts/rockchip/rk3588-zzy-minimal.dtb
```

```text
-rw-r--r-- 1 zzy zzy 89K Nov  2 20:29 .../rk3588-zzy-minimal.dtb
```

## 2.6 裁剪内核

> 通用配置**还启用了大量其他厂商平台** ✗，以及**图形、音频**等功能 ✗ → 都要裁掉 ✓

**调整目标**：

| 目标 | 做法 |
|---|---|
| 芯片平台只保留 Rockchip | 减少无关平台代码 ✓ |
| 关闭图形/音频/摄像头/蓝牙/PCIe/SATA/以太网 | 聚焦**串口、eMMC、USB WiFi** ✓ |
| 无线基础框架、温度监测**编入内核** | 初次启动不需要先加载模块 ✓ |
| 暂时关闭 CPU 与设备动态调频 | 配合尚未完整适配的电源描述 ✓ |
| **保留模块加载能力** | 后面用自己编译的 WiFi 驱动 ✓ |

**关闭其他芯片平台**：

```bash
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
./scripts/config --file /mnt/rk3588-task-e/out/linux-zzy/.config \
  --disable ARCH_ACTIONS --disable ARCH_AIROHA \
  --disable ARCH_SUNXI --disable ARCH_ALPINE \
  --disable ARCH_APPLE --disable ARCH_ARTPEC \
  --disable ARCH_ASPEED --disable ARCH_AXIADO \
  --disable ARCH_BCM --disable ARCH_BCM2835 \
  --disable ARCH_BCM_IPROC --disable ARCH_BCMBCA \
  --disable ARCH_BRCMSTB --disable ARCH_BERLIN \
  --disable ARCH_BLAIZE --disable ARCH_BST \
  --disable ARCH_CIX --disable ARCH_EXYNOS \
  --disable ARCH_K3 --disable ARCH_LG1K \
  --disable ARCH_HISI --disable ARCH_KEEMBAY \
  --disable ARCH_MEDIATEK --disable ARCH_MESON \
  --disable ARCH_MICROCHIP --disable ARCH_SPARX5 \
  --disable ARCH_MVEBU --disable ARCH_NXP \
  --disable ARCH_LAYERSCAPE --disable ARCH_MXC \
  --disable ARCH_S32 --disable ARCH_MA35 \
  --disable ARCH_NPCM --disable ARCH_QCOM \
  --disable ARCH_REALTEK --disable ARCH_RENESAS \
  --disable ARCH_SEATTLE --disable ARCH_INTEL_SOCFPGA \
  --disable ARCH_SOPHGO --disable ARCH_STM32 \
  --disable ARCH_SYNQUACER --disable ARCH_TEGRA \
  --disable ARCH_TESLA_FSD --disable ARCH_SPRD \
  --disable ARCH_THUNDER --disable ARCH_THUNDER2 \
  --disable ARCH_UNIPHIER --disable ARCH_VEXPRESS \
  --disable ARCH_VISCONTI --disable ARCH_XGENE \
  --disable ARCH_ZYNQMP \
  --enable ARCH_ROCKCHIP
```

**调整功能与驱动**：

```bash
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
./scripts/config --file /mnt/rk3588-task-e/out/linux-zzy/.config \
  --disable DRM \
  --disable FB \
  --disable SOUND \
  --disable MEDIA_SUPPORT \
  --disable BT \
  --disable PCI \
  --disable ATA \
  --disable ETHERNET \
  --disable USB_NET_DRIVERS \
  --disable CPU_FREQ \
  --disable PM_DEVFREQ \
  --enable CFG80211 \
  --enable RFKILL \
  --enable ROCKCHIP_THERMAL \
  --enable MODULES &&
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  olddefconfig
```

| 配置 | 为什么这样设 |
|---|---|
| `CFG80211` | Linux 的**无线配置框架** ✓ —— 后续 WiFi 驱动需要与它配合 ✓ |
| `RFKILL` | 无线设备**开关控制**框架 ✓ |
| `ROCKCHIP_THERMAL` | 瑞芯微**温度传感器**驱动 ✓ |
| `USB_NET_DRIVERS` | 另一类 **USB 网络适配器**驱动集合 ✓ —— 关它**不会**关 USB 主机控制器或无线驱动框架 ✓ |

**检查结果**：

```bash
grep -E '^(CONFIG_(ARCH_ROCKCHIP|CFG80211|RFKILL|ROCKCHIP_THERMAL|MODULES)=|# CONFIG_(DRM|SOUND|MEDIA_SUPPORT|BT|PCI|ATA|ETHERNET|CPU_FREQ|PM_DEVFREQ) is not set)' \
  /mnt/rk3588-task-e/out/linux-zzy/.config
```

```text
CONFIG_ARCH_ROCKCHIP=y
# CONFIG_CPU_FREQ is not set
CONFIG_MODULES=y
# CONFIG_BT is not set
CONFIG_CFG80211=y
CONFIG_RFKILL=y
# CONFIG_PCI is not set
# CONFIG_ATA is not set
# CONFIG_ETHERNET is not set
CONFIG_ROCKCHIP_THERMAL=y
# CONFIG_MEDIA_SUPPORT is not set
# CONFIG_DRM is not set
# CONFIG_SOUND is not set
```

**补充设置：关掉 UFS**：

```bash
./scripts/config --file /mnt/rk3588-task-e/out/linux-zzy/.config \
  --disable SCSI_UFSHCD \
  --disable PM_DEVFREQ &&
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  olddefconfig
```

> ⚠️ **为什么连 UFS 也要关**：默认的 UFS 存储驱动会**重新开启设备调频框架** ✗ → 而这块板用 eMMC ✓ → 一起关掉 ✓

## 2.7 编译内核

```bash
set -o pipefail
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 Image rockchip/rk3588-zzy-minimal.dtb \
  2>&1 | tee "/mnt/rk3588-task-e/logs/linux-image-$(date +%Y%m%d-%H%M%S).log"

task_build_status=${PIPESTATUS[0]}
printf '内核编译返回码：%s\n' "$task_build_status"
```

> **最小内核编译成功** ✓ —— 但要验证它是否完好 ✓，还需要一个**最小根文件系统** ✓（内核没有 U-Boot 那样的命令行 ✓，得在 rootfs 里测 ✓）

---

# 三、rootfs（Buildroot）

## 3.1 下载与解压

```bash
# Windows 侧下载
curl.exe --fail --location --retry 3 --proxy http://127.0.0.1:7890 \
  --output "G:\linux-study\linux\rk3588-task-e\downloads\buildroot-2026.08.1.tar.xz" \
  https://buildroot.org/downloads/buildroot-2026.08.1.tar.xz

# WSL 侧解压
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
tar -xJf \
  /mnt/g/linux-study/linux/rk3588-task-e/downloads/buildroot-2026.08.1.tar.xz \
  -C /mnt/rk3588-task-e/src &&
cd /mnt/rk3588-task-e/src/buildroot-2026.08.1 &&
grep -n '^export BR2_VERSION' Makefile
```

| 资源 | 地址 |
|---|---|
| 官网 | https://buildroot.org/download.html |
| 源码（GitLab）| https://gitlab.com/buildroot.org/buildroot/ |

## 3.2 配置目标（menuconfig）

**目标规格**：

| 项目 | 选择 |
|---|---|
| **目标架构** | **AArch64 小端** ✓（生成 RK3588 能跑的 64 位程序）|
| **工具链** | **Buildroot toolchain** ✓（由 Buildroot 构建配套交叉工具链）|
| **C 库** | **musl** ✓（适合小型系统 ✓）|

```bash
cd /mnt/rk3588-task-e/src/buildroot-2026.08.1 &&
mkdir -p /mnt/rk3588-task-e/out/buildroot-minimal \
  /mnt/rk3588-task-e/tmp \
  /mnt/rk3588-task-e/cache/buildroot-dl &&
export TMPDIR=/mnt/rk3588-task-e/tmp &&
export BR2_DL_DIR=/mnt/rk3588-task-e/cache/buildroot-dl &&
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin &&
make O=/mnt/rk3588-task-e/out/buildroot-minimal menuconfig
```

**menuconfig 里的具体操作**：

| 菜单 | 设置 |
|---|---|
| `Target options` → `Target Architecture (i386)` | 改成 **AArch64 (little endian)** ✓（大小端只是字节序，我们用小端 ✓）|
| `Toolchain` → `C library` | 改成 **musl** ✓ |
| `System configuration` → `System hostname` | **`zzy-rk3588`** ✓ |
| `System configuration` → `System banner` | **`Welcome to ZZY RK3588 Linux`** ✓ |
| `System configuration` → `Root password` | **`1234`** ✓（串口登录密码）|
| `System configuration` → `Run a getty (login prompt) after boot` → `TTY port` | 改成 **`ttyS2`** ✓ |
| `Filesystem images` → `cpio the root filesystem` | ✅ 选中（出现子选项）|
| └ `cpio type` | **`cpio the whole root filesystem`** ✓ |
| └ `Compression method` | **gzip** ✓ |
| └ `Create U-Boot image of the root filesystem` | ❌ 不勾 |
| `Filesystem images` → `[*] tar the root filesystem` | ❌ **取消勾选** |

**验证配置**：

```bash
grep -E '^BR2_(aarch64|TOOLCHAIN_BUILDROOT|TOOLCHAIN_BUILDROOT_MUSL|INIT_BUSYBOX|PACKAGE_BUSYBOX|TARGET_GENERIC_HOSTNAME|TARGET_GENERIC_GETTY_PORT|TARGET_GENERIC_GETTY_BAUDRATE|TARGET_ROOTFS_CPIO|TARGET_ROOTFS_CPIO_FULL|TARGET_ROOTFS_CPIO_GZIP|LINUX_KERNEL|TARGET_UBOOT)=' \
  /mnt/rk3588-task-e/out/buildroot-minimal/.config
```

```text
BR2_aarch64=y
BR2_TOOLCHAIN_BUILDROOT=y
BR2_TOOLCHAIN_BUILDROOT_MUSL=y
BR2_TARGET_GENERIC_HOSTNAME="zzy-rk3588"
BR2_INIT_BUSYBOX=y
BR2_TARGET_GENERIC_GETTY_PORT="ttyS2"
BR2_TARGET_GENERIC_GETTY_BAUDRATE="0"
BR2_PACKAGE_BUSYBOX=y
BR2_TARGET_ROOTFS_CPIO=y
BR2_TARGET_ROOTFS_CPIO_FULL=y
BR2_TARGET_ROOTFS_CPIO_GZIP=y
```

**每项的含义**：

| 配置 | 含义 |
|---|---|
| `BR2_aarch64=y` | 为板子生成 **ARM64 小端**程序 |
| `BR2_TOOLCHAIN_BUILDROOT=y` | 由 Buildroot 构建**交叉编译工具链**（用电脑编译板上的程序）|
| `BR2_TOOLCHAIN_BUILDROOT_MUSL=y` | 使用 **musl C 库**，为程序提供基础运行支持 |
| `BR2_TARGET_GENERIC_HOSTNAME` | 系统主机名 |
| `BR2_INIT_BUSYBOX=y` | 用 **BusyBox 的 init** 管理启动流程、启动登录终端 |
| `BR2_PACKAGE_BUSYBOX=y` | 安装 BusyBox → 提供 `shell`、`ls`、`cat`、`mount` 等基础命令 |
| `BR2_TARGET_GENERIC_GETTY_PORT="ttyS2"` | 在 **UART2** 对应的终端提供登录提示 |
| `BR2_TARGET_GENERIC_GETTY_BAUDRATE="0"` | **保留内核设置的波特率** ✓（不是"串口速度为零" ✗）|
| `BR2_TARGET_ROOTFS_CPIO=y` | 根文件系统打包成 **cpio** |
| `BR2_TARGET_ROOTFS_CPIO_FULL=y` | 打包**整个**根文件系统 |
| `BR2_TARGET_ROOTFS_CPIO_GZIP=y` | 用 **gzip** 压缩 → 生成 `rootfs.cpio.gz` |

## 3.3 编译

> ⚠️ Buildroot 是"配置目录型"的 ✓ → 构建过程中还要**下载各种工具（如 gcc）** ✗ → **网络问题**要先解决 ✓
> （具体怎么解决的网络问题不记录了 ✓）

```bash
set -o pipefail
make O=/mnt/rk3588-task-e/out/buildroot-minimal \
  BR2_DL_DIR=/mnt/rk3588-task-e/cache/buildroot-dl \
  BR2_JLEVEL=4 \
  2>&1 | tee "/mnt/rk3588-task-e/logs/buildroot-minimal-$(date +%Y%m%d-%H%M%S).log"

task_build_status=${PIPESTATUS[0]}
printf 'Buildroot 构建返回码：%s\n' "$task_build_status"
```

## 3.4 整理 cpio.gz

```bash
rootdir=/mnt/rk3588-task-e/out/buildroot-minimal/build/buildroot-fs/cpio/target
table='/mnt/rk3588-task-e/out/buildroot-minimal/build/buildroot-fs/full_devices_table.txt'

PATH="/mnt/rk3588-task-e/out/buildroot-minimal/host/bin:/mnt/rk3588-task-e/out/buildroot-minimal/host/sbin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
  gzip -9 -c -n /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio \
  > /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz

ln -snf /mnt/rk3588-task-e/out/buildroot-minimal/host/aarch64-buildroot-linux-musl/sysroot \
  /mnt/rk3588-task-e/out/buildroot-minimal/staging

ls -lh /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz
```

```text
-rw-r--r-- 1 zzy zzy 974K Nov  3 22:44 .../rootfs.cpio.gz
```

---

# 四、boot 镜像与 extlinux

## 4.1 写 extlinux.conf

```bash
mkdir -p /mnt/rk3588-task-e/out/boot-test/files/rockchip \
  /mnt/rk3588-task-e/out/boot-test/files/extlinux &&
cp /mnt/rk3588-task-e/out/linux-zzy/arch/arm64/boot/Image \
  /mnt/rk3588-task-e/out/boot-test/files/ &&
cp /mnt/rk3588-task-e/out/linux-zzy/arch/arm64/boot/dts/rockchip/rk3588-zzy-minimal.dtb \
  /mnt/rk3588-task-e/out/boot-test/files/rockchip/ &&
cp /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz \
  /mnt/rk3588-task-e/out/boot-test/files/

cat > /mnt/rk3588-task-e/out/boot-test/files/extlinux/extlinux.conf <<'EOF'
DEFAULT zzy-linux
TIMEOUT 30

LABEL zzy-linux
    MENU LABEL ZZY RK3588 Minimal Linux
    LINUX /Image
    FDT /rockchip/rk3588-zzy-minimal.dtb
    INITRD /rootfs.cpio.gz
    APPEND console=ttyS2,1500000n8 earlycon rdinit=/init loglevel=7
EOF
```

**`APPEND` 每项的含义**：

| 参数 | 作用 |
|---|---|
| `console=ttyS2,1500000n8` | Linux 使用 **ttyS2**，串口速率 **1500000** |
| `earlycon` | 尽早输出串口日志 ✓ |
| `rdinit=/init` | 启动后执行根文件系统里的 **`/init`** |
| `loglevel=7` | 输出较详细的早期启动日志，便于定位问题 ✓ |

## 4.2 制作 64 MB 启动镜像

```bash
test ! -e /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img &&
truncate -s 64M /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img &&
mkfs.ext4 -F -L zzy-boot \
  -d /mnt/rk3588-task-e/out/boot-test/files \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img

# 检查
ls -lh /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img
debugfs -R 'cat /extlinux/extlinux.conf' \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img
```

## 4.3 ⚠️ 踩坑：目录没建 → 报错

**报错**：`out/boot-test` 目录和启动配置**都不存在** ✗

**原因**：前面"启动文件整理"还没完成 ✓；第一次写 `extlinux.conf` 时**目录还没建** ✓ → **重做一遍** ✓

**修复后重新生成** ✓ → **这次没问题** ✓

```bash
mkdir -p /mnt/g/linux-study/linux/rk3588-task-e/artifacts &&
cp --sparse=always \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/linux-boot-test.img &&
cmp \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/linux-boot-test.img &&
echo "启动镜像导出并校验通过"
```

→ `G:\linux-study\linux\rk3588-task-e\artifacts\linux-boot-test.img`

---

# 五、烧录（三次波折）

## 5.1 第一次：找不到 flash

**烧录 boot 分区的设置**：

| 设置 | 值 |
|---|---|
| 勾选 | **boot** 行 |
| 存储 | **EMMC** |
| 地址 | **0x00008000** |
| 路径 | `linux-boot-test.img` |
| 强制按地址写 | ✅ 勾选 |

> ⚠️ **这次板子识别不了 USB** ✗ → 只能在 U-Boot 串口里发 **`rockusb 0 mmc 0`** 进入烧录模式 ✓

**结果：大问题** ✗ —— **没法烧录**，报错**找不到 flash** ✗

**原因**：我们的 U-Boot 里的烧录流程**不是瑞芯微那一套** ✗ → 需要用**临时初始化的 U-Boot** 来烧录 ✓

## 5.2 用临时 loader 接管

```bash
cd /mnt/rk3588-task-e/src/rkbin-3e288fe &&
./tools/boot_merger RKBOOT/RK3588MINIALL.ini

ls -lh /mnt/rk3588-task-e/src/rkbin-3e288fe/rk3588_loader_v1.24.114.bin &&
mkdir -p /mnt/g/linux-study/linux/rk3588-task-e/artifacts &&
cp /mnt/rk3588-task-e/src/rkbin-3e288fe/rk3588_loader_v1.24.114.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/ &&
cmp /mnt/rk3588-task-e/src/rkbin-3e288fe/rk3588_loader_v1.24.114.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/rk3588_loader_v1.24.114.bin &&
echo "下载 Loader 导出并校验通过"
```

> 💡 **关于进入烧录模式的按键**：
> "按音量键进入烧录" ✗ 那是**原来系统特制**的 ✓
> **真正的板子复位是按着 `UPDATE` 重启** ✓ → 这次成功直接进入了 **MASKROM** ✓
> 但还是**不能烧原来的东西** ✗ → 继续尝试新方案 ✓

**做法**：在烧录界面**只勾第一行的 loader** ✓ → 地址位置都不变 ✓ → 选中刚生成的文件 → **烧进去** ✓

## 5.3 三件套一起烧

再勾上 **loader + uboot + boot** ✓ → **一起烧** ✓

| 分区 | 作用 |
|---|---|
| **Loader** | 建立 **USB 下载**和 **eMMC 访问**通道；工具还会处理 **IDB** ✓ |
| **Uboot** | 恢复我们自己编译、已验证过的**启动链** ✓ |
| **Boot** | 写入 **Linux 内核、设备树、BusyBox 根文件系统**和**启动配置** ✓ |

**这次成功烧录了** ✓

## 5.4 ⚠️ 却还是停在 U-Boot

> **现象**：正常重启 ✓ → 但**只能进 U-Boot** ✗ —— **U-Boot 找不到我们的 Linux 内核** ✗✓

**分层排查（省略具体命令，只记结论）**：

| 检查 | 结果 |
|---|---|
| `ext4ls mmc 0:3 /` | ✅ 能看到内核、设备树、根文件系统 → **Boot 分区位置正确、ext4 可读** ✓ |
| 查看 `/extlinux/` | ✅ 存在 `extlinux.conf` → **启动配置没漏打包** ✓ |
| 查看启动环境变量 | ✅ 目标是 `mmc0`、方法是 `extlinux` → **基本启动设置存在** ✓ |
| 指定 `mmc0:3` 扫描 | ❌ **仍然没有启动项** → **仅指定分区不足以解决** ✗ |
| `bootdev list` / `bootmeth list` | ✅ 有 eMMC 启动设备和 extlinux 方法 → **不是这两项缺失** ✓ |

> 🔑 **关键判断**：**不能简单说"内核没烧进去"** ✗ —— 数据都在 ✓、方法也注册了 ✓，**问题在自动启动路径** ✓

## 5.5 手动 booti 验证成功 ⭐

> **绕过自动扫描和 extlinux 配置解析** ✓ → **手动完成它们原本该做的事** ✓

**步骤**：

1. 用 `ext4load` 把 **Image** 从 eMMC 读入内存 ✓
2. 把**设备树**读入另一块内存 ✓
3. 把 **`rootfs.cpio.gz`** 读入内存，并**记录文件大小** ✓
4. 设置启动参数，指定**串口**和**初始程序 `/init`** ✓
5. 执行 **`booti`** ✓

**结果：成功打开了我们创建的最小系统** ✓✓

> ⚠️ **但现在是手动开启的** ✗ —— **没有全自动的启动流程** ✗ → **关机重启就没有了** ✗
> ✅ **不过这次成功登录，验证了内核和根文件系统能在这块板上运行** ✓✓

---

# 六、WiFi 驱动（RTL8733BU）

## 6.1 确认芯片 ID

```bash
cat /sys/bus/usb/devices/2-1/idVendor
cat /sys/bus/usb/devices/2-1/idProduct
cat /sys/bus/usb/devices/2-1/product
```

**结论**：

| 项目 | 值 |
|---|---|
| 芯片 | **RTL8733BU** ✓ |
| 设备 ID | **`0bda:b733`** ✓（与原理图一致 ✓）|
| 驱动源码 | https://github.com/wirenboard/rtl8733bu ✓ |
| 确认 | 核对候选驱动源码 → 它的 **RTL8733B 设备表明确包含 `b733`** ✓ |

> 下一步要让**无线驱动绑定它** ✓，才会出现可用的无线网络接口 ✓

**下载驱动源码**：

```bash
curl.exe --fail --location --retry 3 --proxy http://127.0.0.1:7890 \
  --output "G:\linux-study\linux\rk3588-task-e\downloads\rtl8733bu-2d9048be.tar.gz" \
  https://codeload.github.com/wirenboard/rtl8733bu/tar.gz/2d9048be60759206b8db5e2370420333ef0b8478

test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
tar -xzf \
  /mnt/g/linux-study/linux/rk3588-task-e/downloads/rtl8733bu-2d9048be.tar.gz \
  -C /mnt/rk3588-task-e/src
```

**确认无线芯片配置**：

```bash
cd /mnt/rk3588-task-e/src/rtl8733bu-2d9048be60759206b8db5e2370420333ef0b8478 &&
grep -nE '^CONFIG_(RTL8733B|USB_HCI|PLATFORM_WB7)[[:space:]]*=' Makefile
```

```text
60:CONFIG_RTL8733B = y
62:CONFIG_USB_HCI = y
152:CONFIG_PLATFORM_WB7 = y
```

## 6.2 先编译内核 modules

```bash
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
export TMPDIR=/mnt/rk3588-task-e/tmp &&
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 modules

echo "内核模块编译返回码：$?"
ls -lh /mnt/rk3588-task-e/out/linux-zzy/Module.symvers
```

```text
内核模块编译返回码：0
-rw-r--r-- 1 zzy zzy 820K Nov  4 22:10 .../Module.symvers
```

## 6.3 编译驱动：4 次迭代

### 第 1 次：失败

```bash
make -j4 \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KVER=7.3.1-zzy-minimal \
  KSRC=/mnt/rk3588-task-e/out/linux-zzy \
  M="$PWD" \
  modules 2>&1 | tee /mnt/rk3588-task-e/logs/rtl8733bu-build-first.log
```

> **报错**：**旧驱动传递编译参数的方式与 Linux 7.3.1 不兼容** ✗

### 第 2 次：补 ccflags

```bash
cp --update=none Makefile Makefile.before-linux7

grep -Fqx 'ccflags-y += $(EXTRA_CFLAGS)' Makefile ||
printf '\n# Pass legacy driver flags to current Kbuild\nccflags-y += $(EXTRA_CFLAGS)\n' >> Makefile
```

> **作用**：把驱动已有的**头文件路径和功能宏**传给新构建系统 ✓

### 第 3 次：改头文件路径为绝对路径

```bash
sed -i 's|-I$(src)/|-I$(abspath $(src))/|g' Makefile

grep -nF -- '-I$(abspath $(src))/include' Makefile
```

> **作用**：把驱动的头文件路径**明确写成绝对路径** ✓，避免被这段逻辑误转换 ✓

### 第 4 次：改定时器 API（Linux 6.x 改名）

```bash
cp --update=none \
  include/osdep_service_linux.h \
  include/osdep_service_linux.h.before-linux7 &&
sed -i \
  -e 's/\bfrom_timer(/timer_container_of(/g' \
  -e 's/\bdel_timer_sync(/timer_delete_sync(/g' \
  -e 's/\bdel_timer(/timer_delete(/g' \
  include/osdep_service_linux.h

grep -nE 'timer_container_of|timer_delete_sync|timer_delete\(' \
  include/osdep_service_linux.h
```

```text
371:    _timer *ptimer = timer_container_of(ptimer, in_timer, timer);
400:    *bcancelled = timer_delete_sync(&ptimer->timer) == 1 ? 1 : 0;
405:    timer_delete(&ptimer->timer);
```

> **背景**：Linux 6.x 起 `from_timer` 改名为 `timer_container_of`、`del_timer_sync` 改名为 `timer_delete_sync`、`del_timer` 改名为 `timer_delete` ✓

**还不行** ✗ → **直接让 AI 批量改源码** ✓（跳过逐个修改 ✓）→ **AI 改好了** ✓

## 6.4 打包进 Buildroot overlay

```bash
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
mkdir -p /mnt/rk3588-task-e/configs/rootfs-overlay/lib/modules/7.3.1-zzy-minimal/extra &&
install -m 644 \
  /mnt/rk3588-task-e/src/rtl8733bu-2d9048be60759206b8db5e2370420333ef0b8478/8733bu.ko \
  /mnt/rk3588-task-e/configs/rootfs-overlay/lib/modules/7.3.1-zzy-minimal/extra/

cd /mnt/rk3588-task-e/src/buildroot-2026.08.1 &&
./utils/config \
  --file /mnt/rk3588-task-e/out/buildroot-minimal/.config \
  --set-str BR2_ROOTFS_OVERLAY \
  "/mnt/rk3588-task-e/configs/rootfs-overlay" &&
make O=/mnt/rk3588-task-e/out/buildroot-minimal olddefconfig
```

**增量构建 rootfs**：

```bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export TMPDIR=/mnt/rk3588-task-e/tmp

make O=/mnt/rk3588-task-e/out/buildroot-minimal \
  BR2_DL_DIR=/mnt/rk3588-task-e/cache/buildroot-dl \
  BR2_JLEVEL=4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/buildroot-with-wifi-module.log

task_rootfs_status=${PIPESTATUS[0]}
echo "根文件系统构建返回码：$task_rootfs_status"
```

> ⚠️ 中间有一次**环境问题** ✗ → 重设 `PATH` / `TMPDIR` 后重跑 ✓

**检查镜像内容**：

```bash
ls -lh /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz
gzip -dc /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz \
  | cpio -t 2>/dev/null \
  | grep '8733bu.ko'
```

```text
-rw-r--r-- 1 zzy zzy 1.9M Nov  4 22:39 .../rootfs.cpio.gz
lib/modules/7.3.1-zzy-minimal/extra/8733bu.ko
```

> ✅ **驱动已经打进新 rootfs** ✓

---

# 七、集成收尾（自动启动 + WiFi + SSH）

> ⚠️ 本来应该此时上板验证 ✓，但**太慢了** ✗ → 直接**把 3 个部分一起完成**再来上板 ✓
> （省略大量命令行操作 ✓，只说内容 ✓）

## 7.1 U-Boot 自启动

> 改成**开机直接从 eMMC 第 3 分区**读取 **Image 和设备树** ✓，再读取**第 8 分区的 UUID** 传给 Linux ✓，最后执行 **`booti`** ✓

## 7.2 内核与设备树

> **直接沿用上一版本** ✓

## 7.3 补齐 WiFi + SSH 所需软件

| 软件 | 作用 |
|---|---|
| **`iw`** | 查看无线设备、扫描等**诊断** ✓ |
| **`wpa_supplicant`** | 与**热点完成认证和连接** ✓ |
| **`wpa_cli` / `wpa_passphrase`** | 在串口**配置热点、生成密码配置** ✓ |
| **BusyBox `udhcpc`** | 向路由器**获取 IP、网关和 DNS** ✓ |
| **`wireless-regdb`** | 补上此前缺少的**无线监管数据库** ✓ |
| **Dropbear** | 提供 **SSH 登录服务** ✓ |

## 7.4 开机服务

> 新增 **`S41wifi` 启动脚本** ✓，按顺序：
> **加载驱动 → 等待 `wlan0` → 启动无线连接程序 → 在后台请求 IP** ✓

## 7.5 根文件系统

> 从 **"内存试运行"** 改成 **"eMMC 持久保存"** ✓
> → Buildroot 另外生成一个 **256 MiB 的 ext4 rootfs 镜像** ✓ → 准备写入现有 rootfs 分区 ✓ → **Linux 直接以它作为 `/`** ✓

## 7.6 烧录文件

全部在 `G:\linux-study\linux\rk3588-task-e\artifacts\integrated-wifi-ssh` ✓

| 分区 | 地址 | 文件 |
|---|---|---|
| **Loader** | `0x00000000` | `rk3588_loader_v1.24.114.bin` |
| **Uboot** | `0x00000040` | `u-boot-rockchip.bin` |
| **Boot** | `0x00008000` | `boot-integrated.img` |
| **Rootfs**（可用 `System` 行）| `0x008B8000` | `rootfs-integrated.ext4` |

> ⚠️ **注意 `Rootfs` 的地址是 `0x008B8000`** ✓（比 boot 分区大得多 ✓）

## 7.7 上板验证

```text
# 串口登录
用户名: root
密码: （前面设的）

# 检查
uname -r
mount | grep ' on / '
lsmod
ip link show wlan0
dmesg | tail -n 80
```

**检查没问题** ✓ → **扫描并连接 WiFi** ✓ → **连接成功** ✓ → **手动保存到 eMMC**（保证下次开机直连 ✓）→ **通过 IP 成功 SSH 登录** ✓✓

> 🎯 **阶段成功** ✓

## 7.8 收尾与最终镜像

**最后检查发现**：
- ✅ 系统通过全部检查、确实连通了 ✓
- ⚠️ 还有点**内核和设备树的冲突** ✗，但**不影响系统运行** ✓ → **到此这个定制就算大体成功了** ✓

**小修**：

1. 把**修复版 WiFi 驱动**纳入 Buildroot ✓
2. 在内核构建中解决**无线数据库加载时机** ✓
3. 修改设备树，**收尾不使用的硬件节点** ✓
4. 重新构建两部分镜像 ✓
5. 最终打包成完整镜像 ✓

> （这些都是细枝末节 ✓，不记录 ✓，直接 AI 完成 ✓）

**最终镜像**：

```
G:\linux-study\linux\rk3588-task-e\artifacts\release-r1\zzy-rk3588-release-r1-update.img
```

**烧录上板重新测试** ✓ → **这次终于没有问题** ✓ → 🎉 **完整定制彻底完成** ✓

---

# 八、最终成果

| 项目 | 结果 |
|---|---|
| **U-Boot** | 自己编译的 `2026.10-zzy-minimal` ✓（含板级设备树 ✓）|
| **Linux 内核** | `7.3.1-zzy-minimal` ✓（只留 Rockchip 平台 ✓ + 裁剪图形/音频/蓝牙/PCIe/SATA/以太网 ✓）|
| **rootfs** | Buildroot + musl + BusyBox ✓ → 从 cpio.gz 内存运行 → 最终 **256 MiB ext4 持久化** ✓ |
| **WiFi** | RTL8733BU（`0bda:b733`）✓ 驱动编译 + 打包 ✓ |
| **SSH** | Dropbear ✓ |
| **自启动** | ✅ 从 eMMC 第 3 分区读 Image/dtb、第 8 分区读 rootfs → `booti` ✓ |
| **最终镜像** | `zzy-rk3588-release-r1-update.img` ✓ 上板验证通过 ✓✓ |

---

# 九、问题总结（三大教训）⭐

## 教训 1：上游支持 RK3588 ≠ 直接支持这块板

- 最初用 **EVB 参考配置** ✓，但**不能把参考板的电源、引脚、外设描述全部搬过来** ✗
- → **对照硬件资料** ✓，核对 **UART2 M0 / eMMC / USB WiFi 所在通路** ✓，再建立**自己的最小设备树** ✓
- **eMMC 先采用保守的频率和总线配置** ✓ → **先验证读写与启动**，再考虑性能 ✓
- **内存和存储容量也没有直接固定成当前板子的 8+64** ✓

## 教训 2：自编译 U-Boot 的烧录方式与原来 SDK 镜像不同

**第一类：U-Boot 超过 Flash 大小**
> 当时用的是**包含多个启动阶段的 `u-boot-rockchip.bin`** ✗ → 不能简单按旧系统普通 U-Boot 分区的方式处理 ✗
> → 后来根据**组合镜像的布局**，用正确地址 + **强制按地址写**完成验证 ✓

**第二类：U-Boot 能识别 eMMC，但 RKDevTool 读取 Flash 信息失败**
> 执行 `rockusb` 后工具显示的**模式名称**，也**不足以单独证明**已经进入芯片 BootROM 的 **Maskrom** ✗✓
> → 后来通过**硬件按键**进入下载模式 ✓，并用 **rkbin 组合的 Loader** 建立下载流程 ✓ → 才完成写入 ✓

## 教训 3：镜像烧录成功，却仍然停在 U-Boot

- **分层检查**后发现：**eMMC 能读 ✓、boot 分区能读 ✓、Image/DTB/rootfs.cpio.gz/extlinux.conf 都存在 ✓、启动设备和 extlinux 方法也已注册 ✓**
- → **所以不能简单说"内核没烧进去"** ✗✓
- **接下来绕过自动扫描** ✓：用 `ext4load` 分别读取内核、设备树、初始根文件系统 → 设置启动参数 → **`booti`** ✓ → **成功进入 Linux** ✓
- → **证明内核和最小 rootfs 能工作** ✓，**问题集中在自动启动路径** ✓
- → 后来把**明确的加载流程写入 U-Boot 启动命令** ✓，最终再改为**按分区名查询** ✓

> 💬 **其他大部分问题都是 AI 帮我解决的** ✓，我没特别注意 ✓
> **上面这 3 个是我比较有体会的** ✓
> **这次编译任务完成，达到了从启动 → 内核 → 根文件系统的全流程自定制** ✓✓

---

# 附录：术语与缩写

| 术语 | 含义 |
|---|---|
| **rkbin** | Rockchip 官方的二进制固件仓库（DDR 初始化、BL31 等）|
| **DDR/TPL** | DDR 初始化固件（`ROCKCHIP_TPL`）|
| **BL31** | ARM Trusted Firmware 的 EL3 阶段（安全世界）|
| **SPL** | Secondary Program Loader（U-Boot 第二阶段）|
| **IDB** | Image Description Block（Rockchip 镜像描述块）|
| **Maskrom** | RK 芯片内置的 USB 下载模式（BootROM 级）|
| **bootph** | U-Boot 设备树中的"启动阶段可见"标记（`bootph-all` 等）|
| **bootflow** | U-Boot 的启动流程框架（`bootflow scan`）|
| **extlinux.conf** | U-Boot/Linux 的启动配置（类 GRUB）|
| **defconfig** | 内核/U-Boot 的默认配置模板 |
| **olddefconfig** | 按默认值补齐新增配置项 |
| **musl** | 轻量 C 标准库（替代 glibc）|
| **BusyBox** | 集成了 shell/ls/cat/mount 等上百个命令的单个可执行文件 |
| **cpio / initramfs** | 内存根文件系统格式 |
| **overlay** | Buildroot 的"覆盖层"，往 rootfs 里追加文件 |
| **CFG80211** | Linux 无线配置框架 |
| **RFKILL** | 无线开关控制框架 |
| **Dropbear** | 轻量 SSH 服务端 |
| **udhcpc** | BusyBox 的 DHCP 客户端 |
| **wpa_supplicant** | WiFi 认证/连接程序 |
| **wireless-regdb** | 无线监管数据库（各国频段/功率限制）|
| **tsadc** | Rockchip 温度传感器 ADC 控制器 |
| **DWCMSHC** | Synopsys 的 eMMC 控制器 IP |
