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
这次我们直接使用最新的linux的内核来完成，分两个部分来完成，首先就是先制作一个最简单的镜像，第一部分先在linux板子上面跑起来，使用串口来命令行查询，第二部分就是在跑起来的内核上面再来查询板子上面的一些外设的问题，比如emmc和wifi这种
现在的我们就要先完成第一部分的东西，固定源码与兼容性检查和配置内核和设备树，还是先自己连接wsl以及配置工作文件盘，然后我们开始第一步，先下载我们的内核文件：
cd G:\linux-study\linux\rk3588-task-e\downloads
curl.exe --fail --location --retry 3 --proxy http://127.0.0.1:7890 --output linux-7.3.1.tar.xz https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.3.1.tar.xz
（也可以手动下载https://www.kernel.org/，这是linux内核的官网，https://github.com/torvalds/linux这是内核的github官网）

现在完成后可以使用：
curl.exe --fail --location --retry 3 --proxy http://127.0.0.1:7890 --output "G:\linux-study\linux\rk3588-task-e\downloads\sha256sums.asc" https://cdn.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc
（进行下载的验证）

然后先确认目标位置然后解压我们的内核代码：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
tar -xJf \
  /mnt/g/linux-study/linux/rk3588-task-e/downloads/linux-7.3.1.tar.xz \
  -C /mnt/rk3588-task-e/src

通过代码确认版本和参数：
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
head -n 5 Makefile
输出：
# SPDX-License-Identifier: GPL-2.0
VERSION = 7
PATCHLEVEL = 3
SUBLEVEL = 1
EXTRAVERSION =

版本确认是 Linux 7.3.1，源码准备完成，接下来生成内核基础配置，这一步类似之前 U-Boot 的 defconfig：告诉构建系统目标是 ARM64，先生成一份功能配置清单 .config
这里也是采用上游 ARM64 通用配置作为起点，主要是先看一下有哪些可以参考的，然后我们再加上我们的板子特殊的东西，这样综合着节约我们的工作量：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
mkdir -p /mnt/rk3588-task-e/out/linux-zzy /mnt/rk3588-task-e/tmp &&
export TMPDIR=/mnt/rk3588-task-e/tmp &&
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  defconfig
（这个代码输出参数文件到我们的工作目录，一是给ai查看现在的配置参数，二是用作后面做参看和现在查看）

grep -E '^CONFIG_(ARM64|ARCH_ROCKCHIP|SERIAL_8250_DW|MMC_SDHCI_OF_DWCMSHC|EXT4_FS)=' \
  /mnt/rk3588-task-e/out/linux-zzy/.config
输出：
CONFIG_ARM64=y
CONFIG_ARCH_ROCKCHIP=y
CONFIG_SERIAL_8250_DW=y
CONFIG_MMC_SDHCI_OF_DWCMSHC=y
CONFIG_EXT4_FS=y
（检查发现基础配置生成成功）

运行：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
./scripts/config --file /mnt/rk3588-task-e/out/linux-zzy/.config \
  --set-str LOCALVERSION "-zzy-minimal" \
  --disable LOCALVERSION_AUTO \
  --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT \
  --enable DEBUG_INFO_NONE
（给自己的内核加名字：7.3.1-zzy-minimal，关闭 DWARF 调试信息）

让内核配置系统整理上面的修改及其依赖：
export TMPDIR=/mnt/rk3588-task-e/tmp

make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  olddefconfig

使用代码查看结果：
grep -E '^(CONFIG_LOCALVERSION=|CONFIG_DEBUG_INFO_NONE=|# CONFIG_LOCALVERSION_AUTO)' \
  /mnt/rk3588-task-e/out/linux-zzy/.config
输出：
CONFIG_LOCALVERSION="-zzy-minimal"
# CONFIG_LOCALVERSION_AUTO is not set
CONFIG_DEBUG_INFO_NONE=y

