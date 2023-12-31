# fw.cmake - contains firmware specific build stuff
list(APPEND NEXBOOT_FW_DRIVER_HEADERS
    include/nexboot/drivers/uart16550.h
    include/nexboot/drivers/vgaconsole.h
    include/nexboot/drivers/ps2kbd.h)
list(APPEND NEXBOOT_FW_HEADERS
    include/nexboot/bios/bios.h)
