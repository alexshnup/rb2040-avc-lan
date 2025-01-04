cmake_minimum_required(VERSION 3.13)

# Импортируйте Pico SDK (укажите правильный путь к pico_sdk_import.cmake)
include(pico_sdk_import.cmake)

project(avc_lan_capture_project)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Инициализируйте Pico SDK
pico_sdk_init()

# Добавьте исполняемый файл
add_executable(avc_lan_capture
    main.c
    avc_lan_capture.pio
)

# Генерируйте заголовочные файлы из PIO-программы
pico_generate_pio_header(avc_lan_capture ${CMAKE_CURRENT_LIST_DIR}/avc_lan_capture.pio)

# Подключите необходимые библиотеки
target_link_libraries(avc_lan_capture
    pico_stdlib
    hardware_pio
    hardware_clocks
)

# Включите USB-подключение, отключите стандартный вывод через UART
pico_enable_stdio_usb(avc_lan_capture 1)
pico_enable_stdio_uart(avc_lan_capture 0)

# Добавьте дополнительные выходные файлы (UF2, BIN и т.д.)
pico_add_extra_outputs(avc_lan_capture)