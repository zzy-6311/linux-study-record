从正点的官方SDK编译一个RK3588的linux系统
使用wsl2里面的ubuntu24来完成这个任务

首先是解压这个SDK包：tar -xzf "/mnt/h/linux/rk3588/atk_dlrk3588_linux6.12_sdk_release_v1.1.0_20260921.tar.gz" -C /mnt/h/rk3588-sdk
这个时候解压到了/mnt/h/rk3588-sdk（这个文件格式是wsl对windous的文件记录格式）
这个时候的文件下面的目录是:
zzy@LAPTOP-4S39SRHC:/mnt/g$ ls -la /mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12
total 0
drwxrwxrwx 1 zzy zzy 4096 Sep 22 15:28 .
drwxrwxrwx 1 zzy zzy 4096 Oct 24 20:34 ..
drwxrwxrwx 1 zzy zzy 4096 Sep 22 15:22 .repo
-rwxrwxrwx 1 zzy zzy  542 Sep 21 18:10 README.md
drwxrwxrwx 1 zzy zzy 4096 Sep 21 18:00 buildroot
drwxrwxrwx 1 zzy zzy 4096 Sep 21 18:01 debian
-rwxrwxrwx 1 zzy zzy   81 Aug 20 11:58 repo.sh
drwxrwxrwx 1 zzy zzy 4096 Sep 21 18:09 ubuntu
-rwxrwxrwx 1 zzy zzy  423 Sep 21 18:10 使用说明.md

然后运行.repo/repo/repo sync -l -j2进行git仓库检出，检出里面的包含代码（从 SDK 自带的 .repo 仓库本地检出源码，会在当前 SDK 目录写入内核、U-Boot 等源码）
运行需要python（wsl里面需要安装一下）
现在的文件目录是：
zzy@LAPTOP-4S39SRHC:/mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12$ ls -la
total 0
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:52 .
drwxrwxrwx 1 zzy zzy 4096 Oct 24 20:34 ..
drwxrwxrwx 1 zzy zzy 4096 Sep 22 15:22 .repo
lrwxrwxrwx 1 zzy zzy   12 Oct 24 21:21 Copyright_Statement.md -> docs/LICENSE
lrwxrwxrwx 1 zzy zzy   31 Oct 24 21:20 Makefile -> device/rockchip/common/Makefile
-rwxrwxrwx 1 zzy zzy  542 Sep 21 18:10 README.md
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:12 app
lrwxrwxrwx 1 zzy zzy   39 Oct 24 21:20 build.sh -> device/rockchip/common/scripts/build.sh
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:19 buildroot
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:19 debian
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:19 device
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:21 docs
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:28 external
lrwxrwxrwx 1 zzy zzy   11 Oct 24 21:38 kernel -> kernel-6.12
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:38 kernel-6.12
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:38 prebuilts
-rwxrwxrwx 1 zzy zzy   81 Aug 20 11:58 repo.sh
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:42 rkbin
lrwxrwxrwx 1 zzy zzy   41 Oct 24 21:20 rkflash.sh -> device/rockchip/common/scripts/rkflash.sh
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:43 tools
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:51 u-boot
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:52 ubuntu
drwxrwxrwx 1 zzy zzy 4096 Oct 24 21:52 yocto
-rwxrwxrwx 1 zzy zzy  423 Sep 21 18:10 使用说明.md

现在这些目录分别是：
kernel-6.12：Linux 6.12 内核源码；
kernel -> kernel-6.12：指向内核源码的快捷方式；
u-boot：U-Boot 启动程序源码；
rkbin：Rockchip 的启动相关二进制固件；
device：板级配置，决定开发板、显示接口、内核和 rootfs 类型；
buildroot、ubuntu、debian、yocto：不同根文件系统的构建体系；
external、prebuilts、tools：外部软件包、预编译工具和构建工具；
build.sh：SDK 的统一编译入口；
rkflash.sh：烧录相关脚本

下一步就是运行./build.sh lunch，不能直接运行./build.sh，因为直接运行默认运行编译all，但是我们要学则一下编译什么，所以先配置
这个时候出现了wsl环境不能支持，他需要编译在ext4 / XFS / Btrfs / F2FS / ZFS 等 Linux 文件系统，所以我们创建一个虚拟盘
使用powershell打开diskpart，create vdisk file="H:\wsl\rk3588-ext4.vhdx" maximum=153600 type=expandable，创建一个150G的虚拟盘，再挂载到wsl
这里需要管理员权限使用管理员powershell运行wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare，成功后（可以运行lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT检查，没问题下一步）
在刚才的虚拟盘上创建文件系统sudo mkfs.ext4 /dev/sde，运行lsblk -f /dev/sde检查：
zzy@LAPTOP-4S39SRHC:/mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12$ lsblk -f /dev/sde
NAME
    FSTYPE FSVER LABEL UUID                                 FSAVAIL FSUSE% MOUNTPOINTS
