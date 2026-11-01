本次任务就是独立构建的最小板级系统，最终目标就是烧上板子能串口配置Wi-Fi，或者提前把Wi-Fi配置写进镜像，然后通过Wi-Fi登录SSH、查看和操作文件系统
然后全流程全部自己定制，不使用正点现成的资料和材料，只使用芯片厂提供的一些初始化代码，剩下的从各个官网完全自己完成定制，包括uboot的板子启动，linux内核的搭建和roofs的配置
然后我们分步骤来完成我们这个项目，roofs的编译前面的debain已经是完全自定义了，虽然这次使用buildroot，但是大概相同，所以不用太在意，我们主要关注uboot和内核

现在第一步我们来编译uboot，首先就要搞清楚编译uboot需要什么
现在我们来开始，首先就是挂载wsl的文件开始，先wsl登录进去，然后我们下载的uboot源码在G:\linux-study\linux\rk3588-task-e
我们需要把这个新的文件夹挂载到我们的wsl，所以先sudo mkdir -p /mnt/rk3588-task-e，先挂载盘，然后：
sudo mount -t ext4 -o loop \
  /mnt/g/linux-study/linux/rk3588-task-e/rk3588-task-e.ext4 \
  /mnt/rk3588-task-e
（让Linux把G盘这个镜像文件当作磁盘使用，将里面的ext4文件系统接到/mnt/rk3588-task-e）

就可以使用：findmnt --mountpoint /mnt/rk3588-task-e -o TARGET,SOURCE,FSTYPE,UUID，就可以看到挂载的详细数据了
（现在文件里面有的源码就是ai帮我下载的uboot2026.10的最新代码，里面直接还带有对rk3588的驱动支持（不等于直接支持板子），然后如果手动下载https://github.com/u-boot/u-boot这是github的源码位置，https://docs.u-boot.org/en/stable/build/source.html这个就是，现在的代码是ai直接使用git的获取代码直接获取的）
挂载没有问题了，我们就可以开始编译uboot的源码，在此之前就先把临时文件设置到工作盘上：
cd /mnt/rk3588-task-e/src/u-boot
mkdir -p /mnt/rk3588-task-e/tmp
export TMPDIR=/mnt/rk3588-task-e/tmp

然后我们就可以生成配置：
make O=/mnt/rk3588-task-e/out/uboot-reference \
  CROSS_COMPILE=aarch64-linux-gnu- \
  evb-rk3588_defconfig
（这一步会编译少量运行在电脑上的配置工具，再生成/mnt/rk3588-task-e/out/uboot-reference/.config，这里的.config就是后续构建U-Boot的配置清单，记录目标芯片、设备树、串口和启用的功能）

我们可以检查关键结果：
zzy@LAPTOP-4S39SRHC:/mnt/rk3588-task-e/src/u-boot$ grep -E '^(CONFIG_ROCKCHIP_RK3588|CONFIG_DEFAULT_DEVICE_TREE|CONFIG_BAUDRATE)=' \
  /mnt/rk3588-task-e/out/uboot-reference/.config
输出：
CONFIG_DEFAULT_DEVICE_TREE="rockchip/rk3588-evb1-v10"（当前采用瑞芯微RK3588 EVB参考板的设备树）
CONFIG_ROCKCHIP_RK3588=y（已启用RK3588芯片支持）
CONFIG_BAUDRATE=1500000（U-Boot配置的默认串口波特率为1500000，后续连接它时串口工具需要匹配）

设备树现在还是参考板的，尚未适配正点原子板卡，设备树的作用，就是告诉U-Boot这块板上的串口、存储、电源等硬件是怎样连接和配置的，所以我们现在需要弄清楚怎么配置的硬件
指令：
sed -n '1,180p' \
  dts/upstream/src/arm64/rockchip/rk3588-evb1-v10.dts
（这条查看参考板设备树的开头，重点认识板名、引用的公共描述、默认串口和供电配置）（输出源码太长了不复制了）
指令：
find arch/arm/dts -maxdepth 1 \
  -name '*rk3588*' -name '*u-boot*'
（这条查找U-Boot专用的设备树补充文件）

