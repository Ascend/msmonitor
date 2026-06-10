# MindSpore框架下msMonitor的使用方法

## 简介

在MindSpore框架下使用msMonitor组件，用于监测MindSpore模型的训练过程中的性能指标，支持动态profiling自定义for循环方式和callback方式。

## 功能介绍

### 1. 动态profiling自定义for循环方式

1. 启动dynolog daemon进程，详细介绍请参见[dynolog](./dynolog_instruct.md)。

    ```bash
    # 命令行方式开启dynolog daemon
    dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs
    ```

2. 使能dynolog环境变量

    ```bash
    export MSMONITOR_USE_DAEMON=1
    ```

3. （可选）配置msMonitor日志路径，默认路径为当前目录下的msmonitor_log。

    ```bash
    export MSMONITOR_LOG_PATH=<LOG PATH>
    # 示例：
    export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
    ```

    > [!NOTE] Note
    前3步以及第5步操作请参见 [npu-monitor](./npumonitor_instruct.md) 或 [nputrace](./nputrace_instruct.md) 使用示例。

4. 拉起训练任务，在训练任务中实例化 `DynamicProfilerMonitor` 对象，且在每一次训练后，调用 `step()` 方法。

    示例代码如下：

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
        # 定义模型
        net = Net()
        for i in range(step_num):
            # 模型训练
            train(net)
            # 调用step方法实现nputrace或npu-monitor功能
            dp.step()
    ```

5. 通过 dyno CLI 使能 nputrace 或 npu-monitor

    ```bash
    # 使能 nputrace
    dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --log-file /tmp/profile_data

    # 使能 npu-monitor
    dyno --certs-dir /home/ssl_certs npu-monitor --report-interval-s 30 --mspti-activity-kind Marker,Kernel
    ```

### 2. 动态profiling callback方式

该使能方式与动态profiling自定义for循环方式一致，唯一区别是将`step()`方法适配在`step_begin`、`step_end`回调函数中。

示例代码如下：

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
            self.dp.step() # 调用step方法实现npu trace dump或npu monitor功能
        if step_num == self.stop_step:
            self.dp.stop()
```
