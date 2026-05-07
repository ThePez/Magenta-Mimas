default_board := "xiao_ble/nrf52840/sense"

all target:
    just build {{target}}
    just flash {{target}}

flash target:
    west flash --runner uf2 -d {{target}}/build

build target:
    west build -p \
        -b {{default_board}} \
        -d {{target}}/build \
        {{target}}

    # Fix clangd warnings
    @sed -i 's/-fno-reorder-functions//g' {{target}}/build/compile_commands.json
    @sed -i 's/-fno-printf-return-value//g' {{target}}/build/compile_commands.json
    @sed -i 's/-mfp16-format=ieee//g' {{target}}/build/compile_commands.json

    # Update clangd compile_commands symlink to most recent build
    @ln -sf {{target}}/build/compile_commands.json compile_commands.json