可以看到设备树的源码开头：
#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/input/input.h>
#include <dt-bindings/pinctrl/rockchip.h>
#include <dt-bindings/soc/rockchip,vop2.h>
#include <dt-bindings/usb/pd.h>
#include "rk3588.dtsi"
这里面rk3588.dtsi是芯片级描述，主要描述RK3588芯片有哪些控制器，它们的寄存器、中断、时钟等信息，这个是芯片的描述是可以复用的
而板名是：
model = "Rockchip RK3588 EVB1 V10 Board";
    compatible = "rockchip,rk3588-evb1-v10"
"rockchip,rk3588";说明还是描述的EVB板使用了哪些控制器，以及外部器件、供电和引脚如何连接，这个不一定和正点的板子一样，所以需要修改
还有源码：
aliases {
                ethernet0 = &gmac0;
                mmc0 = &sdhci;
        };
其中mmc0 = &sdhci给sdhci控制器指定了MMC设备别名，但是这里只是给存储设备一个编号，不等于后续实际功能
还有源码：
chosen {
                stdout-path = "serial2:1500000n8";
        };
这里是这里指定默认控制台使用serial2，通信参数为1500000波特率、无校验、8位数据位，串口配置是我们首先关心的内容，主要是要注意EVB板子和我们的板子串口编号的不同

然后我们先执行：cat arch/arm/dts/rk3588-evb1-v10-u-boot.dtsi
再执行：sed -n '1,220p' arch/arm/dts/rk3588-u-boot.dtsi
主要是看启动早期究竟需要哪些设备，以及它们怎样与主设备树配合，这两个文件都很短它们通过逐层引用，把公共配置复用起来，所以配置不一定直接写在当前文件里，所以还要继续
但是我们看到：u-boot,spl-boot-order = "same-as-spl", &sdhci;
这条命令，优先尝试从加载 SPL 自身的启动设备继续加载后续阶段，如果前面的尝试没有成功，再尝试sdhci对应的存储设备，它控制的是 SPL 从哪里加载后续启动阶段，不是 Linux 的根文件系统在哪里，所以我们继续看他的引用源码：sed -n '1,240p' arch/arm/dts/rk3588s-u-boot.dtsi
这次的输出特别长，我们可要经常看到bootph：
&uart2 {
        bootph-all;
        clock-frequency = <24000000>;
};
他是用来说明一个设备的使用周期的，因为启动早期的程序需要尽量小，因此不会把完整设备树中的所有设备都保留下来，所以bootph用来表明再那些时候用，这个串口bootph-all，表达的是：UART2在各启动阶段都需要，并为它指定24MHz的时钟频率
这里面还有很多很多的存储、时钟和引脚等，不详细看了，我们直接看板级连接，不再逐个追读所有公共文件
先查看 EVB 主设备树中 UART2 和 eMMC 的具体配置：
sed -n '/^&uart2 {/,/^};/p' \
  dts/upstream/src/arm64/rockchip/rk3588-evb1-v10.dts
输出：
&uart2 {
        pinctrl-0 = <&uart2m0_xfer>;（选择 UART2 的 M0 收发引脚组）
        status = "okay";（启用 UART2）
};

sed -n '/^&sdhci {/,/^};/p' \
  dts/upstream/src/arm64/rockchip/rk3588-evb1-v10.dts
输出：
&sdhci {（指定连接方式和允许使用的模式）
        bus-width = <8>;（使用 8 位数据总线，板上需要相应的数据线连接）
        no-sdio;（不按 SDIO 设备进行探测）
        no-sd;（不按 SD 存储卡进行探测）
        non-removable;（表示这是固定连接、不可拔插的存储设备）
        mmc-hs400-1_8v;（声明支持采用 1.8 V 信号的 HS400 模式）
        mmc-hs400-enhanced-strobe;（声明支持 HS400 增强选通模式）
        status = "okay";（启用该控制器）
};

这两段说明：参考板启用了 UART2 的 M0 引脚组，并把 sdhci 配置为连接板载 eMMC
到这里我们就完成了串口、eMMC 和早期启动配置的定位，现在来看一下正点的板子上面的这些地方的连接方式是什么样子的
这个工作我实在是不太懂所以交给ai来主力阅读，只做ai的阅读解释，经过查看正点的板子的原理图发现：
串口：可以沿用 UART2 M0，引脚配置相同
内存：RK3588＋LPDDR4X，这个容量大小不一定，所以不能完全照搬
eMMC：eMMC 焊在核心板上，没找到资料，只能靠前面的内容上板了自己读
供电：不能整套复制 EVB
Wi-Fi：后续应该走 USB 驱动路线

