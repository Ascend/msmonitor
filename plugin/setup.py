# Copyright (c) 2025, Huawei Technologies Co., Ltd.
# All rights reserved.
#
# Licensed under the Apache License, Version 2.0  (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import os
import sys
import shutil
import subprocess
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
            '-DCMAKE_BUILD_TYPE=' + cfg
        ]

        env = os.environ.copy()
        env['CXXFLAGS'] = '{} -DVERSION_INFO=\\"{}\\"'.format(env.get('CXXFLAGS', ''),
                                                             self.distribution.get_version())

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)
        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp, env=env)
        subprocess.check_call(['cmake', '--build', '.', '-j', '8'] + build_args,
                              cwd=self.build_temp)


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