现在的内核设置正确，现在来配置我们的设备树，本次先描述：
串口：UART2 M0，1500000 波特率，与前面的硬件核对一致
eMMC：4 位、最高 25 MHz，作为首轮验证设置
USB：启用板载 Wi-Fi 所接的 USB2 HOST0 通路
温度监测：启用芯片温度传感器，使用内部热关断通路
内存容量：由启动链传递，不写死为 8 GB

我们直接指令创建设备树：
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

把设备树列入到我们的内核构建列表：
grep -qF 'rk3588-zzy-minimal.dtb' arch/arm64/boot/dts/rockchip/Makefile ||
printf '\ndtb-$(CONFIG_ARCH_ROCKCHIP) += rk3588-zzy-minimal.dtb\n' \
  >> arch/arm64/boot/dts/rockchip/Makefile

只编译这一份设备树：
export TMPDIR=/mnt/rk3588-task-e/tmp

make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  rockchip/rk3588-zzy-minimal.dtb

可以通过指令查看产物：/mnt/rk3588-task-e/src/linux-7.3.1$ ls -lh /mnt/rk3588-task-e/out/linux-zzy/arch/arm64/boot/dts/rockchip/rk3588-zzy-minimal.dtb
输出：
-rw-r--r-- 1 zzy zzy 89K Nov  2 20:29 /mnt/rk3588-task-e/out/linux-zzy/arch/arm64/boot/dts/rockchip/rk3588-zzy-minimal.dtb

下一步是裁剪内核配置，通用配置还启用了大量其他厂商的平台，以及图形、音频等功能，温度监测和无线基础框架才默认是模块不启用，所以我们要裁剪掉这些不需要的，调整的目标是：
芯片平台只保留：Rockchip	减少无关平台代码
关闭图形、音频、摄像头、蓝牙、PCIe、SATA、以太网驱动框架：聚焦串口、eMMC、USB Wi-Fi
无线基础框架、温度监测编入内核：初次启动不需要先加载这些模块
暂时关闭 CPU 和设备动态调频：配合目前尚未完整适配的电源描述
保留模块加载能力：后面使用自己编译的 Wi-Fi 驱动

先关闭其他芯片：
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

调整功能和驱动：
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
/*
CFG80211：Linux 的无线配置框架，后续 Wi-Fi 驱动需要与它配合
RFKILL：无线设备开关控制框架
ROCKCHIP_THERMAL：瑞芯微温度传感器驱动
USB_NET_DRIVERS：另一类 USB 网络适配器驱动集合，关闭它不会关闭 USB 主机控制器或整个无线驱动框架
*/

整理依赖并检查结果：
export TMPDIR=/mnt/rk3588-task-e/tmp
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  olddefconfig
然后：
grep -E '^(CONFIG_(ARCH_ROCKCHIP|CFG80211|RFKILL|ROCKCHIP_THERMAL|MODULES)=|# CONFIG_(DRM|SOUND|MEDIA_SUPPORT|BT|PCI|ATA|ETHERNET|CPU_FREQ|PM_DEVFREQ) is not set)' \
  /mnt/rk3588-task-e/out/linux-zzy/.config
输出：
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

还有一点补充设置：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
export TMPDIR=/mnt/rk3588-task-e/tmp &&
./scripts/config --file /mnt/rk3588-task-e/out/linux-zzy/.config \
  --disable SCSI_UFSHCD \
  --disable PM_DEVFREQ &&
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  olddefconfig
（默认的 UFS 存储驱动会重新开启设备调频框架，这块板使用 eMMC，因此一起关闭 UFS）

然后我们开始编译（采用四个并行任务，控制内存占用）：
set -o pipefail

make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 Image rockchip/rk3588-zzy-minimal.dtb \
  2>&1 | tee "/mnt/rk3588-task-e/logs/linux-image-$(date +%Y%m%d-%H%M%S).log"