现在来准备开始进行我们的配置编译，先在电脑上面按安装我们需要的环境：
sudo apt-get update &&
sudo apt-get install libgnutls28-dev python3-pyelftools

然后我们先创建一个我们自己的配置文件后面用来存储我们的配置：
cd /mnt/rk3588-task-e/src/u-boot &&
cp --update=none configs/evb-rk3588_defconfig configs/zzy-rk3588_defconfig

给文件写标识：
sed -i '/^CONFIG_LOCALVERSION=/d' configs/zzy-rk3588_defconfig

printf '%s\n' 'CONFIG_LOCALVERSION="-zzy-rk3588"' \
  >> configs/zzy-rk3588_defconfig
（就是编译的时候给个名字）

用自己的配置生成独立构建目录：
临时文件保存在 G 盘工作空间：
mkdir -p /mnt/rk3588-task-e/tmp
export TMPDIR=/mnt/rk3588-task-e/tmp

make O=/mnt/rk3588-task-e/out/uboot-zzy \
  CROSS_COMPILE=aarch64-linux-gnu- \
  zzy-rk3588_defconfig

检查结果：
grep -E '^(CONFIG_LOCALVERSION|CONFIG_DEFAULT_DEVICE_TREE|CONFIG_ROCKCHIP_RK3588|CONFIG_BAUDRATE)=' \
  /mnt/rk3588-task-e/out/uboot-zzy/.config

现在主要就是使用把原来的参考的配置复制下来，创建一个新的编译工程，后面我们再来修改其中不一样的配置
现在下载rk自己的固件：
cd /mnt/rk3588-task-e/src &&
git init rkbin &&
cd rkbin &&
git remote add origin https://github.com/rockchip-linux/rkbin.git &&
git fetch --depth 1 origin 3e288fe814e059dd06833495f845cab04ac20a5c &&
git checkout --detach FETCH_HEAD
（因为rk官方不提供DDR的初始化源码，只给二进制文件，所以只能借助他们的）

如果下载完成可以findmnt --mountpoint /mnt/rk3588-task-e -o TARGET,UUID，可以看到文本输出：f0c827b8-89bf-4b52-b620-d6b51c803569，这样就说明没有问题，就可以解压了：
mkdir -p /mnt/rk3588-task-e/src/rkbin-3e288fe &&
tar -xzf \
  /mnt/g/linux-study/linux/rk3588-task-e/downloads/rkbin-3e288fe.tar.gz \
  -C /mnt/rk3588-task-e/src/rkbin-3e288fe \
  --strip-components=1

成功的话可以检查文件：
zzy@LAPTOP-4S39SRHC:/mnt/rk3588-task-e/src/rkbin$ cd /mnt/rk3588-task-e/src/rkbin-3e288fe &&
sha256sum \
  bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin \
  bin/rk35/rk3588_bl31_v1.56.elf \
  | tee /mnt/rk3588-task-e/logs/rkbin-input-sha256.txt
2853a0da7ab895af43d50615af73eccf0694115dd0202483ee1c93c9071a42c3  bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin
2864c2fc36333e03e7e3fe6459225d320d310850b4ac7620d1f3601efb7d4a9b  bin/rk35/rk3588_bl31_v1.56.elf

然后下一步，因为rk官方的初始化没有源码，所以我们需要自己导出他的参数来看和我们的需求是否匹配：
cd /mnt/rk3588-task-e/src/rkbin-3e288fe &&
python3 tools/ddrbin_tool.py rk3588 \
  -g /mnt/rk3588-task-e/logs/ddr-original-params.txt \
  bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin

查看一下关键参数：
grep -niE 'freq|uart|baud|channel|rank|capacity|size|stride' \
  /mnt/rk3588-task-e/logs/ddr-original-params.txt

我们先来一下使用EVB的派生编译一下，验证打包和编译流程没有问题，先把外部的文件和源码引入：
cd /mnt/rk3588-task-e/src/u-boot
export TMPDIR=/mnt/rk3588-task-e/tmp
export ROCKCHIP_TPL=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin
export BL31=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_bl31_v1.56.elf
（告诉编译器哪里获取 DDR 初始化固件和 BL31）

