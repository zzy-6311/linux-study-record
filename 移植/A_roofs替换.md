（原本的一些环境配置都在文档B里面做过了，是先做的B在做的A，所以一些环境就不配置了）
目的：保留现有 U-Boot、Linux 6.12 内核和设备树，自己制作 Debian 13（trixie）ARM64 根文件系统
首先，我们直接用正点官方的debain包来解包制作就没很大的学习的意思了，所以我们来使用debootstrap和qemu-aarch64-static自己从头制作一个roofs包
（debootstrap 用于下载并生成 Debian 最小根文件系统，qemu-aarch64-static 用于在 x86_64 电脑上执行 ARM64 程序，后面配置 Debian 时需要它）
还是先打开wsl，挂载我的虚拟盘使用指令：
wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare（这一步需要管理员权限）
wsl
配置一下环境：
（检查文件环境）
lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINTS
findmnt -T /mnt/rk3588
df -h /mnt/rk3588
（检查工具版本）//这两个包在SDK编译的时候就安装了，也是SDK需要用来配置roofs
command -v debootstrap
debootstrap --version
qemu-aarch64-static --version
（创建工作目录）
mkdir -p /mnt/rk3588/debian-rk3588
cd /mnt/rk3588/debian-rk3588

下面开始第一步的制作：
sudo debootstrap \
  --arch=arm64 \
  --foreign \
  trixie \
  rootfs \
  https://deb.debian.org/debian

/*
--arch=arm64：目标系统是 RK3588 使用的 ARM64，不是电脑的 x86_64；
--foreign：只进行第一阶段，在 x86_64 主机上下载并解包 ARM64 软件；
trixie：Debian 13；
rootfs：生成到当前目录下的 rootfs/；
最后的地址：Debian 官方软件源。
*/

把 QEMU 放入 rootfs：（这样 chroot 进入 ARM64 根文件系统后，仍然能找到模拟器）
cd /mnt/rk3588/debian-rk3588
sudo cp /usr/bin/qemu-aarch64-static rootfs/usr/bin/

然后配置刚才解包的 Debian 软件包：
sudo chroot rootfs \
  /usr/bin/qemu-aarch64-static -L / \
  /bin/sh /debootstrap/debootstrap --second-stage

解包结束显示：
I: Configuring ifupdown...
I: Configuring libc-bin...
I: Configuring ca-certificates...
I: Base system installed successfully.