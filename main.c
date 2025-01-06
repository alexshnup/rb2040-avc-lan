#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "avc_lan_capture.pio.h" // Включаем сгенерированный заголовочный файл

const uint pin_tx_high = 2;
const uint pin_tx_low = 3;  // второй инвертированный пин нельзя перенанзначить он всегда +1 (следующий)
const uint pin_rx = 4;


int main() {
    stdio_init_all();

    PIO pio = pio0;
    uint sm_tx = 0;
    uint sm_rx = 1;
    // int sm_rx = pio_claim_unused_sm(pio, true);

    uint offset_tx = pio_add_program(pio, &differential_manchester_tx_program);
    uint offset_rx = pio_add_program(pio, &differential_manchester_rx_program);
    printf("Transmit program loaded at %d\n", offset_tx);
    printf("Receive program loaded at %d\n", offset_rx);

    // // Configure state machines, set bit rate at 5 Mbps
    // differential_manchester_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 125.f / (16 * 5));
    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 125.f / (16 * 5));

    // // Configure state machines, set bit rate at 10 KHz
    // differential_manchester_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 390.625);
    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 390.625);

    // Configure state machines, set bit rate at 20 KHz
    // differential_manchester_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 195.3125);
    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 195.3125);

    differential_manchester_tx_program_init(pio, sm_tx, offset_tx, pin_tx_high, 870);
    differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 350);

//    test_program_init(pio, sm_rx, offset_rx, pin_rx, 1);


    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 16);


    
    // uint example_program_start_bit_offset;    // Смещение инструкции start_bit
    // uint example_program_do_transmit_offset;  // Смещение инструкции do_transmit

    // Получаем абсолютные смещения меток
    uint start_bit_abs = offset_tx + differential_manchester_rx_offset_start;
    uint stop_bit_abs = offset_tx + differential_manchester_tx_offset_stop;

    while (true) {
        pio_sm_set_enabled(pio, sm_tx, false);
        pio_sm_exec(pio, sm_tx, start_bit_abs);
        pio_sm_put_blocking(pio, sm_tx, 0x01);
        pio_sm_put_blocking(pio, sm_tx, 0x04);
        pio_sm_put_blocking(pio, sm_tx, 0x06);
        pio_sm_exec(pio, sm_tx, stop_bit_abs);
        pio_sm_set_enabled(pio, sm_tx, true);
        // printf("%08x\n", pio_sm_get_blocking(pio, sm_rx));
        sleep_ms(1000);
        
        pio_sm_set_enabled(pio, sm_tx, false);
        pio_sm_exec(pio, sm_tx, start_bit_abs);
        pio_sm_put_blocking(pio, sm_tx, 0x02);
        pio_sm_exec(pio, sm_tx, stop_bit_abs);
        pio_sm_set_enabled(pio, sm_tx, true);
        // printf("%08x\n", pio_sm_get_blocking(pio, sm_rx));
        sleep_ms(1000);

        pio_sm_set_enabled(pio, sm_tx, false);
        pio_sm_exec(pio, sm_tx, start_bit_abs);
        pio_sm_put_blocking(pio, sm_tx, 0);
        pio_sm_exec(pio, sm_tx, stop_bit_abs);
        pio_sm_set_enabled(pio, sm_tx, true);
        // printf("%08x\n", pio_sm_get_blocking(pio, sm_rx));
        sleep_ms(1000);



    }

        // bool started = false;
        // while (true) {
        //     if (!pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
        //         uint32_t data = pio_sm_get_blocking(pio, sm_rx);
        //         // if (duration != 0) {
        //         if (started != true || data != 0) {
        //             started = true;
        //             // printf("%x\n", data);
        //             printf("%x \t %u\n", data, data);

        //         }
        //     }
        //     // if (!pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
        //         // uint32_t duration = pio_sm_get(pio, sm_rx);
        //         // if (duration != 0) {
        //             // printf("Duration: %u clocks\n", duration);
        //         // }
        //     // }
        // }

        // while (true) {
        //     if (!pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
        //         uint32_t cycles = pio_sm_get_blocking(pio, sm_rx);
        //         printf("PIO us: %u\n", cycles/57);
        //     }
        // }
}
