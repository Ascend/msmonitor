# dynolog

## Overview

dynolog receives RPC requests from dyno CLI and triggers nputrace and npu-monitor.

## dynolog Functions

**Option Description**

| Option                 | Type  | Description                                                   | Mandatory (Y/N)|
|---------------------|--------|-------------------------------------------------------|:----:|
| --enable-ipc-monitor | action | Indicates whether to enable the IPC monitoring function for communicating with dyno. This function is disabled by default.                 |  N   |
| --port              |  i32   | Indicates the port number listened by the dynolog daemon process. The default value is **1778**.                       |  N   |
| --certs-dir         | String | Indicates the path of the TLS certificate used for communication between dyno and dynolog RPC. If the value is **NO_CERTS**, certificate verification is not used.|  Y   |
| --metric_log_dir    | String | Indicates the path for outputting metric data to the drive.                                    |  N   |
| --use_JSON          | action | Indicates whether to record metric data in JSON format to logs. By default, this function is disabled.                       |  N   |

**Example**

- The dynolog daemon can be started using systemd or the command line.

```bash
# Method 1: Use systemd to start the service.
# Modify the /etc/dynolog.gflags configuration file to enable ipc_monitor.
echo "--enable_ipc_monitor" | sudo tee -a /etc/dynolog.gflags
sudo systemctl start dynolog
```

```bash
# Method 2: Run the following command:
dynolog --enable-ipc-monitor --certs-dir /home/server_certs
```
