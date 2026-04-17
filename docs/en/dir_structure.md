# Project Directory

The full project directory structure is as follows:

```ColdFusion
├── docs                                                     # Project document directory
│   └── en                                                   # English document directory
│       ├── dir_structure.md                                 # Project directory structure description
│       ├── dyno_instruct.md                                 # Instructions for using the dyno client
│       ├── dynolog_instruct.md                              # Instructions for using the dynolog server
│       ├── faq.md                                           # FAQs
│       ├── figures                                          # Directory of figures in documents
│       ├── install_guide.md                                 # Installation guide
│       ├── mindspore_adapter_instruct.md                    # Instructions for using MindSpore adapter
│       ├── mindstudio_vulnerability_handling_procedure.md   # Vulnerability handling procedure
│       ├── npumonitor_instruct.md                           # Instructions for using npu-monitor
│       ├── nputrace_instruct.md                             # Instructions for using nputrace
│       ├── public_ip_address.md                             # Public IP address description
│       └── security_statement.md                            # Security statement
├── dynolog_npu                                              # Code directory of the dynolog_npu module
│   ├── CMakeLists.txt
│   ├── cli                                                  # Source code directory of the dyno client
│   │   └── src
│   ├── cmake                                                # CMake configuration files
│   ├── dynolog                                              # Source code directory of the dynolog server
│   │   └── src
│   └── scripts                                              # RPM packaging script
│       └── rpm                                              # RPM packaging files
├── plugin                                                   # Code directory of the plugin module
│   ├── CMakeLists.txt
│   ├── IPCMonitor                                           # Python module for IPC monitoring
│   ├── ipc_monitor                                          # Core code for IPC monitoring
│   ├── cmake                                                # CMake configuration files
│   ├── stub
│   └── third_party                                          # Third-party dependency library
├── scripts                                                  # Directory for storing build and test scripts
│   ├── apply_dyno_patches.sh                                # Script for applying dyno patches
│   ├── build.sh                                             # Main build script
│   ├── gen_dyno_patches.sh                                  # Script for generating dyno patches
│   ├── run_st.sh                                            # Script for running ST
│   └── run_ut.sh                                            # Script for running UT
├── test                                                     # Test code directory
│   ├── st                                                   # System test case directory
│   └── ut                                                   # Unit test case directory
├── third_party                                              # Third-party dependency library
│   └── dynolog                                              # dynolog third-party dependency
├── LICENSE                                                  # Project license
├── Third_Party_Open_Source_Software_Notice                  # Third-party open-source software notice
├── README.md                                                  # Project description document
```