sde ext4   1.0         68f2291e-d9f7-4c41-80c6-0d0511aa6338

使用命令把刚才创建的虚拟盘，挂载到wsl里面作为磁盘设备，并且更改权限和所属人员，并且检查是否成功如下：
zzy@LAPTOP-4S39SRHC:/mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12$ sudo mkdir -p /mnt/rk3588
zzy@LAPTOP-4S39SRHC:/mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12$ sudo mount /dev/sde /mnt/rk3588
zzy@LAPTOP-4S39SRHC:/mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12$ sudo chown zzy:zzy /mnt/rk3588
zzy@LAPTOP-4S39SRHC:/mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12$ df -hT /mnt/rk3588
findmnt -T /mnt/rk3588
Filesystem     Type  Size  Used Avail Use% Mounted on
/dev/sde       ext4  147G   28K  140G   1% /mnt/rk3588
TARGET      SOURCE   FSTYPE OPTIONS
/mnt/rk3588 /dev/sde ext4   rw,relatime

现在盘没有问题后，我们就在wsl解决了上面那个文件结构不支持的问题，现在就可以继续上面的包编译来做了（后面可能还有编译环境不支持等问题，问题还多呢）
使用mv /mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12 /mnt/rk3588/把代码移动到wsl下面挂载的虚拟盘（移动了2个小时）（检查没有问题后下一步）
继续运行./build.sh lunch，这次不弹出，得到屏幕选项：
Log saved at /mnt/rk3588/atk_dlrk3588_linux6.12/output/sessions/2026-10-25_10-26-33
Pick a defconfig:

1. 01_atk_dlrk3588_auto2mipi_2hdmi_defconfig
2. 02_atk_dlrk3588_2mipi720x1280_2hdmi_defconfig
3. 03_atk_dlrk3588_2mipi800x1280_2hdmi_defconfig
4. 04_atk_dlrk3588_2mipi1080x1920_2hdmi_defconfig
5. 05_atk_dlrk3588_2dp_2hdmi_defconfig
6. 06_atk_dlrk3588_ubuntu_panthor_auto2mipi_2hdmi_defconfig
7. 07_atk_dlrk3588_ubuntu_panthor_2dp_2hdmi_defconfig
Which would you like? [1]:

我们选择7，前面5个都是buildroot的包，只有6、7是ubuntu的包，输入7回车，这样就把编译的参数配置好了，最后显示：
make[1]: Leaving directory '/mnt/rk3588/atk_dlrk3588_linux6.12/device/rockchip/common/kconfig'
#
# configuration written to /mnt/rk3588/atk_dlrk3588_linux6.12/output/.config    //（表示配置已正式保存到 output/.config）
#
Using current kernel version(6.12)  //（表示 SDK 确认使用 Linux 6.12 内核）

然后就使用编译命令./build.sh，在zzy（也就是我自己）的用户名下面开始编译
编译退出了，报错：check-kernel.sh: line 93: lz4: command not found（WSL 里没有安装 lz4）
安装lz4：sudo apt update && sudo apt install -y lz4，安装后版本检查：
zzy@LAPTOP-4S39SRHC:/mnt/rk3588/atk_dlrk3588_linux6.12$ lz4 --version
*** LZ4 command line interface 64-bits v1.9.4, by Yann Collet ***

然后我们继续编译，其实上面已经看到了：
Platform RK3588 is build OK
Running mk-loader.sh - build_uboot succeeded
说明U-Boot 和 loader 已经成功编译打包，至少我们这个板子的基础启动完成了，主要是内核的编译
然后又出问题了，依旧是包找不到，因为我们使用的并不是正点自己打包的这种编译环境，所以很容易出现这种问题，但是每一次出错改一次很麻烦，所以尝试一次性安装全部的依赖
但是正点的包没有直接提供依赖表，所以我们根据它的检查项，一次性批量安装主要依赖：
sudo apt install -y \
  build-essential gcc g++ make cmake git repo \
  python3 python-is-python3 python3-pip python3-setuptools python3-wheel \
  flex bison bc rsync lz4 zstd xz-utils cpio unzip zip curl wget \
  device-tree-compiler u-boot-tools clang-format \
  libssl-dev libgmp-dev libmpc-dev libncurses-dev libelf-dev \
  dwarves swig pkg-config \
  binfmt-support e2fsprogs dosfstools mtools parted fdisk \
  sudo ca-certificates

