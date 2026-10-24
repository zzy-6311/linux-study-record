#!/bin/bash
# 自动定位脚本所在目录并从那里取源码，编译放到 WSL 内部（绕开「路径不能有空格」的限制）
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
BUILD=/root/build/mytoggle

mkdir -p "$BUILD" "$SRC/out"
cp "$SRC/mytoggle.c" "$SRC/Makefile" "$BUILD/"

cd "$BUILD"
make

cp "$BUILD/mytoggle.ko" "$SRC/out/"
echo "OK -> out/mytoggle.ko"
ls -l "$SRC/out/mytoggle.ko"
