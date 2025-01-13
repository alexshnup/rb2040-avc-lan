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

uint32_t sleep_rx_afer_tx = 2900;

int divideAndCeil(int a, int b) {
    // Ternary operator for correct handling of negative numbers
    return (a + b - 1) / b;
}

// Function calculates odd parity bit for a byte.
// Returns 1 if the number of set bits is odd, otherwise 0.
uint8_t parity_bit(uint32_t byte) {
    // Variable to hold the XOR of all bits
    uint8_t parity = 0;

    // Iterate over all 8 bits of the byte
    for (int i = 0; i < 32; i++) {
        // XOR the current bit with the parity result
        parity ^= (byte >> i) & 1;
    }

    return parity;
}

void print_bits_4(uint8_t value) {
    value &= 0x0F; // Mask to limit to 4 bits
    for (int i = 3; i >= 0; i--) {
        printf("%d", (value >> i) & 1);
    }
    printf("\n");
}

void print_bits_8(uint8_t value) {
    for (int i = 7; i >= 0; i--) {
        printf("%d", (value >> i) & 1);
    }
    printf("\n");
}

void print_bits_12(uint16_t value) {
    value &= 0x0FFF; // Mask to limit to 12 bits
    for (int i = 11; i >= 0; i--) {
        printf("%d", (value >> i) & 1);
    }
    printf("\n");
}

