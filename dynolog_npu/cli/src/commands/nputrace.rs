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

use crate::error::{err, Result};
use serde_json::{json, Value};
use crate::DynoClient;
use super::utils;

#[derive(Debug)]
pub enum NpuTraceTriggerConfig {
    DurationBased {
        profile_start_time: u64,
        duration_ms: u64,
    },
    IterationBased {
        start_step: i64,
        iterations: i64,
    },
}

impl NpuTraceTriggerConfig {
    fn config(&self) -> String {
        match *self {
            NpuTraceTriggerConfig::DurationBased {
                profile_start_time,
                duration_ms,
            } => format!(
                "PROFILE_START_TIME={}\nACTIVITIES_DURATION_MSECS={}",
                profile_start_time, duration_ms
            ),
            NpuTraceTriggerConfig::IterationBased {
                start_step,
                iterations,
            } => format!(
                r#"PROFILE_START_ITERATION=0
PROFILE_START_STEP={}
ACTIVITIES_ITERATIONS={}"#,
                start_step, iterations
            ),
        }
    }
}

// torch npu profiler config
#[derive(Debug)]
pub struct NpuTraceOptions {
    pub record_shapes: bool,
    pub profile_memory: bool,
    pub with_stack: bool,
    pub with_flops: bool,
    pub with_modules: bool,
    pub activities: String,
    pub analyse: bool,
    pub async_mode: bool,
    pub profiler_level: String,
    pub aic_metrics: String,
    pub l2_cache: bool,
    pub op_attr: bool,
    pub msprof_tx: bool,
    pub gc_detect_threshold: Option<f32>,
    pub data_simplification: String,
    pub export_type: String,
    pub host_sys: String,
    pub sys_io: bool,
    pub sys_interconnection: bool,
    pub mstx_domain_include: Option<String>,
    pub mstx_domain_exclude: Option<String>,
    pub rank_list: Option<String>,
}

impl NpuTraceOptions {
    fn config(&self) -> String {
        let is_rank = self.rank_list.is_some();
        format!(
            r#"
PROFILE_RECORD_SHAPES={}
PROFILE_PROFILE_MEMORY={}
PROFILE_WITH_STACK={}
PROFILE_WITH_FLOPS={}
PROFILE_WITH_MODULES={}
PROFILE_ACTIVITIES={}
PROFILE_ANALYSE={}
PROFILE_ASYNC_MODE={}
PROFILE_PROFILER_LEVEL={}
PROFILE_AIC_METRICS={}
PROFILE_L2_CACHE={}
PROFILE_OP_ATTR={}
PROFILE_MSPROF_TX={}
PROFILE_GC_DETECT_THRESHOLD={}
PROFILE_DATA_SIMPLIFICATION={}
PROFILE_EXPORT_TYPE={}
PROFILE_HOST_SYS={}
PROFILE_SYS_IO={}
PROFILE_SYS_INTERCONNECTION={}
PROFILE_MSTX_DOMAIN_INCLUDE={}
PROFILE_MSTX_DOMAIN_EXCLUDE={}
PROFILE_IS_RANK={}
PROFILE_RANK_LIST={}"#,
            self.record_shapes,
            self.profile_memory,
            self.with_stack,
            self.with_flops,
            self.with_modules,
            self.activities,
            self.analyse,
            self.async_mode,
            self.profiler_level,
            self.aic_metrics,
            self.l2_cache,
            self.op_attr,
            self.msprof_tx,
            self.gc_detect_threshold.map_or("None".to_string(), |v| v.to_string()),
            self.data_simplification,
            self.export_type,
            self.host_sys,
            self.sys_io,
            self.sys_interconnection,
            self.mstx_domain_include.clone().map_or("None".to_string(), |v| v.to_string()),
            self.mstx_domain_exclude.clone().map_or("None".to_string(), |v| v.to_string()),
            is_rank,
            self.rank_list.clone().map_or("None".to_string(), |v| v.to_string()),
        )
    }
}

#[derive(Debug)]
pub struct NpuTraceConfig {
    pub log_file: String,
    pub trigger_config: NpuTraceTriggerConfig,
    pub trace_options: NpuTraceOptions,
}

impl NpuTraceConfig {
    fn config(&self) -> String {
        format!(
            "ACTIVITIES_LOG_FILE={}\n{}{}",
            self.log_file,
            self.trigger_config.config(),
            self.trace_options.config()
        )
    }
}

