# Monitor

## Overview

Monitor is a set of APIs integrated in MindStudio Monitor. You can call these APIs to start and stop performance monitoring and obtain monitoring data.

## Preparations

Install msMonitor. For details, see [msMonitor Installation Guide](./install_guide.md). You are advised to download the software package for installation.

## Monitor Functions

**Function**

Provides easy-to-use APIs to collect profile data of compute operators, communication operators, APIs, Runtime APIs, and MSTX. You can select metrics to be collected as needed.

**API Description**

For details, see [Monitor Feature APIs](mindstudio_monitor_api_reference.md#monitor-feature-apis) for mindstudio_monitor.

**Example**

1. Import the Monitor APIs to the model Python script.

   ```python
   from msmonitor import Monitor, ActivityKind
   ```

2. Call the Monitor APIs in the model Python script to start performance monitoring.

   ```python
   import torch
   import torch.nn as nn

   class FeatureExtractor(nn.Module):

       def __init__(self, in_channels=3, out_channels=16, kernel_size=3):
           super(FeatureExtractor, self).__init__()
           self.conv = nn.Conv2d(in_channels, out_channels, kernel_size, stride=1, padding=1)
           self.relu = nn.ReLU()
           self.pool = nn.AdaptiveAvgPool2d((4, 4))

       def forward(self, x):
           x = self.conv(x)
           x = self.relu(x)
           x = self.pool(x)
           return x

   from msmonitor import Monitor, ActivityKind

   # Enable performance monitoring.
   monitor = Monitor()
   monitor.start(kinds=[
       ActivityKind.API,
       ActivityKind.Kernel,
       ActivityKind.Marker
   ])

   # Run the model.
   batch_size = 4
   input_tensor = torch.randn(batch_size, 3, 32, 32).npu()
   extractor = FeatureExtractor(in_channels=3, out_channels=16, kernel_size=3).npu()
   linear_layer = nn.Linear(in_features=256, out_features=128).npu()

   for i in range(10):
       range_id = torch.npu.mstx.range_start(f"step {i}", torch.npu.current_stream())
       features = extractor(input_tensor)
       flat_features = features.view(batch_size, -1)
       x = linear_layer(flat_features)
       w = torch.randn(128, 64).npu()
       y = torch.matmul(x, w)
       torch.npu.mstx.range_end(range_id)

   torch.npu.synchronize()

   # Stop performance monitoring.
   monitor.stop()

   # (Optional) Obtain profile data online. For details, see step 3.
   result = monitor.get_result()

   # (Optional) Save the profile data to a local file. For details, see step 4.
   monitor.save("monitor_result.xlsx")
   ```

3. (Optional) Obtain profile data online. For details about the returned data structure, see [ActivityData Data Structure](mindstudio_monitor_api_reference.md#activitydata-data-structure).

   ```python
   # Obtain and print profile data.
   result = monitor.get_result()
   for kind, data in result.items():
       for item in data:
           print(f"kind: {kind}, name: {item.name}, durationNs: {item.endNs-item.startNs}")
   ```

4. (Optional) Save the profile data to a local file. Currently, only the Excel format is supported. For details about the file, see [Output File Description](#output-file-description).

   ```python
   # Save the profile data to a local file.
   monitor.save("monitor_result.xlsx")
   ```

## Output File Description

The output Excel file contains multiple sheets. Each sheet corresponds to a data type, such as API, kernel, and marker. You can view different sheets to analyze the execution time of operators and APIs.

See the following figure.

![Monitor data flushing](./figures/monitor_feature_result.png)

The fields on each sheet are described as follows:

### Marker

* `Name`: mstx dotting message content
* `SourceKind`: message source type, either `Host` or `Device`
* `Domain`: name of the domain to which the message belongs
* `ID`: message ID
* `Start(us)`: mstx dotting start time, in microseconds
* `End(us)`: mstx dotting end time, in microseconds
* `Pid`: process ID when `SourceKind` is `Host`, and `0` when `SourceKind` is `Device`
* `Tid`: thread ID when `SourceKind` is `Host`, and `0` when `SourceKind` is `Device`
* `Device ID`: ID of the device to which the marker belongs when `SourceKind` is `Device`, or `0` when `SourceKind` is `Host`
* `Stream ID`: ID of the stream to which the marker belongs when `SourceKind` is `Device`, or `0` when `SourceKind` is `Host`
* `Duration(us)`: mstx dotting execution time, in microseconds

### Kernel

* `Name`: name of a compute operator
* `Start(us)`: operator execution start time, in microseconds
* `End(us)`: operator execution end time, in microseconds
* `Device ID`: ID of the device where the operator is executed
* `Stream ID`: ID of the stream where the operator is executed
* `Correlation ID`: operator execution correlation ID, which is used to associate with API data
* `Type`: operator type, for example, `KERNEL_AICORE`, `KERNEL_AIVEC`, or `KERNEL_AICPU`
* `Duration(us)`: operator execution time, in microseconds

### Communication

* `Name`: name of a communication operator
* `Start(us)`: operator execution start time, in microseconds
* `End(us)`: operator execution end time, in microseconds
* `Device ID`: ID of the device where the operator is executed
* `Stream ID`: ID of the stream where the operator is executed
* `Count`: data volume transmitted by the operator
* `DataType`: data type transmitted by the operator, for example, `FP32` or `INT8`
* `CommName`: name of the communicator to which the operator belongs
* `AlgType`: communication algorithm type of the operator, for example, `RING` or `MESH`
* `Correlation ID`: operator execution correlation ID, which is used to associate with API data
* `Duration(us)`: operator execution time, in microseconds

### API, AclAPI, NodeAPI, and RuntimeAPI

* `Name`: API name
* `Start(us)`: API call start time, in microseconds
* `End(us)`: API call end time, in microseconds
* `Pid`: ID of the process that calls the API
* `Tid`: ID of the thread that calls the API
* `Correlation ID`: API call correlation ID, which is used to associate with kernel/communication data
* `Duration(us)`: API call duration, in microseconds
