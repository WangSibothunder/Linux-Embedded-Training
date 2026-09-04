#!/bin/bash
set -eo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
sdk_env=${SDK_ENV:-/opt/fsl-imx-x11/4.1.15-2.0.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi}
if [ ! -f "$sdk_env" ]; then
    echo '找不到开发板 SDK；请设置 SDK_ENV=/匹配板卡的/environment-setup-...。' >&2
    exit 1
fi
# Keep native and ARM object files in separate build directories.
source "$sdk_env"
mkdir -p "$project_dir/build/imx6ull-armhf"
cd "$project_dir/build/imx6ull-armhf"
qmake -v
qmake "$project_dir/UART2UART.pro"
make -j"${JOBS:-4}"
file uart2uart
echo "构建完成：$PWD/uart2uart"