再安装SDK自带的QEMU和debootstrap包：
sudo apt install -y \
  /mnt/rk3588/atk_dlrk3588_linux6.12/ubuntu/ubuntu-build-service/packages/qemu-user-static_*.deb \
  /mnt/rk3588/atk_dlrk3588_linux6.12/ubuntu/ubuntu-build-service/packages/debootstrap_*.deb

安装完成后我们重新编译./build.sh，发现还是有问题U-Boot 和 Linux 内核都已经编译成功，boot.img 已生成，失败发生在 Ubuntu rootfs 的 chroot 阶段
所以我们改用 Ubuntu 仓库的 QEMU：sudo apt install -y qemu-user-static，然后继续./build.sh
但是还是有报错，失败点换成了 recovery（恢复分区），缺少一个软件包，安装软件包：sudo apt install -y gettext，然后再继续
又出现了一个报错是因为Buildroot 硬性要求 PATH 中不能有带空格的条目，所以/mnt/c/Program Files/这种不能运行，执行单次修复：
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v ' ' | tr '\n' ':' | sed 's/:$//')
echo "$PATH"
然后继续./build.sh，这次没有再报错，编译成功，最后一段：
Make firmware OK!
------ OK ------
********rkImageMaker ver 2.29********
Generating new image, please wait...
Writing head info...
Writing boot file...
Writing firmware...
Generating MD5 data...
MD5 data generated successfully!
New image generated successfully!

Run 'make edit-package-file' if you want to modify the package-file.

Running mk-updateimg.sh - build_updateimg succeeded.
Images under /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/ are ready!
Running mk-firmware.sh - build_firmware succeeded.
Running 99-all.sh - build_all succeeded.

通过指令ls -lh /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/，可以看到这些文件的指向路径是什么：
zzy@LAPTOP-4S39SRHC:/mnt/rk3588/atk_dlrk3588_linux6.12$ ls -lh /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/
total 0
lrwxrwxrwx 1 zzy zzy 40 Oct 25 12:02 MiniLoaderAll.bin -> ../../u-boot/rk3588_loader_v1.24.100.bin
lrwxrwxrwx 1 zzy zzy 26 Oct 25 12:03 boot.img -> ../../kernel-6.12/boot.img
lrwxrwxrwx 1 zzy zzy 11 Oct 25 12:02 misc.img -> ../misc.img
lrwxrwxrwx 1 zzy zzy 22 Oct 25 13:07 oem.img -> ../extra-parts/oem.img
lrwxrwxrwx 1 zzy zzy 56 Oct 25 13:07 parameter.txt -> ../../device/rockchip/.chips/rk3588/parameter-ubuntu.txt
lrwxrwxrwx 1 zzy zzy 23 Oct 25 13:07 recovery.img -> ../recovery/ramboot.img
lrwxrwxrwx 1 zzy zzy 32 Oct 25 13:07 rootfs.img -> ../../ubuntu/rockchip-rootfs.img
lrwxrwxrwx 1 zzy zzy 22 Oct 25 12:02 uboot.img -> ../../u-boot/uboot.img
lrwxrwxrwx 1 zzy zzy 26 Oct 25 13:07 update.img -> ../update/Image/update.img
lrwxrwxrwx 1 zzy zzy 27 Oct 25 13:07 userdata.img -> ../extra-parts/userdata.img

通过指令du -shL /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/，看文件大小
zzy@LAPTOP-4S39SRHC:/mnt/rk3588/atk_dlrk3588_linux6.12$ du -shL /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/
6.2G    /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/

发现updata.img文件时6.2G，确实是正常大小，现在拷贝出来做烧录，用cp /mnt/rk3588/atk_dlrk3588_linux6.12/output/update/Image/update.img "/mnt/d/linux/sdk编译包/atk_dlrk3588_ubuntu26_update.img"拷贝，再用md5sum /mnt/rk3588/atk_dlrk3588_linux6.12/output/update/Image/update.img "/mnt/d/linux/sdk编译包/atk_dlrk3588_ubuntu26_update.img"校验拷贝完整性，两个校验码一样就可以了
然后就是烧录了