task_build_status=${PIPESTATUS[0]}
printf '内核编译返回码：%s\n' "$task_build_status"

完成后我们最原始的最小内核就编译成功了，但是要验证是否成功还需要一个最小根文件系统。不然不能进入系统，因为内核没有uboot那样的命令行，需要检查内核是否完好，需要在roofs里面来检测，所以我们先来使用buildroot来创建roofs，先下载buildroot的源码：
（github的发布地址是：https://gitlab.com/buildroot.org/buildroot/；官网在：https://buildroot.org/download.html）
curl.exe --fail --location --retry 3 --proxy http://127.0.0.1:7890 --output "G:\linux-study\linux\rk3588-task-e\downloads\buildroot-2026.08.1.tar.xz" https://buildroot.org/downloads/buildroot-2026.08.1.tar.xz

命令下载完成后，回到wsl进行解压到我们的工作区：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
tar -xJf \
  /mnt/g/linux-study/linux/rk3588-task-e/downloads/buildroot-2026.08.1.tar.xz \
  -C /mnt/rk3588-task-e/src &&
cd /mnt/rk3588-task-e/src/buildroot-2026.08.1 &&
grep -n '^export BR2_VERSION' Makefile

然后就要来配置我们的buildroot系统：
目标架构：AArch64 小端//生成 RK3588 可以运行的 64 位程序
工具链：Buildroot toolchain//由 Buildroot 构建与用户空间配套的交叉编译工具链
C 库：musl//提供 BusyBox 等程序需要的基础库，适合小型系统

打开配置菜单，并且先进行一点小设置：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/buildroot-2026.08.1 &&
mkdir -p /mnt/rk3588-task-e/out/buildroot-minimal \
  /mnt/rk3588-task-e/tmp \
  /mnt/rk3588-task-e/cache/buildroot-dl &&
export TMPDIR=/mnt/rk3588-task-e/tmp &&
export BR2_DL_DIR=/mnt/rk3588-task-e/cache/buildroot-dl &&
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin &&
make O=/mnt/rk3588-task-e/out/buildroot-minimal menuconfig

进入蓝色配置行，先进入Target options，进入Target Architecture (i386)里面来选择平台选择AArch64 (little endian)//这里还有个big endian区别是大小端字节序，我们就用小端
现在返回最初页面，然后进入Toolchain，找到C library，进入修改成musl
然后再返回最初页面，选择System configuration，进入修改：
System hostname：zzy-rk3588（板子的主机名）
System banner：Welcome to ZZY RK3588 Linux（登录前显示的欢迎文字）
Root password：1234（串口登录密码）
然后在这一页进入Run a getty (login prompt) after boot，TTY port改成ttyS2
然后返回最主页进入Filesystem images，选择cpio the root filesystem (for use as an initial RAM filesystem)，按空格选中出现子选项，下面：
cpio type选择cpio the whole root filesystem，Compression method选择gzip，Create U-Boot image of the root filesystem不勾选
然后下面的[*] tar the root filesystem按下空格改成不选中
然后保存退出，通过指令查询是否设置正确：
grep -E '^BR2_(aarch64|TOOLCHAIN_BUILDROOT|TOOLCHAIN_BUILDROOT_MUSL|INIT_BUSYBOX|PACKAGE_BUSYBOX|TARGET_GENERIC_HOSTNAME|TARGET_GENERIC_GETTY_PORT|TARGET_GENERIC_GETTY_BAUDRATE|TARGET_ROOTFS_CPIO|TARGET_ROOTFS_CPIO_FULL|TARGET_ROOTFS_CPIO_GZIP|LINUX_KERNEL|TARGET_UBOOT)=' \
  /mnt/rk3588-task-e/out/buildroot-minimal/.config
