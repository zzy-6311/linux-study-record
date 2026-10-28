# 为 RK3588 定制 Debian 13（trixie）ARM64 根文件系统

> **前置**：环境配置已在《B_SDK编译.md》中完成（先做 B 再做 A）
> **目的**：保留现有的 U-Boot、Linux 6.12 内核和设备树，**自己从零制作** Debian 13（trixie）ARM64 根文件系统
> **工具**：`debootstrap` + `qemu-aarch64-static`

---

## 📋 目录

- [0. 思路与总览](#0-思路与总览)
- [1. 挂载虚拟盘并检查环境](#1-挂载虚拟盘并检查环境)
- [2. 第一阶段：debootstrap（--foreign）](#2-第一阶段debootstrap--foreign)
- [3. 第二阶段：chroot 内完成配置](#3-第二阶段chroot-内完成配置)
- [4. 准备 chroot 运行环境](#4-准备-chroot-运行环境)
- [5. 配置软件源](#5-配置软件源)
- [6. 【原理】为什么这样能"模拟"板子系统](#6-原理为什么这样能模拟板子系统)
- [7. 禁止服务自启（policy-rc.d）](#7-禁止服务自启policy-rcd)
- [8. 安装基础软件](#8-安装基础软件)
- [9. 主机名与用户](#9-主机名与用户)
- [10. 语言环境与开机服务](#10-语言环境与开机服务)
- [11. 配内核模块与固件](#11-配内核模块与固件)
- [12. 配置文件系统挂载表 fstab](#12-配置文件系统挂载表-fstab)
- [13. 卸载所有临时挂载](#13-卸载所有临时挂载)
- [14. 收尾清理（关键 4 项）](#14-收尾清理关键-4-项)
- [15. 打包成 ext4 镜像](#15-打包成-ext4-镜像)
- [16. 镜像验收（只读挂载）](#16-镜像验收只读挂载)
- [17. 导出与 SHA-256 校验](#17-导出与-sha-256-校验)
- [18. 把自制 rootfs 打包成 update.img](#18-把自制-rootfs-打包成-updateimg)
- [19. 导出 update.img 与校验](#19-导出-updateimg-与校验)
- [20. 烧录上板与首次启动（WiFi / SSH）](#20-烧录上板与首次启动wifi--ssh)
- [附录 A：完整命令速查](#附录-a完整命令速查)
- [附录 B：如果要 WiFi，必须在第 11 步补这一步](#附录-b如果要-wifi必须在第-11-步补这一步)

---

## 0. 思路与总览

**为什么不直接解包正点官方的 Debian 包？**
那样"学不到东西" ✓ —— 所以我们用 `debootstrap` 从头做一个最小 Debian，再自己装所需软件。

| 工具 | 作用 |
|---|---|
| **`debootstrap`** | 下载并生成 Debian 最小根文件系统 |
| **`qemu-aarch64-static`** | 在 x86_64 电脑上**执行 ARM64 程序**（后面配置 Debian 全靠它） |

**整体流程**：

```text
① debootstrap --foreign   → 在 x86_64 上下载并解包 ARM64 软件包
② chroot + qemu 二阶段    → 在 ARM64 环境里完成包配置
③ 装软件 / 建用户 / 配服务
④ 拷贝内核模块 + 固件 + fstab
⑤ 清理 → 打成 ext4 镜像 → 验收 → 导出
⑥ 与 SDK 的其它分区一起打包成 update.img → 烧录上板
```

---

## 1. 挂载虚拟盘并检查环境

**① 在 Windows 侧（管理员 PowerShell）挂载虚拟盘**：

```powershell
wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare
```

**② 进入 WSL**：

```bash
wsl
```

**③ 检查文件系统挂载**：

```bash
lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINTS
findmnt -T /mnt/rk3588
df -h /mnt/rk3588
```

**④ 检查工具版本**：

```bash
command -v debootstrap
debootstrap --version
qemu-aarch64-static --version
```

> 💡 这两个工具在《B_SDK编译》阶段已经装好了：
> - `debootstrap` → 装的是 **SDK 自带的 `.deb`**
> - `qemu-user-static` → 后来**换成了 Ubuntu 仓库版**（SDK 自带那版在 WSL 下不工作）

**⑤ 创建工作目录**：

```bash
mkdir -p /mnt/rk3588/debian-rk3588
cd /mnt/rk3588/debian-rk3588
```

---

## 2. 第一阶段：debootstrap（--foreign）

```bash
sudo debootstrap \
  --arch=arm64 \
  --foreign \
  trixie \
  rootfs \
  https://deb.debian.org/debian
```

| 参数 | 含义 |
|---|---|
| `--arch=arm64` | 目标系统是 RK3588 用的 **ARM64**，不是电脑的 x86_64 |
| `--foreign` | 只做**第一阶段**：在 x86_64 主机上下载并解包 ARM64 软件包 |
| `trixie` | Debian 13 |
| `rootfs` | 生成到当前目录下的 `rootfs/` |
| `https://deb.debian.org/debian` | Debian 官方软件源 |

---

## 3. 第二阶段：chroot 内完成配置

**① 先把 QEMU 放进 rootfs**（这样 chroot 进入 ARM64 根文件系统后仍能找到模拟器）：

```bash
cd /mnt/rk3588/debian-rk3588
sudo cp /usr/bin/qemu-aarch64-static rootfs/usr/bin/
```

**② 执行第二阶段**（配置刚才解包的软件包）：

```bash
sudo chroot rootfs \
  /usr/bin/qemu-aarch64-static -L / \
  /bin/sh /debootstrap/debootstrap --second-stage
```

成功标志：

```text
I: Configuring ifupdown...
I: Configuring libc-bin...
I: Configuring ca-certificates...
I: Base system installed successfully.
```

---

## 4. 准备 chroot 运行环境

```bash
cd /mnt/rk3588/debian-rk3588
sudo cp /etc/resolv.conf rootfs/etc/resolv.conf
sudo mount -t proc proc rootfs/proc
sudo mount -t sysfs sysfs rootfs/sys
sudo mount --rbind /dev rootfs/dev
sudo mount --make-rprivate rootfs/dev
```

> 这四行的作用：给 Debian rootfs 接入**网络解析**（resolv.conf）、**进程信息**（proc）、**系统信息**（sys）、**设备接口**（dev）

**进入 Debian ARM64 系统**：

```bash
sudo chroot rootfs \
  /usr/bin/qemu-aarch64-static -L / \
  /bin/bash
```

> 整体作用：从 WSL 的 Ubuntu 环境，**进入我们新建的 Debian ARM64 环境**

进入后先整理搜索路径（保证 Debian 能找到自己的命令）：

```bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
```

---

## 5. 配置软件源

```bash
cat > /etc/apt/sources.list <<'EOF'
deb https://deb.debian.org/debian trixie main contrib non-free non-free-firmware
deb https://deb.debian.org/debian trixie-updates main contrib non-free non-free-firmware
deb https://security.debian.org/debian-security trixie-security main contrib non-free non-free-firmware
EOF
```

更新索引，准备工作完成：

```bash
apt update
```

---

## 6. 【原理】为什么这样能"模拟"板子系统

### 整件事在干什么

> 进入 Debian 的这个用户空间，**就是在模拟板子的用户空间** ✓
> 前面的操作是用 `debootstrap` 把 Debian 的**官方骨架**搭起来 ✓ → 在电脑上完成一个**模拟系统** ✓
> 然后在模拟的系统里**安装软件、配置** ✓ → 最后**打包** ✓ → 就是一个 **rootfs（根文件系统）** ✓
> 烧到板子上 ✓ → 板子上就有了我们在这里装的所有东西 ✓

### 各部分的职责

**① debootstrap 的两阶段**

| 阶段 | 做什么 |
|---|---|
| 第一阶段（`--foreign`） | 把软件包**解包**到 `rootfs/` |
| 第二阶段（`--second-stage`） | 执行软件包的**配置脚本** |

完成后 `rootfs/` 中就有了：

```text
/bin/bash                  Shell 程序
/usr/bin/apt               软件包管理工具
/lib/ld-linux-aarch64.so.1 ARM64 动态加载器
/lib/aarch64-linux-gnu/    ARM64 运行库
/etc/                      系统配置
/var/lib/dpkg/             软件包数据库
```

**② chroot 的作用**

执行 `sudo chroot rootfs` 后，**WSL 的 Linux 内核会把这个进程的根目录切换到 `rootfs`** ✓

路径随之变化：

```text
chroot 前：/mnt/rk3588/debian-rk3588/rootfs/etc/os-release
chroot 后：/etc/os-release
```

**③ QEMU 的作用**

chroot 后，里面的 `bash` 是 **ARM64 程序** ✓，而你的电脑是 **x86_64** ✗ → 不能直接执行 ✗
所以要运行：`/usr/bin/qemu-aarch64-static -L / /bin/bash`

实际过程：

```text
1. WSL 先运行 x86_64 版的 qemu-aarch64-static
2. QEMU 读取 ARM64 的 /bin/bash
3. 根据 rootfs 中的动态加载器和 libc 启动 Bash
4. 把 ARM64 指令翻译成电脑能执行的指令
5. Bash 需要读写文件/联网时，QEMU 把系统调用转交给 WSL 的 Linux 内核
```

> **所以看到的 Bash 在运行，是 QEMU 用户模式模拟的结果** ✓
> **并不是电脑原生执行了 ARM64 程序** ✗ —— **QEMU 就是个翻译器** ✓

### ⚠️ 这里没有启动什么

```text
✗ 没有启动 Debian 内核
✗ 没有启动完整的 systemd
✗ 没有图形桌面
✓ 只是进入了一个使用 Debian 用户空间的 ARM64 Bash
```

**真正把它烧到 RK3588 板子上时** ✓，才会按**真实路径**直接进入我们配置好的这套系统 ✓

---

## 7. 禁止服务自启（policy-rc.d）

**目的**：防止 `apt install` 时在 chroot 中尝试启动服务（当前环境没有真正运行 systemd）

```bash
cat > /usr/sbin/policy-rc.d <<'EOF'
#!/bin/sh
exit 101
EOF
chmod +x /usr/sbin/policy-rc.d
```

> `exit 101` = 告诉安装脚本"**现在不要启动服务**"，但不影响它们在真正开机时运行 ✓
> **这个文件只用于构建阶段，最终打包前会删除** ✓

---

## 8. 安装基础软件

```bash
apt install -y \
  systemd-sysv udev dbus kmod procps \
  sudo openssh-server \
  network-manager wpasupplicant rfkill \
  iproute2 iputils-ping ethtool \
  ca-certificates curl wget \
  nano locales console-setup \
  usbutils pciutils
```

| 包 | 作用 |
|---|---|
| `systemd-sysv` | 开发板启动后的**系统初始化和服务管理** |
| `sudo` | 普通用户提权 |
| `openssh-server` | SSH 远程登录 |
| `network-manager` / `wpasupplicant` | 有线 / **WiFi** 网络管理 |
| `iproute2` / `iputils-ping` / `ethtool` | 网络配置与检查 |
| `kmod` | `lsmod`、`modprobe` 等内核模块工具 |
| `nano` / `locales` | 编辑器 / 语言环境 |
| `usbutils` / `pciutils` | `lsusb`、`lspci` 等硬件检查工具 |

### 安装过程中会弹两个蓝底配置界面

**① `keyboard-configuration`（键盘布局）**
→ 一般键盘选 **English (US)** ✓

**② `console-setup`（控制台字符集）**
→ 默认选中 **`Guess optimal character set`**（自动选择）✓ 直接确认 ✓

跑完显示：

```text
Updating certificates in /etc/ssl/certs...
0 added, 0 removed; done.
Running hooks in /etc/ca-certificates/update.d...
done.
```

---

## 9. 主机名与用户

### ① 设置主机名

```bash
echo rk3588-debian > /etc/hostname

cat > /etc/hosts <<'EOF'
127.0.0.1  localhost
127.0.1.1  rk3588-debian
::1        localhost ip6-localhost ip6-loopback
ff02::1    ip6-allnodes
ff02::2    ip6-allrouters
EOF
```

> 命令行提示符和局域网中都会用 **`rk3588-debian`** 这个名字 ✓

### ② 创建普通用户 `zzy`

```bash
useradd -m -s /bin/bash zzy
passwd zzy                # 按提示输入密码
usermod -aG sudo,adm,dialout,audio,video,plugdev zzy
```

### ③ 验证

```bash
id zzy
cat /etc/hostname
ls -ld /home/zzy
```

```text
uid=1000(zzy) gid=1000(zzy) groups=1000(zzy),4(adm),20(dialout),27(sudo),29(audio),44(video),46(plugdev)
rk3588-debian
drwx------ 2 zzy zzy 4096 Oct 27 12:56 /home/zzy
```

---

## 10. 语言环境与开机服务

### ① 生成语言环境

```bash
sed -i \
  -e 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' \
  -e 's/^# *zh_CN.UTF-8 UTF-8/zh_CN.UTF-8 UTF-8/' \
  /etc/locale.gen

locale-gen
```

### ② 默认用英文

```bash
update-locale LANG=en_US.UTF-8
```

> **为什么先默认英文？** 避免启动日志或串口程序出现乱码 ✓
> 中文环境已经生成 ✓，后续可以随时切换 ✓

### ③ 设为命令行启动（不启动图形桌面）

```bash
systemctl set-default multi-user.target
```

> 先验收"**串口登录 + 网络**" ✓，暂时不让它尝试启动图形桌面 ✓

### ④ 启用开机服务

```bash
systemctl enable \
  serial-getty@ttyFIQ0.service \
  NetworkManager.service \
  ssh.service
```

> `enable` 只是**设置开机时自动启用** ✓，**不会在 chroot 中立即启动** ✓

### ⑤ 验证

```bash
cat /etc/default/locale
systemctl is-enabled \
  serial-getty@ttyFIQ0.service \
  NetworkManager.service \
  ssh.service
systemctl get-default
```

```text
#  File generated by update-locale
LANG=en_US.UTF-8
enabled
enabled
enabled
multi-user.target
```

---

## 11. 配内核模块与固件

> 我们做的这个 rootfs 里**没有任何内核模块** ✗ —— 因为官方软件包不知道这块板子的参数 ✓
> 所以要**从 B 文档里编译出的官方 SDK rootfs 里借** ✓

### ① 先退出 chroot

```bash
exit
```

### ② 只读挂载参考 rootfs

```bash
mkdir -p /mnt/rk3588/ubuntu-reference

sudo mount -o loop,ro \
  "/mnt/d/linux/sdk编译包/分体包/rootfs.img" \
  /mnt/rk3588/ubuntu-reference
```

### ③ 查看它用的内核版本和固件

```bash
ls -la /mnt/rk3588/ubuntu-reference/lib/modules
ls /mnt/rk3588/ubuntu-reference/lib/firmware | head
```

```text
total 12
drwxr-xr-x  3 root root 4096 Oct 25 12:17 .
drwxr-xr-x 67 root root 4096 Oct 25 12:17 ..
drwxr-xr-x  3 root root 4096 Oct 25 12:17 6.12.69      ← 内核版本

regulatory.db
regulatory.db.p7s
rtl8723befw.bin
rtl8723befw_36.bin
rtl8733bu_config
rtl8733bu_fw
rtl8852bu_config
rtl8852bu_fw
rtlbt
```

### ④ 复制内核模块

```bash
cd /mnt/rk3588/debian-rk3588

sudo mkdir -p rootfs/lib/modules rootfs/lib/firmware

sudo cp -a \
  /mnt/rk3588/ubuntu-reference/lib/modules/6.12.69 \
  rootfs/lib/modules/
```

### ⑤ 复制固件

```bash
sudo cp -a --remove-destination \
  /mnt/rk3588/ubuntu-reference/lib/firmware/regulatory.db \
  /mnt/rk3588/ubuntu-reference/lib/firmware/regulatory.db.p7s \
  rootfs/lib/firmware/
```

> ⚠️ **这里只复制了 WiFi 的"法规数据库"，没有复制 WiFi/BT 芯片固件** ✗
> **要 WiFi 功能的话务必看 [附录 B](#附录-b如果要-wifi必须在第-11-步补这一步)** ✓

### ⑥ 生成模块依赖

```bash
sudo chroot rootfs \
  /usr/bin/qemu-aarch64-static -L / \
  /sbin/depmod -a 6.12.69
```

### ⑦ 验证

```bash
ls -l rootfs/lib/firmware/regulatory.db*
ls -lh rootfs/lib/modules/6.12.69/modules.dep
sudo du -sh rootfs/lib/modules/6.12.69 rootfs/lib/firmware
```

```text
-rw-r--r-- 1 root root 6380 Aug 18 01:26 rootfs/lib/firmware/regulatory.db
-rw-r--r-- 1 root root 6380 Jun  6 01:02 rootfs/lib/firmware/regulatory.db-debian
-rw-r--r-- 1 root root 6380 Jun  6 01:02 rootfs/lib/firmware/regulatory.db-upstream
-rw-r--r-- 1 root root 1085 Aug 18 01:26 rootfs/lib/firmware/regulatory.db.p7s
-rw-r--r-- 1 root root 1118 Jun  6 01:02 rootfs/lib/firmware/regulatory.db.p7s-debian
-rw-r--r-- 1 root root 1085 Jun  6 01:02 rootfs/lib/firmware/regulatory.db.p7s-upstream
-rw-r--r-- 1 root root 7.6K Oct 27 21:28 rootfs/lib/modules/6.12.69/modules.dep
19M     rootfs/lib/modules/6.12.69
380K    rootfs/lib/firmware
```

---

## 12. 配置文件系统挂载表 fstab

```bash
cd /mnt/rk3588/debian-rk3588

sudo tee rootfs/etc/fstab > /dev/null <<'EOF'
/dev/root       /               ext4        defaults                0 1
proc            /proc           proc        defaults                0 0
devtmpfs        /dev            devtmpfs    defaults                0 0
devpts          /dev/pts        devpts      mode=0620,ptmxmode=0000,gid=5 0 0
tmpfs           /dev/shm        tmpfs       nosuid,nodev,noexec     0 0
sysfs           /sys            sysfs       nosuid,nodev,noexec     0 0
configfs        /sys/kernel/config configfs defaults                0 0
debugfs         /sys/kernel/debug debugfs   defaults                0 0
PARTLABEL=oem   /oem            ext4        defaults                0 2
PARTLABEL=userdata /userdata    ext4        defaults                0 2
EOF

sudo mkdir -p rootfs/oem rootfs/userdata
```

### 验证并卸载参考镜像

```bash
sudo umount /mnt/rk3588/ubuntu-reference

cat rootfs/etc/fstab
sudo du -sh rootfs
```

```text
/dev/root       /               ext4        defaults                0 1
...
PARTLABEL=userdata /userdata    ext4        defaults                0 2

du: cannot access 'rootfs/proc/23794/task/23794/fd/4': No such file or directory
du: cannot access 'rootfs/proc/23794/fdinfo/3': No such file or directory
675M    rootfs
```

> `du: cannot access .../proc/...` 的警告**是正常的** ✓ —— `/proc` 是动态目录，`du` 扫它必然会碰到瞬间消失的条目 ✓

---

## 13. 卸载所有临时挂载

```bash
findmnt -R "$(pwd)/rootfs"
findmnt | grep -E 'rk3588|rootfs|ubuntu'
```

```text
├─/init                                              rootfs[/init]           rootfs      ro,size=4059340k,nr_inodes=1014835
└─/mnt/rk3588                                        /dev/sdd                ext4        rw,relatime
  ├─/mnt/rk3588/debian-rk3588/rootfs/proc            proc                    proc        rw,relatime
  ├─/mnt/rk3588/debian-rk3588/rootfs/sys             sysfs                   sysfs       rw,relatime
  └─/mnt/rk3588/debian-rk3588/rootfs/dev             none                    devtmpfs    rw,nosuid,relatime,...
    ├─/mnt/rk3588/debian-rk3588/rootfs/dev/pts       devpts                  devpts      rw,nosuid,noexec,noatime,gid=5,mode=620,...
    ├─/mnt/rk3588/debian-rk3588/rootfs/dev/shm       none                    tmpfs       rw,nosuid,nodev,noatime
    ├─/mnt/rk3588/debian-rk3588/rootfs/dev/hugepages hugetlbfs               hugetlbfs   rw,nosuid,nodev,relatime,pagesize=2M
    └─/mnt/rk3588/debian-rk3588/rootfs/dev/mqueue    mqueue                  mqueue      rw,nosuid,nodev,noexec,relatime
```

**执行卸载**：

```bash
sudo umount -R "$(pwd)/rootfs/proc"
sudo umount -R "$(pwd)/rootfs/sys"
sudo umount -R "$(pwd)/rootfs/dev"

# 确认已全部卸载
findmnt -R "$(pwd)/rootfs" || true
```

---

## 14. 收尾清理（关键 4 项）

### ① 先检查两处"会泄露构建环境"的配置

```bash
ls -l rootfs/etc/resolv.conf
cat rootfs/etc/resolv.conf
cat rootfs/etc/machine-id 2>/dev/null || true
```

```text
-rw-r--r-- 1 root root 200 Oct 27 20:24 rootfs/etc/resolv.conf
# This file was automatically generated by WSL. To stop automatic generation of this file, add the following entry to /etc/wsl.conf:
# [network]
# generateResolvConf = false
nameserver 10.255.255.254
2f31dc28eb0947c281733484898ef72c
```

**两处需要修正** ✗：

| 问题 | 原因 | 处理 |
|---|---|---|
| `resolv.conf` 里是 `10.255.255.254` | 这是 **WSL 的 DNS**，在板卡上**无效** ✗ | 清空 ✓（NetworkManager 启动后会重新生成 ✓）|
| `machine-id` 已有值 | 这是在**构建机**上生成的 ✗ | 清空 ✓（让板卡首次启动时生成自己的 ✓）|

> ⚠️ **`machine-id` 必须清空** —— 否则多台设备会共用同一个身份 ✗（影响 systemd、日志、某些服务授权）

### ② 执行清理

```bash
# 清理 chroot 内下载的软件包缓存
sudo chroot rootfs \
  /usr/bin/qemu-aarch64-static -L / \
  /usr/bin/apt-get clean

# 删除只在构建阶段使用的 QEMU 和服务启动限制
sudo rm -f rootfs/usr/bin/qemu-aarch64-static
sudo rm -f rootfs/usr/sbin/policy-rc.d

# 清除 WSL 的 DNS 配置（NetworkManager 启动后会重新生成）
sudo truncate -s 0 rootfs/etc/resolv.conf

# 让板卡首次启动时生成新的 machine-id
sudo truncate -s 0 rootfs/etc/machine-id

# 清理镜像中不需要保留的临时文件
sudo rm -rf rootfs/tmp/* rootfs/var/tmp/*
```

### ③ 验证

```bash
findmnt -R "$(pwd)/rootfs" || true

test ! -e rootfs/usr/bin/qemu-aarch64-static && echo "QEMU 已移除"
test ! -e rootfs/usr/sbin/policy-rc.d && echo "policy-rc.d 已移除"
wc -c rootfs/etc/resolv.conf rootfs/etc/machine-id

sudo du -sh --exclude=proc --exclude=sys --exclude=dev rootfs
df -h /mnt/rk3588
```

```text
QEMU 已移除
policy-rc.d 已移除
0 rootfs/etc/resolv.conf
0 rootfs/etc/machine-id
0 total
531M    rootfs
Filesystem      Size  Used Avail Use% Mounted on
/dev/sdd        147G   30G  110G  22% /mnt/rk3588
```

**现在状态符合制作镜像的条件** ✓：

```text
✓ 所有临时挂载已解除
✓ 构建辅助文件已删除
✓ 清理后实际系统约 531 MB
✓ 虚拟盘还有 110 GB 可用
```

---

## 15. 打包成 ext4 镜像

**容量规划**：531 MB 系统 ✓ → 做 **1 GiB** 镜像 ✓ → 约 **400~450 MB** 空闲用于首次启动和日志 ✓

```bash
truncate -s 1G rootfs.img

sudo mkfs.ext4 -F \
  -L rootfs \
  -d rootfs \
  rootfs.img
```

> `-d rootfs` = **把 `rootfs/` 目录的内容直接写进镜像** ✓（不用先挂载再拷 ✓）

### 检查镜像

```bash
ls -lh rootfs.img
sudo fsck.ext4 -fn rootfs.img
sudo dumpe2fs -h rootfs.img 2>/dev/null | grep -E \
  'Filesystem volume name|Block count|Free blocks|Inode count|Free inodes|Block size'
```

```text
-rw-r--r-- 1 zzy zzy 1.0G Oct 27 22:26 rootfs.img
e2fsck 1.47.0 (5-Feb-2023)
Pass 1: Checking inodes, blocks, and sizes
Pass 2: Checking directory structure
Pass 3: Checking directory connectivity
Pass 4: Checking reference counts
Pass 5: Checking group summary information
rootfs: 19683/65536 files (0.1% non-contiguous), 138382/262144 blocks
Filesystem volume name:   rootfs
Inode count:              65536
Block count:              262144
Free blocks:              123762
Free inodes:              45853
Block size:               4096
```

---

## 16. 镜像验收（只读挂载）

```bash
mkdir -p verify-rootfs
sudo mount -o loop,ro rootfs.img verify-rootfs
```

### 检查清单

```bash
cat verify-rootfs/etc/os-release
grep '^zzy:' verify-rootfs/etc/passwd
grep '^zzy:' verify-rootfs/etc/group || true

ls -l verify-rootfs/sbin/init
ls -l verify-rootfs/etc/systemd/system/getty.target.wants/
ls -l verify-rootfs/etc/systemd/system/multi-user.target.wants/

test -f verify-rootfs/lib/modules/6.12.69/modules.dep \
  && echo "6.12.69 模块依赖文件存在"

wc -c verify-rootfs/etc/resolv.conf verify-rootfs/etc/machine-id
```

```text
PRETTY_NAME="Debian GNU/Linux 13 (trixie)"
NAME="Debian GNU/Linux"
VERSION_ID="13"
VERSION="13 (trixie)"
VERSION_CODENAME=trixie
DEBIAN_VERSION_FULL=13.7
ID=debian
HOME_URL="https://www.debian.org/"
SUPPORT_URL="https://www.debian.org/support"
BUG_REPORT_URL="https://bugs.debian.org/"

zzy:x:1000:1000::/home/zzy:/bin/bash
zzy:x:1000:

lrwxrwxrwx 1 root root 22 Apr 14  2026 verify-rootfs/sbin/init -> ../lib/systemd/systemd

total 0
lrwxrwxrwx 1 root root 38 Oct 26 17:36 getty@tty1.service -> /usr/lib/systemd/system/getty@.service
lrwxrwxrwx 1 root root 45 Oct 27 21:11 serial-getty@ttyFIQ0.service -> /usr/lib/systemd/system/serial-getty@.service

total 0
lrwxrwxrwx 1 root root 44 Oct 27 20:43 ModemManager.service -> /usr/lib/systemd/system/ModemManager.service
lrwxrwxrwx 1 root root 46 Oct 27 20:43 NetworkManager.service -> /usr/lib/systemd/system/NetworkManager.service
lrwxrwxrwx 1 root root 45 Oct 27 20:42 console-setup.service -> /usr/lib/systemd/system/console-setup.service
lrwxrwxrwx 1 root root 36 Oct 26 18:00 cron.service -> /usr/lib/systemd/system/cron.service
lrwxrwxrwx 1 root root 44 Oct 26 18:00 e2scrub_reap.service -> /usr/lib/systemd/system/e2scrub_reap.service
lrwxrwxrwx 1 root root 42 Oct 26 18:01 networking.service -> /usr/lib/systemd/system/networking.service
lrwxrwxrwx 1 root root 40 Oct 26 17:36 remote-fs.target -> /usr/lib/systemd/system/remote-fs.target
lrwxrwxrwx 1 root root 35 Oct 27 20:43 ssh.service -> /usr/lib/systemd/system/ssh.service
lrwxrwxrwx 1 root root 46 Oct 27 20:43 wpa_supplicant.service -> /usr/lib/systemd/system/wpa_supplicant.service

6.12.69 模块依赖文件存在
0 verify-rootfs/etc/resolv.conf
0 verify-rootfs/etc/machine-id
0 total
```

### 检查完必须卸载

```bash
sudo umount verify-rootfs
findmnt -R "$(pwd)/verify-rootfs" || true
```

---

## 17. 导出与 SHA-256 校验

```bash
cp -n rootfs.img "/mnt/d/linux/sdk编译包/分体包/debian-rootfs.img"

# 校验：两个 SHA-256 必须一致
sha256sum rootfs.img
sha256sum "/mnt/d/linux/sdk编译包/分体包/debian-rootfs.img"
ls -lh "/mnt/d/linux/sdk编译包/分体包/debian-rootfs.img"
```

```text
d8275f1d8cd97e5ccb5c088b64ea9a7443f5f4b3a4bda930aa008ebb7c014c98  rootfs.img
d8275f1d8cd97e5ccb5c088b64ea9a7443f5f4b3a4bda930aa008ebb7c014c98  /mnt/d/linux/sdk编译包/分体包/debian-rootfs.img
-rwxrwxrwx 1 zzy zzy 1.0G Oct 27 22:30 /mnt/d/linux/sdk编译包/分体包/debian-rootfs.img
```

✅ **导出到了**：`D:\linux\sdk编译包\分体包\debian-rootfs.img`

---

## 18. 把自制 rootfs 打包成 update.img

> 到这里我们手里只有**一个 rootfs** ✓
> 但板子要烧的 `update.img` 需要把 **loader + 参数 + U-Boot + boot + recovery + oem + userdata + rootfs** 全都装进去 ✓
> 所以做法是：**其它分区沿用 B 文档里已烧录成功的 SDK 固件** ✓，**只把 rootfs 换成我们自制的 Debian** ✓

### ① 建立独立打包目录

```bash
PACK=/mnt/rk3588/debian-rk3588/pack-debian-update
SRC="/mnt/d/linux/sdk编译包/分体包"
TOOLS=/mnt/rk3588/atk_dlrk3588_linux6.12/tools/linux/Linux_Pack_Firmware/rockdev

mkdir -p "$PACK/Image"

# 复制打包工具和分区清单
cp "$TOOLS/afptool" "$PACK/"
cp "$TOOLS/rkImageMaker" "$PACK/"
cp "$TOOLS/rk3588-package-file" "$PACK/package-file"
```

| 文件 | 作用 |
|---|---|
| **`afptool`** | 把 `package-file` 描述的所有分区**打成一个镜像** |
| **`rkImageMaker`** | 给镜像**加上 RK3588 固件头**，生成可烧录的 `update.img` |
| **`rk3588-package-file`** | 分区清单（每个分区对应哪个文件） |

### ② 用软链接接入各分区镜像

```bash
# 前 8 个分区：沿用 SDK 编译好的固件
ln -sfn "$SRC/MiniLoaderAll.bin" "$PACK/Image/MiniLoaderAll.bin"
ln -sfn "$SRC/parameter.txt"     "$PACK/Image/parameter.txt"
ln -sfn "$SRC/uboot.img"         "$PACK/Image/uboot.img"
ln -sfn "$SRC/misc.img"          "$PACK/Image/misc.img"
ln -sfn "$SRC/boot.img"          "$PACK/Image/boot.img"
ln -sfn "$SRC/recovery.img"      "$PACK/Image/recovery.img"
ln -sfn "$SRC/oem.img"           "$PACK/Image/oem.img"
ln -sfn "$SRC/userdata.img"      "$PACK/Image/userdata.img"

# 只有 rootfs 指向我们自己做的 Debian 镜像
ln -sfn "/mnt/d/linux/sdk编译包/debian-rootfs.img" \
        "$PACK/Image/rootfs.img"
```

**替换对照表**：

| 分区 | 来源 |
|---|---|
| `MiniLoaderAll.bin` | B 文档 SDK 编译产物 ✓ 沿用 |
| `parameter.txt` | B 文档 SDK 编译产物 ✓ 沿用 |
| `uboot.img` | B 文档 SDK 编译产物 ✓ 沿用 |
| `boot.img` | B 文档 SDK 编译产物 ✓ 沿用 |
| `misc.img` / `recovery.img` / `oem.img` / `userdata.img` | B 文档 SDK 编译产物 ✓ 沿用 |
| **`rootfs.img`** | **我们自制的 Debian 13** ⭐ 唯一替换项 |

> 💡 **为什么用软链接而不是复制？**
> - **省空间** ✓（不复制 1 GB 的 rootfs ✓）
> - **不改原文件** ✓（后续想换 rootfs 只要改链接 ✓）

### ③ 打包

```bash
cd "$PACK"

# 第一步：把各分区打成一个 update.img
./afptool -pack ./ Image/update.img

# 第二步：加上 RK3588 固件头，生成最终的可烧录镜像
./rkImageMaker -RK3588 \
  Image/MiniLoaderAll.bin \
  Image/update.img \
  update.img \
  -os_type:androidos
```

成功输出：

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
```

---

## 19. 导出 update.img 与校验

```bash
cp "$PACK/update.img" \
   "/mnt/d/linux/sdk编译包/atk_dlrk3588_debian13_update.img"
sync

# 校验
sha256sum "$PACK/update.img"
sha256sum "/mnt/d/linux/sdk编译包/atk_dlrk3588_debian13_update.img"
ls -lh "/mnt/d/linux/sdk编译包/atk_dlrk3588_debian13_update.img"
```

```text
236e23cfa2177417da63bf66d40093eb1c4f6781226db868b85bd4a1a2382ef1  /mnt/rk3588/debian-rk3588/pack-debian-update/update.img
236e23cfa2177417da63bf66d40093eb1c4f6781226db868b85bd4a1a2382ef1  /mnt/d/linux/sdk编译包/atk_dlrk3588_debian13_update.img
-rwxrwxrwx 1 zzy zzy 1.2G Oct 28 20:08 /mnt/d/linux/sdk编译包/atk_dlrk3588_debian13_update.img
```

✅ **两个 SHA-256 完全一致** → 导出没有问题 ✓

---

## 20. 烧录上板与首次启动（WiFi / SSH）

### ① 烧录

用 **RKDevTool** 加载 `atk_dlrk3588_debian13_update.img` ✓ → 进入 MaskROM/Loader 模式 ✓ → 执行烧录 ✓

### ② 首次启动观察

烧完开机后确认：

```text
✓ 系统能正常启动
✓ 【没有桌面、没有图形化管理界面】← 这是预期的（我们设的是 multi-user.target）
✓ 能通过串口登录（serial-getty@ttyFIQ0）
```

> ⚠️ **我们做的是"最小系统"** ✓ —— **没有桌面是故意的** ✓
> 目的就是先验收"**串口登录 + 网络**"这两件事 ✓

### ③ 配置 WiFi

先用**串口终端**登录板子 ✓，然后：

```bash
# 连接 WiFi（--ask 会交互提示输入密码）
nmcli --ask device wifi connect "iQOO 12 Pro" ifname wlxf0a882e4993d
```

> 💡 **`wlxf0a882e4993d` 是 USB 无线网卡的接口名** ✓（`wlx` + MAC 地址 ✓）
> 如果你用的是**板载 WiFi** ✓，接口名通常是 **`wlan0`** ✓ ——
> 那就用 `ifname wlan0` ✓，并且需要按 [附录 B](#附录-b如果要-wifi必须在第-11-步补这一步) 补上**芯片固件** ✓

**检查网络状态**：

```bash
nmcli device status
ip addr show wlxf0a882e4993d
```

### ④ 从电脑 SSH 登录

```bash
ssh zzy@<板子IP>
```

✅ **测试通过，SSH 成功连上** ✓

---

## 🎯 最终成果

| 产物 | 路径 | 大小 | 说明 |
|---|---|---|---|
| **定制 rootfs** | `D:\linux\sdk编译包\分体包\debian-rootfs.img` | 1.0 GiB | Debian 13 (trixie) / ARM64 / 内容 531 MB |
| **完整烧录包** ⭐ | `D:\linux\sdk编译包\atk_dlrk3588_debian13_update.img` | 1.2 GiB | 可直接用 RKDevTool 烧录 |

### 从零到成品的完整链路

```text
SDK 编译（B 文档）
   └─→ U-Boot / 6.12 内核 / boot.img / loader …（其它分区）

debootstrap 自制 rootfs（A 文档 1~17 步）
   └─→ debian-rootfs.img（1.0 GiB，Debian 13）

两者合并（A 文档 18~19 步）
   └─→ atk_dlrk3588_debian13_update.img（1.2 GiB）⭐ 可烧录

烧录上板（A 文档 20 步）
   └─→ 串口登录 ✓ → 配 WiFi ✓ → SSH 登录 ✓
```

## 附录 A：完整命令速查

```bash
# ========== 0. 挂载虚拟盘（Windows 管理员 PowerShell）==========
#   wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare

# ========== 1. 进入 WSL 并检查 ==========
wsl
lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINTS
findmnt -T /mnt/rk3588
command -v debootstrap && debootstrap --version
qemu-aarch64-static --version
mkdir -p /mnt/rk3588/debian-rk3588
cd /mnt/rk3588/debian-rk3588

# ========== 2. debootstrap 第一阶段 ==========
sudo debootstrap --arch=arm64 --foreign trixie rootfs https://deb.debian.org/debian

# ========== 3. 第二阶段 ==========
sudo cp /usr/bin/qemu-aarch64-static rootfs/usr/bin/
sudo chroot rootfs /usr/bin/qemu-aarch64-static -L / /bin/sh /debootstrap/debootstrap --second-stage

# ========== 4. chroot 运行环境 ==========
sudo cp /etc/resolv.conf rootfs/etc/resolv.conf
sudo mount -t proc proc rootfs/proc
sudo mount -t sysfs sysfs rootfs/sys
sudo mount --rbind /dev rootfs/dev
sudo mount --make-rprivate rootfs/dev
sudo chroot rootfs /usr/bin/qemu-aarch64-static -L / /bin/bash
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# ========== 5. 软件源（chroot 内执行）==========
cat > /etc/apt/sources.list <<'EOF'
deb https://deb.debian.org/debian trixie main contrib non-free non-free-firmware
deb https://deb.debian.org/debian trixie-updates main contrib non-free non-free-firmware
deb https://security.debian.org/debian-security trixie-security main contrib non-free non-free-firmware
EOF
apt update

# ========== 6. 禁止服务自启 ==========
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d
chmod +x /usr/sbin/policy-rc.d

# ========== 7. 安装基础软件 ==========
apt install -y \
  systemd-sysv udev dbus kmod procps sudo openssh-server \
  network-manager wpasupplicant rfkill iproute2 iputils-ping ethtool \
  ca-certificates curl wget nano locales console-setup usbutils pciutils
# 键盘选 English (US)，字符集选 Guess optimal character set

# ========== 8. 主机名 ==========
echo rk3588-debian > /etc/hostname
cat > /etc/hosts <<'EOF'
127.0.0.1  localhost
127.0.1.1  rk3588-debian
::1        localhost ip6-localhost ip6-loopback
ff02::1    ip6-allnodes
ff02::2    ip6-allrouters
EOF

# ========== 9. 用户 ==========
useradd -m -s /bin/bash zzy
passwd zzy
usermod -aG sudo,adm,dialout,audio,video,plugdev zzy

# ========== 10. 语言与服务 ==========
sed -i -e 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' \
       -e 's/^# *zh_CN.UTF-8 UTF-8/zh_CN.UTF-8 UTF-8/' /etc/locale.gen
locale-gen
update-locale LANG=en_US.UTF-8
systemctl set-default multi-user.target
systemctl enable serial-getty@ttyFIQ0.service NetworkManager.service ssh.service

# ========== 11. 内核模块与固件（exit 出 chroot 后执行）==========
exit
mkdir -p /mnt/rk3588/ubuntu-reference
sudo mount -o loop,ro "/mnt/d/linux/sdk编译包/分体包/rootfs.img" /mnt/rk3588/ubuntu-reference
cd /mnt/rk3588/debian-rk3588
sudo mkdir -p rootfs/lib/modules rootfs/lib/firmware
sudo cp -a /mnt/rk3588/ubuntu-reference/lib/modules/6.12.69 rootfs/lib/modules/
sudo cp -a --remove-destination \
  /mnt/rk3588/ubuntu-reference/lib/firmware/regulatory.db \
  /mnt/rk3588/ubuntu-reference/lib/firmware/regulatory.db.p7s \
  rootfs/lib/firmware/
# ⚠️ 要 WiFi 的话，这里还要拷固件 —— 见附录 B
sudo chroot rootfs /usr/bin/qemu-aarch64-static -L / /sbin/depmod -a 6.12.69

# ========== 12. fstab ==========
sudo tee rootfs/etc/fstab > /dev/null <<'EOF'
/dev/root       /               ext4        defaults                0 1
proc            /proc           proc        defaults                0 0
devtmpfs        /dev            devtmpfs    defaults                0 0
devpts          /dev/pts        devpts      mode=0620,ptmxmode=0000,gid=5 0 0
tmpfs           /dev/shm        tmpfs       nosuid,nodev,noexec     0 0
sysfs           /sys            sysfs       nosuid,nodev,noexec     0 0
configfs        /sys/kernel/config configfs defaults                0 0
debugfs         /sys/kernel/debug debugfs   defaults                0 0
PARTLABEL=oem   /oem            ext4        defaults                0 2
PARTLABEL=userdata /userdata    ext4        defaults                0 2
EOF
sudo mkdir -p rootfs/oem rootfs/userdata
sudo umount /mnt/rk3588/ubuntu-reference

# ========== 13. 卸载临时挂载 ==========
sudo umount -R "$(pwd)/rootfs/proc"
sudo umount -R "$(pwd)/rootfs/sys"
sudo umount -R "$(pwd)/rootfs/dev"
findmnt -R "$(pwd)/rootfs" || true

# ========== 14. 收尾清理 ==========
sudo chroot rootfs /usr/bin/qemu-aarch64-static -L / /usr/bin/apt-get clean
sudo rm -f rootfs/usr/bin/qemu-aarch64-static
sudo rm -f rootfs/usr/sbin/policy-rc.d
sudo truncate -s 0 rootfs/etc/resolv.conf
sudo truncate -s 0 rootfs/etc/machine-id
sudo rm -rf rootfs/tmp/* rootfs/var/tmp/*

# ========== 15. 打包镜像 ==========
truncate -s 1G rootfs.img
sudo mkfs.ext4 -F -L rootfs -d rootfs rootfs.img
sudo fsck.ext4 -fn rootfs.img

# ========== 16. 验收 ==========
mkdir -p verify-rootfs
sudo mount -o loop,ro rootfs.img verify-rootfs
cat verify-rootfs/etc/os-release
ls -l verify-rootfs/sbin/init
test -f verify-rootfs/lib/modules/6.12.69/modules.dep && echo "模块依赖存在"
sudo umount verify-rootfs

# ========== 17. 导出与校验 ==========
cp -n rootfs.img "/mnt/d/linux/sdk编译包/分体包/debian-rootfs.img"
sha256sum rootfs.img "/mnt/d/linux/sdk编译包/分体包/debian-rootfs.img"

# ========== 18. 打包成 update.img ==========
PACK=/mnt/rk3588/debian-rk3588/pack-debian-update
SRC="/mnt/d/linux/sdk编译包/分体包"
TOOLS=/mnt/rk3588/atk_dlrk3588_linux6.12/tools/linux/Linux_Pack_Firmware/rockdev

mkdir -p "$PACK/Image"
cp "$TOOLS/afptool" "$PACK/"
cp "$TOOLS/rkImageMaker" "$PACK/"
cp "$TOOLS/rk3588-package-file" "$PACK/package-file"

ln -sfn "$SRC/MiniLoaderAll.bin" "$PACK/Image/MiniLoaderAll.bin"
ln -sfn "$SRC/parameter.txt"    "$PACK/Image/parameter.txt"
ln -sfn "$SRC/uboot.img"        "$PACK/Image/uboot.img"
ln -sfn "$SRC/misc.img"         "$PACK/Image/misc.img"
ln -sfn "$SRC/boot.img"         "$PACK/Image/boot.img"
ln -sfn "$SRC/recovery.img"     "$PACK/Image/recovery.img"
ln -sfn "$SRC/oem.img"          "$PACK/Image/oem.img"
ln -sfn "$SRC/userdata.img"     "$PACK/Image/userdata.img"
ln -sfn "/mnt/d/linux/sdk编译包/debian-rootfs.img" "$PACK/Image/rootfs.img"

cd "$PACK"
./afptool -pack ./ Image/update.img
./rkImageMaker -RK3588 Image/MiniLoaderAll.bin Image/update.img update.img -os_type:androidos

# ========== 19. 导出与校验 ==========
cp "$PACK/update.img" "/mnt/d/linux/sdk编译包/atk_dlrk3588_debian13_update.img"
sync
sha256sum "$PACK/update.img" "/mnt/d/linux/sdk编译包/atk_dlrk3588_debian13_update.img"

# ========== 20. 烧录上板后（板子上执行）==========
nmcli --ask device wifi connect "iQOO 12 Pro" ifname wlxf0a882e4993d
nmcli device status
ip addr show wlxf0a882e4993d
```

---

## 附录 B：如果要 WiFi，必须在第 11 步补这一步

**问题**：第 11 步只复制了 `regulatory.db`（WiFi **法规数据库**）✗
**但参考 rootfs 里还有这些芯片固件** ✗ —— 不复制的话 **WiFi / 蓝牙起不来** ✗

```text
rtl8852bu_fw          ← RK3588 板载 WiFi 芯片（RTL8852BU）的主固件
rtl8852bu_config      ← 该芯片的配置
rtlbt                 ← 蓝牙固件
rtl8733bu_fw / rtl8733bu_config  ← 另一颗 WiFi 芯片
rtl8723befw.bin / rtl8723befw_36.bin  ← 老的 RTL8723BE
```

**补拷命令**（在第 11 步⑤之后、⑥之前执行）：

```bash
cd /mnt/rk3588/debian-rk3588

# 一次性拷走参考 rootfs 里的全部固件（推荐）
sudo cp -a --remove-destination \
  /mnt/rk3588/ubuntu-reference/lib/firmware/* \
  rootfs/lib/firmware/

# 验证
ls rootfs/lib/firmware/ | head -20
du -sh rootfs/lib/firmware
```

> 💡 **最省事的做法**：直接把整个 `firmware` 目录都拷过去 ✓
> 体积通常只有几 MB ~ 几十 MB ✓，但能让**所有无线芯片都能工作** ✓

**验证 WiFi 是否可用**（烧录上板后）：

```bash
ip link                          # 看有没有 wlan0
rfkill list                      # 看 WiFi 是否被禁用
dmesg | grep -i -E 'rtl|firmware'   # 看固件加载日志
nmcli device wifi list           # 扫描 WiFi
```

---

---