fn parse_pids(pids: &str) -> std::result::Result<Vec<i64>, String> {
    let mut result = Vec::new();
    for (index, pid) in pids.split(',').enumerate() {
        let pid = pid.trim();
        if pid.is_empty() {
            return Err(format!(
                "element {} is empty; expected a comma-separated list of integers",
                index + 1
            ));
        }
        result.push(pid.parse::<i64>().map_err(|_| {
            format!(
                "element {} is not an integer; expected a comma-separated list of integers",
                index + 1
            )
        })?);
    }
    Ok(result)
}

fn build_request(config: &str, job_id: u64, pids: &[i64], process_limit: u32) -> String {
    json!({
        "fn": "setKinetOnDemandRequest",
        "config": config,
        "job_id": job_id,
        "pids": pids,
        "process_limit": process_limit,
    })
    .to_string()
}

pub fn run_nputrace(
    mut client: DynoClient,
    job_id: u64,
    pids: &str,
    process_limit: u32,
    config: NpuTraceConfig,
) -> Result<()> {
    let pids = parse_pids(pids).map_err(|e| err(format!("invalid value for '--pids': {}", e)))?;

    let config_str = config.config();
    println!("NpuTrace config = \n{}", config_str);

    let request_json = build_request(&config_str, job_id, &pids, process_limit);

    utils::send_msg(&mut client, &request_json)?;

    let resp_str = utils::get_resp(&mut client)?;

    println!("response = {}", resp_str);

    let resp_v: Value = serde_json::from_str(&resp_str)?;
    let processes = if let Some(val) = resp_v.get("processesMatched") {
        if let Some(arr) = val.as_array() {
            arr
        } else {
            println!("'processesMatched' is not an array");
            return Ok(());
        }
    } else {
        return Ok(());
    };

    if processes.is_empty() {
        println!("No processes were matched, please check --job-id or --pids flags");
    } else {
        println!("Matched {} processes", processes.len());
        println!("Trace output files will be written to:");

        for pid in processes {
            let pid = pid.as_i64().unwrap();
            println!(
                "    {}",
                config.log_file.replace(".json", &format!("_{}.json", pid))
            );
        }
    }

    Ok(())
}


#[cfg(test)]
mod test {
    use super::*;
    use crate::*;

    #[test]
    fn test_nputrace_trigger_config() {
        let trigger_config = NpuTraceTriggerConfig::DurationBased {
            profile_start_time: 1000,
            duration_ms: 1000,
        };
        assert_eq!(
            trigger_config.config(),
            r#"PROFILE_START_TIME=1000
ACTIVITIES_DURATION_MSECS=1000"#
        );

        let trigger_config = NpuTraceTriggerConfig::IterationBased {
            start_step: 1000,
            iterations: 1000,
        };
        assert_eq!(
            trigger_config.config(),
            r#"PROFILE_START_ITERATION=0
PROFILE_START_STEP=1000
ACTIVITIES_ITERATIONS=1000"#
        );
    }

    #[test]
    fn test_nputrace_config() {
        let config = NpuTraceConfig {
            log_file: "test.json".to_string(),
            trigger_config: NpuTraceTriggerConfig::DurationBased {
                profile_start_time: 1000,
                duration_ms: 1000,
            },
            trace_options: NpuTraceOptions {
                record_shapes: true,
                profile_memory: false,
                with_stack: true,
                with_flops: true,
                with_modules: true,
                activities: "CPU,NPU".to_string(),
                analyse: false,
                async_mode: false,
                profiler_level: "Level0".to_string(),
                aic_metrics: "AiCoreNone".to_string(),
                l2_cache: true,
                op_attr: true,
                msprof_tx: true,
                gc_detect_threshold: Some(0.1),
                data_simplification: "true",
                export_type: "Text".to_string(),
                host_sys: "cpu".to_string(),
                sys_io: true,
                sys_interconnection: true,
                mstx_domain_include: Some("domain1".to_string()),
                mstx_domain_exclude: Some("domain2".to_string()),
                rank_list: Some("0,1,2,3".to_string()),
            },
        };
        assert_eq!(
            config.config(),
            r#"ACTIVITIES_LOG_FILE=test.json
PROFILE_START_TIME=1000
ACTIVITIES_DURATION_MSECS=1000
PROFILE_RECORD_SHAPES=true
PROFILE_PROFILE_MEMORY=false
PROFILE_WITH_STACK=true
PROFILE_WITH_FLOPS=true
PROFILE_WITH_MODULES=true
PROFILE_ACTIVITIES=CPU,NPU
PROFILE_ANALYSE=false
PROFILE_ASYNC_MODE=false
PROFILE_PROFILER_LEVEL=Level0
PROFILE_AIC_METRICS=AiCoreNone
PROFILE_L2_CACHE=true
PROFILE_OP_ATTR=true
PROFILE_MSPROF_TX=true
PROFILE_GC_DETECT_THRESHOLD=0.1
PROFILE_DATA_SIMPLIFICATION=true
PROFILE_EXPORT_TYPE=Text
PROFILE_HOST_SYS=cpu
PROFILE_SYS_IO=true
PROFILE_SYS_INTERCONNECTION=true
PROFILE_MSTX_DOMAIN_INCLUDE=domain1
PROFILE_MSTX_DOMAIN_EXCLUDE=domain2
PROFILE_IS_RANK=true
PROFILE_RANK_LIST=0,1,2,3"#
        );
    }

