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

set -e

# Define Color Codes
RED='\e[0;31m'
GREEN='\e[0;32m'
WHITE='\e[0;37m'
NC='\e[0m'

FILE_PATH="$(readlink -f "$0")"
PRE_SMOKE_PATH="$(dirname $(dirname "$FILE_PATH"))/test/pre_smoke"
if [ -z "$PYTHONPATH" ]; then
    export PYTHONPATH="${PRE_SMOKE_PATH}"
else
    export PYTHONPATH="${PRE_SMOKE_PATH}:$PYTHONPATH"
fi
result_file=${PRE_SMOKE_PATH}/run_all_result.txt
output_path=${PRE_SMOKE_PATH}/result
rm -rf $result_file
rm -rf $output_path
touch "$result_file"
mkdir -p $output_path

test_list=""
cd $PRE_SMOKE_PATH
test_list=$(ls | grep ".sh")
num_of_cases=$(echo "$test_list" | wc -l)

if [ ! -z "$test_list" ]; then
    echo -e "${WHITE}========================================${NC}"
    echo -e "${GREEN}[DEBUG] There are $num_of_cases test cases:${NC}"
    echo -e "${WHITE}----------------------------------------${NC}"
    echo -e "$test_list" | sed 's/^/    /'
    echo -e "${WHITE}========================================${NC}"

    for i in $test_list
    do
        echo -e "${WHITE}====================${i%.sh} Test Case ====================${NC}"
        start_time=$(date "+%s")
        bash $i $output_path
        check_exit_code=$?
        end_time=$(date "+%s")
        duration_time=$(( ${end_time} - ${start_time} ))
        if [ 0 -ne ${check_exit_code} ]; then
            echo "$i fail" >> $result_file
            echo -e "--------------------------------------${WHITE}${i%.sh}${NC}------${RED}FAIL${NC}"
        else
            echo "$i pass ${duration_time}" >> $result_file
            echo -e "--------------------------------------${WHITE}${i%.sh}${NC}------${GREEN}PASS${NC}"
        fi
    done
    echo -e "${GREEN}[DEBUG] End msprof_smoke_test${NC}"
else
    echo -e "${RED}[DEBUG] No test cases: $test_list${NC}"
fi
