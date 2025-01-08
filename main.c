#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "avc_lan_capture.pio.h" // Включаем сгенерированный заголовочный файл

const uint pin_tx_high = 2;
const uint pin_tx_low = 3;  // второй инвертированный пин нельзя перенанзначить он всегда +1 (следующий)
const uint pin = 4;

void print_bits(uint32_t value) {
    // Проходимся по битам от старшего (31) к младшему (0)
    for (int i = 31; i >= 0; i--) {
        // Сдвигаем value вправо на i и проверяем младший бит
        printf("%d", (value >> i) & 1);
    }
    printf("\n");
}

int main() {
    stdio_init_all();

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);  // or pull_down if your wiring calls for it

    PIO pio = pio0;
    // uint sm_tx = 0;
    int sm_tx = pio_claim_unused_sm(pio, true);
    // uint sm_rx = 1;
    int sm_rx = pio_claim_unused_sm(pio, true);


    uint offset_tx = pio_add_program(pio, &differential_manchester_tx_program);
    uint offset_rx = pio_add_program(pio, &differential_manchester_rx_program);

    printf("Transmit program loaded at %d\n", offset_tx);
    printf("Receive program loaded at %d\n", offset_rx);

    pio_sm_config c = differential_manchester_rx_program_get_default_config(offset_rx);
    sm_config_set_in_pins(&c, pin);
    sm_config_set_jmp_pin(&c, pin);

    // Use separate FIFOs (no PIO_FIFO_JOIN). 
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);

    // Decide your clock divider
    sm_config_set_clkdiv(&c, 1.0f);

    // Initialize SM
    pio_sm_init(pio, sm_rx, offset_rx, &c);
    pio_sm_set_enabled(pio, sm_rx, true);

    // // Configure state machines, set bit rate at 5 Mbps
    // differential_manchester_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 125.f / (16 * 5));
    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 125.f / (16 * 5));

    // // Configure state machines, set bit rate at 10 KHz
    // differential_manchester_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 390.625);
    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 390.625);

    // Configure state machines, set bit rate at 20 KHz
    // differential_manchester_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 195.3125);
    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 195.3125);

    differential_manchester_tx_program_init(pio, sm_tx, offset_tx, pin_tx_high, 400);
    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 125.f);

//    test_program_init(pio, sm_rx, offset_rx, pin_rx, 1);


    // differential_manchester_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 16);

    // Ждем подключения USB
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }
    
    // uint example_program_start_bit_offset;    // Смещение инструкции start_bit
    // uint example_program_do_transmit_offset;  // Смещение инструкции do_transmit

    // Получаем абсолютные смещения меток
    uint start_bit_abs = offset_tx + differential_manchester_tx_offset_start;
    uint stop_bit_abs = offset_tx + differential_manchester_tx_offset_stop;
    // uint start_abs = offset_rx + differential_manchester_rx_offset_start;
    uint32_t value = 0;


    // 
    pio_sm_set_enabled(pio, sm_tx, false);

    // pio_sm_exec(pio, sm_rx, start_abs);
    // pio_sm_set_enabled(pio, sm_rx, true);
    // pio_sm_put_blocking(pio, sm_rx, 900);

    while (true) {

        printf("send \n");
        
        // // Первым словом SM выталкивает нам "mov isr, x" => длину HIGH при старте.
        // uint32_t x_val = pio_sm_get_blocking(pio, sm_rx);
        // uint measuredHigh = 200 - x_val; // если вы делаете именно так

        pio_sm_set_enabled(pio, sm_tx, false);
        pio_sm_exec(pio, sm_tx, start_bit_abs);
        // pio_sm_put_blocking(pio, sm_tx, 0x00);
        pio_sm_put_blocking(pio, sm_tx, 0x01);
        // pio_sm_put_blocking(pio, sm_tx, 0xff);
        // pio_sm_put_blocking(pio, sm_tx, 0x04);
        // pio_sm_put_blocking(pio, sm_tx, 0x06);
        pio_sm_set_enabled(pio, sm_tx, true);


        
        printf("try to read \n");
        if (!pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
            uint32_t val = pio_sm_get_blocking(pio, sm_rx);
            printf("Got cycle count: %u\n", val);
        }



        // printf("Start bit HIGH length ~ %u us\n", measuredHigh);

        // // Теперь ждём байт данных (8 бит), которые SM должна autopush-ить.
        // uint32_t data_byte = pio_sm_get_blocking(pio, sm_rx) & 0xFF; 
        // printf("Data byte = 0x%02x\n", data_byte);


        sleep_ms(500);

        // int x_val = (int16_t)(pio_sm_get_blocking(pio, sm_rx) & 0xFFFF); 

        // uint32_t x_val = pio_sm_get_blocking(pio, sm_rx);
        // int y_val = (int16_t)(pio_sm_get_blocking(pio, sm_rx) & 0xFFFF);
        // // printf("x_val=%d y_val=%d\n", x_val, y_val);
        // printf("x_val=%d\n", x_val);

        // // value = pio_sm_get_blocking(pio, sm_rx);
        // value = pio_sm_get(pio, sm_rx);
        // // printf("\n");
        // print_bits(value);
        // printf("\t %08x\n", value);
        
        // pio_sm_set_enabled(pio, sm_tx, false);
        // pio_sm_exec(pio, sm_tx, start_bit_abs);
        // pio_sm_put_blocking(pio, sm_tx, 0x02);
        // pio_sm_exec(pio, sm_tx, stop_bit_abs);
        // pio_sm_set_enabled(pio, sm_tx, true);
        // value = pio_sm_get_blocking(pio, sm_rx);
        // printf("\n");
        // print_bits(value);
        // // printf("\t %08x\n", value);
        // sleep_ms(500);

        // pio_sm_set_enabled(pio, sm_tx, false);
        // pio_sm_exec(pio, sm_tx, start_bit_abs);
        // pio_sm_put_blocking(pio, sm_tx, 0);
        // pio_sm_exec(pio, sm_tx, stop_bit_abs);
        // pio_sm_set_enabled(pio, sm_tx, true);
        // value = pio_sm_get_blocking(pio, sm_rx);
        // printf("\n");
        // print_bits(value);
        // // printf("\t %08x\n", value);
        // sleep_ms(100);

        // pio_sm_set_enabled(pio, sm_tx, false);
        // pio_sm_exec(pio, sm_tx, start_bit_abs);
        // pio_sm_put_blocking(pio, sm_tx, 0);
        // pio_sm_exec(pio, sm_tx, stop_bit_abs);
        // pio_sm_set_enabled(pio, sm_tx, true);
        // value = pio_sm_get_blocking(pio, sm_rx);
        // printf("\n");
        // print_bits(value);
        // // printf("\t %08x\n", value);
        // sleep_ms(100);



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
