# msMonitor Security Statement

## System Security Hardening

You are advised to enable the `address space layout randomization` (ASLR) (level 2) in the system. Run the following command to enable it:

    echo 2 > /proc/sys/kernel/randomize_va_space

## User Account Recommendations

 1. Use the principle of least privilege. For example, prevent other users from writing data by disabling permissions like `666` and `777`.

 2. Before installing or using the tools, ensure the user's `umask` is set to `0027` or a more restrictive value. Otherwise, issues may occur, such as source code compilation failures, configuration file read failures, or excessive permissions for generated directories and files.

 3. All tools in this repository are designed to run with minimal permissions. For security reasons, do not use `root` or other privileged accounts. Always install and execute tools as a regular user.

 4. If a tool depends on CANN, use the CANN package installed by the same non-privileged user. After running the `source` command, do not modify environment variables in `set_env.sh`.

## File Permission Control

 1. In the default security mode, the root certificate, server certificate, server private key file, and CRL file are required for the online monitoring function of msMonitor. Ensure that the directory permission is `700` and the certificate permission is `600`.

 2. When providing input files to the tool, it is recommended that the file owner matches the process owner of the tool and that file permissions restrict write access for `group` and `other`s. By default, tool files written to the drive are created with permissions that prevent write access for other users. You can manually control the permissions for the generated files as needed.

 3. Maintain strict permission control during installation and use. For details about the recommended file permission settings, see the following table.

MindStudio-Monitor

| Type                              | Maximum Linux Permission|
| ---------------------------------- | ------------------- |
| Home directory                        | 750 (rwxr-x---)   |
| Program files (including scripts and library files)    | 550 (r-xr-x---)   |
| Program file directory                      | 550 (r-xr-x---)   |
| Configuration files                          | 640 (rw-r-----)   |
| Configuration file directory                      | 750 (rwxr-x---)   |
| Log files (recorded or archived)    | 440 (r--r-----)   |
| Log files (being recorded)                | 640 (rw-r-----)   |
| Log file directory                      | 750 (rwxr-x---)   |
| Debug files                         | 640 (rw-r-----)   |
| Debug file directory                     | 750 (rwxr-x---)   |
| Temporary file directory                      | 750 (rwxr-x---)   |
| Maintenance and upgrade file directory                  | 770 (rwxrwx---)   |
| Service data files                      | 640 (rw-r-----)   |
| Service data file directory                  | 750 (rwxr-x---)   |
| Key components, private keys, certificates, and ciphertext file directory| 700 (rwx------)   |
| Key components, private keys, certificates, and ciphertext files    | 600 (rw-------)   |
| APIs and scripts for encryption and decryption            | 500 (r-x------)   |

## Vulnerability Security Statement

For details about the vulnerability mechanism, see [msMonitor Vulnerability Handling Mechanism Description](./mindstudio_vulnerability_handling_procedure.md).

## Data Security Statement

1. Loading and saving data during the use of the tools may involve data risks.
2. The tool reads model weights from drive—typically files you've downloaded from the internet and specified via command-line arguments or configuration files. Using untrusted model weights can introduce unknown security risks. Before passing any model weights to the tool, verify their integrity using methods like SHA256 to ensure they come from a trusted source.

## Build Security Statement

​    msMonitor supports installation through source code compilation. During compilation, the system downloads third-party dependencies, executes shell build scripts, and generates temporary program files and compilation directories. To reduce security risks, you can perform permission control on files within the source code directory. During the build process, you can modify build scripts as needed to avoid security risks and ensure the security of the build results.

## Running Security Statement

1. When loading a dataset that exceeds available memory, or when monitoring runs too long and generated data fills the available drive space, the tool may exit unexpectedly.

2. If an exception occurs during operation, the tool will exit the process and print error messages. This is expected behavior. You are advised to locate the specific cause of the error based on the error prompts, such as by viewing log files or result files generated during the collection and parsing process.

## Public Network Address Statement

