// Copyright (c) Meta Platforms, Inc. and affiliates.
// Copyright (c) 2025-2026. Huawei Technologies Co., Ltd. All rights reserved.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use std::fmt;

#[derive(Debug)]
pub struct CliError(pub String);

impl fmt::Display for CliError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl std::error::Error for CliError {}

pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

pub fn err(msg: impl Into<String>) -> Box<dyn std::error::Error> {
    Box::new(CliError(msg.into()))
}
