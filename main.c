#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "avc_lan_capture.pio.h" // Включаем сгенерированный заголовочный файл

const uint pin_tx_high = 2;
const uint pin_tx_low = 3;  // второй инвертированный пин нельзя перенанзначить он всегда +1 (следующий)
const uint pin_rx = 4;

void print_bits(uint32_t value) {
    // Проходимся по битам от старшего (31) к младшему (0)
    for (int i = 31; i >= 0; i--) {
        // Сдвигаем value вправо на i и проверяем младший бит
        printf("%d", (value >> i) & 1);
    }
    printf("\n");
}

void print_byte_bits(uint8_t byte) {
    for (int i = 7; i >= 0; --i) {
        // Сдвигаем byte вправо на i позиций и берём младший бит:
        int bit = (byte >> i) & 1;
        printf("%d", bit);
    }
    printf("\n");
}

int main() {
    stdio_init_all();

    PIO pio = pio0;
    // uint sm_tx = 0;
    int sm_tx = pio_claim_unused_sm(pio, true);
    // uint sm_rx = 1;
    int sm_rx = pio_claim_unused_sm(pio, true);


    uint offset_tx = pio_add_program(pio, &avc_lan_tx_program);
    uint offset_rx = pio_add_program(pio, &avc_lan_rx_program);

    printf("Transmit program loaded at %d\n", offset_tx);
    printf("Receive program loaded at %d\n", offset_rx);

    // // Configure state machines, set bit rate at 5 Mbps
    // avc_lan_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 125.f / (16 * 5));
    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 125.f / (16 * 5));

    // // Configure state machines, set bit rate at 10 KHz
    // avc_lan_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 390.625);
    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 390.625);

    // Configure state machines, set bit rate at 20 KHz
    // avc_lan_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 195.3125);
    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 195.3125);

    avc_lan_tx_program_init(pio, sm_tx, offset_tx, pin_tx_high, 400);
    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 125.f);

   test_program_init(pio, sm_rx, offset_rx, pin_rx, 60);


    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 16);

    // Ждем подключения USB
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    
    // uint example_program_start_bit_offset;    // Смещение инструкции start_bit
    // uint example_program_do_transmit_offset;  // Смещение инструкции do_transmit

    // Получаем абсолютные смещения меток
    uint start_bit_abs = offset_tx + avc_lan_tx_offset_start;
    uint stop_bit_abs = offset_tx + avc_lan_tx_offset_stop;
    // uint start_abs = offset_rx + avc_lan_rx_offset_start;
    uint32_t value = 0;


    // 
    pio_sm_set_enabled(pio, sm_tx, false);

    // pio_sm_exec(pio, sm_rx, start_abs);
    // pio_sm_set_enabled(pio, sm_rx, true);
    // pio_sm_put_blocking(pio, sm_rx, 900);


    printf("send \n");
    pio_sm_set_enabled(pio, sm_tx, false);
    pio_sm_exec(pio, sm_tx, start_bit_abs);
    // pio_sm_put_blocking(pio, sm_tx, 0x00);
    pio_sm_put_blocking(pio, sm_tx, 0x01);
    // pio_sm_put_blocking(pio, sm_tx, 0xff);
    // pio_sm_put_blocking(pio, sm_tx, 0x04);
    // pio_sm_put_blocking(pio, sm_tx, 0x06);
    pio_sm_set_enabled(pio, sm_tx, true);


    printf("try to read \n");

    uint64_t prev_timestamp = 0;
    while (true) {
        
        if (!pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
            uint8_t byte = (uint8_t)pio_sm_get_blocking(pio, sm_rx);
            
            // Фиксируем текущий момент времени в микросекундах
            uint64_t current_timestamp = time_us_64();

            uint8_t inverted_byte = ~byte; // инвертируем байт
            // обработка inverted_byte далее...
            print_byte_bits(inverted_byte);
            // printf(" %u\n", inverted_byte);

            if (prev_timestamp != 0) {
                uint64_t delta = current_timestamp - prev_timestamp;
                printf("Получен байт: 0x%02X, время с предыдущего байта: %llu мкс\n",
                    inverted_byte, delta);
            } else {
                printf("Получен первый байт: 0x%02X\n", byte);
            }
            
        }
    }
}
