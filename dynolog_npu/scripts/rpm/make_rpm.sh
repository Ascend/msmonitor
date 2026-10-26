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

# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Please run this script from the root level of the project.
#  ./scripts/rpm/make_rpm.sh

set -eux -o pipefail
ARCH=${ARCH:-"x86_64"}
VERSION=${VERSION:-"$(tr -d '\n' < version.txt)"}
DYNOROOT="$PWD"
SPECBASE="$DYNOROOT/scripts/rpm/"
SCRIPTS="$DYNOROOT/scripts/"

# For some reason the RPM build tools only work with the directory
# path at ~/rpmbuild. Just using this path for now.
RPMDIR="$HOME/rpmbuild"

# Check dependencies
if ! command -v rpmbuild;
then
  echo "Please install : sudo dnf install -y rpmdevtools rpmlint"
  exit 1
fi
if ! command -v rpmlint;
then
  echo "Please install : sudo dnf install -y rpmdevtools rpmlint"
  exit 1
fi

rm -rf "$RPMDIR"
mkdir "$RPMDIR"
cd "$RPMDIR"

mkdir BUILD RPMS SOURCES SPECS SRPMS

# Setup RPM spec file with specified version
perl -p -e "s/__VERSION__/$VERSION/" "$SPECBASE/dynolog.spec" > SPECS/dynolog.spec

# Setup sources
mkdir "dynolog-${VERSION}"

# Build the binary
cd -
IFS=" " read -r -a generated_bins <<< "$(./scripts/build.sh | tail -n 1)"

cd "$RPMDIR"

cp "${generated_bins[@]}" "dynolog-${VERSION}/"

# Add static files
cp "$SCRIPTS/dynolog.service" "dynolog-${VERSION}/"
cp "$SCRIPTS/pytorch/unitrace.py" "dynolog-${VERSION}/"
cp "$SCRIPTS/dynolog.conf" "dynolog-${VERSION}/"

# Compress sources
tar --create --file "dynolog-${VERSION}.tar.gz" "dynolog-${VERSION}"

mv "dynolog-${VERSION}.tar.gz"  SOURCES

# Sanity checks
rpmlint SPECS/dynolog.spec
tree .

# Build src rpm
rpmbuild -bs SPECS/dynolog.spec

# Build binary rpm
rpmbuild -bb SPECS/dynolog.spec

# Test results
tree .
rpmfile=$(ls "$RPMDIR/RPMS/${ARCH}/dynolog"-*rpm)
rpm -qi "$rpmfile"

if [[ $(grep -o "-" <<< "$rpmfile" | wc -l) -ge 2 ]] ; then
  reversed_file=$(echo "$rpmfile" | rev)
  reversed_file_fixed=$(echo "$reversed_file" | sed 's/-/_/2; s/-/_/1')
  new_rpmfile=$(echo "$reversed_file_fixed" | rev)
  if [[ "$rpmfile" != "$new_rpmfile" ]] ; then
    mv "$rpmfile" "$new_rpmfile"
  fi
fi
rpmfile=$(ls "$RPMDIR/RPMS/${ARCH}/dynolog"_*rpm)
cp "$rpmfile" "$DYNOROOT/"

{
echo
echo "---------------------------------------------------------------"
echo "RPM generated at $rpmfile"
echo "To install you can use --nodeps flag "
echo "rpm -i $rpmfile"
echo "---------------------------------------------------------------"
} 2> /dev/null
