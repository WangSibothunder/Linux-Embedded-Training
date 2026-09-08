# PWMFanControl

ELF1 / i.MX6ULL 的 Qt 5 PWM7 风扇控制界面。

程序通过 `pwm-fan-self.ko` 创建的 sysfs 属性控制风扇，不直接访问寄存器：

- `fan_level`：0～4 档，对应 PWM 0、64、128、192、255。
- `pwm`：0～255 精细调节。
- `fan_timeout`：自动关闭秒数，0 表示禁用，最大 86400 秒。

## 编译

先加载 ELF1 SDK 环境，再执行：

```bash
mkdir -p build && cd build
qmake ../PWMFanControl.pro
make -j4
```

## 运行

```bash
QT_QPA_PLATFORM=xcb /usr/bin/pwmfancontrol
```

sysfs 默认只有 root 可以写。初次硬件验证请以 root 运行；正式部署时再配置最小权限。

测试时可以用 `--sysfs /tmp/模拟目录` 指定包含 `pwm`、`fan_level`、`fan_timeout` 三个文件的目录。

## 工程附带内容

- `driver/`：修复后的 `pwm-fan-self.c` 和外部模块 Makefile。
- `dts/`：启用 PWM7 及风扇节点的设备树源码。
- `output/`：已经为本开发板编译并校验的 DTB、KO 和 Qt ARM 程序。
- `DEPLOYMENT_REPORT.md`：2026-09-08 实际部署、测试和恢复信息。
