# FAQ

- **Q: Why is no data reported after the npu-monitor command is sent on dyno CLI?**
- A: The npu-monitor function is developed based on MSPTI. If no data is reported, check whether the path of **libmspti.so** is correctly set in LD_PRELOAD, and then check whether the RPC request of dyno CLI is received in the dynolog log.

----------------------------------------------------------------------------------------------------------------------------------------------------
