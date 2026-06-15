# Demo integrating [HUB75](https://github.com/JuPfu/hub75_lvgl) with [LVGL](https://github.com/lvgl/lvgl)

Demos are based on https://github.com/JuPfu/hub75_lvgl/

This project uses git submodules. To update them after checkout use:
```
git submodule update --init --recursive
```

## LVGL
LVGL support can be disabled using 

## FOTA support
FOTA support is based on the official [example](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/ota_update).

To get started, first flash the partition table onto your device:
```
picotool load build/util/partitions.uf2
picotool reboot -u
```

Flash the demo application:
```
picotool load -x build/demo.uf2
```

### Firmware Update
Use the provided python script to upload new firmware:
```
python3 util/python_ota_update.py 192.168.0.103 demo.uf2
```

The new firmware image is confirmed and persisted if a WiFi connection can be reestablished after reboot. In case this can't be done within 16s, a forced reboot will switch back to the old firmware.

TODO: split fota.cpp into one part dealing with flash and one part handling communications so the transport can be something else (protobuf via UDP, UART, ...) as well.
