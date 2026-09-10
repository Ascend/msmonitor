# dyno Usage Guide

<!-- md-trans-meta sourceCommit=4222e88544e08580745f0b6eec2d1d22cda8f31b translatedAt=2026-08-12T08:30:55.102Z pushedAt=2026-08-12T08:32:41.113Z -->

## Introduction

dyno is responsible for sending RPC requests from the dyno CLI to trigger nputrace and npu-monitor functions.

## Usage Guide

The dyno command is used as follows:

```bash
dyno [OPTIONS] <SUBCOMMAND> <SUBCOMMAND_ARGS>
```

`[OPTIONS]` are optional. For details, see [Parameter Description](#OPTIONS). `<SUBCOMMAND>` is a [common subcommand](#SUBCOMMAND) of dyno, and `<SUBCOMMAND_ARGS>` are the arguments of `<SUBCOMMAND>`.

## Feature Introduction

**Parameter Description**<a name="OPTIONS"></a>

| Name        | Type   | Description                                                         | Mandatory |
| ----------- | ------ | ------------------------------------------------------------ | :------: |
| --hostname  | String | Hostname of the dynolog daemon process. Default value: `localhost`.              |    N     |
| --port      | i32    | Port number on which the dynolog daemon process listens. Default value: `1778`.               |    N     |
| --certs-dir | String | Path to the TLS certificate for RPC communication between dyno and dynolog. When the value is `NO_CERTS`, certificate verification is not used. Default value: `NO_CERTS`. |    N     |
| --help      | action | Help information for the dyno command for all available options and feature descriptions. |    N     |
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

Run the following command to view the current collection running status:

```bash
dyno status
```

After the command is executed, a JSON string example is output:

```json
{"current_step":1,"npumonitor":"Idle","nputrace":"Ready","start_step":5,"stop_step":10}
```

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
