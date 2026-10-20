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

"""utils 单测：用假的 torch / mindspore 模块驱动，不依赖真实框架或 NPU。

验证：rank 与通信组在环境变量、已初始化分布式、导入失败和框架回退之间的选择。
运行：python -m unittest test_ut_python.test_utils
"""

# 假 torch / mindspore 模块在运行时挂属性，静态分析看不到。
# pylint: disable=no-member

import json
import os
import sys
import types
import unittest
import warnings

# ---- 构造 IPCMonitor 包环境 ----
_IPCMONITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "plugin", "IPCMonitor"))
sys.path.insert(0, os.path.dirname(_IPCMONITOR_DIR))

_pkg = types.ModuleType("IPCMonitor")
_pkg.__path__ = [_IPCMONITOR_DIR]
sys.modules["IPCMonitor"] = _pkg

from IPCMonitor import utils  # noqa: E402

_MISSING = object()
_TORCH_NAMES = (
    "torch",
    "torch.distributed",
    "torch.distributed.distributed_c10d",
)
_MS_NAMES = (
    "mindspore",
    "mindspore.communication",
    "mindspore.communication._comm_helper",
)


class _HcclGroup:
    def __init__(self, backend, comm_name, group_name, group_rank, ranks):
        self.backend = backend
        self.comm_name = comm_name
        self.group_rank = group_rank
        self.ranks = ranks
        self.options = types.SimpleNamespace(hccl_config={"group_name": group_name})

    def _get_backend(self, device):
        return self

    def get_hccl_comm_name(self, rank, init_comm=False):
        return self.comm_name


def _install_torch(available=True, initialized=True, rank=0):
    torch = types.ModuleType("torch")
    dist = types.ModuleType("torch.distributed")
    c10d = types.ModuleType("torch.distributed.distributed_c10d")
    torch.distributed = dist
    torch.device = lambda name: ("device", name)
    dist.is_available = lambda: available
    dist.is_initialized = lambda: initialized
    dist.get_rank = lambda: rank
    dist.distributed_c10d = c10d

    class _World:
        pg_map = {}

    c10d._world = _World()
    c10d._get_default_group = lambda: None
    dist.get_backend = lambda group: group.backend
    dist.get_group_rank = lambda group, global_rank: group.group_rank
    dist.get_process_group_ranks = lambda group: group.ranks
    sys.modules["torch"] = torch
    sys.modules["torch.distributed"] = dist
    sys.modules["torch.distributed.distributed_c10d"] = c10d
    return torch, dist, c10d


def _install_mindspore(inited=False, backend="hccl", rank=3):
    mindspore = types.ModuleType("mindspore")
    comm = types.ModuleType("mindspore.communication")
    helper = types.ModuleType("mindspore.communication._comm_helper")

    class _GlobalComm:
        INITED = inited
        BACKEND = backend

    class _Backend:
        HCCL = "hccl"

    comm.GlobalComm = _GlobalComm
    comm.get_rank = lambda: rank
    helper.Backend = _Backend
    helper._group_map = {}
    helper._get_group_map = lambda: helper._group_map
    comm.get_comm_name = lambda name: helper._group_map[name]["comm"]
    comm.get_local_rank = lambda name: helper._group_map[name]["local"]
    comm.get_process_group_ranks = lambda name: helper._group_map[name]["ranks"]
    sys.modules["mindspore"] = mindspore
    sys.modules["mindspore.communication"] = comm
    sys.modules["mindspore.communication._comm_helper"] = helper
    return comm, helper


def _restore(names, saved):
    for name in names:
        old = saved[name]
        if old is _MISSING:
            sys.modules.pop(name, None)
        else:
            sys.modules[name] = old


