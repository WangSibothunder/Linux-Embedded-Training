# PWM7 风扇控制部署记录

日期：2026-09-08  
目标板：ELF1 / i.MX6ULL，`192.168.0.232`  
运行内核：`4.1.15-g9bb2b74f`

## 已完成

1. 检查设备树：`pwm-fan-self` 使用 PWM7，周期 50000 ns（20 kHz），引脚为 `CSI_VSYNC__PWM7_OUT`；CSI 控制器已禁用且原 CSI VSYNC 复用已取消。
2. 修复驱动定时器回调中的潜在自等待死锁，将可能睡眠的 PWM 操作移至工作队列，并使用互斥锁保护状态。
3. 修正外部模块 Makefile，明确传递 `ARCH=arm`、交叉编译器和板端内核版本后缀。
4. 在 ELF1 SDK 环境中成功编译 DTB、内核模块和 Qt 5 ARM 程序。
5. 将原启动 DTB 备份后部署新 DTB并重启，确认运行时设备树存在 `/proc/device-tree/pwm-fan-self`。
6. 模块成功加载并绑定到 `/sys/devices/platform/pwm-fan-self`。
7. 实测档位 0～4 对应 PWM 0、64、128、192、255，全部通过读回验证。
8. 实测 2 秒自动关闭：PWM、档位和超时值均自动归零，通过；随后恢复 4 档、PWM 255、超时 0。
9. 模块安装到 `/lib/modules/4.1.15-g9bb2b74f/extra/` 并执行 `depmod -a`；`/etc/modules-load.d/pwm-fan-self.conf` 已配置开机加载。
10. Qt 程序安装为 `/usr/bin/pwmfancontrol`，已在本地 Xorg `DISPLAY=:0` 启动。

## 板上文件与恢复

- 当前 DTB：`/run/media/mmcblk1p1/imx6ull-elf1-emmc.dtb`
- 原 DTB 备份：`/run/media/mmcblk1p1/imx6ull-elf1-emmc.dtb.before-pwm7-20260908`
- 模块：`/lib/modules/4.1.15-g9bb2b74f/extra/pwm-fan-self.ko`
- Qt 程序：`/usr/bin/pwmfancontrol`
- Qt 日志：`/tmp/pwmfancontrol.log`

如果新设备树导致后续启动异常，应在串口/U-Boot 环境启动可用系统，再用备份 DTB 恢复原文件。恢复操作必须先确认启动分区已挂载到 `/run/media/mmcblk1p1`。

## SHA-256

```text
80deb67bc6e9960b2d498313b01afffd66746974dcc378af38b6d0376c6316ce  imx6ull-elf1-emmc.dtb
17c7a02223f50d6860e6970691b234d03e7f163800cdda6b671993d7ed6a4ff9  pwm-fan-self.ko
8053084af57e065b5cfc38def6e63641c32d2e32b395ccc97dee04db03e74b21  pwmfancontrol
```

Qt 启动日志中的 `libEGL warning: DRI2: failed to authenticate` 没有导致程序退出；它是当前板载 Xorg/EGL 环境的提示，不影响本次 Widgets 界面运行。