输出：
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
这些设置意思是：
BR2_aarch64=y：为板子生成 ARM64 小端程序
BR2_TOOLCHAIN_BUILDROOT=y：由 Buildroot 构建交叉编译工具链，用电脑编译板上的程序
BR2_TOOLCHAIN_BUILDROOT_MUSL=y：使用 musl C 库，为程序提供基础运行支持
BR2_TARGET_GENERIC_HOSTNAME="zzy-rk3588"：系统主机名
BR2_INIT_BUSYBOX=y：使用 BusyBox 的 init 管理启动流程、启动登录终端
BR2_PACKAGE_BUSYBOX=y：安装 BusyBox，提供 shell、`ls`、`cat`、`mount` 等基础命令
BR2_TARGET_GENERIC_GETTY_PORT="ttyS2"：在 UART2 对应的终端提供登录提示
BR2_TARGET_GENERIC_GETTY_BAUDRATE="0"：保留内核设置的波特率，不是串口速度为零
BR2_TARGET_ROOTFS_CPIO=y：将根文件系统打包成 cpio
BR2_TARGET_ROOTFS_CPIO_FULL=y：打包整个根文件系统
BR2_TARGET_ROOTFS_CPIO_GZIP=y：使用 gzip 压缩，最终生成 rootfs.cpio.gz

因为现在的这个buildroot只是一个配置目录类型的，所以他在构建的过程中还需要各种下载类似gcc一类的工具，所以还需要解决网络的问题，这个不记录了，反正解决网络连接后，把前面的环境全部复位就开始编译一个mini版本：
set -o pipefail

make O=/mnt/rk3588-task-e/out/buildroot-minimal \
  BR2_DL_DIR=/mnt/rk3588-task-e/cache/buildroot-dl \
  BR2_JLEVEL=4 \
  2>&1 | tee "/mnt/rk3588-task-e/logs/buildroot-minimal-$(date +%Y%m%d-%H%M%S).log"

task_build_status=${PIPESTATUS[0]}
printf 'Buildroot 构建返回码：%s\n' "$task_build_status"

编译完了，我们整理一下启动文件准备打包：
rootdir=/mnt/rk3588-task-e/out/buildroot-minimal/build/buildroot-fs/cpio/target
table='/mnt/rk3588-task-e/out/buildroot-minimal/build/buildroot-fs/full_devices_table.txt'
PATH="/mnt/rk3588-task-e/out/buildroot-minimal/host/bin:/mnt/rk3588-task-e/out/buildroot-minimal/host/sbin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" gzip -9 -c -n /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio > /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz
ln -snf /mnt/rk3588-task-e/out/buildroot-minimal/host/aarch64-buildroot-linux-musl/sysroot /mnt/rk3588-task-e/out/buildroot-minimal/staging
Buildroot 构建返回码：0
zzy@LAPTOP-4S39SRHC:/mnt/rk3588-task-e/src/buildroot-2026.08.1$ ls -lh /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz
-rw-r--r-- 1 zzy zzy 974K Nov  3 22:44 /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz

配置启动配置：
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
（加载指定的内核、设备树和 initramfs
Linux 使用 ttyS2，串口速率为 1500000
启动后执行根文件系统里的 /init
输出较详细的早期启动日志，便于定位问题）

制作64MB的启动镜像：
test ! -e /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img &&
truncate -s 64M /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img &&
mkfs.ext4 -F -L zzy-boot \
  -d /mnt/rk3588-task-e/out/boot-test/files \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img

检查：
ls -lh /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img
debugfs -R 'cat /extlinux/extlinux.conf' \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img

出现了报错，是前面的启动文件整理尚未完成，`out/boot-test` 目录和启动配置都不存在，修复一下，创建目录：
mkdir -p /mnt/rk3588-task-e/out/boot-test/files/rockchip \
  /mnt/rk3588-task-e/out/boot-test/files/extlinux &&
cp /mnt/rk3588-task-e/out/linux-zzy/arch/arm64/boot/Image \
  /mnt/rk3588-task-e/out/boot-test/files/ &&