然后开始编译并保留日志：
set -o pipefail
make O=/mnt/rk3588-task-e/out/uboot-zzy \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/uboot-zzy-build-01.log

很快就编译完了，我们检查一下参数：
ls -lh \
  /mnt/rk3588-task-e/out/uboot-zzy/u-boot \
  /mnt/rk3588-task-e/out/uboot-zzy/spl/u-boot-spl.bin \
  /mnt/rk3588-task-e/out/uboot-zzy/u-boot-rockchip.bin
输出：
-rw-r--r-- 1 zzy zzy 121K Nov  1 12:39 /mnt/rk3588-task-e/out/uboot-zzy/spl/u-boot-spl.bin
-rwxr-xr-x 1 zzy zzy 7.6M Nov  1 12:38 /mnt/rk3588-task-e/out/uboot-zzy/u-boot
-rw-r--r-- 1 zzy zzy 9.2M Nov  1 12:39 /mnt/rk3588-task-e/out/uboot-zzy/u-boot-rockchip.bin

得到的是uboot编译的3个文件，到这里我们就完成了编译uboot的前面两步，阅读固件配置和验证编译路径的无误，后面就是下一步进行板上不一样的地方进行修改
现在这个初始版本是：
- 保留已核实的 UART2 M0
- 保留 eMMC，首次使用 1 位数据总线、最高 25 MHz，降低初始验证复杂度
- 关闭 SD 卡
- 保留 USB 下载功能的基础支持
- 不写死内存、eMMC 容量

现在我们来完成我们自己的板子的适配：
需要先写我们自己的设备树：
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
（引用上游最小设备树，再覆盖我们需要调整的部分，串口和 USB 的公共配置因此可以直接复用）

然后建立 U-Boot 专用补充文件：
cat > arch/arm/dts/rk3588-zzy-minimal-u-boot.dtsi <<'EOF'
// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
#include "rk3588-generic-u-boot.dtsi"

/ {
	chosen {
		u-boot,spl-boot-order = "same-as-spl", &sdhci;
	};
};
EOF
（复用早期启动节点标记，并让 SPL 优先沿用自身启动设备，再尝试 eMMC）

总之前面这两个都是在复用官方资源的情况下，创建我们自己的设备树和uboot的说明
然后我们来设置建立对应的编译配置：
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
（指定自己的设备树和版本名称）
//`CONFIG_DEFAULT_DEVICE_TREE` 选择 U-Boot 自己使用的设备树；`CONFIG_DEFAULT_FDT_FILE` 只是后续加载 Linux 设备树时使用的默认文件名，不会自动生成 Linux 的设备树

然后继续生成我们的编译配置，也就是给我们自己的编译配置前置环境：
export TMPDIR=/mnt/rk3588-task-e/tmp
export ROCKCHIP_TPL=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin
export BL31=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_bl31_v1.56.elf

然后生成独立配置：
make O=/mnt/rk3588-task-e/out/uboot-minimal \
  CROSS_COMPILE=aarch64-linux-gnu- \
  zzy-rk3588-minimal_defconfig

set -o pipefail
make O=/mnt/rk3588-task-e/out/uboot-minimal \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/uboot-minimal-build-01.log

把这两段编译成功，其实现在干的事情就是把原本的开源的配置改成了我们要的：
版本：2026.10-zzy-minimal
板名：ZZY ATK-DLRK3588 Minimal Bring-up
串口：UART2、1500000
eMMC：1 位、最高 25 MHz、已启用
SD卡：已禁用
SPL加载顺序：沿用启动设备，然后尝试 eMMC

现在要来配置我们的uboot的一些真实配置，但是我们手上没有板子的内部资料，所以只能先使用手上的linux查一下，这一步在正规公司里面就是直接查内部的硬件资料，所以还算是正常定制

我们查询uname -a、lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINTS、cat /proc/partitions
主要就是查询：板子当前从 eMMC 运行，Linux 将它识别为 /dev/mmcblk0，根文件系统在第 8 个分区
查询结果如下：
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
alientek@ATK-DLRK3588:~$ cat /proc/partitions
major minor  #blocks  name

   1        0       4096 ram0
 179        0   60416000 mmcblk0
 179        1       4096 mmcblk0p1
 179        2       4096 mmcblk0p2
 179        3      65536 mmcblk0p3
 179        4     131072 mmcblk0p4
 179        5      32768 mmcblk0p5
 179        6     131072 mmcblk0p6
 179        7    4194304 mmcblk0p7
 179        8   55844832 mmcblk0p8
 179       32       4096 mmcblk0boot0
 179       64       4096 mmcblk0boot1

