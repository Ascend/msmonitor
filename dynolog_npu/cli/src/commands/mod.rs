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

// Export all command submodules to be used in main.rs
// Note: This "intermediate" commands module is purely for organizational purposes.
// This allows for a clear distinction between the command dispatching code and the command
// handling code. Additionally, explicitly "exporting" all the command modules here allows
// us to avoid having to explicitly list all the command modules in main.rs.

pub mod status;
pub mod version;
pub mod dcgm;
pub mod gputrace;
pub mod nputrace;
pub mod npumonitor;
pub mod utils;
pub mod path;
// ... add new command modules here
