# Using msMonitor with the MindSpore Framework

<!-- md-trans-meta sourceCommit=76329dd28bc91c4378603d72fe34ab47b750c710 translatedAt=2026-08-12T08:30:55.025Z pushedAt=2026-08-12T08:32:50.337Z -->

## Introduction

The msMonitor component is used with the MindSpore framework to monitor performance metrics during the training of MindSpore models. It supports dynamic profiling in both custom for loop mode and callback mode.

## Feature Introduction

### 1. Dynamic Profiling Custom Loop Mode

1. Start the dynolog daemon process. For details, see [dynolog](./dynolog_instruct.md).

    ```bash
    # Start the dynolog daemon via command line
    dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs
    ```

2. Enable the dynolog environment variable.

    ```bash
    export MSMONITOR_USE_DAEMON=1
    ```

3. (Optional) Configure the msMonitor log path. The default path is msmonitor_log in the current directory.

    ```bash
    export MSMONITOR_LOG_PATH=<LOG PATH>
    # Example:
    export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
    ```

    > [!NOTE]
    > 
    > For steps 1 through 3 and step 5, see the usage examples in [npu-monitor](./npumonitor_instruct.md) or [nputrace](./nputrace_instruct.md).

4. Start the training task, instantiate the `DynamicProfilerMonitor` object in the training task, and call the `step()` method after each training iteration.

    The sample code is as follows:

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
        for_ in range(2):
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
        # Define the model.
        net = Net()
        for i in range(step_num):
            # Model training.
            train(net)
            # Call the step method to implement nputrace or npu-monitor functionality.
            dp.step()
    ```

5. Enable nputrace or npu-monitor through the dyno CLI.

    ```bash
    # Enable nputrace
    dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --log-file /tmp/profile_data

    # Enable npu-monitor
    dyno --certs-dir /home/ssl_certs npu-monitor --report-interval-s 30 --mspti-activity-kind Marker,Kernel
    ```

### 2. Dynamic Profiling Callback Mode

This enabling method is consistent with the dynamic profiling custom for loop mode, with the only difference being that the <code>step()</code> method is adapted into the <code>step_begin</code> and <code>step_end</code> callback functions.

The sample code is as follows:

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
            self.dp.step() # Call the step method to implement NPU trace dump or NPU monitor function
        if step_num == self.stop_step:
            self.dp.stop()
```
