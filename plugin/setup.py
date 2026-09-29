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
import os
import sys
import shutil
import subprocess  # nosec B404
import pybind11

from setuptools import setup, Extension, find_namespace_packages
from setuptools.command.build_ext import build_ext
from setuptools.command.build_py import build_py


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        super().__init__(name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def run(self):
        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg]

        ext_dir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = [
            '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + ext_dir,
            '-DPYTHON_EXECUTABLE=' + sys.executable,
            '-DCMAKE_PREFIX_PATH=' + pybind11.get_cmake_dir(),
            '-DCMAKE_INSTALL_PREFIX=' + ext_dir,
            '-DCMAKE_BUILD_TYPE=' + cfg,
        ]

        env = os.environ.copy()
        env['CXXFLAGS'] = '{} -DVERSION_INFO=\\"{}\\"'.format(env.get('CXXFLAGS', ''), self.distribution.get_version())

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)
        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp, env=env)  # nosec B603
        subprocess.check_call(['cmake', '--build', '.', '-j', '8'] + build_args, cwd=self.build_temp)  # nosec B603


class CustomBuildPy(build_py):
    def run(self):
        super().run()

        source_dir = os.path.join(os.path.dirname(__file__), 'IPCMonitor')
        target_dir = os.path.join(self.build_lib, 'msmonitor')

        os.makedirs(target_dir, exist_ok=True)
        shutil.copytree(source_dir, target_dir, dirs_exist_ok=True)


setup(
    name="mindstudio_monitor",
    version=os.getenv("WHL_VERSION", "26.0.0"),
    description="mindstudio monitor",
    packages=find_namespace_packages(include=["IPCMonitor*"]),
    include_package_data=True,
    ext_modules=[CMakeExtension('IPCMonitor')],
    cmdclass=dict(build_ext=CMakeBuild, build_py=CustomBuildPy),
    install_requires=["pybind11", "xlsxwriter"],
)
