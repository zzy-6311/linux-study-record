# 用正点 SDK 的 Buildroot 构建带桌面的 rootfs

> **本篇定位**：《B_SDK编译》用的是 **Ubuntu（无桌面）** ✓；本篇换成 **Buildroot（带图形桌面）** ✓
> **同样的 SDK、同样的虚拟盘、同样的烧录流程** ✓ —— 只是**换了 rootfs 体系** ✓
> **最终产物**：`atk_dlrk3588_buildroot_update.img`（**约 2 GB** ✓ —— 比 Debian/Ubuntu 版大，因为带图形桌面 ✓）

---

## 📋 目录

- [0. 目标与思路](#0-目标与思路)
- [1. 挂载虚拟盘 + 备份旧配置](#1-挂载虚拟盘--备份旧配置)
- [2. lunch 选 buildroot](#2-lunch-选-buildroot)
- [3. ⭐ 两套配置（本篇最关键的概念）](#3--两套配置本篇最关键的概念)
- [4. 打开 Buildroot 软件菜单](#4-打开-buildroot-软件菜单)
- [5. 只编 rootfs（关掉全量编译）](#5-只编-rootfs关掉全量编译)
- [6. 编译 rootfs（含"找不到内核"的坑）](#6-编译-rootfs含找不到内核的坑)
- [7. 发现内核不匹配 → 重编内核](#7-发现内核不匹配--重编内核)
- [8. 把内核模块装进 rootfs](#8-把内核模块装进-rootfs)
- [9. 打包成 firmware 镜像](#9-打包成-firmware-镜像)
- [10. 上板验证](#10-上板验证)
- [附录 A：完整命令速查](#附录-a完整命令速查)
- [附录 B：B / C / D / E 四篇对比](#附录-bb--c--d--e-四篇对比)
- [术语表](#术语表)

---

## 0. 目标与思路

**目标**：用 SDK 自带的 **Buildroot** 体系，构建一个**带图形化桌面**的 rootfs ✓

**和已有几篇的关系**：

| 文档 | rootfs 来源 | 特点 |
|---|---|---|
| **B** | SDK 的 **Ubuntu** | 无桌面，纯命令行 ✓ |
| **C**（本篇）| SDK 的 **Buildroot** | **带图形桌面** ✓ |
| **D** | 裁剪 B 的 Ubuntu rootfs | 瘦身（2.7G→680M）✓ |
| **E** | **从零自定制**（U-Boot+内核+Buildroot）| 全自己搞 ✓ |

**流程**：

```text
① 挂载虚拟盘 → 备份原 SDK 配置
② ./build.sh lunch → 选 buildroot 配置（选项 5）
③ 创建自己的两套配置（板级 + Buildroot）
④ ./build.sh buildroot-config → 选要装进 rootfs 的软件
⑤ 关掉"全量编译" → 只编 rootfs
⑥ 编 rootfs（踩"找不到内核"的坑 → 修）
⑦ 发现内核与带桌面的 rootfs 不配对 → 重编内核
⑧ 把内核模块装进 rootfs → 重编 rootfs
⑨ 打包 firmware（复用 Loader/U-Boot）→ 上板验证
```

---

## 1. 挂载虚拟盘 + 备份旧配置

**① 挂载虚拟盘（Windows 管理员 PowerShell）**：

```powershell
wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare
```

**② 进入 WSL**，先**备份原来的 SDK 编译配置**：

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&
cp --update=none output/.config output/.config.before-C-ubuntu
```

> 💡 **为什么要备份？** `/mnt/rk3588/.../output/.config` 是**上一次（B 篇 Ubuntu）**的编译配置 ✓
> 这次换成 buildroot ✓ → **先把旧配置留一份** ✓，出问题可以对比/回退 ✓

---

## 2. lunch 选 buildroot

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12
./build.sh lunch
```

会列出 7 个配置：

```text
1. 01_atk_dlrk3588_auto2mipi_2hdmi_defconfig
2. 02_atk_dlrk3588_2mipi720x1280_2hdmi_defconfig
3. 03_atk_dlrk3588_2mipi800x1280_2hdmi_defconfig
4. 04_atk_dlrk3588_2mipi1080x1920_2hdmi_defconfig
5. 05_atk_dlrk3588_2dp_2hdmi_defconfig          ← 【选这个】Buildroot
6. 06_atk_dlrk3588_ubuntu_panthor_auto2mipi_2hdmi_defconfig
7. 07_atk_dlrk3588_ubuntu_panthor_2dp_2hdmi_defconfig
Which would you like? [1]:
```

| 选项 | rootfs 类型 |
|---|---|
| 1–5 | **Buildroot** ✓ |
| 6–7 | Ubuntu ✓ |

**输入 `5` 回车** ✓

---

## 3. ⭐ 两套配置（本篇最关键的概念）

### 3.1 为什么会"要配两个地方"

**疑问**：我们不是只配 rootfs 吗 ✓，为什么还要配 **SDK 的板级配置** ✗？

**答案**：因为 **rootfs 必须和内核配套** ✓✓

| 配置 | 位置 | 决定什么 |
|---|---|---|
| **① SDK 板级配置** | `device/rockchip/rk3588/*_defconfig` | **用哪套内核、设备树、rootfs 类型** ✓ |
| **② Buildroot 配置** | `buildroot/configs/*_defconfig` | **rootfs 里装哪些软件** ✓ |

> 🔑 **两者必须一致** ✓：
> 内核要能驱动 rootfs 里的程序（GPU 驱动、内核模块版本 ✓）
> 所以**改 rootfs 类型时，板级配置也得跟着换** ✓

### 3.2 创建自己的一套配置

**做法**：把官方的两份配置**复制成自己名字的副本** ✓ → **改副本，不动原件** ✓（安全 ✓，且下次编译还在 ✓）

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&

# ① Buildroot 软件配置：alientek → zzy
cp --update=none buildroot/configs/alientek_rk3588_defconfig \
                  buildroot/configs/zzy_rk3588_defconfig &&

# ② SDK 板级配置：05 → 08
cp --update=none device/rockchip/rk3588/05_atk_dlrk3588_2dp_2hdmi_defconfig \
                  device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig &&

# ③ 让板级配置指向我们自己的 buildroot 配置
sed -i 's/^RK_BUILDROOT_BASE_CFG=.*/RK_BUILDROOT_BASE_CFG="zzy_rk3588"/' \
  device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig &&

# ④ 加载新配置
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig
```

**命名对照**：

| 官方 | 我们的 |
|---|---|
| `buildroot/configs/alientek_rk3588_defconfig` | **`zzy_rk3588_defconfig`** ✓ |
| `device/rockchip/rk3588/05_atk_dlrk3588_2dp_2hdmi_defconfig` | **`08_zzy_rk3588_2dp_2hdmi_defconfig`** ✓ |
| `RK_BUILDROOT_BASE_CFG="alientek_rk3588"` | **`"zzy_rk3588"`** ✓ |

---

## 4. 打开 Buildroot 软件菜单

### 4.1 先屏蔽 PATH 里的空格

```bash
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)"
```

> ⚠️ **和 D 篇一样的坑** ✓：Windows 的 `C:\Program Files\...` 会以 `/mnt/c/Program Files/...` 混进 WSL 的 PATH ✓
> → Buildroot 硬性要求 **PATH 里不能有空格** ✗
> ⚠️ **这是临时的** ✓ —— 新开终端就失效 ✓，每次跑 `./build.sh` 前都要执行 ✓

### 4.2 进 menuconfig 选软件

```bash
./build.sh buildroot-config
```

> 会出现一个**蓝底的 menuconfig** ✓ —— 这就是**选择 rootfs 里要带哪些软件** 的地方 ✓

**选 `kmod` 与 `kmod utilities`**：

```text
1. 按  /  打开搜索
2. 输入  BR2_PACKAGE_KMOD_TOOLS
3. 按搜索结果里对应的数字编号，跳进 kmod utilities
4. 按  y  确认选中
5. 保存退出
```

> **结果**：`kmod`（提供 `lsmod` / `modprobe` / `depmod`）+ `kmod utilities` 都会编进 rootfs ✓

### 4.3 和 Debian 方式的差别（原文的解释）

> **Debian 的编译**：是**下载 Debian 包来编译** ✓ —— 形式上等于**模拟出 Debian 环境** ✓，再在模拟终端里装软件 ✓
> **Buildroot 不一样**：它是**通过 SDK 编译** ✓ —— 用**直接选择的方式** ✓，即**改配置文件** ✓
>
> **好处**：
> - 下次编译**还是带着这些软件** ✓（配置持久化 ✓）
> - 和 Debian 一样，**开机后系统就自带这些软件** ✓
>
> **也可以不配置** ✓ → 等上板后再自行安装 ✓
> **这只是"批量编译配置"的方法** ✓

---

## 5. 只编 rootfs（关掉全量编译）

**默认流程是全量编译** ✗（内核 + U-Boot + rootfs 全来 ✓）→ 这次**只要 rootfs** ✓ → 先关掉

```bash
sed -i 's/^RK_ROOTFS_INSTALL_MODULES=y$/# RK_ROOTFS_INSTALL_MODULES is not set/' \
  device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig &&

./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig
```

> 💡 `RK_ROOTFS_INSTALL_MODULES` = **把内核模块装进 rootfs** ✓
> 现在**还没编内核** ✓ → 没有模块可装 ✗ → **先关掉** ✓
> （第 8 节编完内核后**会重新打开它** ✓）

**开始编 rootfs**：

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)" &&
./build.sh rootfs
```

---

## 6. 编译 rootfs（含"找不到内核"的坑）

### 6.1 报错

> 编译到最后报错 ✓ —— 提示 **"找不到内核"** ✗（原文没贴具体输出 ✓）

### 6.2 原因

> 我们是**单独编译 rootfs** ✓ → **没有内核** ✓ → 而编译流程里**有一段要读内核** ✗

**具体是哪一段？** 是 **`RK_ROOTFS_DEBUG_INFO`** ✓
> 打开它时 ✓ → 构建流程会去**取内核的调试信息** ✗ → 可我们**没有内核** ✗✓

### 6.3 修复

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&
printf '\n# RK_ROOTFS_DEBUG_INFO is not set\n' >> device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig &&
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig &&
./build.sh rootfs
```

### 6.4 编译成功

```text
saving cache in little endian encoding
Running 99-owner.sh for /mnt/rk3588/atk_dlrk3588_linux6.12/buildroot/output/zzy_rk3588/target (buildroot init=busybox)...
Running 30-rootfs.sh - build_buildroot /mnt/rk3588/atk_dlrk3588_linux6.12/output/buildroot/images succeeded.
Running 30-rootfs.sh - build_rootfs rootfs succeeded.
```

**镜像位置**：

```text
/mnt/rk3588/atk_dlrk3588_linux6.12/output/firmware/rootfs.img
```

---

## 7. 发现内核不匹配 → 重编内核

### 7.1 问题

> 把镜像复制出来 ✓ → 打包准备上板 ✓ → **发现原来 Ubuntu 用的内核不对** ✗

**原因**：

| 对比 | B 篇（Ubuntu）| 本篇（Buildroot）|
|---|---|---|
| rootfs | 无桌面 ✓ | **带图形化桌面** ✗ |
| 内核要求 | 基础驱动即可 ✓ | **需要显卡/显示相关驱动** ✗ |
| 结果 | — | **原内核不匹配** ✗ → 进不了桌面 ✗ |

> ✅ **但 Loader 和 U-Boot 可以复用** ✓ —— 它们**和内核/rootfs 无关** ✓

### 7.2 重编配套内核

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)" &&
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig &&
./build.sh kernel
```

> **关键**：用**同一份 `08_zzy...` 板级配置**编内核 ✓ → 这样内核参数、设备树、GPU 驱动都和 buildroot rootfs 配套 ✓✓

---

## 8. 把内核模块装进 rootfs

内核编好了 ✓ → 现在**把模块装进 rootfs** ✓ → **重编一次 rootfs** ✓

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&

# 重新打开"安装内核模块"
sed -i 's/^# RK_ROOTFS_INSTALL_MODULES is not set$/RK_ROOTFS_INSTALL_MODULES=y/' \
  device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig &&

./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig &&

export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)" &&
./build.sh rootfs
```

> 🔑 **顺序不能反** ✓：**先编内核 → 再开 INSTALL_MODULES → 再编 rootfs** ✓
> 反了的话 ✓ → `INSTALL_MODULES=y` 但**模块还不存在** ✗ → 又报错 ✓

---

## 9. 打包成 firmware 镜像

**把 D 篇（分体包）里已经验证过的 Loader/U-Boot/misc/recovery 复制进来** ✓ → 再整体打包 ✓

```bash
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&
cp --update=none \
  /mnt/d/linux/sdk编译包/分体包/MiniLoaderAll.bin \
  /mnt/d/linux/sdk编译包/分体包/uboot.img \
  /mnt/d/linux/sdk编译包/分体包/misc.img \
  /mnt/d/linux/sdk编译包/分体包/recovery.img \
  output/firmware/ &&
./build.sh firmware
```

**分区来源汇总**：

| 分区 | 来源 |
|---|---|
| `MiniLoaderAll.bin` | D 篇分体包 ✓ 复用 |
| `uboot.img` | D 篇分体包 ✓ 复用 |
| `misc.img` / `recovery.img` | D 篇分体包 ✓ 复用 |
| `boot.img` | **本次新编的内核** ✓ |
| `rootfs.img` | **本次新编的 Buildroot rootfs** ✓ |

**产物**：

```text
D:\linux\sdk编译包\atk_dlrk3588_buildroot_update.img
```

> 💡 **约 2 GB** ✓ —— 比原来的 Debian 包大 ✗
> **原因**：**多了图形化桌面** ✓（桌面环境体积很大 ✓）

---

## 10. 上板验证

```text
① 烧录 update.img
② 成功开机 ✓
③ 串口工具 → 配网络（WiFi）
④ SSH 登录 ✓
⑤ 完工 ✓
```

**登录信息**：

```text
用户名: root
密码  : root
```

---

## 附录 A：完整命令速查

```bash
# ========== 0. 挂载虚拟盘（Windows 管理员）==========
#   wsl --mount "H:\wsl\rk3588-ext4.vhdx" --vhd --bare

# ========== 1. 备份旧配置 ==========
cd /mnt/rk3588/atk_dlrk3588_linux6.12
cp --update=none output/.config output/.config.before-C-ubuntu

# ========== 2. lunch 选 5（buildroot）==========
./build.sh lunch

# ========== 3. 创建自己的两套配置 ==========
cd /mnt/rk3588/atk_dlrk3588_linux6.12
cp --update=none buildroot/configs/alientek_rk3588_defconfig buildroot/configs/zzy_rk3588_defconfig
cp --update=none device/rockchip/rk3588/05_atk_dlrk3588_2dp_2hdmi_defconfig device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig
sed -i 's/^RK_BUILDROOT_BASE_CFG=.*/RK_BUILDROOT_BASE_CFG="zzy_rk3588"/' device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig

# ========== 4. Buildroot 菜单（PATH 去空格 → menuconfig）==========
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)"
./build.sh buildroot-config
#   → 按 / 搜索 BR2_PACKAGE_KMOD_TOOLS → 选中 → y → 保存退出

# ========== 5. 只编 rootfs（先关 INSTALL_MODULES）==========
sed -i 's/^RK_ROOTFS_INSTALL_MODULES=y$/# RK_ROOTFS_INSTALL_MODULES is not set/' device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig
cd /mnt/rk3588/atk_dlrk3588_linux6.12
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)"
./build.sh rootfs

# ========== 6. 遇"找不到内核" → 关 DEBUG_INFO 再编 ==========
printf '\n# RK_ROOTFS_DEBUG_INFO is not set\n' >> device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig
./build.sh rootfs

# ========== 7. 重编配套内核 ==========
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)"
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig
./build.sh kernel

# ========== 8. 装内核模块 → 重编 rootfs ==========
sed -i 's/^# RK_ROOTFS_INSTALL_MODULES is not set$/RK_ROOTFS_INSTALL_MODULES=y/' device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)"
./build.sh rootfs

# ========== 9. 打包 firmware ==========
cp --update=none \
  /mnt/d/linux/sdk编译包/分体包/MiniLoaderAll.bin \
  /mnt/d/linux/sdk编译包/分体包/uboot.img \
  /mnt/d/linux/sdk编译包/分体包/misc.img \
  /mnt/d/linux/sdk编译包/分体包/recovery.img \
  output/firmware/
./build.sh firmware
```

---

## 附录 B：B / C / D / E 四篇对比

| | **B** | **C**（本篇）| **D** | **E** |
|---|---|---|---|---|
| **rootfs 来自** | SDK 的 Ubuntu | SDK 的 **Buildroot** | 裁剪 B 的 Ubuntu | **从零 Buildroot** |
| **有桌面吗** | ❌ 无 | ✅ **有图形桌面** | ❌ 无 | ❌ 无 |
| **U-Boot 来源** | SDK 编 | SDK 编（复用 D）| — | **自己编** ✓ |
| **内核来源** | SDK 编 | SDK 编（换版重编）| — | **自己编** ✓ |
| **镜像大小** | — | **≈ 2 GB** | 1.3 GB | — |
| **产物** | `atk_dlrk3588_ubuntu26_update.img` | **`atk_dlrk3588_buildroot_update.img`** | `atk_dlrk3588_ubuntu26_minimal_update.img` | `zzy-rk3588-release-r1-update.img` |
| **登录** | zzy/… | **root/root** | zzy/… | root/… |

**共用/复用的东西** ✓：

```
虚拟盘 H:\wsl\rk3588-ext4.vhdx
SDK 目录 /mnt/rk3588/atk_dlrk3588_linux6.12
分体包 Loader/U-Boot/misc/recovery（D 篇）
PATH 去空格的那条命令
```

---

## 术语表

| 术语 | 含义 |
|---|---|
| **SDK 板级配置** | `device/rockchip/rk3588/*_defconfig`，决定用哪套内核/设备树/rootfs 类型 |
| **Buildroot 配置** | `buildroot/configs/*_defconfig`，决定 rootfs 里装哪些软件 |
| **`RK_BUILDROOT_BASE_CFG`** | 板级配置里指向 buildroot 配置名的变量 |
| **`build.sh lunch`** | SDK 的配置选择菜单 |
| **`build.sh buildroot-config`** | 打开 Buildroot 的 menuconfig |
| **`build.sh rootfs`** | 只编根文件系统 |
| **`build.sh kernel`** | 只编内核 |
| **`build.sh firmware`** | 把各分区打包成 firmware 目录/镜像 |
| **`RK_ROOTFS_INSTALL_MODULES`** | 是否把内核模块装进 rootfs |
| **`RK_ROOTFS_DEBUG_INFO`** | 是否为 rootfs 带上内核调试信息（**需要内核存在**）|
| **`BR2_PACKAGE_KMOD_TOOLS`** | Buildroot 里的 kmod 工具包（`lsmod`/`modprobe`/`depmod`）|
| **menuconfig** | 蓝底的内核/Buildroot 配置界面 |
| **全量编译** | 内核+U-Boot+rootfs 一起编（SDK 默认行为）|
