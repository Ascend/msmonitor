# Using msMonitor in MindSpore

## Overview

msMonitor can be used to monitor the performance metrics during the training of MindSpore models. It supports dynamic profiling in custom for loop mode and callback mode.

## Functions

**Example**

1. Dynamic profiling in custom for loop mode

Step 1: Start the dynolog daemon process.

Step 2: Enable the dynolog environment variables.

Step 3: Configure the msMonitor log path.

- For details about the first three steps and step 5, see [NPU-Monitor](./npumonitor_instruct.md) or [NPUsniffer](./nputrace_instruct.md).

Step 4: Start a training job.
Instantiate the DynamicProfilerMonitor object in the training job and call **step()** after each training.

- The sample code is as follows:

```python
import numpy as np
import mindspore
import mindspore.dataset as ds
from mindspore import nn
from mindspore.profiler import DynamicProfilerMonitor

class Net(nn.Cell):
    def __init__(self):
        super(Net, self).__init__()
        self.fc = nn.Dense(2, 2)

    def construct(self, x):
        return self.fc(x)


def generator_net():
    for _ in range(2):
        yield np.ones([2, 2]).astype(np.float32), np.ones([2]).astype(np.int32)


def train(test_net):
    optimizer = nn.Momentum(test_net.trainable_params(), 1, 0.9)
    loss = nn.SoftmaxCrossEntropyWithLogits(sparse=True)
    data = ds.GeneratorDataset(generator_net(), ["data", "label"])
    model = mindspore.train.Model(test_net, loss, optimizer)
    model.train(1, data)
        
if __name__ == '__main__':
    dp = DynamicProfilerMonitor()
    step_num = 100
    # Define a model.
    net = Net()
    for i in range(step_num):
        # Train the model.
        train(net)
        # Call step to implement the npu trace dump or npu monitor function.
        dp.step()
```

Step 5: Use dyno CLI to enable trace dump or npu-monitor.

1. Dynamic profiling in callback mode:
Compared with the for loop mode, this mode adapts step() to the step_begin and step_end callback functions.

- The sample code is as follows:

```python
import mindspore
from mindspore.profiler import DynamicProfilerMonitor

class StopAtStep(mindspore.Callback):
    def __init__(self, start_step, stop_step):
        super(StopAtStep, self).__init__()
        self.start_step = start_step
        self.stop_step = stop_step
        self.dp = DynamicProfilerMonitor()

    def step_begin(self, run_context):
        cb_params = run_context.original_args()
        step_num = cb_params.cur_step_num
        if step_num == self.start_step:
            self.dp.start()

    def step_end(self, run_context):
        cb_params = run_context.original_args()
        step_num = cb_params.cur_step_num
        if self.start_step <= step_num < self.stop_step:
            self.dp.step() # Call step to implement the npu trace dump or npu monitor function.
        if step_num == self.stop_step:
            self.dp.stop()
```
