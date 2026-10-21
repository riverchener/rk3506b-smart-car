#!/bin/bash
# Rebuild the Smart_car ARM app on the VM. Usage: vm.py exec @vmctl/build_app.sh
SDK=$HOME/atk_dlrk3506_linux6.1_release_v1.3.1_20260326
HOST=$SDK/buildroot/output/alientek_rk3506/host/bin
SRC=$HOME/work/Smart_car_rk3506

cd "$SRC" || exit 1
rm -rf build_rk3506 && mkdir -p build_rk3506 && cd build_rk3506 || exit 1
"$HOST/qmake" .. \
  QMAKE_CC="$HOST/arm-buildroot-linux-gnueabihf-gcc" \
  QMAKE_CXX="$HOST/arm-buildroot-linux-gnueabihf-g++" \
  QMAKE_LINK="$HOST/arm-buildroot-linux-gnueabihf-g++" >/dev/null 2>&1
make -j8 2>&1 | grep -E '错误|error|Error' | head -25
echo "make rc=${PIPESTATUS[0]}"
ls -la "$SRC/Smart_in-car_entertainment"
