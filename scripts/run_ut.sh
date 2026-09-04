#!/bin/bash
set -e

TOP_DIR=$(dirname "$(dirname "$(realpath "$0")")")
echo "TOP_DIR: $TOP_DIR"

export LD_LIBRARY_PATH=${TOP_DIR}/plugin/stub:${LD_LIBRARY_PATH:-}

cd ${TOP_DIR}
mkdir -p ${TOP_DIR}/test/build_llt
cd ${TOP_DIR}/test/build_llt

if [[ -n "${1:-}" && "$1" == "plugin" ]]; then
    echo "Building plugin tests..."
    cmake ../ -DPACKAGE=ut -DMODE=plugin || { echo "ERROR: cmake configure failed"; exit 1; }
else
    echo "Building all tests..."
    cmake ../ -DPACKAGE=ut -DMODE=all || { echo "ERROR: cmake configure failed"; exit 1; }
fi

make -j$(nproc) || { echo "ERROR: UT 编译失败"; exit 1; }

echo "执行所有测试文件..."
TEST_DIR="./ut/plugin/ipc_monitor"
if [ -d "$TEST_DIR" ]; then
    TEST_FILES=$(find "$TEST_DIR" -name "test_*" -type f -executable)
    if [ -z "$TEST_FILES" ]; then
        echo "未找到任何测试文件"
        exit 1
    fi
    for TEST_FILE in $TEST_FILES; do
        echo "执行测试: $TEST_FILE"
        if ! "$TEST_FILE"; then
            echo "测试执行失败: $TEST_FILE"
            exit 1
        fi
        echo "测试通过: $TEST_FILE"
    done
    echo "所有测试执行通过"
else
    echo "测试目录不存在: $TEST_DIR"
    exit 1
fi