class UtilsTest(unittest.TestCase):
    def setUp(self):
        self._env = dict(os.environ)
        self._torch = {name: sys.modules.get(name, _MISSING) for name in _TORCH_NAMES}
        self._ms = {name: sys.modules.get(name, _MISSING) for name in _MS_NAMES}
        for name in _TORCH_NAMES + _MS_NAMES:
            sys.modules.pop(name, None)
        os.environ.pop("RANK", None)
        os.environ.pop("RANK_ID", None)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._env)
        _restore(_TORCH_NAMES, self._torch)
        _restore(_MS_NAMES, self._ms)

    def test_pytorch_rank_from_env_and_from_distributed(self):
        _install_torch()
        os.environ["RANK"] = "4"
        self.assertEqual(utils.get_pytorch_rank_id(), 4)

        del os.environ["RANK"]
        _install_torch(available=False, initialized=True)
        self.assertIsNone(utils.get_pytorch_rank_id())
        _install_torch(initialized=False)
        self.assertIsNone(utils.get_pytorch_rank_id())

        _install_torch(initialized=True, rank=6)
        self.assertEqual(utils.get_pytorch_rank_id(), 6)

    def test_pytorch_rank_import_failure(self):
        sys.modules["torch"] = None
        with self.assertRaises(RuntimeError):
            utils.get_pytorch_rank_id()

    def test_pytorch_parallel_group_hccl_and_default(self):
        _, dist, c10d = _install_torch(initialized=True, rank=1)
        skipped = _HcclGroup("gloo", "skip", "gloo", 0, [0])
        empty = _HcclGroup("hccl", "", "empty", 0, [0])
        used = _HcclGroup("hccl", "comm-a", "tp", 1, [0, 1])
        c10d._world.pg_map = {skipped: None, empty: None, used: None}
        default = _HcclGroup("hccl", "comm-default", "ignored", 1, [0, 1])
        c10d._get_default_group = lambda: default
        dist.get_rank = lambda: 1

        payload = json.loads(utils.get_pytorch_parallel_group_info())
        self.assertEqual(set(payload), {"comm-a", "comm-default"})
        self.assertEqual(payload["comm-a"]["group_name"], "tp")
        self.assertEqual(payload["comm-a"]["group_rank"], 1)
        self.assertEqual(payload["comm-a"]["global_ranks"], [0, 1])
        self.assertEqual(payload["comm-default"]["group_name"], "default_group")

    def test_pytorch_parallel_group_empty_or_uninitialized(self):
        _install_torch(initialized=False)
        self.assertEqual(utils.get_pytorch_parallel_group_info(), "")

        _, _, c10d = _install_torch(initialized=True)
        c10d._world.pg_map = {_HcclGroup("hccl", "", "x", 0, [0]): None}
        c10d._get_default_group = lambda: _HcclGroup("hccl", "", "d", 0, [0])
        self.assertEqual(utils.get_pytorch_parallel_group_info(), "")

    def test_pytorch_parallel_group_failure(self):
        _, dist, _ = _install_torch(initialized=True)

        def _down():
            raise RuntimeError("dist down")

        dist.is_initialized = _down
        with self.assertRaises(RuntimeError):
            utils.get_pytorch_parallel_group_info()

    def test_mindspore_rank_from_env_distributed_and_failure(self):
        _install_mindspore(inited=False)
        os.environ["RANK_ID"] = "8"
        self.assertEqual(utils.get_mindspore_rank_id(), 8)

        del os.environ["RANK_ID"]
        self.assertIsNone(utils.get_mindspore_rank_id())

        comm, _ = _install_mindspore(inited=True, rank=2)
        comm.GlobalComm.INITED = True
        self.assertEqual(utils.get_mindspore_rank_id(), 2)

        sys.modules["mindspore"] = None
        with self.assertRaises(RuntimeError):
            utils.get_mindspore_rank_id()

    def test_mindspore_parallel_group(self):
        _install_mindspore(inited=False)
        self.assertEqual(utils.get_mindspore_parallel_group_info(), "")

        comm, helper = _install_mindspore(inited=True, backend="not-hccl")
        self.assertEqual(utils.get_mindspore_parallel_group_info(), "")

        comm.GlobalComm.BACKEND = helper.Backend.HCCL
        helper._group_map = {"": {"comm": "", "local": 0, "ranks": [0]}}
        self.assertEqual(utils.get_mindspore_parallel_group_info(), "")
        helper._group_map = {
            "": {"comm": "", "local": 0, "ranks": [0]},
            "tp": {"comm": "ms-comm", "local": 1, "ranks": [0, 1]},
        }
        payload = json.loads(utils.get_mindspore_parallel_group_info())
        self.assertEqual(list(payload), ["ms-comm"])
        self.assertEqual(payload["ms-comm"]["group_name"], "tp")
        self.assertEqual(payload["ms-comm"]["group_rank"], 1)
        self.assertEqual(payload["ms-comm"]["global_ranks"], [0, 1])

        def _down():
            raise RuntimeError("ms down")

        helper._get_group_map = _down
        with self.assertRaises(RuntimeError):
            utils.get_mindspore_parallel_group_info()

    def test_rank_id_fallback_chain(self):
        _install_torch(initialized=True, rank=5)
        self.assertEqual(utils.get_rank_id(), 5)

        _install_torch(initialized=False)
        _install_mindspore(inited=True, rank=7)
        self.assertEqual(utils.get_rank_id(), 7)

        sys.modules["torch"] = None
        sys.modules["mindspore"] = None
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            self.assertEqual(utils.get_rank_id(), -1)
        self.assertTrue(any("set rank id to -1" in str(item.message) for item in caught))

    def test_parallel_group_info_fallback_chain(self):
        _, _, c10d = _install_torch(initialized=True, rank=0)
        group = _HcclGroup("hccl", "only-torch", "tp", 0, [0])
        c10d._world.pg_map = {group: None}
        c10d._get_default_group = lambda: _HcclGroup("hccl", "", "d", 0, [0])
        self.assertIn("only-torch", utils.get_parallel_group_info())

        _install_torch(initialized=False)
        comm, helper = _install_mindspore(inited=True, backend="hccl")
        comm.GlobalComm.BACKEND = helper.Backend.HCCL
        helper._group_map = {"tp": {"comm": "only-ms", "local": 0, "ranks": [0]}}
        self.assertIn("only-ms", utils.get_parallel_group_info())

        sys.modules["torch"] = None

        sys.modules["mindspore"] = None
        with warnings.catch_warnings(record=True):
            warnings.simplefilter("always")
            self.assertEqual(utils.get_parallel_group_info(), "")


if __name__ == "__main__":
    unittest.main()