cp /mnt/rk3588-task-e/out/linux-zzy/arch/arm64/boot/dts/rockchip/rk3588-zzy-minimal.dtb \
  /mnt/rk3588-task-e/out/boot-test/files/rockchip/ &&
cp /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz \
  /mnt/rk3588-task-e/out/boot-test/files/

写入启动配置：
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

重新创建镜像：
test ! -e /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img &&
truncate -s 64M /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img &&
mkfs.ext4 -F -L zzy-boot \
  -d /mnt/rk3588-task-e/out/boot-test/files \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img

再重新检查：
ls -lh /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img
debugfs -R 'cat /extlinux/extlinux.conf' \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img

这次没有问题，重新导出：
mkdir -p /mnt/g/linux-study/linux/rk3588-task-e/artifacts &&
cp --sparse=always \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/linux-boot-test.img &&
cmp \
  /mnt/rk3588-task-e/out/boot-test/linux-boot-test.img \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/linux-boot-test.img &&
echo "启动镜像导出并校验通过"

文件在：G:\linux-study\linux\rk3588-task-e\artifacts\linux-boot-test.img
现在来烧录，这次需要烧录boot，还是勾选boot，然后选择emmc，改地址是0x00008000，然后选中我们的镜像路径，勾选强制按地址写，然后烧录（这次板子不知道为什么不能识别我的usb，所以只能uboot串口发送rockusb 0 mmc 0，进入的烧录）
现在出现了大问题，没办法烧录，报错找不到flash，因为我们的uboot里面的烧录流程不是使用的瑞芯微的那一套，所以需要使用临时初始化uboot来烧录
先命令：
cd /mnt/rk3588-task-e/src/rkbin-3e288fe &&
./tools/boot_merger RKBOOT/RK3588MINIALL.ini
（创建文件按这份配置组合组件）

确认生成结果导出到G盘：
ls -lh /mnt/rk3588-task-e/src/rkbin-3e288fe/rk3588_loader_v1.24.114.bin &&
mkdir -p /mnt/g/linux-study/linux/rk3588-task-e/artifacts &&
cp /mnt/rk3588-task-e/src/rkbin-3e288fe/rk3588_loader_v1.24.114.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/ &&
cmp /mnt/rk3588-task-e/src/rkbin-3e288fe/rk3588_loader_v1.24.114.bin \
  /mnt/g/linux-study/linux/rk3588-task-e/artifacts/rk3588_loader_v1.24.114.bin &&
echo "下载 Loader 导出并校验通过"

就生成到了G:\linux-study\linux\rk3588-task-e\artifacts\rk3588_loader_v1.24.114.bin
（其实前面的按音量键进入烧录有问题，那都是原来系统特制的，真正的板子复位是按着UPDATE重启，这次成功直接进入了MASKROM，但是还是不能烧录原来的东西，继续尝试新方案）
我们现在就是要给现在的板子烧录一个临时的loader接管我们的烧录，把这个单独的临时loader烧录进去，在烧录界面只勾选第一行的loader，然后烧录地址和位置等都不变，选中我们刚才生成的文件，烧录进去
下一步把前面的loader和uboot还有boot一起勾选，一起烧录，三部分的功能是：
Loader：建立 USB 下载和 eMMC 访问通道；工具还会处理 IDB。
Uboot：恢复我们自己编译、已验证过的启动链。
Boot：写入 Linux 内核、设备树、BusyBox 根文件系统和启动配置。

这次成功的烧录了，下一步就是验证 U-Boot 能否加载 Linux，并进入最小系统的串口登录界面，现在正常重启，但是只能进入uboot的空间，也就是uboot不能成功的找到我们的linux内核
此处省略一些检查的命令，只解释我们的结论：
ext4ls mmc 0:3 /：能看到内核、设备树目录、根文件系统-Boot 分区位置正确，ext4 可以读取
查看 /extlinux/：存在 extlinux.conf-启动配置没有漏打包
查看启动环境变量：目标是 mmc0，方法是 extlinux-基本启动设置存在
指定 mmc0:3 扫描：仍然没有启动项-仅指定分区不足以解决
bootdev list、`bootmeth list`：有 eMMC 启动设备和 extlinux 方法-不是这两项完全缺失

