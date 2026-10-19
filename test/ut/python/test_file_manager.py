# -------------------------------------------------------------------------
# This file is part of the MindStudio project.
# Copyright (c) 2026 Huawei Technologies Co.,Ltd.
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

"""FileManager 单测：临时目录加 mock，不改真实权限，也不创建系统软链接。

验证：建目录、建文件、属主检查、可写检查，以及路径超长拒绝。
运行：python -m unittest test_ut_python.test_file_manager
"""

# 各用例文件各自构造 IPCMonitor 包，和已有 UT 一样，不抽公共模块。
# pylint: disable=duplicate-code

import os
import sys
import tempfile
import types
import unittest
from unittest import mock

# ---- 构造 IPCMonitor 包环境 ----
_IPCMONITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "plugin", "IPCMonitor"))
sys.path.insert(0, os.path.dirname(_IPCMONITOR_DIR))

_pkg = types.ModuleType("IPCMonitor")
_pkg.__path__ = [_IPCMONITOR_DIR]
sys.modules["IPCMonitor"] = _pkg

from IPCMonitor.file_manager import FileManager  # noqa: E402


def _create_file_by_path(path):
    # chrome trace 用例把该类方法换成了空操作，这里走打桩前的原方法。
    chrome = sys.modules.get("test_chrome_trace_exporter")
    patcher = getattr(chrome, "_file_manager_patch", None)
    original = getattr(patcher, "temp_original", None)
    if original is None:
        return FileManager.create_file_by_path(path)
    func = getattr(original, "__func__", original)
    return func(FileManager, path)


class FileManagerTest(unittest.TestCase):
    def test_make_dir_existing_symlink_and_failure(self):
        directory = tempfile.mkdtemp()
        FileManager.make_dir_safety(directory)  # 已存在则直接返回

        with mock.patch("IPCMonitor.file_manager.os.path.islink", return_value=True):
            with self.assertRaises(RuntimeError):
                FileManager.make_dir_safety("linked")

        missing = os.path.join(tempfile.mkdtemp(), "missing")
        with mock.patch("IPCMonitor.file_manager.os.makedirs", side_effect=OSError("disk")):
            with self.assertRaises(RuntimeError):
                FileManager.make_dir_safety(missing)

    def test_create_file_existing_symlink_and_failure(self):
        directory = tempfile.mkdtemp()
        path = os.path.join(directory, "already.txt")
        with open(path, "w", encoding="utf-8") as f:
            f.write("keep")
        FileManager.create_file_safety(path)
        with open(path, encoding="utf-8") as f:
            self.assertEqual(f.read(), "keep")

        created = os.path.join(directory, "new.txt")
        FileManager.create_file_safety(created)
        self.assertTrue(os.path.isfile(created))

        with mock.patch("IPCMonitor.file_manager.os.path.islink", return_value=True):
            with self.assertRaises(RuntimeError):
                FileManager.create_file_safety("linked")

        blocked = os.path.join(tempfile.mkdtemp(), "blocked.txt")
        with mock.patch("IPCMonitor.file_manager.os.open", side_effect=OSError("ro")):
            with self.assertRaises(RuntimeError):
                FileManager.create_file_safety(blocked)

    def test_owner_check_missing_root_match_and_mismatch(self):
        directory = tempfile.mkdtemp()
        with self.assertRaises(RuntimeError):
            FileManager.check_path_owner_consistent(os.path.join(directory, "missing"))

        with mock.patch("IPCMonitor.file_manager.os.getuid", return_value=0):
            FileManager.check_path_owner_consistent(directory)  # root 跳过属主比对

        owner = os.stat(directory).st_uid
        with mock.patch("IPCMonitor.file_manager.os.getuid", return_value=owner):
            FileManager.check_path_owner_consistent(directory)

        with mock.patch("IPCMonitor.file_manager.os.getuid", return_value=owner + 1):
            with self.assertRaises(RuntimeError):
                FileManager.check_path_owner_consistent(directory)

    def test_directory_write_check_symlink_and_permission(self):
        directory = tempfile.mkdtemp()
        FileManager.check_directory_path_writeable(directory)

        with mock.patch("IPCMonitor.file_manager.os.path.islink", return_value=True):
            with self.assertRaises(RuntimeError):
                FileManager.check_directory_path_writeable(directory)

        with mock.patch("IPCMonitor.file_manager.os.access", return_value=False):
            with self.assertRaises(RuntimeError):
                FileManager.check_directory_path_writeable(directory)

    def test_create_file_by_path_success_and_too_long(self):
        path = os.path.join(tempfile.mkdtemp(), "nested", "out.txt")
        _create_file_by_path(path)
        self.assertTrue(os.path.isfile(path))

        long_path = "x" * (FileManager.MAX_PATH_LENGTH + 1)
        with (
            mock.patch("IPCMonitor.file_manager.os.path.realpath", return_value=long_path),
            mock.patch("IPCMonitor.file_manager.os.path.abspath", return_value=long_path),
        ):
            with self.assertRaises(RuntimeError):
                _create_file_by_path("short")


if __name__ == "__main__":
    unittest.main()
