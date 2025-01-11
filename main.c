#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "avc_lan_capture.pio.h" // Включаем сгенерированный заголовочный файл

const uint pin_tx_high = 2;
const uint pin_tx_low = 3;  // второй инвертированный пин нельзя перенанзначить он всегда +1 (следующий)
const uint pin_rx = 4;

uint32_t calculating_delay = 0;
uint32_t byte_counting = 0;

// используется для отправки последнего пакета. Так как у нас отправка по факту после прихода нового по умолчанию
uint64_t count_cycles_without_new_data = 0;
#define LAST_DATA_THRESHOLD 50000 

#define MAX_BUFFER_SIZE 1024  // макс. размер буфера в байтах
#define DELTA_THRESHOLD 1000   // порог в микросекундах

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

char* byte_to_bit_string(uint8_t byte) {
    // Выделение памяти для 9 символов: 8 бит + завершающий нулевой символ
    char *bit_string = malloc(9);
    if (!bit_string) {
        return NULL; // обработка ошибки выделения памяти
    }

    for (int i = 7; i >= 0; --i) {
        // Получаем очередной бит и сохраняем как символ
        bit_string[7 - i] = ((byte >> i) & 1) ? '1' : '0';
    }
    bit_string[8] = '\0'; // Завершающий нулевой символ

    return bit_string;
}

uint8_t reverse_bits(uint8_t byte) {
    uint8_t reversed = 0;
    for (int i = 0; i < 8; ++i) {
        // Извлекаем i-й бит из исходного байта:
        uint8_t bit = (byte >> i) & 1;
        // Устанавливаем соответствующий бит в перевёрнутом байте:
        reversed |= bit << (7 - i);
    }
    return reversed;
}



uint32_t reverse_Xbits(uint32_t value, int width) {
    // Проверка допустимости ширины (не больше 32 для uint32_t).
    if (width < 1 || width > 32) {
        return value; // Можно обработать ошибку иным способом, если нужно.
    }

    uint32_t reversed = 0;
    for (int i = 0; i < width; ++i) {
        // Извлекаем i-й бит исходного значения в пределах заданной ширины
        uint32_t bit = (value >> i) & 1;
        // Устанавливаем соответствующий бит в результирующем значении
        reversed |= bit << (width - 1 - i);
    }
    // Сохраняем биты за пределами ширины без изменений:
    uint32_t mask = ~((1u << width) - 1); // маска для битов вне области реверса
    reversed |= (value & mask);

    return reversed;
}

//////////////////////////////////////////////////////////////



// Глобальный буфер для накопления битов пакета и текущая длина в битах
static uint8_t bit_buffer[MAX_BUFFER_SIZE] = {0};
static int bit_length = 0;

// Функция для добавления новых байтов в битовый буфер
void append_byte_to_buffer(uint8_t byte) {
    // Добавляем байт в буфер, предполагая, что буфер достаточно велик
    int byte_offset = bit_length / 8;
    int bit_offset = bit_length % 8;

    if (bit_offset == 0) {
        // Если байт выровнен по байту, просто запишем
        bit_buffer[byte_offset] = byte;
    } else {
        // Если последний байт не заполнен до конца, заполняем остаток
        bit_buffer[byte_offset] |= byte << bit_offset;
        if (byte_offset + 1 < MAX_BUFFER_SIZE) {
            bit_buffer[byte_offset + 1] = byte >> (8 - bit_offset);
        }
    }
    bit_length += 8;
}

// Функция для извлечения n битов из буфера, начиная с pos
uint32_t extract_bits(int pos, int n) {
    uint32_t value = 0;
    for (int i = 0; i < n; ++i) {
        int byte_index = (pos + i) / 8;
        int bit_index = (pos + i) % 8;
        int bit = (bit_buffer[byte_index] >> bit_index) & 1;
        value |= (bit << i);
    }
    return value;
}

