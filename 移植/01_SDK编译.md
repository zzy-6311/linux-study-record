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
