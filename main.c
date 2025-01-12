#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "avc_lan_capture.pio.h" // Include the generated header file

const uint pin_tx_high = 2;
// const uint pin_tx_low = 3;  // The second inverted pin cannot be reassigned; it is always +1 (the next one)
const uint pin_rx = 4;

uint32_t calculating_delay = 0;
uint32_t byte_counting = 0;

// Used for sending the last packet. Since our sending happens by default after a new one arrives
uint64_t count_cycles_without_new_data = 0;
uint32_t LAST_DATA_THRESHOLD = 50000;

#define MAX_BUFFER_SIZE 1024  // max buffer size in bytes
#define DELTA_THRESHOLD 1000   // threshold in microseconds

#define CMD_BUFFER_SIZE 100

void print_bits(uint32_t value) {
    // Iterate over bits from the most significant (31) to the least significant (0)
    for (int i = 31; i >= 0; i--) {
        // Shift value right by i and check the least significant bit
        printf("%d", (value >> i) & 1);
    }
    printf("\n");
}

void print_byte_bits(uint8_t byte) {
    for (int i = 7; i >= 0; --i) {
        // Shift byte right by i positions and take the least significant bit:
        int bit = (byte >> i) & 1;
        printf("%d", bit);
    }
    printf("\n");
}

char* byte_to_bit_string(uint8_t byte) {
    // Allocate memory for 9 characters: 8 bits + null terminator
    char *bit_string = malloc(9);
    if (!bit_string) {
        return NULL; // handle memory allocation error
    }

    for (int i = 7; i >= 0; --i) {
        // Get the current bit and store it as a character
        bit_string[7 - i] = ((byte >> i) & 1) ? '1' : '0';
    }
    bit_string[8] = '\0'; // Null terminator

    return bit_string;
}

uint8_t reverse_bits(uint8_t byte) {
    uint8_t reversed = 0;
    for (int i = 0; i < 8; ++i) {
        // Extract the i-th bit from the original byte:
        uint8_t bit = (byte >> i) & 1;
        // Set the corresponding bit in the reversed byte:
        reversed |= bit << (7 - i);
    }
    return reversed;
}

uint32_t reverse_Xbits(uint32_t value, int width) {
    // Check that width is valid (no more than 32 for uint32_t).
    if (width < 1 || width > 32) {
        return value; // Could handle error differently if needed.
    }

    uint32_t reversed = 0;
    for (int i = 0; i < width; ++i) {
        // Extract the i-th bit of the original value within the specified width
        uint32_t bit = (value >> i) & 1;
        // Set the corresponding bit in the resulting value
        reversed |= bit << (width - 1 - i);
    }
    // Preserve bits beyond the specified width without changes:
    uint32_t mask = ~((1u << width) - 1); // mask for bits outside reversal area
    reversed |= (value & mask);

    return reversed;
}

//////////////////////////////////////////////////////////////

// Global buffer for accumulating packet bits and current length in bits
static uint8_t bit_buffer[MAX_BUFFER_SIZE] = {0};
static int bit_length = 0;

// Function to add new bytes to the bit buffer
void append_byte_to_buffer(uint8_t byte) {
    // Add byte to the buffer, assuming the buffer is large enough
    int byte_offset = bit_length / 8;
    int bit_offset = bit_length % 8;

    if (bit_offset == 0) {
        // If byte-aligned, just write
        bit_buffer[byte_offset] = byte;
    } else {
        // If the last byte is not completely filled, fill the remainder
        bit_buffer[byte_offset] |= byte << bit_offset;
        if (byte_offset + 1 < MAX_BUFFER_SIZE) {
            bit_buffer[byte_offset + 1] = byte >> (8 - bit_offset);
        }
    }
    bit_length += 8;
}

// Function to extract n bits from the buffer starting at pos
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

