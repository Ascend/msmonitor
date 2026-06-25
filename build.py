#!/usr/bin/python3
# -*- coding: utf-8 -*-
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
import argparse
import logging
import os
import shutil
import subprocess
import sys
import traceback
from pathlib import Path

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')


class BuildManager:
    """
    统一构建管理：依赖拉取 → 编译 → 安装 / 测试。

    用法:
        python build.py                             完整构建（拉取依赖 + Release 编译）
        python build.py local                       本地构建（跳过依赖拉取, Release 编译）
        python build.py test                        单元测试（拉取依赖 + Debug 编译 + 执行测试）
        python build.py test local                  单元测试（跳过依赖拉取, Debug 编译 + 执行测试）
        python build.py --version/-v <version>      指定构建版本号（用于 run/exe/dmg 包）
        python build.py --extra/-e KEY=VALUE        指定额外构建选项，可多次使用

    参数说明:
        - 参数: command    : 构建动作: 为空时为全构建, local 为跳过依赖下载, test 为运行单元测试。
        - 参数: --version  : 构建版本号，不传时默认 1.0.0。
        - 参数: --extra    : 额外构建选项，格式为 KEY=VALUE，可多次指定。
    """

    def __init__(self):
        self.project_root = Path(__file__).resolve().parent
        ap = argparse.ArgumentParser(description='Build the project and optionally run tests.')
        ap.add_argument(
            'command',
            nargs='*',
            default=[],
            choices=[[], 'local', 'test'],
            help='Build action: omit for full build, "local" to skip dependency download, "test" to run unit tests',
        )
        ap.add_argument(
            '-v', '--version', type=str, default='1.0.0', help='Build version for run/exe/dmg packages (default: 1.0.0)'
        )
        ap.add_argument(
            '-e',
            '--extra',
            metavar='KEY=VALUE',
            action='append',
            default=[],
            help='Extra build options in KEY=VALUE format, can be specified multiple times',
        )
        self.args = ap.parse_args()

    def _execute_command(self, cmd, timeout_seconds=36000, cwd=None, env=None):
        logging.info("Running: %s", " ".join(cmd))
        subprocess.run(cmd, timeout=timeout_seconds, check=True, cwd=cwd, env=env)

    def _get_extra_options(self):
        opts = {}
        for opt in self.args.extra:
            key, _, val = opt.partition('=')
            opts[key] = val
        return opts

    def _archive_artifacts(self, src_dir, pattern):
        artifacts_dir = self.project_root / "artifacts"
        artifacts_dir.mkdir(exist_ok=True)
        for src in Path(src_dir).glob(pattern):
            shutil.copy2(src, artifacts_dir)
            logging.info("Archived %s -> %s", src, artifacts_dir / src.name)

    def _fetch_dynolog(self):
        third_party_dir = self.project_root / "third_party"
        third_party_dir.mkdir(parents=True, exist_ok=True)
        url = "https://ascend-package.obs.cn-north-4.myhuaweicloud.com/msmonitor/dynolog.tar.gz"
        archive = third_party_dir / "dynolog.tar.gz"
        archive.unlink(missing_ok=True)
        self._execute_command(["wget", "-O", "dynolog.tar.gz", url], cwd=third_party_dir)
        self._execute_command(["tar", "-zxf", "dynolog.tar.gz"], cwd=third_party_dir)
        archive.unlink()

    def _build_whl(self):
        plugin_dir = self.project_root / "plugin"
        # 同一 workspace 内会用多个 Python 版本依次构建，dist 残留旧版本 whl
        shutil.rmtree(plugin_dir / "dist", ignore_errors=True)
        self._execute_command(["pip", "install", "protobuf", "setuptools"], cwd=plugin_dir)
        self._execute_command(["bash", "build.sh"], cwd=plugin_dir)
        self._archive_artifacts(plugin_dir / "dist", "*.whl")

    def _build_dynalog(self):
        # 在非 local 场景下按需更新依赖；在 local 场景下仅使用本地已有代码，不更新依赖。
        if 'local' not in self.args.command:
            self._fetch_dynolog()

        self._execute_command(["bash", "scripts/build.sh", "-t", "rpm"])
        self._execute_command(["bash", "scripts/build.sh", "-t", "deb"])
        self._archive_artifacts(self.project_root, "dynolog*.rpm")
        self._archive_artifacts(self.project_root, "dynolog*.deb")

    def run(self):
        os.chdir(self.project_root)

        if 'test' in self.args.command:
            # -------------------- 单元测试 --------------------
            scripts_dir = self.project_root / "scripts"
            self._execute_command(["pip", "install", "pybind11"])
            self._execute_command(["bash", "run_ut.sh", "all"], cwd=scripts_dir)
        else:
            # -------------------- 产品构建 --------------------
            logging.info("--version: %s", self.args.version)
            extra_options = self._get_extra_options()
            for key, val in extra_options.items():
                logging.info("--extra: %s = %s", key, val)

            whl_only = extra_options.get('whl', '').lower() == 'true'
            dynolog_only = extra_options.get('dynolog', '').lower() == 'true'

            if whl_only:
                self._build_whl()
            elif dynolog_only:
                self._build_dynalog()
            else:
                self._build_whl()
                self._build_dynalog()


if __name__ == "__main__":
    try:
        BuildManager().run()
    except Exception:
        logging.error("Unexpected error: %s", traceback.format_exc())
        sys.exit(1)
