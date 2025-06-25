# SSK Multi-Pen Plotter Firmware

A custom firmware for an ESP32-based multi-pen plotter with automatic tool changing capabilities, designed for educational tinkering labs across Kerala schools.

## 🎯 Project Overview

This project is part of a government initiative undertaken by **SSK** to establish tinkering labs in **98 schools across Kerala**. The multi-pen plotter provides students with hands-on experience in machine programming and operation, fostering practical learning in automation and digital fabrication.

### Key Features

- **🖊️ Multi-Pen Plotting**: Support for multiple pens with automatic tool changing
- **🔄 Automatic Tool Change**: M6T<X> command for seamless pen switching (X = pen number)
- **📱 Multiple User Interfaces**: Built-in UI for drawing and image import
- **🌐 Connectivity**: Serial, WiFi, and Bluetooth communication support
- **💾 Storage**: SD card support for storing drawings and configurations
- **🔦 Laser Pointer**: Integrated laser pointer for positioning and alignment
- **⌨️ Input Control**: Keypad interface via daughter board
- **🎛️ Web Configuration**: FluidNC web installer for easy machine setup

## 🏗️ Hardware

- **Microcontroller**: ESP32 with custom PCB design
- **Motion Control**: Stepper motor drivers
- **Tool System**: Automatic pen changing mechanism
- **Storage**: SD card module
- **Interface**: Keypad daughter board
- **Positioning Aid**: Laser pointer module
- **Connectivity**: WiFi, Bluetooth, Serial communication

## 🚀 Installation

### Prerequisites

- FluidNC Web Installer
- Git (for cloning the repository)

### Setup Process

1. **Clone the Repository**

   ```bash
   git clone https://github.com/your-repo/ssk-mtm-academic-internship.git
   cd ssk-mtm-academic-internship
   ```

2. **Install Firmware**
   - flash the firmware using platformio or arduino

3. **Configure Machine**
   - Use the FluidNC web installer to set the machine configuration
   - Upload your specific plotter parameters and tool definitions

## 📖 Usage

### Basic Operations

#### Tool Changing

Use the M6T command to change tools automatically:

```gcode
M6T1  ; Change to pen 1
M6T2  ; Change to pen 2
M6T3  ; Change to pen 3
```

#### Drawing Operations

- Import images through the built-in UI
- Use the web interface for real-time control
- Connect via serial, WiFi, or Bluetooth for remote operation

### User Interface Options

1. **Built-in UI**: Direct machine interface for drawing and image import
2. **Web Interface**: Browser-based control panel
3. **Serial Terminal**: Command-line interface for advanced users
4. **Mobile Apps**: Bluetooth connectivity for mobile control

## 🔧 Configuration

The machine configuration is handled through the FluidNC web installer. Key parameters include:

- Stepper motor settings
- Tool change positions and sequences
- Communication interface settings
- SD card and storage options
- UI preferences and display settings

## 🎓 Educational Applications

Perfect for:

- **STEM Education**: Hands-on programming and automation learning
- **Art & Design**: Digital art creation and physical plotting
- **Engineering Concepts**: Understanding CNC principles and G-code
- **Problem Solving**: Troubleshooting and optimization exercises

## 🔄 Version History

### Current Version

- ✅ Automatic tool changing with M6T<X> command
- ✅ Multiple UI options with built-in drawing interface
- ✅ Enhanced connectivity (Serial, WiFi, Bluetooth)
- ✅ Improved SD card handling
- ✅ Laser pointer integration
- ✅ Keypad control interface

### Previous Versions

- Basic pen plotting functionality
- Single tool operation
- Limited connectivity options

## 🛠️ Technical Details

### Based on FluidNC

This firmware is a modified version of [FluidNC by bdring](https://github.com/bdring/FluidNC), customized specifically for multi-pen plotting applications with automatic tool changing capabilities.

### Communication Protocols

- **Serial**: USB/UART communication
- **WiFi**: Wireless network connectivity
- **Bluetooth**: Mobile device pairing

### Supported G-code Commands

- Standard FluidNC G-code set
- Custom M6T<X> tool change commands
- Extended plotting-specific commands

## 🤝 Contributing

This project is part of the SSK initiative for educational tinkering labs. Contributions that enhance the educational value and machine functionality are welcome.


## 🙏 Acknowledgments

- **FluidNC Project**: [bdring/FluidNC](https://github.com/bdring/FluidNC) for the base firmware
- **Students and Educators**: Who will use and improve this system
- **FabLab Kerala MTM team**: for build the project from ground up

---

*Empowering the next generation of makers and engineers through hands-on learning experiences.*