现在还缺少分区名称、起始位置和前部预留空间的信息，继续查询：lsblk -o NAME,SIZE,START,PARTLABEL,PARTUUID、sudo fdisk -l /dev/mmcblk0、
cat /sys/class/block/mmcblk0/device/type
cat /sys/class/block/mmcblk0/device/name
grep MemTotal /proc/meminfo

得到：
alientek@ATK-DLRK3588:~$ lsblk -o NAME,SIZE,START,PARTLABEL,PARTUUID
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
mmcblk0boot0    4M
mmcblk0boot1    4M
zram0           0B
alientek@ATK-DLRK3588:~$ sudo fdisk -l /dev/mmcblk0
[sudo: authenticate] Password:
sudo: Authentication failed, try again.
[sudo: authenticate] Password:
Disk /dev/mmcblk0: 57.62 GiB, 61865984000 bytes, 120832000 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes
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
alientek@ATK-DLRK3588:~$ cat /sys/class/block/mmcblk0/device/type
cat /sys/class/block/mmcblk0/device/name
grep MemTotal /proc/meminfo
MMC
A3A561
MemTotal:        8106268 kB
alientek@ATK-DLRK3588:~$ lsblk -o NAME,SIZE,START,PARTLABEL,PARTUUID
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
mmcblk0boot0    4M
mmcblk0boot1    4M
zram0           0B
alientek@ATK-DLRK3588:~$ sudo fdisk -l /dev/mmcblk0
[sudo: authenticate] Password:
Disk /dev/mmcblk0: 57.62 GiB, 61865984000 bytes, 120832000 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes
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
alientek@ATK-DLRK3588:~$ cat /sys/class/block/mmcblk0/device/type
cat /sys/class/block/mmcblk0/device/name
grep MemTotal /proc/meminfo
MMC
A3A561
MemTotal:        8106268 kB

得到数据是：
- eMMC：`/dev/mmcblk0`，61,865,984,000 字节，符合标称 64 GB 的容量等级。
- 内存：Linux 可用约 7.73 GiB，与 8 GB 板子预留部分内存后的情况相符。
- 分区表：GPT，每个扇区 512 字节。

磁盘位置：
前 8 MiB：包含主 GPT、分区前预留空间；早期加载程序的位置需要结合启动格式判断
8～12 MiB：uboot 分区
12～16 MiB：misc 分区
16 MiB 开始：boot 分区，后面还有 recovery、rootfs 等

然后我们下一步就是按照这个配置把我们的uboot设置成我们板子适配的，
下一步我们修改自启动策略，等待 3 秒，然后只从 eMMC 查找 extlinux.conf，按文件中的配置加载 Linux：
cd /mnt/rk3588-task-e/src/u-boot

sed -i \
  -e '/^CONFIG_BOOTDELAY=/d' \
  -e '/^CONFIG_BOOTCOMMAND=/d' \
  configs/zzy-rk3588-minimal_defconfig
（清除这个文件中可能已有的同名设置）

cat >> configs/zzy-rk3588-minimal_defconfig <<'EOF'
CONFIG_BOOTDELAY=3
CONFIG_BOOTCOMMAND="setenv boot_targets mmc0; setenv bootmeths extlinux; bootflow scan -lbG"
EOF
（写入启动策略）

作用是：
等待 3 秒，留出通过串口中断自动启动的机会
把启动搜索设备限定为 mmc0，对应我们设备树中的 eMMC
使用 extlinux 启动方式，跳过全局启动方法
找到有效配置后尝试启动

然后重新生成配置：
make O=/mnt/rk3588-task-e/out/uboot-minimal \
  CROSS_COMPILE=aarch64-linux-gnu- \
  zzy-rk3588-minimal_defconfig

可以使用：
grep -E '^CONFIG_(BOOTDELAY|BOOTCOMMAND|BOOTMETH_EXTLINUX|FS_EXT4)=' \
  /mnt/rk3588-task-e/out/uboot-minimal/.config
