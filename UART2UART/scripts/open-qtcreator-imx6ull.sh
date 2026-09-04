#!/bin/bash
set -eo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
sdk_env=${SDK_ENV:-/opt/fsl-imx-x11/4.1.15-2.0.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi}
source "$sdk_env"
echo '请先保存并退出未加载 SDK 环境的旧 Qt Creator 实例。'
exec /opt/Qt5.6.2/Tools/QtCreator/bin/qtcreator "$project_dir/UART2UART.pro"
