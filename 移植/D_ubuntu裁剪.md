# Ubuntu 26.04 rootfs 裁剪记录（2.7 GB → 680 MB）

> **目标**：对 B 文档里编译出的 Ubuntu rootfs 做**瘦身** ✓
> **产物**：一个只有串口 + SSH 的最小 Ubuntu 系统 ✓（**无桌面、无浏览器、无蓝牙、无音频、无多媒体、无打印扫描**）
> **成果**：**内容 2.7 GB → 680 MB**；**镜像文件 3.1 GB → 1.3 GB**

---

## 📋 目录

- [0. 目标与思路](#0-目标与思路)
- [1. 创建工作副本](#1-创建工作副本)
- [2. 挂载并摸底](#2-挂载并摸底)
- [3. 切换为可写模式](#3-切换为可写模式)
- [4. 搭好 chroot 运行环境](#4-搭好-chroot-运行环境)
- [5. 保护关键包（apt-mark manual）](#5-保护关键包apt-mark-manual)
- [6. 分四批卸载软件](#6-分四批卸载软件)
- [7. 手工删除 Chromium](#7-手工删除-chromium)
- [8. 清理 apt 缓存](#8-清理-apt-缓存)
- [9. 查看裁剪效果](#9-查看裁剪效果)
- [10. 配置启动方式](#10-配置启动方式)
- [11. 配置 DNS](#11-配置-dns)
- [12. 关闭无用服务](#12-关闭无用服务)
- [13. 移除构建期临时策略](#13-移除构建期临时策略)
- [14. 卸载镜像并检查文件系统](#14-卸载镜像并检查文件系统)
- [15. 压缩镜像体积（关键两步）](#15-压缩镜像体积关键两步)
- [16. 【原理】为什么删了软件镜像不变小](#16-原理为什么删了软件镜像不变小)
- [17. 导出精简 rootfs](#17-导出精简-rootfs)
- [18. 打包成 update.img](#18-打包成-updateimg)
- [19. 上板验证](#19-上板验证)
- [附录 A：完整命令速查](#附录-a完整命令速查)
- [附录 B：⚠️ apt-mark 清单建议补这几项](#附录-b-apt-mark-清单建议补这几项)

---

## 0. 目标与思路

**这次学习：如何对已有的镜像做裁剪，降低包大小。**

思路是：

```text
① 复制一份镜像做"工作副本"（不动原件）
② 挂载 → 摸底：看谁占空间
③ chroot 进去 → 用 apt 卸载不需要的包
④ 清理缓存 → 卸载 → 文件系统检查
⑤ 【关键】resize2fs 缩文件系统 → truncate 缩文件 → e2fsck
⑥ 导出 → 打包成 update.img → 上板验证
```

> ⚠️ **牢记第 ⑤ 步的顺序**：**先 `resize2fs` 缩文件系统** ✓，**再 `truncate` 缩文件** ✓
> 反过来（先 truncate）会**直接破坏文件系统** ✗

---

## 1. 创建工作副本

```bash
mkdir -p ~/rk3588-task-d/{images,rootfs,logs}

cp --update=none --sparse=always \
  "/mnt/d/linux/sdk编译包/分体包/ubuntu-rootfs.img" \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img

# 校验副本和原件一致
cmp \
  "/mnt/d/linux/sdk编译包/分体包/ubuntu-rootfs.img" \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img &&
echo "工作副本校验通过"
```

| 参数 | 作用 |
|---|---|
| `--update=none` | 目标已存在时**不覆盖** ✓ |
| `--sparse=always` | 保留**稀疏文件**特性 ✓（不把空洞写成实数据 ✓ 省空间 ✓）|
| `cmp` | 逐字节比对 ✓ 确认副本无损 ✓ |

> 📁 目录规划：
> ```text
> ~/rk3588-task-d/
> ├── images/   ← 镜像文件
> ├── rootfs/   ← 挂载点
> └── logs/     ← 记录
> ```

---

## 2. 挂载并摸底

### ① 只读挂载

```bash
sudo mount -o loop,ro,noload \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img \
  ~/rk3588-task-d/rootfs
```

| 参数 | 作用 |
|---|---|
| `loop` | 把镜像文件当块设备 ✓ |
| `ro` | 只读 ✓（摸底阶段不改动 ✓）|
| `noload` | **不加载 ext4 日志** ✓（纯只读检查时最安全 ✓）|

> `ubuntu-rootfs-work.img` 里不仅有**文件内容** ✓，还有**目录结构、所有者、权限** ✓
> 挂载后 WSL 就能通过 `rootfs/` 访问镜像内部 ✓

### ② 看总占用

```bash
df -h ~/rk3588-task-d/rootfs
```

### ③ 逐目录摸底

```bash
sudo du -x -h --max-depth=1 ~/rk3588-task-d/rootfs \
  | sort -h \
  | tee ~/rk3588-task-d/logs/usage-before.txt
```

> `-x` = **不跨文件系统** ✓（避免把 `/proc` 之类的也统计进去 ✓）

```text
4.0K    rootfs/boot
4.0K    rootfs/media
4.0K    rootfs/mnt
4.0K    rootfs/oem
4.0K    rootfs/proc
4.0K    rootfs/srv
4.0K    rootfs/userdata
12K     rootfs/tmp
16K     rootfs/dev
16K     rootfs/lost+found
20K     rootfs/home
20K     rootfs/opt
20K     rootfs/root
24K     rootfs/sys
96K     rootfs/run
6.5M    rootfs/info
13M     rootfs/rockchip-test
40M     rootfs/etc
458M    rootfs/var
2.2G    rootfs/usr        ← 【最大头】占 80% 以上 ✗
2.7G    rootfs
```

**结论**：空间几乎全在 **`/usr`** ✓（`/var` 458 MB 次之 ✓）
→ 所以要动的主要是**桌面环境、多媒体、字体图标**这些装在 `/usr` 里的东西 ✓

---

## 3. 切换为可写模式

```bash
sudo umount ~/rk3588-task-d/rootfs &&
sudo mount -o loop,rw \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img \
  ~/rk3588-task-d/rootfs &&
findmnt --mountpoint ~/rk3588-task-d/rootfs
```

> 从 `ro` 换成 `rw` ✓ —— 后面要真的删东西了 ✓

---

## 4. 搭好 chroot 运行环境

**目标**：制作一个**通过串口和 SSH 管理**的精简 Ubuntu ✓
只留最基础的东西 ✓ —— 后面要用什么自己再装 ✓

```bash
task_root="$HOME/rk3588-task-d/rootfs"

# /dev：完整镜像进去，但设为从属（卸载时不会连带卸载宿主的）
sudo mount --rbind /dev "$task_root/dev" &&
sudo mount --make-rslave "$task_root/dev" &&

# /proc
sudo mount -t proc -o nosuid,nodev,noexec proc "$task_root/proc" &&

# /sys：先 bind，再 remount 成只读
sudo mount --bind /sys "$task_root/sys" &&
sudo mount -o remount,bind,ro "$task_root/sys" &&

# 构建期禁止服务自启
printf '#!/bin/sh\nexit 101\n' \
  | sudo tee "$task_root/usr/sbin/policy-rc.d" >/dev/null &&
sudo chmod 755 "$task_root/usr/sbin/policy-rc.d"
```

| 挂载 | 作用 |
|---|---|
| `/dev`（`--rbind` + `--make-rslave`）| 让镜像里的程序能访问设备 ✓，但**卸载时不传播**回宿主 ✓ |
| `/proc` | 进程信息接口 ✓ |
| `/sys`（bind + 只读 remount）| 内核/硬件信息接口 ✓，只读更安全 ✓ |
| `policy-rc.d` | 禁止 `apt` 期间启动服务 ✓（**打包前会删掉** ✓）|

### 验证 chroot 能跑 ARM64 程序

```bash
sudo chroot "$task_root" /usr/bin/dpkg --print-architecture
sudo chroot "$task_root" /bin/cat /etc/os-release
```

```text
arm64
PRETTY_NAME="Ubuntu 26.04.1 LTS"
```

> ✅ 看到 `arm64` 和 `Ubuntu 26.04.1 LTS` ✓
> 说明 **WSL 正在借助 QEMU 运行工作镜像中的 ARM64 程序** ✓，并读取镜像内的系统配置 ✓
> （QEMU 的 `binfmt_misc` 注册在《B_SDK编译》阶段已经做好了 ✓）

---

## 5. 保护关键包（apt-mark manual）

**为什么必须先做这一步？**
直接删桌面 ✗ → 容易被 `apt` 把**你还要的东西**当成"桌面的依赖"顺手删掉 ✗
→ 所以先**明确标注"这些是手动安装的，不许删"** ✓

```bash
sudo chroot ~/rk3588-task-d/rootfs apt-mark manual \
  apt dpkg ubuntu-keyring ca-certificates \
  openssh-server openssh-client openssh-sftp-server \
  network-manager netplan.io wpasupplicant wireless-regdb \
  iw rfkill iproute2 iputils-ping wget \
  systemd systemd-sysv systemd-timesyncd udev dbus \
  kmod e2fsprogs util-linux mount \
  login passwd adduser sudo bash busybox coreutils \
  grep sed findutils tar gzip xz-utils \
  tzdata locales python3 procps netbase
```

> ⚠️ **这个清单少了 4 个关键包** ✗ —— 见 [附录 B](#附录-b-apt-mark-清单建议补这几项) ✓
> 如果漏了 `linux-firmware` / `linux-modules-*` ✓，后面的 `autoremove` 可能把它们删掉 ✗ → **WiFi 或模块加载直接废** ✗✓

---

## 6. 分四批卸载软件

> 统一使用的 apt 参数：
>
> | 参数 | 作用 |
> |---|---|
> | `--no-download` | **只用本地缓存**，不联网 ✓ |
> | `--no-install-recommends` | 不装推荐包 ✓ |
> | `-o Dir::State::lists=/nonexistent` | **不读软件源索引** ✓（纯按 dpkg 数据库处理 ✓ 更快更稳 ✓）|
> | `-o Dir::Cache::pkgcache=` | 不用 pkgcache ✓ |
> | `-o Dir::Cache::srcpkgcache=` | 不用 srcpkgcache ✓ |

### 6.1 第一批：GNOME 桌面及主要桌面应用

**包含**：图形登录界面（`gdm3` ✓）、桌面（`gnome-shell` ✓）、设置中心 ✓、文件管理器 ✓、图形终端 ✓、图形更新工具 ✓

> 💡 **删掉图形更新工具后，SSH 里的 `apt update` / `apt install` 仍然照常可用** ✓

```bash
sudo chroot ~/rk3588-task-d/rootfs apt-get \
  --no-download --no-install-recommends \
  -o Dir::State::lists=/nonexistent \
  -o Dir::Cache::pkgcache= \
  -o Dir::Cache::srcpkgcache= \
  purge \
  ubuntu-desktop ubuntu-desktop-minimal ubuntu-session \
  gdm3 gnome-shell gnome-control-center \
  nautilus gnome-terminal \
  ubuntu-release-upgrader-gtk update-manager update-notifier
```

### 6.2 第二批：蓝牙 + 音频 + 音视频组件

```bash
sudo chroot ~/rk3588-task-d/rootfs apt-get \
  --no-download --no-install-recommends \
  -o Dir::State::lists=/nonexistent \
  -o Dir::Cache::pkgcache= \
  -o Dir::Cache::srcpkgcache= \
  purge \
  bluez \
  pipewire pipewire-alsa pipewire-audio pipewire-bin pipewire-pulse \
  wireplumber \
  alsa-base alsa-utils linux-sound-base sound-theme-freedesktop \
  gstreamer1.0-alsa gstreamer1.0-pipewire \
  gstreamer1.0-plugins-base-apps gstreamer1.0-plugins-extra \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-tools
```

### 6.3 第三批：杂项支持包

**内容**：调制解调器 ✓、PPTP 拨号 ✓、USB 模式切换 ✓、打印/扫描 ✓、色彩管理 ✓、定位服务 ✓、SPICE 代理 ✓

```bash
sudo chroot ~/rk3588-task-d/rootfs apt-get \
  --no-download --no-install-recommends \
  -o Dir::State::lists=/nonexistent \
  -o Dir::Cache::pkgcache= \
  -o Dir::Cache::srcpkgcache= \
  purge \
  modemmanager network-manager-pptp pptp-linux ppp \
  usb-modeswitch usb-modeswitch-data \
  ghostscript openprinting-ppds foomatic-db-compressed-ppds \
  printer-driver-pnm2ppa libsane1 libsane-common \
  colord colord-data geoclue-2.0 spice-vdagent
```

### 6.4 清理不再需要的自动依赖

```bash
sudo chroot ~/rk3588-task-d/rootfs apt-get \
  --no-download --no-install-recommends \
  -o Dir::State::lists=/nonexistent \
  -o Dir::Cache::pkgcache= \
  -o Dir::Cache::srcpkgcache= \
  autoremove --purge
```

### 6.5 第四批：图形残留 + 字体 + 图标 + 完整版 Vim

```bash
sudo chroot ~/rk3588-task-d/rootfs apt-get \
  --no-download --no-install-recommends \
  -o Dir::State::lists=/nonexistent \
  -o Dir::Cache::pkgcache= \
  -o Dir::Cache::srcpkgcache= \
  --autoremove purge \
  adwaita-icon-theme hicolor-icon-theme \
  fonts-dejavu-core fonts-dejavu-mono \
  fonts-noto-cjk fonts-noto-mono \
  fonts-urw-base35 fonts-wqy-zenhei \
  xfonts-encodings xfonts-utils \
  gcr gcr4 gnome-keyring gnome-keyring-pkcs11 pinentry-gnome3 \
  glycin-loaders glycin-thumbnailers \
  gvfs gvfs-backends gvfs-common gvfs-daemons gvfs-libs \
  udisks2 xauth \
  gstreamer1.0-plugins-base v4l-utils \
  vim vim-common vim-runtime
```

> 💡 这里一次删掉了 **`gnome-keyring`** ✓ 和 **`vim`** ✓
> —— WiFi 密码改由 `NetworkManager` 自己保存 ✓（`nmcli` 连接时写入 ✓）
> —— 只剩 `nano`/`busybox` 可用作编辑器 ✓（**如果 nano 被 autoremove 了就补装** ✓ 见附录 B ✓）

---

## 7. 手工删除 Chromium

**目标**：什么都自己装、什么都不留 ✓ → 所以要**手工砍掉** Chromium 的残留文件 ✓

```bash
sudo chroot ~/rk3588-task-d/rootfs /bin/rm -rf -- \
  /usr/lib/chromium-browser \
  /usr/bin/chromium-browser \
  /usr/share/applications/chromium-browser.desktop \
  /usr/share/chromium-browser \
  /etc/chromium-browser \
  /opt/chromium-rkmpp-codec-filter
```

> 最后那个 `/opt/chromium-rkmpp-codec-filter` 是 **Rockchip 的硬件解码滤镜** ✓ —— 不做多媒体了就不需要 ✓

---

## 8. 清理 apt 缓存

```bash
sudo chroot ~/rk3588-task-d/rootfs apt-get clean &&

sudo chroot ~/rk3588-task-d/rootfs /bin/rm -f -- \
  /var/cache/apt/pkgcache.bin \
  /var/cache/apt/srcpkgcache.bin
```

---

## 9. 查看裁剪效果

```bash
df -h ~/rk3588-task-d/rootfs
```

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/loop0      3.1G  680M  2.3G  23% /home/zzy/rk3588-task-d/rootfs
```

**内容从 2.7 GB → 680 MB** ✓✓（省了 **75%** ✓）

> ⚠️ **但镜像文件本身还是 3.1 GB** ✗ —— 因为 ext4 只是把块标记为"空闲" ✓，文件不会自动缩小 ✓
> → 见第 15 节的压缩 ✓

---

## 10. 配置启动方式

### ① 默认进入命令行（不再进图形桌面）

```bash
sudo systemctl --root="$HOME/rk3588-task-d/rootfs" \
  set-default multi-user.target
```

### ② 启用需要的服务

```bash
sudo systemctl --root="$HOME/rk3588-task-d/rootfs" \
  enable NetworkManager.service ssh.socket serial-getty@ttyFIQ0.service
```

> 💡 `ssh.socket`（**socket 激活**）—— Ubuntu 24.04+ 默认用 socket 激活代替 `ssh.service` ✓
> `--root=` 是**离线操作** ✓（不启动 systemd 也能改 ✓）

---

## 11. 配置 DNS

```bash
printf '[main]\ndns=default\nrc-manager=file\n' \
  | sudo tee ~/rk3588-task-d/rootfs/etc/NetworkManager/conf.d/90-minimal-dns.conf
```

> 作用：让 `NetworkManager` 联网后**直接写 `/etc/resolv.conf` 文件** ✓
> 这样板子上就能正常解析域名 ✓ → `apt update` 才能用 ✓

---

## 12. 关闭无用服务

```bash
sudo systemctl --root="$HOME/rk3588-task-d/rootfs" disable \
  bluetooth-rfkill-unblock.service \
  rkaiq_3A.service \
  usb-gadget.service
```

| 服务 | 为什么关 |
|---|---|
| `bluetooth-rfkill-unblock` | 蓝牙已经卸载了 ✓ |
| `rkaiq_3A` | 摄像头 3A 服务 ✓ 不用了 ✓ |
| `usb-gadget` | USB 从设备模式 ✓ 不用了 ✓ |

### 删除两个无效的启动链接

```bash
sudo chroot ~/rk3588-task-d/rootfs /bin/rm -f -- \
  /etc/systemd/system/display-manager.service \
  /etc/systemd/system/multi-user.target.wants/bt_8852bs.service
```

> 这两个指向的**目标已经不存在**了 ✓（`gdm3` 卸了 ✓、`bluez` 卸了 ✓）→ 留着就是**报错源** ✗

### 修正服务文件权限

```bash
sudo chmod 644 \
  ~/rk3588-task-d/rootfs/usr/lib/systemd/system/bluetooth-rfkill-unblock.service \
  ~/rk3588-task-d/rootfs/usr/lib/systemd/system/rkaiq_3A.service
```

> 服务描述文件**不需要执行权限** ✓，也**不应允许所有用户修改** ✓ → `644` 正确 ✓

---

## 13. 移除构建期临时策略

```bash
sudo chroot ~/rk3588-task-d/rootfs /bin/rm -f -- \
  /usr/sbin/policy-rc.d
```

> ⚠️ **这一步千万别忘** ✗
> `policy-rc.d` 是**构建期的临时策略** ✓（`exit 101` = 拒绝所有服务启动 ✗）
> 忘了删 ✓ → 板子上**所有服务都不会自动启动** ✗✓ → **串口/SSH 全连不上** ✗✓

---

## 14. 卸载镜像并检查文件系统

```bash
sync &&

sudo umount -R ~/rk3588-task-d/rootfs/dev &&
sudo umount ~/rk3588-task-d/rootfs/proc &&
sudo umount ~/rk3588-task-d/rootfs/sys &&
sudo umount ~/rk3588-task-d/rootfs
```

> ⚠️ **卸载顺序**：先 `/dev`（`-R` 递归 ✓）、再 `/proc`、再 `/sys`、最后镜像本身 ✓

### 检查文件系统一致性

```bash
sudo e2fsck -f ~/rk3588-task-d/images/ubuntu-rootfs-work.img
```

```text
e2fsck 1.47.0 (5-Feb-2023)
Pass 1: Checking inodes, blocks, and sizes
Pass 2: Checking directory structure
Pass 3: Checking directory connectivity
Pass 3A: Optimizing directories
Inode 18468 extent tree (at level 1) could be shorter.  Optimize<y>? yes
Pass 4: Checking reference counts
Pass 5: Checking group summary information

/home/zzy/rk3588-task-d/images/ubuntu-rootfs-work.img: ***** FILE SYSTEM WAS MODIFIED *****
/home/zzy/rk3588-task-d/images/ubuntu-rootfs-work.img: 22767/210912 files (0.1% non-contiguous), 206354/842752 blocks
```

> 💡 `Optimize<y>?` 是**交互提问** ✓ —— 输入 `yes` 或直接回车 ✓
> 想避免交互（脚本里）→ 加 **`-y`**：`sudo e2fsck -fy <img>` ✓

---

## 15. 压缩镜像体积（关键两步）

> **顺序绝对不能反** ✗：
> ```text
> ✔ 正确：resize2fs（缩文件系统）→ truncate（缩文件）→ e2fsck
> ✘ 错误：truncate 先做 → 文件系统被截断 → 镜像直接废
> ```

### ① 缩小文件系统

```bash
sudo resize2fs \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img 1280M
```

**为什么是 1280M？**
```
清理后实际内容 : 680 MB
留 600 MB 余量 : 首次启动、日志、临时文件、以后装点小软件
→ 1280M 合适 ✓
```

### ② 缩小镜像文件

```bash
sudo truncate -s 1280M \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img
```

### ③ 再检查一次

```bash
sudo e2fsck -f \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img
```

### ④ 确认变小了

```bash
ls -lh ~/rk3588-task-d/images/ubuntu-rootfs-work.img
```

```text
-rwxr-xr-x 1 zzy zzy 1.3G Oct 31 11:33 /home/zzy/rk3588-task-d/images/ubuntu-rootfs-work.img
```

> `1280M` → `ls` 显示 **1.3G** ✓（1024 进制换算 ✓）

---

## 16. 【原理】为什么删了软件镜像不变小

> **镜像 = 一个系统盘的克隆** ✓
> 烧录就是把这个系统盘**整盘复制**到板子的存储分区 ✓，**不是"安装包式"的安装** ✓

所以：

| 事实 | 解释 |
|---|---|
| 镜像可以从现有分区**复制**出来 | 本质是 dd/镜像文件 ✓ |
| 也可以先**创建空 ext4**，再把文件放进去 | 《A_roofs替换》就是这么做的 ✓ |
| **删了软件，镜像不会自动变小** | ext4 只是把块标为"空闲" ✓，文件长度不变 ✓ |
| 所以必须 `resize2fs` + `truncate` | 手动把文件系统 + 文件都缩掉 ✓ |

> **根文件系统镜像保存的是"已经准备好的文件系统及其结构"** ✓
> **烧录是把它部署到存储分区** ✓
> **软件安装主要发生在镜像制作阶段** ✓，首次启动可能再完成少量初始化 ✓

---

## 17. 导出精简 rootfs

```bash
cp --update=none --sparse=always \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img \
  "/mnt/d/linux/sdk编译包/分体包/ubuntu26-minimal-rootfs.img" &&

cmp \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img \
  "/mnt/d/linux/sdk编译包/分体包/ubuntu26-minimal-rootfs.img" &&

echo "精简 rootfs 导出并校验通过"
```

👉 导出到：`D:\linux\sdk编译包\分体包\ubuntu26-minimal-rootfs.img`

---

## 18. 打包成 update.img

> 需要借 SDK 里的打包工具 ✓ → 先把 SDK 虚拟盘挂上 ✓

### ① 挂载 SDK 虚拟盘（Windows 管理员 PowerShell）

```powershell
wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare
```

### ② 准备打包目录

```bash
task_src="/mnt/d/linux/sdk编译包/分体包"
task_pack="$HOME/rk3588-task-d/pack"
task_tools="/mnt/rk3588/atk_dlrk3588_linux6.12/tools/linux/Linux_Pack_Firmware/rockdev"

mkdir -p "$task_pack/Image" "$task_pack/tools" &&

# 打包工具
cp --update=none \
  "$task_tools/afptool" "$task_tools/rkImageMaker" \
  "$task_pack/tools/" &&

# 除 rootfs 外的 8 个分区：沿用 SDK 编译产物
cp --update=none \
  "$task_src"/{MiniLoaderAll.bin,uboot.img,boot.img,recovery.img,misc.img,oem.img,userdata.img,parameter.txt} \
  "$task_pack/Image/" &&

# 把我们裁剪好的 rootfs 放进去
cp --update=none \
  "$task_src/ubuntu26-minimal-rootfs.img" \
  "$task_pack/Image/rootfs.img"
```

> 💡 注意：《A_roofs替换》里用的是**软链接** ✓，这里用的是**实际复制** ✓
> —— 两种都行 ✓；复制会**多占一份空间** ✗，但**不会因为原件移动而断链** ✓

### ③ 写打包清单 `package-file`

> 《A_roofs替换》是**直接复制 SDK 自带的 `rk3588-package-file`** ✓
> 这里是**手写一份** ✓ —— 格式是 `NAME PATH` ✓，效果一样 ✓ ✓

```bash
cat > "$task_pack/Image/package-file" <<'EOF'
# NAME PATH
package-file package-file
parameter parameter.txt
bootloader MiniLoaderAll.bin
uboot uboot.img
misc misc.img
boot boot.img
recovery recovery.img
backup RESERVED
oem oem.img
userdata userdata.img
rootfs rootfs.img
EOF
```

### ④ 执行打包

```bash
cd ~/rk3588-task-d/pack &&

# 第一步：把各分区打成一个 raw 镜像
./tools/afptool -pack ./Image update.raw.img &&

# 第二步：加 RK3588 固件头，生成可烧录镜像
./tools/rkImageMaker -RK3588 \
  ./Image/MiniLoaderAll.bin \
  update.raw.img \
  atk_dlrk3588_ubuntu26_minimal_update.img \
  -os_type:androidos
```

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

👉 产物：`D:\linux\sdk编译包\atk_dlrk3588_ubuntu26_minimal_update.img`

---

## 19. 上板验证

```text
✓ 烧录 → 开机
✓ 串口连接成功
✓ 配置 WiFi（nmcli）
✓ SSH 登录成功
```

**裁剪完成 ⭐**

| 阶段 | 体积 |
|---|---|
| 原始 rootfs 内容 | 2.7 GB |
| 裁剪后内容 | **680 MB**（-75% ✓）|
| 原始镜像文件 | 3.1 GB |
| 裁剪后镜像文件 | **1.3 GB**（-58% ✓）|

---

## 附录 A：完整命令速查

```bash
# ========== 1. 创建工作副本 ==========
mkdir -p ~/rk3588-task-d/{images,rootfs,logs}
cp --update=none --sparse=always \
  "/mnt/d/linux/sdk编译包/分体包/ubuntu-rootfs.img" \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img
cmp "/mnt/d/linux/sdk编译包/分体包/ubuntu-rootfs.img" \
    ~/rk3588-task-d/images/ubuntu-rootfs-work.img && echo "副本校验通过"

# ========== 2. 只读挂载 + 摸底 ==========
sudo mount -o loop,ro,noload ~/rk3588-task-d/images/ubuntu-rootfs-work.img ~/rk3588-task-d/rootfs
df -h ~/rk3588-task-d/rootfs
sudo du -x -h --max-depth=1 ~/rk3588-task-d/rootfs | sort -h | tee ~/rk3588-task-d/logs/usage-before.txt

# ========== 3. 切换为可写 ==========
sudo umount ~/rk3588-task-d/rootfs
sudo mount -o loop,rw ~/rk3588-task-d/images/ubuntu-rootfs-work.img ~/rk3588-task-d/rootfs
findmnt --mountpoint ~/rk3588-task-d/rootfs

# ========== 4. chroot 环境 ==========
task_root="$HOME/rk3588-task-d/rootfs"
sudo mount --rbind /dev "$task_root/dev" && sudo mount --make-rslave "$task_root/dev"
sudo mount -t proc -o nosuid,nodev,noexec proc "$task_root/proc"
sudo mount --bind /sys "$task_root/sys" && sudo mount -o remount,bind,ro "$task_root/sys"
printf '#!/bin/sh\nexit 101\n' | sudo tee "$task_root/usr/sbin/policy-rc.d" >/dev/null
sudo chmod 755 "$task_root/usr/sbin/policy-rc.d"
sudo chroot "$task_root" /usr/bin/dpkg --print-architecture

# ========== 5. 保护关键包（⚠️ 见附录 B 补充）==========
sudo chroot "$task_root" apt-mark manual \
  apt dpkg ubuntu-keyring ca-certificates \
  openssh-server openssh-client openssh-sftp-server \
  network-manager netplan.io wpasupplicant wireless-regdb \
  iw rfkill iproute2 iputils-ping wget \
  systemd systemd-sysv systemd-timesyncd udev dbus \
  kmod e2fsprogs util-linux mount \
  login passwd adduser sudo bash busybox coreutils \
  grep sed findutils tar gzip xz-utils \
  tzdata locales python3 procps netbase

# ========== 6. 四批卸载（每批都用这组参数）==========
APT_OPTS="--no-download --no-install-recommends -o Dir::State::lists=/nonexistent -o Dir::Cache::pkgcache= -o Dir::Cache::srcpkgcache="

sudo chroot "$task_root" apt-get $APT_OPTS purge \
  ubuntu-desktop ubuntu-desktop-minimal ubuntu-session \
  gdm3 gnome-shell gnome-control-center nautilus gnome-terminal \
  ubuntu-release-upgrader-gtk update-manager update-notifier

sudo chroot "$task_root" apt-get $APT_OPTS purge \
  bluez pipewire pipewire-alsa pipewire-audio pipewire-bin pipewire-pulse wireplumber \
  alsa-base alsa-utils linux-sound-base sound-theme-freedesktop \
  gstreamer1.0-alsa gstreamer1.0-pipewire \
  gstreamer1.0-plugins-base-apps gstreamer1.0-plugins-extra \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-tools

sudo chroot "$task_root" apt-get $APT_OPTS purge \
  modemmanager network-manager-pptp pptp-linux ppp \
  usb-modeswitch usb-modeswitch-data \
  ghostscript openprinting-ppds foomatic-db-compressed-ppds \
  printer-driver-pnm2ppa libsane1 libsane-common \
  colord colord-data geoclue-2.0 spice-vdagent

sudo chroot "$task_root" apt-get $APT_OPTS autoremove --purge

sudo chroot "$task_root" apt-get $APT_OPTS --autoremove purge \
  adwaita-icon-theme hicolor-icon-theme fonts-dejavu-core fonts-dejavu-mono \
  fonts-noto-cjk fonts-noto-mono fonts-urw-base35 fonts-wqy-zenhei \
  xfonts-encodings xfonts-utils \
  gcr gcr4 gnome-keyring gnome-keyring-pkcs11 pinentry-gnome3 \
  glycin-loaders glycin-thumbnailers \
  gvfs gvfs-backends gvfs-common gvfs-daemons gvfs-libs \
  udisks2 xauth gstreamer1.0-plugins-base v4l-utils \
  vim vim-common vim-runtime

# ========== 7. 手工删 Chromium ==========
sudo chroot "$task_root" /bin/rm -rf -- \
  /usr/lib/chromium-browser /usr/bin/chromium-browser \
  /usr/share/applications/chromium-browser.desktop \
  /usr/share/chromium-browser /etc/chromium-browser \
  /opt/chromium-rkmpp-codec-filter

# ========== 8. 清缓存 ==========
sudo chroot "$task_root" apt-get clean
sudo chroot "$task_root" /bin/rm -f -- /var/cache/apt/pkgcache.bin /var/cache/apt/srcpkgcache.bin

# ========== 9. 看效果 ==========
df -h "$task_root"

# ========== 10. 启动配置 ==========
sudo systemctl --root="$task_root" set-default multi-user.target
sudo systemctl --root="$task_root" enable NetworkManager.service ssh.socket serial-getty@ttyFIQ0.service

# ========== 11. DNS ==========
printf '[main]\ndns=default\nrc-manager=file\n' \
  | sudo tee "$task_root/etc/NetworkManager/conf.d/90-minimal-dns.conf"

# ========== 12. 关服务 + 删无效链接 + 修权限 ==========
sudo systemctl --root="$task_root" disable bluetooth-rfkill-unblock.service rkaiq_3A.service usb-gadget.service
sudo chroot "$task_root" /bin/rm -f -- \
  /etc/systemd/system/display-manager.service \
  /etc/systemd/system/multi-user.target.wants/bt_8852bs.service
sudo chmod 644 "$task_root/usr/lib/systemd/system/bluetooth-rfkill-unblock.service" \
               "$task_root/usr/lib/systemd/system/rkaiq_3A.service"

# ========== 13. 删临时策略（⚠️ 必删）==========
sudo chroot "$task_root" /bin/rm -f -- /usr/sbin/policy-rc.d

# ========== 14. 卸载 + 检查 ==========
sync
sudo umount -R "$task_root/dev"
sudo umount "$task_root/proc"
sudo umount "$task_root/sys"
sudo umount "$task_root"
sudo e2fsck -fy ~/rk3588-task-d/images/ubuntu-rootfs-work.img

# ========== 15. 压缩（顺序不能反！）==========
sudo resize2fs ~/rk3588-task-d/images/ubuntu-rootfs-work.img 1280M
sudo truncate -s 1280M ~/rk3588-task-d/images/ubuntu-rootfs-work.img
sudo e2fsck -fy ~/rk3588-task-d/images/ubuntu-rootfs-work.img
ls -lh ~/rk3588-task-d/images/ubuntu-rootfs-work.img

# ========== 17. 导出 ==========
cp --update=none --sparse=always \
  ~/rk3588-task-d/images/ubuntu-rootfs-work.img \
  "/mnt/d/linux/sdk编译包/分体包/ubuntu26-minimal-rootfs.img"
cmp ~/rk3588-task-d/images/ubuntu-rootfs-work.img \
    "/mnt/d/linux/sdk编译包/分体包/ubuntu26-minimal-rootfs.img" && echo "导出校验通过"

# ========== 18. 打包 update.img ==========
task_src="/mnt/d/linux/sdk编译包/分体包"
task_pack="$HOME/rk3588-task-d/pack"
task_tools="/mnt/rk3588/atk_dlrk3588_linux6.12/tools/linux/Linux_Pack_Firmware/rockdev"
mkdir -p "$task_pack/Image" "$task_pack/tools"
cp --update=none "$task_tools/afptool" "$task_tools/rkImageMaker" "$task_pack/tools/"
cp --update=none "$task_src"/{MiniLoaderAll.bin,uboot.img,boot.img,recovery.img,misc.img,oem.img,userdata.img,parameter.txt} "$task_pack/Image/"
cp --update=none "$task_src/ubuntu26-minimal-rootfs.img" "$task_pack/Image/rootfs.img"
cat > "$task_pack/Image/package-file" <<'EOF'
# NAME PATH
package-file package-file
parameter parameter.txt
bootloader MiniLoaderAll.bin
uboot uboot.img
misc misc.img
boot boot.img
recovery recovery.img
backup RESERVED
oem oem.img
userdata userdata.img
rootfs rootfs.img
EOF
cd "$task_pack"
./tools/afptool -pack ./Image update.raw.img
./tools/rkImageMaker -RK3588 ./Image/MiniLoaderAll.bin update.raw.img \
  atk_dlrk3588_ubuntu26_minimal_update.img -os_type:androidos
```

---

## 附录 B：⚠️ apt-mark 清单建议补这几项

**问题**：第 5 节的 `apt-mark manual` 清单里**少了 4 个关键包** ✗
→ 后面的 `autoremove --purge` **有可能把它们当"不再需要"删掉** ✗✓

| 缺的包 | 删掉的后果 |
|---|---|
| **`linux-firmware`** | **WiFi / 蓝牙 / GPU 固件全没** ✗ → 上板后无线直接不工作 ✗✓ |
| **`linux-modules-*`**（对应当前内核版本）| **`/lib/modules` 被清空** ✗ → 所有驱动模块加载失败 ✗✓ |
| **`linux-image-*`** | 内核镜像/initramfs 被删 ✗ → **板子起不来** ✗✓ |
| **`nano`** | 删掉 `vim` 之后**没有任何编辑器** ✗（只剩 `busybox vi` ✓ 能用但很别扭 ✓）|

### 补法（在第 5 节之后执行）

```bash
task_root="$HOME/rk3588-task-d/rootfs"

# 先看当前内核相关的包都叫什么
sudo chroot "$task_root" dpkg -l 'linux-*' | grep '^ii'

# 一次性把固件 + 内核包 + nano 都标为"手动安装，不许删"
sudo chroot "$task_root" apt-mark manual \
  linux-firmware \
  $(sudo chroot "$task_root" dpkg -l 'linux-modules-*' 'linux-image-*' 2>/dev/null | awk '/^ii/{print $2}') \
  nano
```

> 💡 **最保险的做法**：裁剪**前后各跑一次**这个检查
> ```bash
> sudo chroot "$task_root" dpkg -l 'linux-firmware' 'linux-modules-*' 'linux-image-*'
> ```
> 两边都有 `ii`（已安装 ✓）就说明没被误删 ✓
> 如果裁剪后变成了 `rc`（已删除只剩配置 ✗）→ **必须补装回来** ✓

### 顺带：怎么确认 WiFi 固件还在

```bash
ls ${task_root}/lib/firmware/ | head -20
du -sh ${task_root}/lib/firmware
```

> 正常应该有几十 MB ✓（`rtl8852bu_fw`、`regulatory.db` 等 ✓）
> 如果只有几 KB ✗ → 就只剩法规数据库，**芯片固件被删了** ✗
