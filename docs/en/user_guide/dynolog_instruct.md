# dynolog Usage Guide

<!-- md-trans-meta sourceCommit=4222e88544e08580745f0b6eec2d1d22cda8f31b translatedAt=2026-08-12T08:30:48.350Z pushedAt=2026-08-12T08:32:48.401Z -->

## Introduction

dynolog is responsible for receiving RPC requests from dyno CLI and triggering nputrace and npu-monitor functions.

## Feature Introduction

**Parameter Description**

| Name                  | Type   | Description                                                    | Mandatory |
|---------------------|--------|-------------------------------------------------------|:----:|
| --enable-ipc-monitor | action | Whether to enable  Inter-Process Communication (IPC) monitoring for communication with dyno. Configuring this parameter enables it. Disabled by default. |  N   |
| --port              |  i32   | Port number that the dynolog daemon process listens on. Default value: `1778`.                   |  N   |
| --certs-dir         | String | Path to TLS certificates for RPC communication between dyno and dynolog. When the value is `NO_CERTS`, certificate verification is not used. Default value: `NO_CERTS`. |  N   |
| --metric_log_dir    | String | Disk storage path for metric data.                                     |  N   |
| --use_JSON          | action | Whether to record metric data in JSON format to the log. Disabled by default.                        |  N   |
| --help              | action | Help information. The native dynolog parameters included therein are not described here. |  N   |

**Usage Example**

The dynolog daemon can be started via either systemd or the command line.

```bash
# Method 1: Use systemd to start the service
# Modify the configuration file /etc/dynolog.gflags to enable the ipc_monitor feature
echo "--enable-ipc-monitor" | sudo tee -a /etc/dynolog.gflags
sudo systemctl start dynolog
```

```bash
# Method 2: Execute via the command line
dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs
```
