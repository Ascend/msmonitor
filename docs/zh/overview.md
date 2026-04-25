# 总体介绍

MindStudio Monitor（MindStudio一站式在线监测工具，msMonitor）提供用户在集群场景性能监测定位端到端能力。

msMonitor基于[dynolog](https://github.com/facebookincubator/dynolog)开发，结合AI框架（[Ascend PyTorch调优工具](https://gitcode.com/Ascend/pytorch/blob/v2.7.1/docs/zh/ascend_pytorch_profiler/ascend_pytorch_profiler_user_guide.md)、[MindSpore调优工具](https://gitcode.com/Ascend/docs/blob/master/MindStudio/master/mindspore_profiler_user_guide.md)）的动态采集能力和[msPTI](https://gitcode.com/Ascend/mspti/blob/master/docs/zh/getting_started/quick_start.md)，为用户提供**nputrace**和**npu-monitor**功能：

- **npu-monitor功能**：轻量常驻后台，监测关键算子耗时。
- **nputrace功能**：获取到框架、CANN以及device的详细性能数据。

![msMonitor](figures/msMonitor.png)  

如上图所示msMonitor分为三部分： 

1. **Dynolog daemon**：dynolog守护进程，每个节点只有一个守护进程，负责接收dyno CLI的RPC请求、触发nputrace和npu-monitor功能、上报数据的处理以及最终数据的展示，dynolog的详细介绍请参见[dynolog](./user_guide/dynolog_instruct.md)。
2. **Dyno CLI**：dyno客户端，为用户提供nputrace和npu-monitor子命令，任意节点都可以安装，dyno的详细介绍请参见[dyno](./user_guide/dyno_instruct.md)。
3. **MSPTI Monitor**：基于MSPTI实现的监测子模块，通过调用MSPTI的API获取性能数据，并上报给Dynolog daemon。
