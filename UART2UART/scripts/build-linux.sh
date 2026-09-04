#!/bin/bash
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
qt_qmake=${QMAKE:-/opt/Qt5.6.2/5.6/gcc_64/bin/qmake}
if [ ! -x "$qt_qmake" ]; then
    echo '找不到 qmake；请设置 QMAKE=/你的Qt5/bin/qmake 再运行。' >&2
    exit 1
fi
mkdir -p "$project_dir/build/linux-x86_64"
cd "$project_dir/build/linux-x86_64"
"$qt_qmake" -v
"$qt_qmake" "$project_dir/UART2UART.pro"
make -j"${JOBS:-4}"
echo "构建完成：$PWD/uart2uart"