void print_bits_32(uint32_t value) {
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
    uint8_t start_bit          = extract_bits(pos, 1); pos += 1;
    uint8_t broadcast_bit      = extract_bits(pos, 1); pos += 1;
    uint16_t master_address    = reverse_Xbits(extract_bits(pos, 12), 12); pos += 12;
    uint8_t master_parity      = extract_bits(pos, 1);  pos += 1;
    uint16_t slave_address     = reverse_Xbits(extract_bits(pos, 12), 12); pos += 12;
    uint8_t slave_parity       = extract_bits(pos, 1);  pos += 1;
    uint8_t slave_ack          = extract_bits(pos, 1);  pos += 1;
    uint8_t control_field      = reverse_Xbits(extract_bits(pos, 4), 4);  pos += 4;
    uint8_t control_parity     = extract_bits(pos, 1);  pos += 1;
    uint8_t control_ack        = extract_bits(pos, 1);  pos += 1;
    uint8_t message_count      = reverse_Xbits(extract_bits(pos, 8), 8);  pos += 8;
    uint8_t message_count_par  = extract_bits(pos, 1);  pos += 1;
    uint8_t message_count_ack  = extract_bits(pos, 1);  pos += 1;

    // Output fields of the first packet segment
    // printf("Startbit:%u\t", start_bit);
    // printf("Broadcast:%u\t", broadcast_bit);
    // printf("Master:%03X (P:%u)\t", master_address, master_parity);
    // printf("Slave:%03X (P:%u, ACK:%u)\t", slave_address, slave_parity, slave_ack);
    // printf("Controlbit:%01X (P:%u, ACK:%u)\t", control_field, control_parity, control_ack);
    // printf("Lenght:%u (P:%u, ACK:%u)\n", message_count, message_count_par, message_count_ack);
    printf("%u ", start_bit);
    printf("%u ", broadcast_bit);
    // printf("%03X %u ", master_address, master_parity);
    printf("%03X ", master_address, master_parity);
    // printf("%03X %u %u ", slave_address, slave_parity, slave_ack);
    printf("%03X ", slave_address, slave_parity, slave_ack);
    // printf("%01X %u %u ", control_field, control_parity, control_ack);
    printf("%01X ", control_field, control_parity, control_ack);
    // printf("%02x %u %u", message_count, message_count_par, message_count_ack);
    printf("%02X", message_count, message_count_par, message_count_ack);


    uint8_t parity = 0;
    // Output messages
    for (uint8_t i = 0; i < message_count; ++i) {
        if (pos + 10 > bit_length) break; // check for sufficient bits
        uint8_t message = reverse_Xbits(extract_bits(pos, 8), 8);  pos += 8;
        uint8_t msg_parity = extract_bits(pos, 1); pos += 1;
        uint8_t msg_ack = extract_bits(pos, 1); pos += 1;

        // due to reading peculiarities, we do not check the last byte if it is FF
        // TODO: not yet clear what this is
        if (i < (message_count-1) && (message == 0xFF) ) {
            if (parity_bit(message) != msg_parity) {
                printf(" |ERROR %02X data parity %u is %u not %u|", message, i, msg_parity, parity_bit(message));
                print_bits_8(message);
            }
            // if (0x01 != msg_ack) {
            //     printf(" |ERROR %02X ACK msg_ack %u is %u not 1|", message, i, msg_ack);
            // }
        }

        // printf("D:%u:\t%02X (P:%u, ACK:%u)\n", i+1, message, msg_parity, msg_ack);
        // printf("-%02X-%u-%u)", i+1, message, msg_parity, msg_ack);
        // printf(" %02X %u %u", message, msg_parity, msg_ack);
        printf(" %02X", message, msg_parity, msg_ack);
    }

    printf("\n"); // Separator between packets

    parity = parity_bit(master_address) & 0x01;
    if (parity != master_parity) {
        printf(" |ERROR master_address parity is %u not %u|\n", master_parity, parity);
    }
    parity = parity_bit(slave_address) & 0x01;
    if (parity != slave_parity) {
        printf(" |ERROR slave_address parity|\n");
    }
    parity = parity_bit(control_field) & 0x01;
    if (parity != control_parity) {
        printf(" |ERROR control_field parity|\n");
    }
    parity = parity_bit(message_count) & 0x01;
    if (parity != message_count_par) {
        printf(" |ERROR message_count parity|\n");
    }

    // if (0x01 != slave_ack) {
    //     printf(" |ERROR ACK slave_ack|");
    // }
    // if (0x01 != control_ack) {
    //     printf(" |ERROR ACK control_ack|");
    // }
    // if (0x01 != message_count_ack) {
    //     printf(" |ERROR ACK message_count_ack|");
    // }


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
// Packet formation
///////////////////////////////////////////////////////////////

// Function to write bits into a byte array from right to left,
// taking bits from most significant to least significant within each field.
void write_bits(uint8_t* bytes, size_t* bit_pos, uint32_t value, size_t bit_count) {
    for (ssize_t i = bit_count - 1; i >= 0; --i) {
        size_t pos = *bit_pos;
        size_t byte_idx = pos / 8;
        size_t bit_idx = pos % 8;
        uint8_t bit = (value >> i) & 0x01;
        if (bit) {
            bytes[byte_idx] |= (1 << bit_idx);
        } else {
            bytes[byte_idx] &= ~(1 << bit_idx);
        }
        (*bit_pos)++;
    }
}

size_t parse_protocol_string(const char* input, uint8_t** output) {
    // Split the string into tokens
    char* str = strdup(input);
    if (!str) return 0;
    const char* delim = " ";
    char* token = strtok(str, delim);
    char* tokens[512];  // Assume maximum tokens
    size_t token_count = 0;
    while (token && token_count < 512) {
        tokens[token_count++] = token;
        token = strtok(NULL, delim);
    }

    size_t max_bytes = 1024;
    uint8_t* bytes = (uint8_t*)calloc(max_bytes, sizeof(uint8_t));
    size_t bit_pos = 0;

    // Process each field sequentially:

    // 1. Broadcast bit (1 bit)
    if (token_count > 0) {
        uint8_t broadcast = (uint8_t)strtol(tokens[0], NULL, 10) & 0x01;
        write_bits(bytes, &bit_pos, broadcast, 1);
    }

    // 2. Master address (12 bits)
    uint16_t master_address = 0xFFF;
    if (token_count > 1) {
        master_address = (uint32_t)strtol(tokens[1], NULL, 16) & 0xFFF;
        write_bits(bytes, &bit_pos, master_address, 12);
    }

    // 3. Parity bit (1 bit)
    if (token_count > 1) {
        uint8_t parity = parity_bit(master_address) & 0x01;
        write_bits(bytes, &bit_pos, parity, 1);
    }

    // 4. Slave address (12 bits)
    uint16_t slave_address = 0xFFF;
    if (token_count > 2) {
        slave_address = (uint32_t)strtol(tokens[2], NULL, 16) & 0xFFF;
        write_bits(bytes, &bit_pos, slave_address, 12);
    }

    // 5. Parity bit (1 bit)
    if (token_count > 2) {
        uint8_t parity = parity_bit(slave_address) & 0x01;
        write_bits(bytes, &bit_pos, parity, 1);
    }

    // 6. ACK bit (1 bit)
    if (token_count > 2) {
        uint8_t ack_bit = 0x00 & 0x01;
        write_bits(bytes, &bit_pos, ack_bit, 1);
    }

    // 7. Control bits (4 bits)
    uint8_t control_bits = 0xFF;  // TODO later
    if (token_count > 3) {
        control_bits = (uint32_t)strtol(tokens[3], NULL, 16) & 0x0F;
        write_bits(bytes, &bit_pos, control_bits, 4);
    }

    // 8. Parity bit (1 bit)
    if (token_count > 3) {
        uint8_t parity = parity_bit(control_bits) & 0x01;
        write_bits(bytes, &bit_pos, parity, 1);
    }

    // 9. ACK bit (1 bit)
    if (token_count > 2) {
        uint8_t ack_bit = 0x00 & 0x01;
        write_bits(bytes, &bit_pos, ack_bit, 1);
    }


    // 10. Data length (8 bits)
    uint8_t data_length = 0;
    if (token_count > 4) {
        data_length = (uint32_t)strtol(tokens[4], NULL, 16) & 0xFF;
        write_bits(bytes, &bit_pos, data_length, 8);
    }

    // 11. Parity bit (1 bit)
    if (token_count > 4) {
        uint8_t parity = parity_bit(data_length) & 0x01;
        write_bits(bytes, &bit_pos, parity, 1);
    }

    // 12. ACK bit (1 bit)
    if (token_count > 4) {
        uint8_t ack_bit = 0x00 & 0x01;
        write_bits(bytes, &bit_pos, ack_bit, 1);
    }

    // 11. Process data bytes
    size_t token_index = 5; // Starting index for data
    for (uint8_t i = 0; i < data_length; ++i) {
        // Ensure there are at least 10 tokens for each data byte
        if ((token_index+1) <= token_count) {

            // 11.1. 8 bits of data
            uint8_t data_byte = (uint32_t)strtol(tokens[token_index], NULL, 16) & 0xFF;
            write_bits(bytes, &bit_pos, data_byte, 8);
            token_index++;

            // 11.2. Parity bit (1 bit)
            uint8_t parity = parity_bit(data_byte) & 0x01;
            write_bits(bytes, &bit_pos, parity, 1);

            // 11.3. ACK bit (1 bit)
            // only on the last one 1
            if (i == (data_length-1)) {
                write_bits(bytes, &bit_pos, 0x01, 1);
                // printf("\n |Last data byte| \n");
            } else {
                write_bits(bytes, &bit_pos, 0x00, 1);
                // printf("\n |NOT Last data byte| \n");
            }
        } else {
            // If there are not enough tokens for the next data byte, break the loop.
            printf("\n |Not Enough bits for Data byte| expected=%u got=%u\n", data_length, (token_count-5));
            break;
        }
    }


    free(str);
    *output = bytes;
    size_t byte_len = (bit_pos + 7) / 8;
    return byte_len;
}

///////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////
// console
///////////////////////////////////////////////////////////////

// Structure to store command information
typedef void (*command_handler_t)(PIO pio_rx, PIO pio_tx, uint sm_rx, uint sm_tx, uint exec_start, const char* args);

typedef struct {
    const char* name;
    command_handler_t handler;
} command_t;

// Declare handlers for different commands
void handle_set_dt(PIO pio_rx, PIO pio_tx, uint sm_rx, uint sm_tx, uint exec_start, const char* args) {
    int value = atoi(args);
    LAST_DATA_THRESHOLD = value;
    printf("LAST_DATA_THRESHOLD set to %d\n", LAST_DATA_THRESHOLD);
}

void handle_status(PIO pio_rx, PIO pio_tx, uint sm_rx, uint sm_tx, uint exec_start, const char* args) {
    (void)args;  // Ignore args if not needed
    printf("Current LAST_DATA_THRESHOLD: %d\n", LAST_DATA_THRESHOLD);
}

void handle_send(PIO pio_rx, PIO pio_tx, uint sm_rx, uint sm_tx, uint exec_start, const char* args) {

    printf("\n");

    // Create a copy of the string since strtok modifies it
    char* args_copy = strdup(args);
    if (!args_copy) {
        printf("Memory allocation failed\n");
        return;
    }


    pio_sm_set_enabled(pio_rx, sm_rx, false);
    pio_sm_set_enabled(pio_tx, sm_tx, false);
    pio_sm_exec(pio_tx, sm_tx, exec_start);

    // pio_sm_set_enabled(pio, sm, false);
    // pio_sm_exec(pio, sm, exec_start);
    // pio_sm_put_blocking(pio, sm, 0x50);
    // pio_sm_put_blocking(pio, sm, 0xC0);
    // pio_sm_put_blocking(pio, sm, 0xFF);
    // pio_sm_put_blocking(pio, sm, 0xFB);
    // pio_sm_put_blocking(pio, sm, 0x02);

    // const char* input = "0 140 0 FFF 0 1 F 0 1 03 0 1 48 0 1 80 1 1 80 1 1";
    uint8_t* output = NULL;
    size_t output_len = parse_protocol_string(args_copy, &output);

    // pio_sm_set_enabled(pio_tx, sm_tx, true);
    // sleep_ms(1000); 

    // printf("-------\n");
    // // Output the result in binary format for verification
    // for (size_t i = 0; i < output_len; ++i) {
    //     printf("%02u: ", i);
    //     for (int bit = 7; bit >= 0; --bit) {
    //         printf("%d", (output[i] >> bit) & 1);
    //     }
    //     printf("\n");
    // }
    // printf("-------\n");

    // size_t output_len = parse_protocol_string(input, &output);
    for (size_t i = 0; i < output_len; ++i) {
        // printf("\n%02X-", output[i]);
        pio_sm_put_blocking(pio_tx, sm_tx, output[i]);
        if (i == 0) {
            // start PIO for transmission
            pio_sm_set_enabled(pio_tx, sm_tx, true);
        }
        
        // sleep_us(10); 

        // Output the result in binary format for verification
        // for (int bit = 7; bit >= 0; --bit) {
        //     printf("%d", (output[i] >> bit) & 1);
        // }
        // printf(" ");
    }

    free(output);


    // Free the memory allocated by strdup
    free(args_copy);

    sleep_us(2900); 
    pio_sm_set_enabled(pio_rx, sm_rx, true);

}

void handle_unknown(const char* cmd) {
    printf("Unknown command: %s\n", cmd);
}

///////////////////////////////////////////////////////////////


// Array of commands and function to search and call the appropriate command

#define COMMAND_COUNT 3  // number of known commands
command_t commands[COMMAND_COUNT] = {
    {"set_dt", handle_set_dt},
    {"status", handle_status},
    {"1", handle_send},
    // Add new commands here but remember to increment COMMAND_COUNT
};

void process_command(PIO pio_rx, PIO pio_tx, uint sm_rx, uint sm_tx, uint exec_start, char* input_line) {
    // Split command and arguments
    char* command_name = strtok(input_line, " ");
    char* args = strtok(NULL, "\n");  // the rest of the line as arguments

    if (!command_name) return;

    for (int i = 0; i < COMMAND_COUNT; ++i) {
        if (strcmp(command_name, commands[i].name) == 0) {
            commands[i].handler(pio_rx, pio_tx, sm_rx, sm_tx, exec_start, args);
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

    // DEBUG package generator
    // const char* input = "0 140 0 FFF 0 1 F 0 1 03 0 1 01 1 02 1 03 0";
    // uint8_t* output = NULL;
    // size_t output_len = parse_protocol_string(input, &output);

    // // Output the result in binary format for verification
    // for (size_t i = 0; i < output_len; ++i) {
    //     for (int bit = 7; bit >= 0; --bit) {
    //         printf("%d", (output[i] >> bit) & 1);
    //     }
    //     printf(" ");
    // }
    // printf("\n");

    // free(output);
    // return 0;


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
    avc_lan_tx_program_init(pio1_instance_tx, sm_tx, offset_tx, pin_tx_high, 168);

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


    char command_line[1024] = {0};  // Buffer for commands
    int cmd_index = 0;

    while (true) {

        if (stdio_usb_connected()) {
            int ch = getchar_timeout_us(0);
            if (ch != PICO_ERROR_TIMEOUT) {
                putchar((char)ch);  // Echo input
                if (ch == '\n' || ch == '\r') {
                    command_line[cmd_index] = '\0';
                    // Remove extra newline characters
                    command_line[strcspn(command_line, "\r\n")] = 0;
                    
                    // Process the completed command
                    process_command(pio0_instance_rx, pio1_instance_tx, sm_rx, sm_tx, start_bit_abs, command_line);

                    memset(command_line, 0, sizeof(command_line));
                    cmd_index = 0;
                    // printf("> ");  // Prompt for new command input
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
