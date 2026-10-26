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

# Please run this script from the root level of the project.
#  ./scripts/debian/make_deb.sh
# The script build x86 deb packages by default, use
# ARCH=<override> to tweak this to build for other archs.

set -eux -o pipefail

VERSION=${VERSION:-"$(tr -d '\n' < version.txt)"}
ARCH=${ARCH:-"x86_64"}
DEBDIR="dynolog_${VERSION}_1.${ARCH}"

mkdir -p "$DEBDIR/usr/local/bin/"
mkdir -p "$DEBDIR/usr/lib/systemd/system/"
# control file goes here
mkdir -p "$DEBDIR/DEBIAN"
# logrotate config file goes here
mkdir -p "$DEBDIR/etc/logrotate.d/"

# Build the binaries
IFS=" " read -r -a generated_bins <<< "$(./scripts/build.sh | tail -n 1)"

cp -p "${generated_bins[@]}" "$DEBDIR/usr/local/bin"
cp -p scripts/dynolog.service "$DEBDIR/usr/lib/systemd/system"
cp -p scripts/pytorch/unitrace.py "$DEBDIR/usr/local/bin"
cp -p scripts/dynolog.conf "$DEBDIR/etc/logrotate.d"
perl -p -e "s/__VERSION__/$VERSION/" scripts/debian/control > "$DEBDIR/DEBIAN/control"

tree "$DEBDIR"
dpkg-deb --root-owner-group --build "$DEBDIR"
