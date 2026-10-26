#! /bin/bash
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

set -e

apply_ascend_patches() {
    cd ./third_party/dynolog || return 1

    if [ ! -d "../../patches" ]; then
        echo "ERROR: patches directory not found"
        cd ../..
        return 1
    fi

    for patch_file in ../../patches/*.patch; do
        if [ -f "$patch_file" ]; then
            echo "Applying patch: $patch_file"
            git apply --check -p1 "$patch_file"
            if [ $? -ne 0 ]; then
                echo "ERROR: Failed to apply patch: $(basename $patch_file)"
                cd ../..
                return 1
            fi
            git apply -p1 "$patch_file"
            if [ $? -ne 0 ]; then
                echo "ERROR: Failed to apply patch: $(basename $patch_file)"
                cd ../..
                return 1
            fi
        fi
    done

    cd ../..
    echo "Successfully applied all Ascend patches"
    return 0
}

apply_ascend_patches
