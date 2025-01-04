#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "avc_lan_capture.pio.h" // Включаем сгенерированный заголовочный файл

#define CAPTURE_PIN 2  // Пин, подключенный к AVC-LAN через делитель напряжения

void avc_lan_capture_program_init(PIO pio, uint sm, uint offset, uint pin);

int main() {
    stdio_init_all();

    // Инициализация PIO
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &avc_lan_capture_program);
    avc_lan_capture_program_init(pio, sm, offset, CAPTURE_PIN);

    // Ждем подключения USB
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

	// // После avc_lan_capture_program_init(...)
	// if (!pio_sm_is_enabled(pio, sm)) {
	// 	printf("State machine не запущена\n");
	// } else {
	// 	printf("State machine успешно запущена\n");
	// }

    printf("Начало захвата данных с AVC-LAN\n");



    // Ждем подключения USB
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    printf("Начало захвата данных с AVC-LAN\n");

    uint32_t loop_counter = 0;

    while (true) {
        loop_counter++;
        if (loop_counter % 1000000 == 0) {
            printf("Цикл работает, loop_counter: %u\n", loop_counter);
        }

        // Проверяем, есть ли данные в FIFO
        if (!pio_sm_is_rx_fifo_empty(pio, sm)) {
            uint32_t data = pio_sm_get(pio, sm);
			if (data != 0) {  // Добавляем проверку на ноль
				printf("Получено: 0x%08x\n", data);
			}
        }
    }


    return 0;
}

void avc_lan_capture_program_init(PIO pio, uint sm, uint offset, uint pin) {
    pio_sm_config c = avc_lan_capture_program_get_default_config(offset);

    // Настраиваем пин как вход
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, false);

    // Настраиваем пины для ввода
    sm_config_set_in_pins(&c, pin);

    // Настраиваем сдвиговый регистр
    // sm_config_set_in_shift(&c, false, false, 32);
	sm_config_set_in_shift(&c, true, true, 32); // Автоперенос после 32 бит

    // Настраиваем частоту тактирования state machine
    // float freq = 220000.0f; // Частота выборки 220 кГц
	float freq = 1000000.0f; // Частота выборки 1 МГц
    float div = (float)clock_get_hz(clk_sys) / freq;
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}