    fn trace_config_with_log_file(log_file: &str) -> NpuTraceConfig {
        NpuTraceConfig {
            log_file: log_file.to_string(),
            trigger_config: NpuTraceTriggerConfig::IterationBased {
                start_step: -1,
                iterations: 1,
            },
            trace_options: NpuTraceOptions {
                record_shapes: false,
                profile_memory: false,
                with_stack: false,
                with_flops: false,
                with_modules: false,
                activities: "CPU,NPU".to_string(),
                analyse: false,
                async_mode: false,
                profiler_level: "Level0".to_string(),
                aic_metrics: "AiCoreNone".to_string(),
                l2_cache: false,
                op_attr: false,
                msprof_tx: false,
                gc_detect_threshold: None,
                data_simplification: "true".to_string(),
                export_type: "Text".to_string(),
                host_sys: "None".to_string(),
                sys_io: false,
                sys_interconnection: false,
                mstx_domain_include: None,
                mstx_domain_exclude: None,
                rank_list: None,
            },
        }
    }

    #[test]
    fn test_parse_pids() {
        assert_eq!(parse_pids("0").unwrap(), vec![0]);
        assert_eq!(parse_pids("1, 2,3").unwrap(), vec![1, 2, 3]);
        assert!(parse_pids("").is_err());
        assert!(parse_pids("1,,2").is_err());
        assert!(parse_pids("1,abc").is_err());
        assert_eq!(
            parse_pids("1,abc").unwrap_err(),
            "element 2 is not an integer; expected a comma-separated list of integers"
        );
        assert_eq!(
            parse_pids("1,,2").unwrap_err(),
            "element 2 is empty; expected a comma-separated list of integers"
        );
    }

    #[test]
    fn test_parse_pids_rejects_json_injection() {
        assert!(parse_pids(r#"0], "fn": "getVersion", "q": [0"#).is_err());
        assert!(parse_pids(r#"0], "job_id": "x", "q": [0"#).is_err());
    }

    #[test]
    fn test_request_json_does_not_allow_mstx_injection() {
        let mut config = trace_config_with_log_file("/tmp/ok");
        config.trace_options.mstx_domain_include =
            Some(r#"x", "fn": "getVersion", "q": ""#.to_string());

        let expected_config = config.config();
        let request = build_request(&expected_config, 0, &[0], 3);
        let parsed: Value = serde_json::from_str(&request).unwrap();

        assert_eq!(parsed["fn"], "setKinetOnDemandRequest");
        assert_eq!(parsed["config"], expected_config);
        // The server splits the config on real newlines, so the decoded value
        // must keep them instead of carrying a literal backslash-n.
        assert!(parsed["config"].as_str().unwrap().contains('\n'));
    }

    #[test]
    fn test_request_json_escapes_special_characters() {
        // Paths containing quotes, backslashes or control characters must still
        // produce a request whose config value is byte-for-byte identical to
        // the config that was serialized.
        for log_file in ["/tmp/x\"y", "/tmp/x\\", "/tmp/x\ty", "/tmp/x\ny"] {
            let config = trace_config_with_log_file(log_file);
            let expected_config = config.config();
            let request = build_request(&expected_config, 0, &[1, 2], 3);
            let parsed: Value = serde_json::from_str(&request)
                .unwrap_or_else(|e| panic!("request for {:?} is not valid JSON: {}", log_file, e));

            assert_eq!(parsed["fn"], "setKinetOnDemandRequest");
            assert_eq!(parsed["config"], expected_config);
            // Escaping must be handled by the JSON serializer alone: the
            // decoded config keeps the real newlines used as separators.
            assert!(parsed["config"].as_str().unwrap().contains('\n'));
            assert_eq!(parsed["pids"], serde_json::json!([1, 2]));
            assert_eq!(parsed["job_id"], 0);
            assert_eq!(parsed["process_limit"], 3);
        }
    }
}
