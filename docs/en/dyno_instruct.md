# dyno

## Overview

dyno sends RPC requests from dyno CLI and triggers nputrace and npu-monitor.

## dyno Functions

**Option Description**

| Option       | Type| Description                                                        | Mandatory (Y/N)|
| ----------- | -------- | ------------------------------------------------------------ | :------: |
| --hostname  | String   | Indicates the host name of the dynolog daemon process. The default value is **localhost**.               |    N     |
| --port      | i32      | Indicates the port number listened by the dynolog daemon process. The default value is **1778**.                |    N     |
| --certs-dir | String   | Indicates the path of the TLS certificate used for communication between dyno and dynolog RPC. If the value is **NO_CERTS**, certificate verification is not used.|    Y     |
| --help      | action   | Obtains the help information about the dyno command and shows all available options and function descriptions.    |    N     |
| --version   | action   | Queries the dyno CLI version information.                                |    N     |

- Common subcommands of dyno

| Command       | Description                                                        |
| ----------- | ------------------------------------------------------------ |
| status      | Queries the execution status of the nputrace or npu-monitor command.                 |
| nputrace    | Sends nputrace-related messages to dynolog daemon. For details, see [nputrace](./nputrace_instruct.md).|
| npu-monitor | Sends npu-monitor-related messages to dynolog daemon. For details, see [npu-monitor](./npumonitor_instruct.md).|
| help        | Obtains the help information about the dyno command and shows all available options and function descriptions.        |
| version     | Queries the dynolog daemon version information.                              |
