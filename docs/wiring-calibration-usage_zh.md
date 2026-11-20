# 四足机器人接线、舵机校准与使用指南

本文档适用于基于 **Quectel Pi M2、PCA9685、8 路舵机和 SSD1306 OLED** 的四足机器人。内容包括硬件接线、舵机初始位置校准、机械安装、程序编译、命令行/Web 控制。

## 1. 硬件准备

| 硬件 | 数量 |
| --- | :---: |
| Quectel Pi M2 | 1 | 
| PCA9685 舵机驱动板 | 1 | 
| 舵机 | 8 | 
| SSD1306 OLED | 1 |
| 四足机器人结构件 | 1 套 | 
| 独立舵机电源 | 1 | 
| 电源降压模块 | 1 | 

配件实物图如下所示：
![配件实物图](images/Photo%20of%20the%20Accessories%20zh.png)


## 2. 接线说明

### 2.1 PCA9685 与 Quectel Pi M2

本项目默认从 `/dev/i2c-7` 访问 PCA9685，7 位地址为 `0x40`。

| PCA9685 | Quectel Pi M2 | 说明 |
| --- | --- | --- |
| `SDA` | Pin 27 | I²C-7 数据线 |
| `SCL` | Pin 28 | I²C-7 时钟线 |
| `VCC` | Pin 17 | PCA9685 逻辑供电 |
| `GND` | Pin 25 | 逻辑地/公共地 |

### 2.2 OLED 与 Quectel Pi M2

本项目默认从 `/dev/i2c-3` 访问 OLED，7 位地址为 `0x3C`。

| OLED | Quectel Pi M2 | 说明 |
| --- | --- | --- |
| `SDA` | Pin 3 | I²C-3 数据线 |
| `SCL` | Pin 5 | I²C-3 时钟线 |
| `VCC` | Pin 1 | OLED 逻辑供电 |
| `GND` | Pin 6 | 公共地 |

### 2.3 舵机通道连接

PCA9685 共提供 16 路 PWM 输出，本项目使用其中 8 路，通道与实际腿部舵机对应表如下：

| PCA9685 通道 | 舵机名称 | 位置 |
|-------------|----------|------|
| CH0 | L1 | 底部舵机左前 |
| CH1 | L2 | 底部舵机左后 |
| CH2 | R1 | 底部舵机右前 |
| CH3 | R2 | 底部舵机右后 |
| CH4 | L3 | 上部舵机左前 |
| CH5 | L4 | 上部舵机左后 |
| CH6 | R3 | 上部舵机右前 |
| CH7 | R4 | 上部舵机右后 |

## 3. 编译程序

```bash
cd /root
git clone https://github.com/Quectel-Pi/demo-quadruped-robot.git
cd demo-quadruped-robot
make
```

编译后生成：

| 文件 | 用途 |
| --- | --- |
| `quadruped_robot` | 舵机校准、动作和 OLED 命令行工具 |
| `quadruped_web` | 浏览器控制服务 |


## 4. 舵机初始位置校准

正式安装机器人机械结构前，需要对各舵机进行初始位置校准。
当前项目提供舵机初始位置校准命令：

```bash
./quadruped_robot servo-center
```
该命令将底部四个舵机转动至 90° 中位位置，以此作为机械装配的初始基准。待舵机转动到位后，按照下图所示安装舵机连接件。
![舵机安装示意图](images/Assembly%20Diagram1.png)

随后执行：
```bash
./quadruped_robot stand
```

使舵机转动至机器人的站立姿态。
最后安装四条腿，完成机器人机械结构的装配，如下图所示。
![舵机安装示意图2](images/Assembly%20Diagram2.jpg)

## 5.OLED表情显示

本项目使用 OLED 显示机器人表情，通过帧缓冲和位图实现动态表情切换。

当前支持以下 9 种表情：

| 表情名称 | 功能 |
| --- | --- |
| `neutral` | 默认表情 |
| `happy` | 开心 |
| `blink` | 眨眼 |
| `sad` | 难过 |
| `worried` | 担忧 |
| `surprised` | 惊讶 |
| `sleepy` | 困倦 |
| `angry` | 生气 |
| `awe` | 震惊 |

可使用以下命令查看程序支持的表情：

```bash
./quadruped_robot face list
```

## 6.启动 M2 Web 控制程序

M2 Web 控制程序建议配置为 systemd 开机自启动服务。

配置完成后，M2 上电进入 Debian 系统后即可自动启动 Web 控制程序，无需连接电脑手动运行。

### 创建 systemd 服务

创建服务文件：

```bash
vi /etc/systemd/system/quadruped-web.service
```

写入以下内容：

```ini
[Unit]
Description=Quadruped Robot Web Control Service
After=network.target

[Service]
Type=simple
WorkingDirectory=/root/demo-quadruped-robot
ExecStart=/root/demo-quadruped-robot/quadruped_web
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
```

保存后重新加载 systemd 配置：

```bash
systemctl daemon-reload
```

设置开机自启动并立即启动：

```bash
systemctl enable quadruped-web.service
systemctl start quadruped-web.service
```

查看运行状态：

```bash
systemctl status quadruped-web.service
```

正常运行时应显示：

```text
Active: active (running)
```

## 7.连接控制网络

Web 控制页面运行在 Quectel Pi M2 上，手机或电脑需要与 M2 处于同一局域网，才能通过浏览器访问 M2 的 Web 服务。

推荐使用以下两种连接方式。

### 方式一：M2 和手机连接同一个 Wi-Fi

将 M2 连接到路由器 Wi-Fi，同时将手机或电脑连接到相同的 Wi-Fi 网络。

M2 连接 Wi-Fi 后，可执行以下命令查看当前 IP 地址：

```bash
hostname -I
```

例如 M2 获取到：

```text
192.168.1.100
```

则在手机或电脑浏览器中访问：

```text
http://192.168.1.100:8080
```

即可进入自平衡小车 Web 控制页面。

### 方式二：使用手机热点

在没有路由器的情况下，可以直接开启手机热点，再让 M2 连接该热点。

M2 连接成功后，同样执行：

```bash
hostname -I
```

获取 M2 在手机热点网络中的 IP 地址，例如：

```text
172.20.10.2
```

然后直接在该手机浏览器中访问：

```text
http://172.20.10.2:8080
```

即可进入控制页面。

> **注意：** 浏览器中填写的是 M2 当前获得的 IP 地址，不是固定地址。M2 重新连接 Wi-Fi 或热点后，IP 地址可能发生变化，需要重新执行 `hostname -I` 查询。
