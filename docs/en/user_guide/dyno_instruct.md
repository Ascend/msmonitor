# dyno Usage Guide

## Introduction

dyno is responsible for sending RPC requests from the dyno CLI to trigger nputrace and npu-monitor functions.

## Usage Guide

The dyno command is used as follows:

```bash
dyno [OPTIONS] <SUBCOMMAND> <SUBCOMMAND_ARGS>
```

`[OPTIONS]` are optional. For details, see [Option Description](#OPTIONS). `<SUBCOMMAND>` is a [common subcommand](#SUBCOMMAND) of dyno, and `<SUBCOMMAND_ARGS>` are the arguments of `<SUBCOMMAND>`.

## Feature Introduction

**Option Description**<a name="OPTIONS"></a>

| Option        | Type   | Description                                                         | Mandatory |
| ----------- | ------ | ------------------------------------------------------------ | :------: |
| --hostname  | String | Hostname of the dynolog daemon process. Default value: `localhost`.              |    N     |
| --port      | i32    | Port number on which the dynolog daemon process listens. Default value: `1778`.               |    N     |
| --certs-dir | String | Path to the TLS certificate for RPC communication between dyno and dynolog. When the value is `NO_CERTS`, certificate verification is not used. Default value: `NO_CERTS`. |    N     |
| --help      | action | Obtains usage help for the dyno command and views all available options and feature descriptions. |    N     |
| --version   | action | Version information of the dyno CLI.                                |    N     |

**Common Subcommands**<a name="SUBCOMMAND"></a>

| Command     | Description                                                         |
| ----------- | ------------------------------------------------------------ |
| status      | Queries the execution status of the nputrace or npu-monitor command. For details, see [status](#status-query). |
| nputrace    | Sends nputrace-related messages to the dynolog daemon. For details, see [nputrace](./nputrace_instruct.md). |
| npu-monitor | Sends npu-monitor-related messages to the dynolog daemon. For details, see [npu-monitor](./npumonitor_instruct.md). |
| help        | Obtains help for the dyno command and views all available options and feature descriptions.         |
| version     | Queries the version information of the dynolog daemon.                               |

## Status Query

Run the following command to view the current collection status:

```bash
dyno status
```

After the command is executed, a JSON string example is output:

```json
{"current_step":1,"npumonitor":"Idle","nputrace":"Ready","start_step":5,"stop_step":10,"trace_id":"123456789","queue_depth":2,"rejected":0}
```

### Field Description

| Field | Description |
| --- | --- |
| `nputrace` | nputrace collection status. For the values, see [Status Description](#status-description). |
| `npumonitor` | npu-monitor collection status. For the values, see [Status Description](#status-description). |
| `current_step` | Current step that has been executed. The default initial value is `-1`. |
| `start_step` / `stop_step` | Boundaries of the collection step range. Displayed only when `nputrace` is `Ready` or `Running`. |
| `trace_id` | Trace ID of the collection configuration that is being collected or of the next configuration to be started at `start_step`. Displayed only when `nputrace` is `Ready` or `Running` and the model process supports reporting. |
| `queue_depth` | Number of collection configurations queued for startup (the configuration being executed is excluded when `Running`). Displayed only when `nputrace` is `Ready` or `Running` and the model process supports reporting. |
| `rejected` | Cumulative number of collection configurations rejected by the model process (for example, when the configuration queue is full). Displayed when the model process supports reporting. |

### Status Description

- `Uninitialized`: The process is not started, or `dynolog init` has not been executed for initialization.
- `Idle`: Initialized, but no collection command has been issued yet.
- `Ready`: The collection command has been issued, but the current step has not yet reached the collection start step.
- `Running`: Data collection is in progress.

### Additional Notes

1. Field Status Set
   - `nputrace`: Supports four statuses: `Uninitialized` / `Idle` / `Ready` / `Running`.
   - `npumonitor`: Supports only three statuses: `Uninitialized` / `Idle` / `Running`.
2. `start_step` and `stop_step` are the boundaries of the collection step interval. The interval rules differ across frameworks:
   - PyTorch: The collection interval is left-closed, right-open `[start_step, stop_step)`, including the start step but excluding the stop step.
   - MindSpore: The collection interval is a closed interval `[start_step, stop_step]`, where both the start and stop steps are included in the collection scope.
3. The `start_step` and `stop_step` fields are displayed in the result only when the `nputrace` status is `Ready` or `Running`.
4. The default initial value of `current_step` is `-1`.
5. Under the MindSpore framework, `nputrace` does not have a `Ready` status.
6. `trace_id`, `queue_depth`, and `rejected` are reported by the model process and are provided only by new model processes/plug-ins:
   - `trace_id` and `queue_depth` are displayed only when `nputrace` is `Ready` or `Running`;
   - `rejected` is displayed when the model process supports reporting;
   - When the model process does not support reporting, the corresponding fields do not appear in the result, preventing "not reported" from being misread as `0`.
7. When multiple model processes report at the same time, the last report takes precedence.