For details about the public network information in the configuration files and scripts within the msMonitor repository, see [Public Network Address](./public_ip_address.md).

## Public API Statement

The msMonitor project is developed using C++ and Python. The provided external APIs are disclosed in the documentation. Official APIs are only provided as Python APIs. Dynamic libraries do not provide services directly. You are not advised to directly call internal APIs that are not disclosed.

For scripting languages such as Python where source code is released, use the public APIs specified in the documentation. Do not directly call source code APIs that are not explicitly disclosed.

## Safe Function Usage

While unsafe functions are not forcibly disabled, you are advised to use their safe variants that explicitly take a buffer length parameter, for example, `memset_s` and `memcpy_s`.

## Communication Security Hardening

**1. Security risks of the native dynolog all-zero listening**

**Background**: msMonitor introduces the open-source third-party library dynolog. msMonitor is adapted to dynolog. The `msmonitor/dynolog_npu/dynolog/src/rpc/SimpleJsonServer.cpp` file introduced by the adaptation contains all-zero listening code. To ensure the tool functions and usability, the native dynolog all-zero listening is not modified.

**Risk**: The `dynolog/src/rpc/SimpleJsonServer.cpp` file of the library contains the all-zero listening function (bind to in6addr_any). The connection requests are processed through the IPv6 socket. By default, both IPv4 and IPv6 protocols are supported, which may cause network risks. The security risk comes from the dynolog open-source third-party library.

**Risk mitigation measures**: In the default security mode, the root certificate, server certificate, server private key file, client certificate, client private key file, and CRL file must be provided. You need to ensure the validity of the provided certificate files, and ensure that the permission on the directory where the certificates are located is `700` and the permission on the certificate file is `600`. You are advised to configure the iptables firewall mechanism to restrict the network access to the RPC port. To restrict the access to the dynolog service through IPv4, run the `echo 1 > /proc/sys/net/ipv6/bindv6only` command to ensure that the service can be accessed only using IPv6.

## Communication Matrix

**Communication matrix information**

| No.| Code Repository                        | Function                     | Source Device                      | Source IP Address                          | Source Port| Destination Device              | Destination IP Address                       | Destination Port (Listening)| Protocol| Port Description                                                                | Port Configuration    | Listening Port Configurable (Yes/No)         | Authentication Mode| Encryption Mode| Plane   | Version     | Special Scenarios| Remarks|
|:---|:----------------------------| :------------------------ |:--------------------------|:------------------------------| :----- |:-------------------| :---------------------------- |:--------------| :--- |:---------------------------------------------------------------------|:---------|:-------------------| :------- | :------- |:--------|:--------|:-----| :--- |
| 1  | MindStudio-Monitor | RPC communication between dyno and dynolog    | dyno client                  | IP address of the server where the dyno client process is running |        | Server where the dynolog server is located| IP address of the server where the dynolog server is located  | 1778          | TCP  | RPC communication                                                               | N/A     | Yes               | Certificate and key| TLS      | Service plane    | All   | None   |      |

## Disclaimer

- This tool is intended solely for debugging and development. Users are responsible for any risks and should carefully review the following information:

  - [X] Data processing and deletion: Users are responsible for managing and deleting any data generated while using this tool. Users are advised to delete such data promptly after use to prevent information leakage.
  - [X] Data confidentiality and transmission: Users understand and agree not to share or transmit any data generated by this tool. Neither the tool nor its developers are responsible for any information leaks, data breaches, or other negative consequences.
  - [X] User input security: Users are responsible for the security of any commands they enter and for any risks or losses resulting from improper input. The tool and its developers are not liable for issues caused by incorrect command usage.
- Disclaimer scope: This disclaimer applies to all individuals and entities using this tool. By using the tool, you acknowledge and accept this statement and assume all risks and responsibilities arising from its use. If you do not agree, please stop using the tool immediately.
- Before using this tool, **please read and understand the preceding disclaimer**. If you have any questions, contact the developer.
