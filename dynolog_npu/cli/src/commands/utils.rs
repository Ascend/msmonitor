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

// Copyright (c) Meta Platforms, Inc. and affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

use std::io::{self, Read, Write};

use crate::error::{err, Result};

use crate::DynoClient;

use nix::unistd::Uid;

fn map_read_error(error: io::Error) -> Box<dyn std::error::Error> {
    if error.kind() == io::ErrorKind::UnexpectedEof {
        return err(
            "Failed to receive a response from the server: the connection was closed \
             before a complete response arrived. The request may have been rejected, \
             please check whether the parameters contain unexpected special characters.",
        );
    }
    Box::new(error)
}

pub fn send_msg(client: &mut DynoClient, msg: &str) -> Result<()> {
    match client {
        DynoClient::Secure(secure_client) => {
            let msg_len: [u8; 4] = i32::try_from(msg.len()).unwrap().to_ne_bytes();
            secure_client.write_all(&msg_len)?;
            secure_client.write_all(msg.as_bytes())?;
            secure_client.flush()?;
        }
        DynoClient::Insecure(insecure_client) => {
            let msg_len: [u8; 4] = i32::try_from(msg.len()).unwrap().to_ne_bytes();
            insecure_client.write_all(&msg_len)?;
            insecure_client.write_all(msg.as_bytes())?;
            insecure_client.flush()?;
        }
    }
    Ok(())
}

pub fn get_resp(client: &mut DynoClient) -> Result<String> {
    let mut len_buf = [0u8; 4];
    let mut resp_buf;

    match client {
        DynoClient::Secure(secure_client) => {
            secure_client.read_exact(&mut len_buf).map_err(map_read_error)?;
            let len = u32::from_ne_bytes(len_buf) as usize;
            resp_buf = vec![0u8; len];
            secure_client
                .read_exact(&mut resp_buf)
                .map_err(map_read_error)?;
        }
        DynoClient::Insecure(insecure_client) => {
            insecure_client
                .read_exact(&mut len_buf)
                .map_err(map_read_error)?;
            let len = u32::from_ne_bytes(len_buf) as usize;
            resp_buf = vec![0u8; len];
            insecure_client
                .read_exact(&mut resp_buf)
                .map_err(map_read_error)?;
        }
    }

    Ok(String::from_utf8(resp_buf)?)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_map_read_error_translates_unexpected_eof() {
        let error = map_read_error(io::Error::new(
            io::ErrorKind::UnexpectedEof,
            "failed to fill whole buffer",
        ));
        let message = error.to_string();
        assert!(message.contains("connection was closed"));
        assert!(!message.contains("UnexpectedEof"));
    }

    #[test]
    fn test_map_read_error_keeps_other_errors() {
        let error = map_read_error(io::Error::new(io::ErrorKind::ConnectionReset, "reset"));
        assert_eq!(error.to_string(), "reset");
    }
}

pub fn is_root() -> bool {
    Uid::current().is_root()
}
