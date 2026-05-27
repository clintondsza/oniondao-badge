idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__arduino-esp32 GxEPD2
)

# -DBOARD_HAS_PSRAM (was board_build.arduino.memory_type = qio_opi in platformio)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD_HAS_PSRAM)

# The Arduino/PlatformIO build did not treat warnings as errors; ESP-IDF adds a
# global -Werror=all. Keep these (benign) warnings non-fatal for the app code,
# matching the original Arduino build. (A blanket -Wno-error can't undo GCC's
# -Werror=all; only specific -Wno-error=<name> resets it.)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format-truncation)