所以我们只能手动启动里面的这个最小设备，绕过了自动扫描和 extlinux 配置解析，手动完成它们原本应当做的事情
用 ext4load 把 Image 从 eMMC 读入内存，把设备树读入另一块内存，把 rootfs.cpio.gz 读入内存，并记录文件大小，设置启动参数，指定串口和初始程序 /init
使用手动烧录终于成功的打开了我们创建的最小系统，但是现在是手动开启的，没有全自动的启动流程，所以关机重启就没有这个功能了
但是现在这个的成功登录成功的验证了内核和根文件系统能在这块板上运行，后面就在这套成果上补齐自动启动、Wi-Fi 和 SSH 就算完工了

现在来尝试补齐这三个，现在有了最小的系统了，现在就开始下一步，使用命令检查一下wifi：
cat /sys/bus/usb/devices/2-1/idVendor
cat /sys/bus/usb/devices/2-1/idProduct
cat /sys/bus/usb/devices/2-1/product

这就确认了板载 Wi-Fi 的驱动方向：RTL8733BU，实际设备 ID 是 0bda:b733，与原理图信息一致核对了候选驱动源码，它的 RTL8733B 设备表明确包含 b733，下一步要让无线驱动绑定它，才会出现可用的无线网络接口，我们采用github的 rtl8733bu 源码（https://github.com/wirenboard/rtl8733bu），先windous下载驱动源码：
curl.exe --fail --location --retry 3 --proxy http://127.0.0.1:7890 --output "G:\linux-study\linux\rk3588-task-e\downloads\rtl8733bu-2d9048be.tar.gz" https://codeload.github.com/wirenboard/rtl8733bu/tar.gz/2d9048be60759206b8db5e2370420333ef0b8478

然后在wsl：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
tar -xzf \
  /mnt/g/linux-study/linux/rk3588-task-e/downloads/rtl8733bu-2d9048be.tar.gz \
  -C /mnt/rk3588-task-e/src
（确认工作磁盘挂载正确，再把源码解压到其中）

进入源码目录，确认无线芯片配置：
cd /mnt/rk3588-task-e/src/rtl8733bu-2d9048be60759206b8db5e2370420333ef0b8478 &&
grep -nE '^CONFIG_(RTL8733B|USB_HCI|PLATFORM_WB7)[[:space:]]*=' Makefile
输出：
60:CONFIG_RTL8733B = y
62:CONFIG_USB_HCI = y
152:CONFIG_PLATFORM_WB7 = y

这三项正确，驱动源码已经就位，开始编译 Wi-Fi 驱动前，还需要补齐内核的模块构建结果：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/linux-7.3.1 &&
export TMPDIR=/mnt/rk3588-task-e/tmp &&
make O=/mnt/rk3588-task-e/out/linux-zzy \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 modules

结束检查：
echo "内核模块编译返回码：$?"
ls -lh /mnt/rk3588-task-e/out/linux-zzy/Module.symvers
输出：
内核模块编译返回码：0
-rw-r--r-- 1 zzy zzy 820K Nov  4 22:10 /mnt/rk3588-task-e/out/linux-zzy/Module.symvers

说明没有问题了，可以开始编译 Wi-Fi 驱动：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
cd /mnt/rk3588-task-e/src/rtl8733bu-2d9048be60759206b8db5e2370420333ef0b8478 &&
export TMPDIR=/mnt/rk3588-task-e/tmp &&
make -j4 \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KVER=7.3.1-zzy-minimal \
  KSRC=/mnt/rk3588-task-e/out/linux-zzy \
  M="$PWD" \
  modules 2>&1 | tee /mnt/rk3588-task-e/logs/rtl8733bu-build-first.log

