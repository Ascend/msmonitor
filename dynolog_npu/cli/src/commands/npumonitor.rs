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
use serde_json::json;
use super::utils;
use crate::DynoClient;

#[derive(Debug)]
pub struct NpuMonitorConfig {
    pub npu_monitor_start: bool,
    pub npu_monitor_stop: bool,
    pub report_interval_s: u32,
    pub duration: f32,
    pub mspti_activity_kind: String,
    pub log_file: String,
    pub export_type: String,
    pub json_rotate_log_lines: String,
    pub json_rotate_log_files: String,
    pub filter: String,
}

impl NpuMonitorConfig {
    fn config(&self) -> String {
        format!(
            r#"NPU_MONITOR_START={}
NPU_MONITOR_STOP={}
REPORT_INTERVAL_S={}
DURATION={}
MSPTI_ACTIVITY_KIND={}
NPU_MONITOR_LOG_FILE={}
NPU_MONITOR_EXPORT_TYPE={}
JSONL_ROTATE_LOG_LINES={}
JSONL_ROTATE_LOG_FILES={}
NPU_MONITOR_FILTER={}"#,
            self.npu_monitor_start,
            self.npu_monitor_stop,
            self.report_interval_s,
            self.duration,
            self.mspti_activity_kind,
            self.log_file,
            self.export_type,
            self.json_rotate_log_lines,
            self.json_rotate_log_files,
            self.filter,
        )
    }
}

fn build_request(config: &str) -> String {
    json!({
        "fn": "setKinetOnDemandRequest",
        "config": config,
        "job_id": 0,
        "pids": [0],
        "process_limit": 3,
    })
    .to_string()
}

pub fn run_npumonitor(mut client: DynoClient, config: NpuMonitorConfig) -> Result<()> {
    let config_str = config.config();
    println!("Npu monitor config = \n{}", config_str);

    let request_json = build_request(&config_str);

    utils::send_msg(&mut client, &request_json)?;
    let resp_str = utils::get_resp(&mut client)?;
    println!("response = {}", resp_str);

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::Value;

    fn monitor_config_with_log_file(log_file: &str) -> NpuMonitorConfig {
        NpuMonitorConfig {
            npu_monitor_start: true,
            npu_monitor_stop: false,
            report_interval_s: 60,
            duration: 0.0,
            mspti_activity_kind: "Marker".to_string(),
            log_file: log_file.to_string(),
            export_type: "DB".to_string(),
            json_rotate_log_lines: String::new(),
            json_rotate_log_files: String::new(),
            filter: String::new(),
        }
    }

    #[test]
    fn test_request_json_escapes_special_characters() {
        for log_file in ["/tmp/x\"y", "/tmp/x\\", "/tmp/x\ty", "/tmp/x\ny"] {
            let config = monitor_config_with_log_file(log_file);
            let expected_config = config.config();
            let request = build_request(&expected_config);
            let parsed: Value = serde_json::from_str(&request)
                .unwrap_or_else(|e| panic!("request for {:?} is not valid JSON: {}", log_file, e));

            assert_eq!(parsed["fn"], "setKinetOnDemandRequest");
            assert_eq!(parsed["config"], expected_config);
            // Escaping must be handled by the JSON serializer alone: the
            // decoded config keeps the real newlines used as separators.
            assert!(parsed["config"].as_str().unwrap().contains('\n'));
            assert_eq!(parsed["pids"], serde_json::json!([0]));
        }
    }
}
