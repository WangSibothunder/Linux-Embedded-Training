#!/bin/bash
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
qt_qmake=${QMAKE:-/opt/Qt5.6.2/5.6/gcc_64/bin/qmake}
mkdir -p "$project_dir/build/tests"
cd "$project_dir/build/tests"
"$qt_qmake" "$project_dir/tests/tests.pro"
make -j"${JOBS:-4}"
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}
./test_uart2uart -v1
