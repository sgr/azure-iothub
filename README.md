# azure-iothub

A tiny c++ wrapper for [azure-iot-sdk-c](https://github.com/Azure/azure-iot-sdk-c)

## Prerequisites

* build-essential
* cmake (>= 2.8.12)
* following dependencies

## Dependencies

|  name   |   debian (raspbian)  |    centos     |          note          |
|:-------:|:--------------------:|:-------------:|:----------------------:|
| OpenSSL |      libssl-dev      | openssl-devel ||
|  cURL   | libcurl4-openssl-dev | libcurl-devel ||
|  uuid   |       uuid-dev       | libuuid-devel ||

## Build

* git clone https://github.com/sgr/azure-iothub
* mkdir <BUILD_DIRECTORY>
* cd <BUILD_DIRECTORY>
* cmake <SOURCE_DIRECTORY>
* make

## Known Issues

* azure-iot-sdk-c は libcurl を必要とするが、CentOS 7 標準パッケージは OpenSSL を使っていないため、HTTP接続の場合初期化で Segmentation Fault を起こす。curl のソースコードから --with-ssl つきでビルドし、 LD_LIBRARY_PATH を編集してビルドしたライブラリを読み込ませる必要がある。
