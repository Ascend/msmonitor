#!/bin/bash
# -------------------------------------------------------------------------
# This file is part of the MindStudio project.
# Copyright (c) 2025 Huawei Technologies Co.,Ltd.
#
# MindStudio is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#
#          http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# -------------------------------------------------------------------------

cur_dir="$(cd "$(dirname "$0")"; pwd -P)"
cd ${cur_dir}

# install pybind11
pip install pybind11

# build stub
sh ./stub/build_stub.sh

# build msmonitor_plugin wheel
python3 setup.py bdist_wheel

# find .whl files in dist
files=$(find dist -type f -name "*.whl" 2>/dev/null)
count=$(echo "$files" | wc -l)
if [ "$count" -eq 1 ]; then
    echo "find .whl in dist: $files"
else
    echo "find no or multi .whl in dist"
    exit 1
fi

# pip install whl
echo "pip install ${files}"
pip install ${files}