// Функция обработки и вывода накопленного пакета
void process_packet() {
    uint64_t start_timestamp = time_us_64();
    

    if (bit_length == 0) return;  // Если буфер пуст, ничего не делаем

    int pos = 0;
    // Извлечение полей согласно заданной структуре
    uint32_t start_bit         = extract_bits(pos, 1); pos += 1;
    uint32_t broadcast_bit     = extract_bits(pos, 1); pos += 1;
    uint32_t master_address    = reverse_Xbits(extract_bits(pos, 12), 12); pos += 12;
    uint32_t master_parity     = extract_bits(pos, 1);  pos += 1;
    uint32_t slave_address     = reverse_Xbits(extract_bits(pos, 12), 12); pos += 12;
    uint32_t slave_parity      = extract_bits(pos, 1);  pos += 1;
    uint32_t slave_ack         = extract_bits(pos, 1);  pos += 1;
    uint32_t control_field     = reverse_Xbits(extract_bits(pos, 4), 4);  pos += 4;
    uint32_t control_parity    = extract_bits(pos, 1);  pos += 1;
    uint32_t control_ack       = extract_bits(pos, 1);  pos += 1;
    uint32_t message_count     = reverse_Xbits(extract_bits(pos, 8), 8);  pos += 8;
    uint32_t message_count_par = extract_bits(pos, 1);  pos += 1;
    uint32_t message_count_ack = extract_bits(pos, 1);  pos += 1;

    // Вывод полей первого сегмента пакета
    // printf("Startbit:%u\t", start_bit);
    // printf("Broadcast:%u\t", broadcast_bit);
    // printf("Master:%03X (P:%u)\t", master_address, master_parity);
    // printf("Slave:%03X (P:%u, ACK:%u)\t", slave_address, slave_parity, slave_ack);
    // printf("Controlbit:%01X (P:%u, ACK:%u)\t", control_field, control_parity, control_ack);
    // printf("Lenght:%u (P:%u, ACK:%u)\n", message_count, message_count_par, message_count_ack);
    printf("%02X-", start_bit);
    printf("%02X-", broadcast_bit);
    // printf("%04X-%u-", master_address, master_parity);
    printf("%04X-", master_address, master_parity);
    // printf("%04X-%u-%u-", slave_address, slave_parity, slave_ack);
    printf("%04X-", slave_address, slave_parity, slave_ack);
    // printf("%02X-%u-%u-", control_field, control_parity, control_ack);
    printf("%02X-", control_field, control_parity, control_ack);
    // printf("%u-%u-%u", message_count, message_count_par, message_count_ack);
    printf("%02u", message_count, message_count_par, message_count_ack);

    // Вывод сообщений
    for (uint32_t i = 0; i < message_count; ++i) {
        if (pos + 10 > bit_length) break; // проверка на достаточность битов
        uint32_t message = extract_bits(pos, 8); pos += 8;
        uint32_t msg_parity = extract_bits(pos, 1); pos += 1;
        uint32_t msg_ack = extract_bits(pos, 1); pos += 1;

        // printf("D:%u:\t%02X (P:%u, ACK:%u)\n", i+1, message, msg_parity, msg_ack);
        // printf("-%02X-%u-%u)", i+1, message, msg_parity, msg_ack);
        printf("-%02X", i+1, message, msg_parity, msg_ack);
    }

    printf("\n"); // Разделитель между пакетами

    // Сброс буфера после обработки
    memset(bit_buffer, 0x00, sizeof(bit_buffer));
    bit_length = 0;
    
    // Фиксируем текущий момент времени в микросекундах
    // Его мы будем использовать для поправки порога задержки между байтами,
    // так как во время работы этой функции происходят дополнительные задержки
    uint64_t current_timestamp = time_us_64();
    calculating_delay = current_timestamp - start_timestamp;
    // printf("process calculate time: %u\n", calculating_delay);
}

// Главная функция обработки входящих байтов
void process_incoming_byte(uint8_t byte, uint64_t delta) {
    // Если delta превышает порог, значит предыдущий пакет завершён
    if (delta > (DELTA_THRESHOLD + calculating_delay)) {
        byte_counting = 0;
        // printf("start process_packet\n");
        process_packet();
        count_cycles_without_new_data = 0;
    } else {
        calculating_delay = 0;
    }
    byte_counting++;
    // Debug вывод количества байт
    // printf("%u - append_byte_to_buffer\n", byte_counting);
    // Добавляем полученный байт в буфер
    append_byte_to_buffer(byte);
}