// Function to process and output the accumulated packet
void process_packet() {
    uint64_t start_timestamp = time_us_64();
    
    if (bit_length == 0) return;  // If buffer is empty, do nothing

    int pos = 0;
    // Extract fields according to the given structure
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

    // Output fields of the first packet segment
    // printf("Startbit:%u\t", start_bit);
    // printf("Broadcast:%u\t", broadcast_bit);
    // printf("Master:%03X (P:%u)\t", master_address, master_parity);
    // printf("Slave:%03X (P:%u, ACK:%u)\t", slave_address, slave_parity, slave_ack);
    // printf("Controlbit:%01X (P:%u, ACK:%u)\t", control_field, control_parity, control_ack);
    // printf("Lenght:%u (P:%u, ACK:%u)\n", message_count, message_count_par, message_count_ack);
    printf("%02X ", start_bit);
    printf("%02X ", broadcast_bit);
    // printf("%04X-%u-", master_address, master_parity);
    printf("%04X ", master_address, master_parity);
    // printf("%04X-%u-%u-", slave_address, slave_parity, slave_ack);
    printf("%04X ", slave_address, slave_parity, slave_ack);
    // printf("%02X-%u-%u-", control_field, control_parity, control_ack);
    printf("%02X ", control_field, control_parity, control_ack);
    // printf("%u-%u-%u", message_count, message_count_par, message_count_ack);
    printf("%02u", message_count, message_count_par, message_count_ack);

    // Output messages
    for (uint32_t i = 0; i < message_count; ++i) {
        if (pos + 10 > bit_length) break; // check for sufficient bits
        uint32_t message = extract_bits(pos, 8); pos += 8;
        uint32_t msg_parity = extract_bits(pos, 1); pos += 1;
        uint32_t msg_ack = extract_bits(pos, 1); pos += 1;

        // printf("D:%u:\t%02X (P:%u, ACK:%u)\n", i+1, message, msg_parity, msg_ack);
        // printf("-%02X-%u-%u)", i+1, message, msg_parity, msg_ack);
        printf(" %02X", i+1, message, msg_parity, msg_ack);
    }

    printf("\n"); // Separator between packets

    // Reset the buffer after processing
    memset(bit_buffer, 0x00, sizeof(bit_buffer));
    bit_length = 0;
    
    // Record the current time in microseconds
    // We will use this to adjust the delay threshold between bytes,
    // since additional delays occur during the execution of this function
    uint64_t current_timestamp = time_us_64();
    calculating_delay = current_timestamp - start_timestamp;
    // printf("process calculate time: %u\n", calculating_delay);
}

// Main function to process incoming bytes
void process_incoming_byte(uint8_t byte, uint64_t delta) {
    // If delta exceeds the threshold, the previous packet is finished
    if (delta > (DELTA_THRESHOLD + calculating_delay)) {
        byte_counting = 0;
        // printf("start process_packet\n");
        process_packet();
        count_cycles_without_new_data = 0;
    } else {
        calculating_delay = 0;
    }
    byte_counting++;
    // Debug output of byte count
    // printf("%u - append_byte_to_buffer\n", byte_counting);
    // Add the received byte to the buffer
    append_byte_to_buffer(byte);
}

///////////////////////////////////////////////////////////////
// console
///////////////////////////////////////////////////////////////

// структура для хранения информации о команде
typedef void (*command_handler_t)(PIO pio, uint sm, uint exec_start, const char* args);

typedef struct {
    const char* name;
    command_handler_t handler;
} command_t;

// объявим обработчики для разных команд
void handle_set_dt(const char* args) {
    int value = atoi(args);
    LAST_DATA_THRESHOLD = value;
    printf("LAST_DATA_THRESHOLD set to %d\n", LAST_DATA_THRESHOLD);
}

void handle_status(const char* args) {
    (void)args;  // Игнорируем args, если не нужны
    printf("Current LAST_DATA_THRESHOLD: %d\n", LAST_DATA_THRESHOLD);
}

void handle_send(PIO pio, uint sm, uint exec_start, const char* args) {
    // Создаём копию строки, так как strtok модифицирует её
    char* args_copy = strdup(args);
    if (!args_copy) {
        printf("Memory allocation failed\n");
        return;
    }

    pio_sm_set_enabled(pio, sm, false);
    pio_sm_exec(pio, sm, exec_start);
    // pio_sm_put_blocking(pio1_instance_tx, sm_tx, 0x00);
    // pio_sm_put_blocking(pio1_instance_tx, sm_tx, 0x01);

    // Используем strtok для разбивки строки по пробелам
    char* token = strtok(args_copy, " ");
    while (token != NULL) {
        // Здесь token содержит очередной элемент, разделённый пробелом
        // Пример: преобразование из шестнадцатеричной строки в число
        unsigned int value = (unsigned int)strtoul(token, NULL, 16);
        
        // Обработка значения
        printf("\nParsed value: 0x%X\n", value);

        
        // Получаем следующий токен
        token = strtok(NULL, " ");
    }

    // запускаем PIO для передачи
    // pio_sm_set_enabled(pio1_instance_tx, sm_tx, true);

    // Освобождаем память, выделенную strdup
    free(args_copy);
}

void handle_unknown(const char* cmd) {
    printf("Unknown command: %s\n", cmd);
}

///////////////////////////////////////////////////////////////


// массив команд и функцию для поиска и вызова подходящей команды

#define COMMAND_COUNT 3  // количество известных команд
command_t commands[COMMAND_COUNT] = {
    {"set_dt", handle_set_dt},
    {"status", handle_status},
    {"01", handle_send},
    // Добавляйте новые команды здесь но не забываем инкрементировать COMMAND_COUNT
};

void process_command(PIO pio, uint sm, uint exec_start, char* input_line) {
    // Разделим команду и аргументы
    char* command_name = strtok(input_line, " ");
    char* args = strtok(NULL, "\n");  // вся оставшаяся строка как аргументы

    if (!command_name) return;

    for (int i = 0; i < COMMAND_COUNT; ++i) {
        if (strcmp(command_name, commands[i].name) == 0) {
            commands[i].handler(pio, sm, exec_start, args);
            return;
        }
    }
    handle_unknown(command_name);
}

///////////////////////////////////////////////////////////////



