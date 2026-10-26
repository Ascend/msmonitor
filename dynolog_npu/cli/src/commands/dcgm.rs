/*
 * -------------------------------------------------------------------------
 * This file is part of the MindStudio project.
 * Copyright (c) 2025 Huawei Technologies Co.,Ltd.
 *
 * MindStudio is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * -------------------------------------------------------------------------
 */

use crate::error::Result;
use crate::DynoClient;
use super::utils;

// This module contains the handling logic for dcgm

/// Pause dcgm module profiling
pub fn run_dcgm_pause(
    mut client: DynoClient,
    duration_s: i32,
) -> Result<()> {
    let msg = format!(r#"{{"fn":"dcgmPause", "duration_s":{}}}"#, duration_s);
    utils::send_msg(&mut client, &msg)?;
    let resp_str = utils::get_resp(&mut client)?;
    println!("{}", resp_str);
    Ok(())
}

/// Resume dcgm module profiling
pub fn run_dcgm_resume(
    mut client: DynoClient,
) -> Result<()> {
    utils::send_msg(&mut client, r#"{"fn":"dcgmResume"}"#)?;
    let resp_str = utils::get_resp(&mut client)?;
    println!("{}", resp_str);
    Ok(())
}