看到生效值：
CONFIG_BOOTMETH_EXTLINUX=y
CONFIG_BOOTDELAY=3
CONFIG_BOOTCOMMAND="setenv boot_targets mmc0; setenv bootmeths extlinux; bootflow scan -lbG"
CONFIG_FS_EXT4=y

然后设置构建输入：
export TMPDIR=/mnt/rk3588-task-e/tmp
export ROCKCHIP_TPL=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_ddr_lp4_2112MHz_lp5_2400MHz_v1.24.bin
export BL31=/mnt/rk3588-task-e/src/rkbin-3e288fe/bin/rk35/rk3588_bl31_v1.56.elf
然后构建：
set -o pipefail
make O=/mnt/rk3588-task-e/out/uboot-minimal \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/uboot-minimal-build-02.log

构建完成就算是我们的定制uboot完成了，本质还是很简单，就是先下载uboot的源码，先使用他们给我们的目标板子设计的一些适配，让后查询我们的板子需要的一些配置有哪些地方不一样，让后一样的复用，不一样的就替换，需要重写一些迷你设备树，然后放入进行编译
现在我们来烧录验证这次的编译结果是否正常，先准备需要下载的loader在wsl执行：
cd /mnt/rk3588-task-e/src/rkbin-3e288fe &&
./tools/boot_merger RKBOOT/RK3588MINIALL.ini
然后复制到windous：
mkdir -p /mnt/g/linux-study/linux/rk3588-task-e/artifacts/uboot-test
cp rk3588_loader_v1.24.114.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/uboot-test/ &&
cp /mnt/rk3588-task-e/out/uboot-minimal/u-boot-rockchip.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/uboot-test/ &&
cmp /mnt/rk3588-task-e/out/uboot-minimal/u-boot-rockchip.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/uboot-test/u-boot-rockchip.bin &&
echo "下载文件准备完成"
这时候文件就复制到了G:\linux-study\linux\rk3588-task-e\artifacts\uboot-test

使用RKDevTool来烧录，很简单：
在首页：
左侧勾选框：只勾选第 3 行，其他行全部不勾
存储：如果可以选择，选 EMMC
地址：双击修改为 0x00000040
名字：可以保留 Uboot
路径：点击该行最右侧单元格，选择下面的文件
页面下方：勾选“强制按地址写”

如果上面每一个都能全部完成就能成功烧录（第一下我忘记了勾选“强制按地址写”），所以烧录成功后直接串口打开就可以简单uboot的常规开局，开始3、2、1倒数，到这一步至少uboot是编译成功了，现在进行uboot的命令行检测，依次输入：version、bdinfo、mmc list、mmc dev 0、mmc info、part list mmc 0，就可以得到大量的数据，太长了不粘贴到这里了，让ai读一下这些指令可以得知：这次基础验证通过了：自己的 U-Boot 能运行、能串口交互，也能识别 eMMC 并读取 GPT 分区表，功能全都是正常的；
自编译版本：2026.10-zzy-minimal，正确
内存识别：8 GiB；尚不代表完成稳定性测试
eMMC 型号与容量：A3A561、57.6 GiB，与原系统一致
分区表读取：八个分区的位置、名称和 UUID 与刷写前一致
实际存储时钟：25 MHz

只是这里我们为了保证我们的第一版本能用，所以emmc使用的单线，低频率都是很低的值，所以现在发现我们的emmc是4线的，所以有些编译结果需要修改
再测试一下驱动的内存依次输入：ext4ls mmc 0:8 /、ext4load mmc 0:8 ${kernel_addr_r} /etc/os-release、md.b ${kernel_addr_r} 100
就是尝试写内存看结果，结果太长不粘贴，ai读取后没问题：
ext4ls：能够识别 ext4 文件系统并列出目录
ext4load：成功将 478 字节读入内存
md.b：在内存地址 0x02000000 看到了正确的文件内容

自己的 U-Boot → eMMC → GPT 分区 → ext4 文件 → 内存

目前确认的是这块 8＋64 板子的基础启动和读取能力，还没有完成全内存稳定性、其他容量型号以及 Linux 启动验证，但是这些复杂验证可以后面再研究，至少现在的功能没有问题了
验证通过，U-Boot 基础阶段可以暂时收尾了，下一步就是我们自己要使用的linux内核的编译

