<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  🇨🇳 <b>简体中文</b> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### 太阳能网关节点：通过最多十个串口读取逆变器和电池（ESP32-S3 固件、其网页面板和协议库）

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**诚实性检查 - 今天真正能运行的部分:** **成熟度：scaffolding。** 固件能在 ESP-IDF 5.4.2 容器中构建，其核心（设置、与各类设备的交互、模拟 UART 的运算：678 项检查）已在电脑上用替身设备测试，它生成的消息被 ARMOR-COMMON 接受，网页面板已在浏览器中对着替身节点试用。**它从未在开发板上运行过，也没有连接过任何逆变器或电池**：Wi-Fi、TLS 面板、更新、硬件和模拟 UART 以及协议格式（根据公开文档和记忆编写）都未经尝试。测试中的 ANT-BMS 帧是他人在自己的设备上抓取的，本项目自己还没有读过任何一台。

---

## 🎯 概述

* **两种开发板，一套固件：** 走 Wi-Fi 的 ESP32-S3-WROOM-1 N16R8（默认）和走以太网线的 Waveshare ESP32-S3-ETH（DHCP 或固定地址；保留自己的 Wi-Fi 网络以便从手机访问）。镜像在构建时选择（`tools/build_node.sh generic s3-wifi` 或 `generic s3-eth`）；引脚表、端口的默认引脚和接入方式随开发板而定，镜像只适用于对应的开发板。两者都能构建并已在电脑上测试，但都没在开发板上运行过。
* **节点固件**（Wi-Fi 的 ESP32-S3-WROOM-1 N16R8，或有线的 Waveshare ESP32-S3-ETH）：十个串口，三个硬件 UART 和七个模拟 UART（最高 19200 波特），每个都独立，可读取逆变器、电池或原始监视，因此一个节点可以只读逆变器、只读电池或两者混合。
* **节点的网页面板，** 与雷达节点相同：用 USB 控制台显示的代码进行设置、登录和用户、Wi-Fi（站点和接入点）、代理、带回滚的空中更新、日志、HTTPS，以及串口页面（显示每个端口收到的内容，用于尚未解码的协议）和读数页面，支持七种语言。
* **Voltronic / MPP Solar 逆变器**（Axpert、PIP、InfiniSolar 及其克隆），RS232 2400 波特：帧及其 CRC，以及读数 `QPIGS`（电网、输出、电池、光伏、状态位）、`QMOD`（模式）、`QPIWS`（按名称列出的警告和故障）和 `QPIRI`（额定值）。只能构造读取命令：设置会改变房屋的供电方式。三种方言，可在端口上选择或由端口自动找到（*auto*）：PI30、REVO（另一种 `QPIGS`，以校验和结尾的应答）和 PI18（`^P005GS`：InfiniSolar V、LV5048、SunGoldPower），仅读取。
* **Pylontech 电池**（US2000、US3000、US5000）：控制台的 `pwr` 表（每个模块的电压、电流、温度和电量）、`bat <n>`（每个电芯的电压和温度）以及 `info <n>`（型号、剩余和满容量、循环次数），汇总为一个电池组及其总容量和能量，还有带两项校验的 RS485 帧。这些格式是凭对公开控制台的记忆写成的，不同固件可能不同。各列按表头名称查找（包括带 Id 列、MosTempr 和 SysAlarm.St 的 US5000 V2.3 格式），剩余电量和均衡来自 `bat`，型号、额定容量和循环次数来自 `info` 和 `stat`，只询问一次并保留半小时。
* **节点的消息**（`armor/solar/<节点>/<设备>/state`，每台逆变器或电池组一条），在 ARMOR-COMMON 中以模式和一致性向量定义；各端口生成的内容会对照它们检查。
* **ANT-BMS 电池**（自制电池组上的黑色板，7S 至 32S），3.3 V UART，19200 波特：支持其固件的两种协议（节点先用一种询问，无应答再用另一种，并保持在有应答的那种），读取电芯、温度、荷电状态、电流、容量以及 MOSFET 和均衡器状态，并用在四种真实型号上抓取的帧做了验证。只读：其写入命令可能在带载时断开电池。端口页面上的按钮还可读取 BMS 的型号、版本以及 56 项保护和均衡设置（新协议，只读）。
* **尚未完成：** ANT-BMS 的蓝牙连接（节点使用更稳定的线缆）、Android 标签页，以及在真实开发板上用真实设备的运行。

## 📂 仓库结构

```text
ARMOR-SOLAR/
├── main/    the ESP-IDF component: app_main, solar_manager (one task per port), uart_ports, network, web_server, api_shared, mqtt_link, node_store, tls_cert
├── core/    voltronic, pylontech, ant_bms, solar_json, json + solar_config, poller, soft_uart, netplan, auth, board_s3 (no hardware in them)
├── panel/   the web panel: index.html, app.js, text.js (7 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs
├── tests/   test_solar.cpp, test_node.cpp, emit_samples.cpp, emit_poller_samples.cpp, check_samples.py
└── docs/    NODE_FIRMWARE, NODE_HARDWARE, PROTOCOLS, SOLAR_MESSAGES, STUDIO_MENUS
```

## 🛠️ 开发环境

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar && build/host/test_node && build/host/test_board_eth      # 600 checks, -Werror
build/host/emit_poller_samples | python tests/check_samples.py   # what the ports make is accepted by ARMOR-COMMON
tools/build_node.sh generic                       # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth               # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet): dist/generic-s3-eth.bin
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
```

参见[固件指南](docs/NODE_FIRMWARE.md)（开发板、端口、首次启动和台架检查清单）以及[接线说明](docs/NODE_HARDWARE.md)。

## 🔗 相关项目

**A.R.M.O.R.**（Autonomous Radar & Multimodal Observation Range）是由若干独立仓库组成的周界安防系统。每个仓库都有自己的版本、测试和 README；家族成员如下：

* **[ARMOR-COMMON](../ARMOR-COMMON)** - 消息契约、验证器、一致性向量和生成的类型
* **[ARMOR-RADAR](../ARMOR-RADAR)** - 适用于 ESP32-S3 的现场节点固件，带三个雷达和自带网页面板
* **ARMOR-SOLAR** (本仓库) - 太阳能逆变器与电池的协议，以及网关节点的消息
* **[ARMOR-SERVER](../ARMOR-SERVER)** - 中央协调器：遥测、报警、设备、太阳能读数和摄像头
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - 网页控制台：摄像头、雷达、报警、太阳能和 2D/3D 场地设计器
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - 带实时 2D/3D 雷达的 Android 操作员客户端
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - 会解释决策且从不执行动作的视觉推理策略
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - 带无法伪造确认的离线语音意图
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - 外壳、电子器件和台架验收矩阵
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - 部署、CM5 测试台、备份与 TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - 带可重复故障的离线遥测模拟器
* **[ARMOR-DOCS](../ARMOR-DOCS)** - 架构、安全基线和能力矩阵

## 📚 文档与社区

更多阅读：

* [能力矩阵：哪些已被证实，哪些没有](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [项目目录：版本以及各仓库之间的依赖](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [本仓库的变更记录](CHANGELOG.md)
* [许可证（GPL-3.0-or-later）](LICENSE)
* 问题、想法与反馈：electrohobby3d@gmail.com

## 👤 作者

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 许可证

GPL-3.0-or-later - 见 [LICENSE](LICENSE)。