task_wifi_status=${PIPESTATUS[0]}
echo "Wi-Fi 驱动编译返回码：$task_wifi_status"

报错了，旧驱动传递编译参数的方式与 Linux 7.3.1 不兼容，开始修改：
cd /mnt/rk3588-task-e/src/rtl8733bu-2d9048be60759206b8db5e2370420333ef0b8478 &&
cp --update=none Makefile Makefile.before-linux7
然后：
grep -Fqx 'ccflags-y += $(EXTRA_CFLAGS)' Makefile ||
printf '\n# Pass legacy driver flags to current Kbuild\nccflags-y += $(EXTRA_CFLAGS)\n' >> Makefile
（将驱动已有的头文件路径和功能宏传给新构建系统）

重新编译，并保存新的日志：
export TMPDIR=/mnt/rk3588-task-e/tmp

make -j4 \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KVER=7.3.1-zzy-minimal \
  KSRC=/mnt/rk3588-task-e/out/linux-zzy \
  M="$PWD" \
  modules 2>&1 | tee /mnt/rk3588-task-e/logs/rtl8733bu-build-second.log

task_wifi_status=${PIPESTATUS[0]}
echo "Wi-Fi 驱动编译返回码：$task_wifi_status"

还有点问题，继续修改：
sed -i 's|-I$(src)/|-I$(abspath $(src))/|g' Makefile
然后：
grep -nF -- '-I$(abspath $(src))/include' Makefile
（把驱动的头文件路径明确写成绝对路径，避免被这段逻辑误转换）

再次编译：
make -j4 \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  KVER=7.3.1-zzy-minimal \
  KSRC=/mnt/rk3588-task-e/out/linux-zzy \
  M="$PWD" \
  modules 2>&1 | tee /mnt/rk3588-task-e/logs/rtl8733bu-build-third.log

task_wifi_status=${PIPESTATUS[0]}
echo "Wi-Fi 驱动编译返回码：$task_wifi_status"

还是有问题，在当前 WSL 驱动目录中，备份并修改这个头文件：
cp --update=none \
  include/osdep_service_linux.h \
  include/osdep_service_linux.h.before-linux7 &&
sed -i \
  -e 's/\bfrom_timer(/timer_container_of(/g' \
  -e 's/\bdel_timer_sync(/timer_delete_sync(/g' \
  -e 's/\bdel_timer(/timer_delete(/g' \
  include/osdep_service_linux.h

检查：
grep -nE 'timer_container_of|timer_delete_sync|timer_delete\(' \
  include/osdep_service_linux.h
输出：
371:    _timer *ptimer = timer_container_of(ptimer, in_timer, timer);
400:    *bcancelled = timer_delete_sync(&ptimer->timer) == 1 ? 1 : 0;
405:    timer_delete(&ptimer->timer);

继续编译还是有问题，直接用ai批量检查源码修改，直接跳过这个驱动的修改，ai直接改好了，我们下一步，把这个驱动放到我们的buildroot里面：
test "$(findmnt -n -o UUID --mountpoint /mnt/rk3588-task-e)" = \
  "f0c827b8-89bf-4b52-b620-d6b51c803569" &&
mkdir -p /mnt/rk3588-task-e/configs/rootfs-overlay/lib/modules/7.3.1-zzy-minimal/extra &&
install -m 644 \
  /mnt/rk3588-task-e/src/rtl8733bu-2d9048be60759206b8db5e2370420333ef0b8478/8733bu.ko \
  /mnt/rk3588-task-e/configs/rootfs-overlay/lib/modules/7.3.1-zzy-minimal/extra/
