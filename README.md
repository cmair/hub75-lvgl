# Demo integrating HUB75 (https://github.com/JuPfu/hub75_lvgl) with LVGL (https://github.com/lvgl/lvgl)

Demos are based on https://github.com/JuPfu/hub75_lvgl/

This repository uses git submodules.

To add git submodules use:
`git submodule add --branch hub75_library https://github.com/cmair/rp2350_hub75.git libraries/hub75`
`git submodule add --branch release/v9.5 https://github.com/lvgl/lvgl.git libraries/lvgl`

This is not necessary for this repository, as they are already set-up. Just update them after checkout:
`git submodule update --init --recursive`
