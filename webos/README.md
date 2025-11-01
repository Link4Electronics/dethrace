# webos dethrace

## Building dethrace

```sh
# Build an ARMv7 version for your television
# Tips!
#   Adjust CMAKE_TOOLCHAIN_FILE to wherever yours is stored
#   Adjust `-j9` to "around" the number of threads you have
#   Don't run in Visual Studio Code terminal if you have less than 64GB RAM
#   Adjust WSL swap space so you have 64GB of pagable memory space
git clone --recursive https://github.com/dethrace-labs/dethrace
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=/opt/arm-webos-linux-gnueabi_sdk-buildroot/share/buildroot/toolchainfile.cmake -DCMAKE_BUILD_TYPE=Release
make -j9
```

## Packaging dethrace

wget https://rr2000.cwaboard.co.uk/R4/PC/carmdemo.zip
mkdir dethrace-ipk
extract only DATA folder and move inside dethrace-ipk/
```sh
ares-package dethrace-ipk/
```

## Debugging dethrace

```sh
/usr/bin/jailer -t native_devmode -i com.dethrace-labs.dethrace -p /media/developer/apps/usr/palm/applications/com.dethrace-labs.dethrace /media/developer/apps/usr/palm/applications/com.dethrace-labs.dethrace/dethrace '{"@system_native_app":true,"nid":"com.dethrace-labs.dethrace"}'
```
