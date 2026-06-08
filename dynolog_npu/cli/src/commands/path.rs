// Copyright (c) Meta Platforms, Inc. and affiliates.
// Copyright (c) 2025-2025. Huawei Technologies Co., Ltd. All rights reserved.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use std::ffi::CString;
use nix::unistd::Uid;
use nix::sys::stat::{self, Mode};
use libc::{R_OK, W_OK, X_OK};
use std::env;
use std::io;
use std::path::{Path, PathBuf};
use path_clean::PathClean;

use super::utils;

const MAX_PATH_SIZE: usize = 4096;
const INPUT_DIR_CHECK_MODE: i32 = R_OK | X_OK;
const OUTPUT_DIR_CHECK_MODE: i32 = W_OK | X_OK;

const INVALID_CHAR: &[(&str, &str)] = &[
    ("\n", "\\n"),
    ("\u{000C}", "\\f"),
    ("\r", "\\r"),
    ("\u{0008}", "\\b"),
    ("\t", "\\t"),
    ("\u{000B}", "\\v"),
    ("\u{007F}", "\\u007F"),
    ("\"", "\\\""),
    ("'", "'"),
    ("\\", "\\\\"),
    ("%", "\\%"),
    (">", "\\>"),
    ("<", "\\<"),
    ("|", "\\|"),
    ("&", "\\&"),
    ("$", "\\$"),
];

pub struct PathUtils;

impl PathUtils {
    pub fn access(path: &str, mode: i32) -> bool {
        let c_path = match CString::new(path) {
            Ok(p) => p,
            Err(_) => {
                println!("ERROR: Invalid path (contains null byte): {}", path);
                return false;
            }
        };

        unsafe {
            libc::access(c_path.as_ptr(), mode) == 0
        }
    }

    /// 检查文件或目录是否存在
    pub fn exists(path: &Path) -> bool {
        Path::new(path).exists()
    }

    /// 判断是否为软链接
    pub fn is_soft_link(path: &Path) -> bool {
        match path.symlink_metadata() {
            Ok(metadata) => metadata.file_type().is_symlink(),
            Err(e) => {
                println!("ERROR: The file lstat failed: {}", e);
                false
            }
        }
    }

    pub fn is_file(path: &Path) -> bool {
        match path.metadata() {
            Ok(metadata) => metadata.is_file(),
            Err(e) => {
                println!("ERROR: The file stat failed: {}", e);
                false
            }
        }
    }

    pub fn is_writable_by_others(path: &Path) -> bool {
        let file_stat = match stat::stat(path) {
            Ok(stat) => stat,
            Err(_) => return true,
        };
        let mode = Mode::from_bits_truncate(file_stat.st_mode);
        mode.contains(Mode::S_IWGRP) || mode.contains(Mode::S_IWOTH)
    }

    pub fn is_owner(path: &Path) -> bool {
        let file_stat = match stat::stat(path) {
            Ok(stat) => stat,
            Err(_) => return false,
        };
        let current_uid = Uid::current();
        let path_uid = Uid::from_raw(file_stat.st_uid);
        current_uid == path_uid
    }

    pub fn get_absolute_path(path: &str) -> Result<PathBuf, io::Error> {
        let abs = if Path::new(path).is_absolute() {
            PathBuf::from(path)
        } else {
            env::current_dir()?.join(path)
        };
        Ok(abs.clean())
    }

    pub fn check_dir(path: &str, should_exist: bool, is_input: bool) -> bool {
        if path.is_empty() {
            println!("ERROR: The path is empty.");
            return false;
        }

        if path.len() > MAX_PATH_SIZE {
            println!("ERROR: The length of path is too long, max allowed: {}", MAX_PATH_SIZE);
            return false;
        }

        for &(invalid, _) in INVALID_CHAR {
            if path.contains(invalid) {
                println!("WARNING: The path contains invalid character: {:?}", invalid);
            }
        }

        let path_buf = match Self::get_absolute_path(path) {
            Ok(p) => p,
            Err(e) => {
                println!("ERROR: Failed to get absolute path for '{}': {}", path, e);
                return false;
            }
        };
        let path_ref = path_buf.as_path();

        if !Self::exists(path_ref) {
            if should_exist {
                println!("ERROR: The path does not exist: {:?}", path_ref);
                return false;
            } else {
                return true;
            }
        }

        if Self::is_file(path_ref) {
            println!("ERROR: The path is a file: {:?}", path_ref);
            return false;
        }

        if Self::is_soft_link(path_ref) {
            println!("WARNING: The path is a soft link: {:?}", path_ref);
        }

        if utils::is_root() {
            return true;
        }

        if is_input {
            if !Self::access(path, INPUT_DIR_CHECK_MODE) {
                println!("WARNING: The path is not readable: {}", path);
            }
        } else {
            if !Self::access(path, OUTPUT_DIR_CHECK_MODE) {
                println!("WARNING: The path is not writable: {}", path);
            }
        }

        true
    }
}