int main() {
    stdio_init_all();

    // Wait for USB connection
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    PIO pio0_instance_rx = pio0;
    PIO pio1_instance_tx = pio1;
    // uint sm_tx = 0;
    // uint sm_rx = 8;
    int sm_rx = pio_claim_unused_sm(pio0_instance_rx, true);
    int sm_tx = pio_claim_unused_sm(pio1_instance_tx, true);


    uint offset_rx = pio_add_program(pio0_instance_rx, &avc_lan_rx_program);
    uint offset_tx = pio_add_program(pio1_instance_tx, &avc_lan_tx_program);

    printf("Receive program loaded at %d\n", offset_rx);
    printf("Transmit program loaded at %d\n", offset_tx);

    // // Configure state machines, set bit rate at 5 Mbps
    // avc_lan_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 125.f / (16 * 5));
    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 125.f / (16 * 5));

    // // Configure state machines, set bit rate at 10 KHz
    // avc_lan_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 390.625);
    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 390.625);

    // Configure state machines, set bit rate at 20 KHz
    // avc_lan_tx_program_init(pio, sm_tx, offset_tx, pin_tx, 195.3125);
    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 195.3125);

    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 125.f);

    avc_lan_rx_program_init(pio0_instance_rx, sm_rx, offset_rx, pin_rx, 60);
    avc_lan_tx_program_init(pio1_instance_tx, sm_tx, offset_tx, pin_tx_high, 400);

    // avc_lan_rx_program_init(pio, sm_rx, offset_rx, pin_rx, 16);

    // uint example_program_start_bit_offset;    // Offset of the start_bit instruction
    // uint example_program_do_transmit_offset;  // Offset of the do_transmit instruction

    // Obtain absolute label offsets
    uint start_bit_abs = offset_tx + avc_lan_tx_offset_start;
    // uint stop_bit_abs = offset_tx + avc_lan_tx_offset_stop;
    // uint32_t value = 0;

    // 
    // printf("send \n");
    // pio_sm_set_enabled(pio1_instance_tx, sm_tx, false);
    // pio_sm_exec(pio1_instance_tx, sm_tx, start_bit_abs);
    // pio_sm_put_blocking(pio1_instance_tx, sm_tx, 0x00);
    // pio_sm_put_blocking(pio1_instance_tx, sm_tx, 0x01);
    // pio_sm_set_enabled(pio1_instance_tx, sm_tx, true);

    printf("start loop \n");

    // printf("%s\n", byte_to_bit_string(0x00));
    // printf("%s\n", byte_to_bit_string(0x01));
    // printf("%s\n", byte_to_bit_string(0x02));

    uint64_t prev_timestamp = time_us_64();


    char command_line[1024] = {0};  // Буфер для команд
    int cmd_index = 0;

    while (true) {

        if (stdio_usb_connected()) {
            int ch = getchar_timeout_us(0);
            if (ch != PICO_ERROR_TIMEOUT) {
                putchar((char)ch);  // Эхо ввода
                if (ch == '\n' || ch == '\r') {
                    command_line[cmd_index] = '\0';
                    // Удаляем лишние символы перевода строки
                    command_line[strcspn(command_line, "\r\n")] = 0;
                    
                    // Обработка заполненной команды
                    process_command(pio1_instance_tx, sm_tx, start_bit_abs, command_line);

                    memset(command_line, 0, sizeof(command_line));
                    cmd_index = 0;
                    printf("> ");  // Приглашение для ввода новой команды
                } else {
                    if (cmd_index < CMD_BUFFER_SIZE - 1) {
                        command_line[cmd_index++] = (char)ch;
                    }
                }
            }
        }

        if (!pio_sm_is_rx_fifo_empty(pio0_instance_rx, sm_rx)) {
            // printf("read \n");
            uint8_t byte = (uint8_t)pio_sm_get_blocking(pio0_instance_rx, sm_rx);
            
            // Record the current time in microseconds
            uint64_t current_timestamp = time_us_64();

            uint8_t inverted_byte = ~byte; // invert the byte
            uint8_t reversed_byte = reverse_bits(inverted_byte);
            // further processing of inverted_byte...
            // print_byte_bits(inverted_byte);
            // printf(" %u\n", inverted_byte);


            // if (prev_timestamp != 0) {
            uint64_t delta = current_timestamp - prev_timestamp;
            process_incoming_byte(reversed_byte, delta);
            // } else {
            //     printf("%s\n", bits);
            // }


            // // Debug - for bit output 
            // char *bits = byte_to_bit_string(reversed_byte);
            // printf("%s, %llu us\n", bits, delta);


            // Update the previous timestamp
            prev_timestamp = current_timestamp;
            
        } else {
            count_cycles_without_new_data++;
            if ((byte_counting > 1) && (count_cycles_without_new_data > LAST_DATA_THRESHOLD)) {
                byte_counting = 0;
                // printf("start process_packet\n"); 
                process_packet();
                count_cycles_without_new_data = 0;
            }
        }

        // tight_loop_contents();
    }
}