///////////////////////////////////////////////////////////////






int main() {
    stdio_init_all();

    // Ждем подключения USB
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    PIO pio = pio0;
    // uint sm_tx = 0;
    // int sm_tx = pio_claim_unused_sm(pio, true);
    // uint sm_rx = 1;
    int sm_rx = pio_claim_unused_sm(pio, true);


    // uint offset_tx = pio_add_program(pio, &avc_lan_tx_program);
    uint offset_rx = pio_add_program(pio, &avc_lan_rx_program);

    // printf("Transmit program loaded at %d\n", offset_tx);
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

    // avc_lan_tx_program_init(pio, sm_tx, offset_tx, pin_tx_high, 400);
    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 125.f);

    avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 60);


    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 16);


    
    // uint example_program_start_bit_offset;    // Смещение инструкции start_bit
    // uint example_program_do_transmit_offset;  // Смещение инструкции do_transmit

    // Получаем абсолютные смещения меток
    // uint start_bit_abs = offset_tx + avc_lan_tx_offset_start;
    // uint stop_bit_abs = offset_tx + avc_lan_tx_offset_stop;
    // uint start_abs = offset_rx + avc_lan_rx_offset_start;
    // uint32_t value = 0;


    // 
    // pio_sm_set_enabled(pio, sm_tx, false);

    // pio_sm_exec(pio, sm_rx, start_abs);
    // pio_sm_set_enabled(pio, sm_rx, true);
    // pio_sm_put_blocking(pio, sm_rx, 900);


    // printf("send \n");
    // pio_sm_set_enabled(pio, sm_tx, false);
    // pio_sm_exec(pio, sm_tx, start_bit_abs);
    // pio_sm_put_blocking(pio, sm_tx, 0x00);
    // pio_sm_put_blocking(pio, sm_tx, 0x01);
    // // pio_sm_put(pio, sm_tx, 0x01);
    // pio_sm_put_blocking(pio, sm_tx, 0xff);
    // // pio_sm_put_blocking(pio, sm_tx, 0x04);
    // // pio_sm_put_blocking(pio, sm_tx, 0x06);
    // pio_sm_set_enabled(pio, sm_tx, true);


    printf("start loop \n");

    // printf("%s\n", byte_to_bit_string(0x00));
    // printf("%s\n", byte_to_bit_string(0x01));
    // printf("%s\n", byte_to_bit_string(0x02));


    uint64_t prev_timestamp = time_us_64();

    while (true) {
        
        if (!pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
            // printf("read \n");
            uint8_t byte = (uint8_t)pio_sm_get_blocking(pio, sm_rx);
            
            // Фиксируем текущий момент времени в микросекундах
            uint64_t current_timestamp = time_us_64();

            uint8_t inverted_byte = ~byte; // инвертируем байт
            uint8_t reversed_byte = reverse_bits(inverted_byte);
            // обработка inverted_byte далее...
            // print_byte_bits(inverted_byte);
            // printf(" %u\n", inverted_byte);

            // Debug - для вывода битов 
            // char *bits = byte_to_bit_string(reversed_byte);
            // printf("%s, %llu us\n", bits, delta);


            // if (prev_timestamp != 0) {
                uint64_t delta = current_timestamp - prev_timestamp;
                process_incoming_byte(reversed_byte, delta);
            // } else {
            //     printf("%s\n", bits);
            // }

            // Обновляем предыдущий таймштамп
            prev_timestamp = current_timestamp;
            
        } else {
            count_cycles_without_new_data++;
            if ( (byte_counting > 1) && (count_cycles_without_new_data > LAST_DATA_THRESHOLD)) {
                byte_counting = 0;
                // printf("start process_packet\n"); 
                process_packet();
                count_cycles_without_new_data = 0;
            }
        }
    }
}
