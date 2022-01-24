# esp32-keyble-homeassistant
Use an ESP32 Ethernet Gateway by Olimex (https://www.olimex.com/Products/IoT/ESP32/ESP32-GATEWAY/open-source-hardware) for the Eqiva Bluetooth smart lock to integrate it in Home Assistant as MQTT lock.

Key points:

- Use of NimBLE-Arduino library (lower memory usage, less crashes!)
- Always-on Bluetooth connection (zero-delay control of the lock with minimal battery impact!)
- Home Assistant MQTT lock entity autoconfiguration!
- Various tweaks to avoid unnecessary logic

The project has been fully tested on the Olimex hardware above (I have the one with external antenna for maximum signal) successfully and it has been running rock solid for a few months. It might be working for similar ESP32 hardware with on-board Ethernet with minimal or no modification.

# How to install:

Pre-requisites not covered here:
- Installed VSCode + Platformio plugin
- Installed Olimex ESP32 USB drivers
- MQTT server configured and running
- HomeAssistant server configured, running and paired to the MQTT server
- Eqiva Lock credentials generated using the guide in the project: https://github.com/oyooyo/keyble

Steps:
- Clone the repo
- Open the project with VSCode + Platformio
- Edit the configuration parameters in src/main.cpp to add your credentials for MQTT and Eqiva Lock
- Connect and flash to the Olimex device
- A lock entity should now appear on your HomeAssistant server, enjoy!

Based on the great work of:

tc-maxx/esp32-keyble
lumokitho/esp32-keyble
MariusSchiffer/esp32-keyble
oyooyo/keyble
