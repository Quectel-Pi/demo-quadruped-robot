# Quadruped Robot Wiring, Servo Calibration, and Usage Guide

This guide applies to the quadruped robot based on the **Quectel Pi M2, PCA9685, eight servos, and an SSD1306 OLED**. It covers hardware wiring, initial servo calibration, mechanical assembly, software compilation, and command-line/Web control.

## 1. Hardware Requirements

| Hardware | Quantity |
| --- | :---: |
| Quectel Pi M2 | 1 | 
| PCA9685 servo driver board | 1 | 
| Servos | 8 | 
| SSD1306 OLED | 1 | 
| Quadruped robot frame | 1 set | 
| Independent servo power supply | 1 | 
| Step-down power module | 1 | 

The hardware accessories are shown below:
![Hardware accessories](images/Photo%20of%20the%20Accessories%20en.png)


## 2. Wiring

### 2.1 PCA9685 and Quectel Pi M2

By default, this project accesses the PCA9685 through `/dev/i2c-7` at the 7-bit address `0x40`.

| PCA9685 | Quectel Pi M2 | Description |
| --- | --- | --- |
| `SDA` | Pin 27 | I²C-7 data line |
| `SCL` | Pin 28 | I²C-7 clock line |
| `VCC` | Pin 17 | PCA9685 logic power |
| `GND` | Pin 25 | Logic ground/common ground |

### 2.2 OLED and Quectel Pi M2

By default, this project accesses the OLED through `/dev/i2c-3` at the 7-bit address `0x3C`.

| OLED | Quectel Pi M2 | Description |
| --- | --- | --- |
| `SDA` | Pin 3 | I²C-3 data line |
| `SCL` | Pin 5 | I²C-3 clock line |
| `VCC` | Pin 1 | OLED logic power |
| `GND` | Pin 6 | Common ground |

### 2.3 Servo Channel Connections

The PCA9685 provides 16 PWM output channels. This project uses eight of them. The mapping between channels and the physical leg servos is shown below:

| PCA9685 Channel | Servo Name | Position |
|-------------|----------|------|
| CH0 | L1 | Lower front-left servo |
| CH1 | L2 | Lower rear-left servo |
| CH2 | R1 | Lower front-right servo |
| CH3 | R2 | Lower rear-right servo |
| CH4 | L3 | Upper front-left servo |
| CH5 | L4 | Upper rear-left servo |
| CH6 | R3 | Upper front-right servo |
| CH7 | R4 | Upper rear-right servo |

## 3. Build the Software

```bash
cd /root
git clone https://github.com/Quectel-Pi/demo-quadruped-robot.git
cd demo-quadruped-robot
make
```

The build produces the following files:

| File | Purpose |
| --- | --- |
| `quadruped_robot` | Command-line tool for servo calibration, motion control, and OLED testing |
| `quadruped_web` | Browser-based control service |


## 4. Initial Servo Position Calibration

Before assembling the robot's mechanical structure, calibrate the initial position of each servo.
The project provides the following command for initial servo calibration:

```bash
./quadruped_robot servo-center
```
This command moves the four lower servos to their 90° center positions, which serve as the initial reference for mechanical assembly. After the servos reach their target positions, install the servo linkages as shown below.
![Servo installation diagram](images/Assembly%20Diagram1.png)

Then run:
```bash
./quadruped_robot stand
```

This moves the servos to the robot's standing posture.
Finally, install all four legs to complete the robot's mechanical assembly, as shown below.
![Servo installation diagram 2](images/Assembly%20Diagram2.jpg)

## 5.OLED Expression Display

This project uses the OLED to display robot expressions. Dynamic expression switching is implemented with a framebuffer and bitmaps.

The following nine expressions are currently supported:

| Expression | Function |
| --- | --- |
| `neutral` | Default expression |
| `happy` | Happy |
| `blink` | Blink |
| `sad` | Sad |
| `worried` | Worried |
| `surprised` | Surprised |
| `sleepy` | Sleepy |
| `angry` | Angry |
| `awe` | Awe |

Use the following command to list the expressions supported by the program:

```bash
./quadruped_robot face list
```

## 6.Start the M2 Web Control Program

It is recommended to configure the M2 Web control program as a systemd service that starts automatically at boot.

After configuration, the Web control program starts automatically when the M2 boots into Debian, with no need to connect a computer and start it manually.

### Create the systemd Service

Create the service file:

```bash
vi /etc/systemd/system/quadruped-web.service
```

Add the following content:

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

Save the file and reload the systemd configuration:

```bash
systemctl daemon-reload
```

Enable the service at boot and start it immediately:

```bash
systemctl enable quadruped-web.service
systemctl start quadruped-web.service
```

Check the service status:

```bash
systemctl status quadruped-web.service
```

When the service is running normally, the output should include:

```text
Active: active (running)
```

## 7.Connect to the Control Network

The Web control page runs on the Quectel Pi M2. The phone or computer must be on the same local network as the M2 to access the M2 Web service through a browser.

The following two connection methods are recommended.

### Method 1: Connect the M2 and Phone to the Same Wi-Fi Network

Connect the M2 to a router's Wi-Fi network, and connect the phone or computer to the same Wi-Fi network.

After the M2 connects to Wi-Fi, run the following command to view its current IP address:

```bash
hostname -I
```

For example, the M2 may obtain:

```text
192.168.1.100
```

Then open the following address in a browser on the phone or computer:

```text
http://192.168.1.100:8080
```

The self-balancing vehicle Web control page will open.

### Method 2: Use a Mobile Hotspot

If no router is available, enable a mobile hotspot and connect the M2 to it.

After the M2 connects successfully, run the same command:

```bash
hostname -I
```

Obtain the M2 IP address on the mobile hotspot network, for example:

```text
172.20.10.2
```

Then open the following address directly in the phone's browser:

```text
http://172.20.10.2:8080
```

The control page will open.

> **Note:** Enter the IP address currently assigned to the M2 in the browser; it is not a static address. The IP address may change after the M2 reconnects to Wi-Fi or a hotspot. Run `hostname -I` again to obtain the current address.