然后告诉 Buildroot 使用这个 overlay：
cd /mnt/rk3588-task-e/src/buildroot-2026.08.1 &&
./utils/config \
  --file /mnt/rk3588-task-e/out/buildroot-minimal/.config \
  --set-str BR2_ROOTFS_OVERLAY \
  "/mnt/rk3588-task-e/configs/rootfs-overlay" &&
make O=/mnt/rk3588-task-e/out/buildroot-minimal olddefconfig
增量构建根文件系统：
export TMPDIR=/mnt/rk3588-task-e/tmp

make O=/mnt/rk3588-task-e/out/buildroot-minimal \
  BR2_DL_DIR=/mnt/rk3588-task-e/cache/buildroot-dl \
  BR2_JLEVEL=4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/buildroot-with-wifi-module.log

task_rootfs_status=${PIPESTATUS[0]}
echo "根文件系统构建返回码：$task_rootfs_status"

有点环境问题，改一下下：
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export TMPDIR=/mnt/rk3588-task-e/tmp

重新运行构建：
make O=/mnt/rk3588-task-e/out/buildroot-minimal \
  BR2_DL_DIR=/mnt/rk3588-task-e/cache/buildroot-dl \
  BR2_JLEVEL=4 \
  2>&1 | tee /mnt/rk3588-task-e/logs/buildroot-with-wifi-module.log

task_rootfs_status=${PIPESTATUS[0]}
echo "根文件系统构建返回码：$task_rootfs_status"

返回码为 0 后，再检查镜像内容：
ls -lh /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz
gzip -dc /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz \
  | cpio -t 2>/dev/null \
  | grep '8733bu.ko'
输出：
-rw-r--r-- 1 zzy zzy 1.9M Nov  4 22:39 /mnt/rk3588-task-e/out/buildroot-minimal/images/rootfs.cpio.gz
lib/modules/7.3.1-zzy-minimal/extra/8733bu.ko

驱动已经打进新 rootfs，结果正确，本来应该此时上板验证的，但是太慢了，我们直接把3个部分一起完成再来上板验证，直接省略大量的命令行操作，说内容：
U-Boot自启动固定，改成开机直接从 eMMC 第 3 分区读取 Image 和设备树，再读取第 8 分区的 UUID，传给 Linux，最后执行 booti
linux的内核和设备树直接沿用上一版本
补齐连接 Wi-Fi 和 SSH 所需的软件：
  iw：查看无线设备、扫描等诊断
  wpa_supplicant：与热点完成认证和连接
  wpa_cli、`wpa_passphrase`：在串口配置热点、生成密码配置
  BusyBox udhcpc：向路由器获取 IP、网关和 DNS
  wireless-regdb：补上此前缺少的无线监管数据库
  Dropbear：提供 SSH 登录服务
开机服务：把这些程序按顺序运行起来，新增 S41wifi 启动脚本：加载驱动 → 等待 wlan0 → 启动无线连接程序 → 在后台请求 IP
根文件系统：从“内存试运行”改成“eMMC 持久保存”，让 Buildroot 另外生成一个 256 MiB 的 ext4 rootfs 镜像，准备写入现有 rootfs 分区；Linux 直接以它作为 /

现在我们来烧录ai编译完的新镜像，所有文件都在G:\linux-study\linux\rk3588-task-e\artifacts\integrated-wifi-ssh，烧录文件：
Loader	0x00000000	rk3588_loader_v1.24.114.bin
Uboot	0x00000040	u-boot-rockchip.bin
Boot	0x00008000	boot-integrated.img
Rootfs（可以使用 System 行）	0x008B8000	rootfs-integrated.ext4

烧录完成后直接串口进入，输入root登录用户，输入密码，然后用指令检测：uname -r、mount | grep ' on / '、lsmod、ip link show wlan0、dmesg | tail -n 80
检查没有问题，尝试扫描连接wifi，使用指令连接wifi，连接成功，手动保存到emmc，保证下次开机可以直接连接，然后通过ip，成功连接到ssh，完成我们的阶段成功