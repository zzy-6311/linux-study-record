现在我们来完成从SDK的buildroot来构建我们需要的roofs环境
首先还是先挂载虚拟盘wsl --mount --vhd "H:\wsl\rk3588-ext4.vhdx" --bare，然后进入wsl
然后按照SDK的运行流程：
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&
cp --update=none output/.config output/.config.before-C-ubuntu &&（这里先备份一下原来SDK的编译设置，然后重新配置我们要的新的）
./build.sh lunch

然后就会正常弹出系统选择：
1. 01_atk_dlrk3588_auto2mipi_2hdmi_defconfig
2. 02_atk_dlrk3588_2mipi720x1280_2hdmi_defconfig
3. 03_atk_dlrk3588_2mipi800x1280_2hdmi_defconfig
4. 04_atk_dlrk3588_2mipi1080x1920_2hdmi_defconfig
5. 05_atk_dlrk3588_2dp_2hdmi_defconfig
6. 06_atk_dlrk3588_ubuntu_panthor_auto2mipi_2hdmi_defconfig
7. 07_atk_dlrk3588_ubuntu_panthor_2dp_2hdmi_defconfig
Which would you like? [1]:

选择5，选择我们要的buildroot，完成后我们需要对我们的SDK包进行个人配置，主要是配置两个，第一就是SDK板级配置，决定使用哪套内核、设备树和 rootfs 配置
第二就是Buildroot 配置决定 rootfs 中包含哪些软件。这里可能有个疑惑，我们不是配置roofs编译吗，为什么需要配置SDK的板级配置，因为我们的roofs也需要适配前面的内核，所以配置需要一致，输入配置：
zzy@LAPTOP-4S39SRHC:/mnt/rk3588/atk_dlrk3588_linux6.12$ cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&
cp --update=none buildroot/configs/alientek_rk3588_defconfig buildroot/configs/zzy_rk3588_defconfig &&
cp --update=none device/rockchip/rk3588/05_atk_dlrk3588_2dp_2hdmi_defconfig device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig &&
sed -i 's/^RK_BUILDROOT_BASE_CFG=.*/RK_BUILDROOT_BASE_CFG="zzy_rk3588"/' device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig &&
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig

然后我们下一步就是打开Buildroot的软件配置菜单，在此之前顺便屏蔽一下空格：
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)"
./build.sh buildroot-config

打开后又是一个蓝色的界面，现在就是要选择后面系统中的一些配置软件，直接把他们选择到SDK编译中去
在蓝色页面中，点击“/”，输入BR2_PACKAGE_KMOD_TOOLS，按照搜索结果按下对应的数字编号，进入kmod utilities，再按“y”确认，然后保存退出，现在就是选择kmod和kmod utilities
因为默认的编译流程就是全量编译，但是这次我们只编译roofs，所以先关闭这个默认编译配置：
sed -i 's/^RK_ROOTFS_INSTALL_MODULES=y$/# RK_ROOTFS_INSTALL_MODULES is not set/' device/rockchip/rk3588/08_zzy_rk3588_2dp_2hdmi_defconfig &&
./build.sh 08_zzy_rk3588_2dp_2hdmi_defconfig

（这里解释一下，debain的编译是我们下载了debain的包来编译的，他的形式就是模拟出debain包的运行，然后在模拟终端安装的，但是buildroot不一样，因为是通过SDK编译，所以选择了通过直接选择的方式来编译，也就是配置编译配置文件，这样下次编译还是带有这些文件，和debain一样的是编译出来开机后的系统也是可以自带这些软件的，并且也可以不配置，等上板后再自行安装，这样只是批量编译配置的方法）

现在配全部都配置好了，现在就可以开始编译我们需要的roofs了：
cd /mnt/rk3588/atk_dlrk3588_linux6.12 &&
export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v ' ' | paste -sd ':' -)" &&（这些都是避免路径报错的）
./build.sh rootfs