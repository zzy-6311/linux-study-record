# RK3588 Linux SDK 编译记录（正点原子 ATK-DLRK3588 · Linux 6.12）

> **目标**：从正点原子官方 SDK 完整编译出一个可烧录的 RK3588 Ubuntu 系统镜像
> **环境**：Windows 11 + WSL2（Ubuntu 24.04）
> **SDK**：`atk_dlrk3588_linux6.12_sdk_release_v1.1.0_20260921.tar.gz`

---

## 📋 目录

- [0. 踩坑总览（先看这个）](#0-踩坑总览先看这个)
- [1. 解压 SDK](#1-解压-sdk)
- [2. 用 repo 检出源码](#2-用-repo-检出源码)
- [3. 目录结构说明](#3-目录结构说明)
- [4. 问题①：WSL 的 /mnt/h 不是 Linux 文件系统](#4-问题wsl-的-mnth-不是-linux-文件系统)
- [5. 配置编译目标（lunch）](#5-配置编译目标lunch)
- [6. 问题②：缺 lz4](#6-问题缺-lz4)
- [7. 一次性补装全部依赖](#7-一次性补装全部依赖)
- [8. 问题③：QEMU / debootstrap](#8-问题qemu--debootstrap)
- [9. 问题④：recovery 缺 gettext](#9-问题recovery-缺-gettext)
- [10. 问题⑤：PATH 里有空格](#10-问题path-里有空格)
- [11. 编译成功](#11-编译成功)
- [12. 产物说明](#12-产物说明)
- [13. 拷贝与校验](#13-拷贝与校验)
- [附录：完整命令速查](#附录完整命令速查)

---

## 0. 踩坑总览（先看这个）

| # | 报错 / 现象 | 根因 | 解决 |
|---|---|---|---|
| ① | `build.sh` 提示文件系统不支持 | `/mnt/h` 是 **drvfs**（Windows 分区），不是 Linux 原生文件系统 | 创建 ext4 虚拟盘并迁过去 |
| ② | `check-kernel.sh: line 93: lz4: command not found` | WSL 里没装 `lz4` | `apt install lz4` |
| ③ | QEMU / debootstrap 阶段失败 | SDK 自带的 `qemu-user-static` 不好用 | 换成 Ubuntu 仓库版 |
| ④ | recovery（恢复分区）阶段失败 | 缺 `gettext` | `apt install gettext` |
| ⑤ | Buildroot 报 PATH 里有空格 | `/mnt/c/Program Files/...` 被混进 PATH | 过滤掉含空格的 PATH 条目 |

---

## 1. 解压 SDK

```bash
# 先建好目标目录
mkdir -p /mnt/h/rk3588-sdk

tar -xzf "/mnt/h/linux/rk3588/atk_dlrk3588_linux6.12_sdk_release_v1.1.0_20260921.tar.gz" \
     -C /mnt/h/rk3588-sdk
```

> 💡 `/mnt/h` 是 WSL 通过 **drvfs** 挂载的 Windows H 盘 —— 现在只是**先解压**在这里，后面会迁到 Linux 原生文件系统（见第 4 节）。

解压后的目录：

```text
/mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12
├── .repo
├── README.md
├── buildroot
├── debian
├── repo.sh
├── ubuntu
└── 使用说明.md
```

---

## 2. 用 repo 检出源码

```bash
cd /mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12
.repo/repo/repo sync -l -j2
```

- `-l` = **local**，从 SDK **自带的 `.repo` 仓库**本地检出（不联网）
- `-j2` = 2 个并发任务
- 会在当前目录写入 **kernel、U-Boot** 等源码

> ⚠️ **repo 是 Python 脚本** —— WSL 里需要先装好 Python（`apt install python3`）

检出完成后的目录：

```text
/mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12
├── .repo
├── Copyright_Statement.md -> docs/LICENSE
├── Makefile            -> device/rockchip/common/Makefile
├── README.md
├── app
├── build.sh            -> device/rockchip/common/scripts/build.sh
├── buildroot
├── debian
├── device
├── docs
├── external
├── kernel              -> kernel-6.12
├── kernel-6.12
├── prebuilts
├── repo.sh
├── rkbin
├── rkflash.sh          -> device/rockchip/common/scripts/rkflash.sh
├── tools
├── u-boot
├── ubuntu
├── yocto
└── 使用说明.md
```

---

## 3. 目录结构说明

| 目录 / 文件 | 作用 |
|---|---|
| `kernel-6.12` | Linux 6.12 内核源码 |
| `kernel` → `kernel-6.12` | 指向内核源码的软链接 |
| `u-boot` | U-Boot 启动程序源码 |
| `rkbin` | Rockchip 启动相关的二进制固件 |
| `device` | 板级配置：决定开发板、显示接口、内核与 rootfs 类型 |
| `buildroot` / `ubuntu` / `debian` / `yocto` | 四种不同的根文件系统构建体系 |
| `external` / `prebuilts` / `tools` | 外部软件包 / 预编译工具 / 构建工具 |
| `build.sh` | SDK 的**统一编译入口** |
| `rkflash.sh` | 烧录相关脚本 |

---

## 4. 问题①：WSL 的 `/mnt/h` 不是 Linux 文件系统

### 现象

直接运行 `./build.sh lunch` 时提示环境不支持 —— SDK 要求代码位于 **ext4 / XFS / Btrfs / F2FS / ZFS** 等 Linux 原生文件系统上。

### 原因

`/mnt/h` 是 WSL 用 **drvfs** 挂载的 Windows 分区，**不支持 Linux 的 POSIX 权限、符号链接、硬链接等特性**，所以 SDK 的脚本会拒绝在这种目录下编译。

### 解决：创建一个 ext4 虚拟盘

**① 在 Windows 侧创建 150 GB 动态虚拟盘**（用管理员权限打开 PowerShell）：

```text
diskpart
create vdisk file="H:\wsl\rk3588-ext4.vhdx" maximum=153600 type=expandable
exit
```

> `maximum=153600` 单位是 MB，即 **150 GiB**；`type=expandable` 表示动态扩展，不占满 150 G。

**② 把虚拟盘挂进 WSL**（同样要**管理员权限**）：

```powershell
wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare
```

**③ 在 WSL 里确认磁盘设备名**：

```bash
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT
```

> ⚠️ **关键**：下面 `mkfs` 用的 `/dev/sdX` **不是固定的**（本例是 `sde`，换台机器可能是 `sdc`/`sdf`）。
> **一定要用上一步 `lsblk` 看到的设备名**，写错会格式化错盘！

**④ 格式化成 ext4**：

```bash
sudo mkfs.ext4 /dev/sde

# 检查
lsblk -f /dev/sde
```

```text
NAME FSTYPE FSVER LABEL UUID                                 FSAVAIL FSUSE% MOUNTPOINTS
sde  ext4   1.0         68f2291e-d9f7-4c41-80c6-0d0511aa6338
```

**⑤ 挂载并改权限**：

```bash
sudo mkdir -p /mnt/rk3588
sudo mount /dev/sde /mnt/rk3588
sudo chown zzy:zzy /mnt/rk3588

# 检查
df -hT /mnt/rk3588
findmnt -T /mnt/rk3588
```

```text
Filesystem     Type  Size  Used Avail Use% Mounted on
/dev/sde       ext4  147G   28K  140G   1% /mnt/rk3588

TARGET      SOURCE   FSTYPE OPTIONS
/mnt/rk3588 /dev/sde ext4   rw,relatime
```

**⑥ 把 SDK 迁移到 ext4**：

```bash
mv /mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12 /mnt/rk3588/
```

> ⏱️ **这一步要跑约 2 小时** —— 因为 `mv` 跨文件系统时实际是「**复制 + 删除**」（drvfs → ext4）。
> 之后所有操作都在 `/mnt/rk3588/atk_dlrk3588_linux6.12` 下进行。

---

## 5. 配置编译目标（lunch）

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12
./build.sh lunch
```

> 💡 **为什么不直接 `./build.sh`？**
> 不带参数会默认编译 `all`（全部配置）。我们只需要**特定板型**，所以先 `lunch` 选配置。

屏幕会列出可选配置：

```text
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
```

| 选项 | 根文件系统 |
|---|---|
| 1–5 | **Buildroot** |
| **6–7** | **Ubuntu** ← 需要 Ubuntu 就选这两个 |

本例输入 **`7`** 回车，配置完成：

```text
make[1]: Leaving directory '/mnt/rk3588/atk_dlrk3588_linux6.12/device/rockchip/common/kconfig'
#
# configuration written to /mnt/rk3588/atk_dlrk3588_linux6.12/output/.config
#
Using current kernel version(6.12)
```

> - `configuration written to ...output/.config` → 配置已正式保存
> - `Using current kernel version(6.12)` → SDK 确认使用 Linux 6.12 内核

---

## 6. 问题②：缺 lz4

```bash
./build.sh
```

编译中断：

```text
check-kernel.sh: line 93: lz4: command not found
```

安装：

```bash
sudo apt update && sudo apt install -y lz4

# 检查
lz4 --version
```

```text
*** LZ4 command line interface 64-bits v1.9.4, by Yann Collet ***
```

继续编译后，U-Boot 和 loader 阶段已经成功：

```text
Platform RK3588 is build OK
Running mk-loader.sh - build_uboot succeeded
```

> 说明**基础启动部分（U-Boot + loader）已编译打包成功**，剩下的是内核和 rootfs。

---

## 7. 一次性补装全部依赖

因为用的**不是正点官方打包好的编译环境**，所以依赖会一个接一个地缺 —— 每次报错再补很麻烦，干脆一次性装齐。

> 正点没有提供依赖清单，以下是根据它的检查项 + 常见 RK3588 构建需求整理的**主要依赖**：

```bash
sudo apt install -y \
  build-essential gcc g++ make cmake git repo \
  python3 python-is-python3 python3-pip python3-setuptools python3-wheel \
  flex bison bc rsync lz4 zstd xz-utils cpio unzip zip curl wget \
  device-tree-compiler u-boot-tools clang-format \
  libssl-dev libgmp-dev libmpc-dev libncurses-dev libelf-dev \
  dwarves swig pkg-config \
  binfmt-support e2fsprogs dosfstools mtools parted fdisk \
  sudo ca-certificates
```

再安装 SDK 自带的 QEMU 与 debootstrap：

```bash
sudo apt install -y \
  /mnt/rk3588/atk_dlrk3588_linux6.12/ubuntu/ubuntu-build-service/packages/qemu-user-static_*.deb \
  /mnt/rk3588/atk_dlrk3588_linux6.12/ubuntu/ubuntu-build-service/packages/debootstrap_*.deb
```

---

## 8. 问题③：QEMU / debootstrap

装完 SDK 自带的包再编译，**U-Boot 和内核都编译成功、`boot.img` 已生成** ✓
失败点转移到了 **Ubuntu rootfs 的 chroot 阶段**。

**原因**：SDK 自带的 `qemu-user-static` 在这套 WSL 环境下不工作。

**解决**：改用 Ubuntu 仓库的版本：

```bash
sudo apt install -y qemu-user-static
./build.sh
```

---

## 9. 问题④：recovery 缺 gettext

失败点换到了 **recovery（恢复分区）** 阶段，缺少一个软件包：

```bash
sudo apt install -y gettext
./build.sh
```

---

## 10. 问题⑤：PATH 里有空格

```text
Buildroot 硬性要求 PATH 中不能有带空格的条目
```

Windows 的 `C:\Program Files\...` 会以 `/mnt/c/Program Files/...` 的形式混进 WSL 的 PATH，导致 Buildroot 报错。

**单次修复**（只对当前终端有效）：

```bash
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v ' ' | tr '\n' ':' | sed 's/:$//')
echo "$PATH"
```

> ⚠️ **这只是临时修复** —— 新开终端、重启 WSL 后都会失效。
> 想永久生效，把上面这行加进 `~/.bashrc` 末尾：
> ```bash
> echo 'export PATH=$(echo "$PATH" | tr ":" "\n" | grep -v " " | tr "\n" ":" | sed "s/:$//")' >> ~/.bashrc
> ```

---

## 11. 编译成功

继续 `./build.sh`，这次没有报错，最后输出：

```text
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
```

---

## 12. 产物说明

```bash
ls -lh /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/
```

```text
total 0
lrwxrwxrwx 1 zzy zzy 40 Oct 25 12:02 MiniLoaderAll.bin -> ../../u-boot/rk3588_loader_v1.24.100.bin
lrwxrwxrwx 1 zzy zzy 26 Oct 25 12:03 boot.img           -> ../../kernel-6.12/boot.img
lrwxrwxrwx 1 zzy zzy 11 Oct 25 12:02 misc.img           -> ../misc.img
lrwxrwxrwx 1 zzy zzy 22 Oct 25 13:07 oem.img            -> ../extra-parts/oem.img
lrwxrwxrwx 1 zzy zzy 56 Oct 25 13:07 parameter.txt      -> ../../device/rockchip/.chips/rk3588/parameter-ubuntu.txt
lrwxrwxrwx 1 zzy zzy 23 Oct 25 13:07 recovery.img       -> ../recovery/ramboot.img
lrwxrwxrwx 1 zzy zzy 32 Oct 25 13:07 rootfs.img         -> ../../ubuntu/rockchip-rootfs.img
lrwxrwxrwx 1 zzy zzy 22 Oct 25 12:02 uboot.img          -> ../../u-boot/uboot.img
lrwxrwxrwx 1 zzy zzy 26 Oct 25 13:07 update.img         -> ../update/Image/update.img
lrwxrwxrwx 1 zzy zzy 27 Oct 25 13:07 userdata.img       -> ../extra-parts/userdata.img
```

> `total 0` 是因为这些都是**软链接**（不是实体文件）。

**看真实占用大小**（`-L` 表示跟随软链接）：

```bash
du -shL /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/
```

```text
6.2G    /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/
```

> 💡 这 **6.2 G 是整个 firmware 目录里所有软链接指向的真实文件之和**（不是单个 `update.img` 的大小）。

---

## 13. 拷贝与校验

把最终镜像拷到 Windows 侧（用于烧录）：

```bash
cp /mnt/rk3588/atk_dlrk3588_linux6.12/output/update/Image/update.img \
   "/mnt/d/linux/sdk编译包/atk_dlrk3588_ubuntu26_update.img"
```

校验拷贝完整性（两个 MD5 一致即可）：

```bash
md5sum /mnt/rk3588/atk_dlrk3588_linux6.12/output/update/Image/update.img \
       "/mnt/d/linux/sdk编译包/atk_dlrk3588_ubuntu26_update.img"
```

---

## 附录：完整命令速查

```bash
# ========== 1. 解压 SDK ==========
mkdir -p /mnt/h/rk3588-sdk
tar -xzf "/mnt/h/linux/rk3588/atk_dlrk3588_linux6.12_sdk_release_v1.1.0_20260921.tar.gz" -C /mnt/h/rk3588-sdk

# ========== 2. repo 检出源码 ==========
cd /mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12
.repo/repo/repo sync -l -j2

# ========== 3. 创建并挂载 ext4 虚拟盘 ==========
# （Windows 管理员 PowerShell）
#   diskpart
#   create vdisk file="H:\wsl\rk3588-ext4.vhdx" maximum=153600 type=expandable
#   exit
#   wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare

# （WSL）
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT     # ← 确认设备名（如 /dev/sde）
sudo mkfs.ext4 /dev/sde
sudo mkdir -p /mnt/rk3588
sudo mount /dev/sde /mnt/rk3588
sudo chown $USER:$USER /mnt/rk3588

# ========== 4. 迁移 SDK 到 ext4（约 2 小时）==========
mv /mnt/h/rk3588-sdk/atk_dlrk3588_linux6.12 /mnt/rk3588/

# ========== 5. 安装依赖 ==========
sudo apt update
sudo apt install -y lz4 qemu-user-static gettext
sudo apt install -y \
  build-essential gcc g++ make cmake git repo \
  python3 python-is-python3 python3-pip python3-setuptools python3-wheel \
  flex bison bc rsync lz4 zstd xz-utils cpio unzip zip curl wget \
  device-tree-compiler u-boot-tools clang-format \
  libssl-dev libgmp-dev libmpc-dev libncurses-dev libelf-dev \
  dwarves swig pkg-config \
  binfmt-support e2fsprogs dosfstools mtools parted fdisk \
  sudo ca-certificates
sudo apt install -y \
  /mnt/rk3588/atk_dlrk3588_linux6.12/ubuntu/ubuntu-build-service/packages/qemu-user-static_*.deb \
  /mnt/rk3588/atk_dlrk3588_linux6.12/ubuntu/ubuntu-build-service/packages/debootstrap_*.deb
sudo apt install -y qemu-user-static

# ========== 6. 清掉 PATH 里的空格（每个终端都要执行）==========
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v ' ' | tr '\n' ':' | sed 's/:$//')

# ========== 7. 选配置并编译 ==========
cd /mnt/rk3588/atk_dlrk3588_linux6.12
./build.sh lunch        # 选 7 = ubuntu + 2DP + 2HDMI
./build.sh

# ========== 8. 检查产物 ==========
ls -lh /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/
du -shL /mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/

# ========== 9. 拷贝并校验 ==========
cp /mnt/rk3588/atk_dlrk3588_linux6.12/output/update/Image/update.img \
   "/mnt/d/linux/sdk编译包/atk_dlrk3588_ubuntu26_update.img"
md5sum /mnt/rk3588/atk_dlrk3588_linux6.12/output/update/Image/update.img \
       "/mnt/d/linux/sdk编译包/atk_dlrk3588_ubuntu26_update.img"
```

---

> 📌 **后续**：烧录（用 RKDevTool 或 `rkflash.sh`）
