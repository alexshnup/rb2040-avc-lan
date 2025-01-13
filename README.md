# AVC-LAN for Raspberry Pico RB2040

This program runs on a Raspberry Pi Pico and uses its PIO (Programmable I/O) using Assembler code to communicate over an AVC-LAN bus. It listens for incoming data on one pin, processes packets based on the AVC-LAN protocol, and can send custom data packets on another pin. The code also provides a simple console interface for adjusting settings and sending packets.

## Key Features:

Receives and parses AVC-LAN packets, checking for parity errors.
Allows sending customized AVC-LAN packets via console commands.
Provides commands to adjust timing parameters and check status.

## Usage Example:

To send data over the AVC-LAN bus, connect to the Pico's console (e.g., via USB serial) and input a command with the following format:

```
1 0 140 440 F 04 01 02 03 04
```
Command Breakdown:
- 1 – Command identifier to send data.
- 0 – Broadcast bit.
- 140 – Master address.
- 440 – Slave address.
- F – Control field.
- 04 – Data length (4 bytes).
- 01 02 03 04 – Data bytes to send.
After entering this command, the program will parse the input, construct an AVC-LAN packet, and transmit it on the bus.

## Pinouts
By default for TX we can use one of two pins GP2 and GP3 (this is differectial line), for RX using pin GP4

## Hardware
- Receive (RX): Uses an LM393 comparator to receive data. The LM393 hardware comparator ensures reliable detection of differential signals.
- Transmit (TX): Uses a PCA82C250 CAN transceiver to transmit data over the AVC-LAN bus.
Note: In the future, connections may be simplified, potentially using resistors with differential signals for transmission without the PCA82C250. However, the LM393 comparator will still be preferred for receiving data.

## Contributing

Contributions to improve this project are welcome. Feel free to fork the repository, make changes, and open a pull request. When contributing:

Follow the existing code style and structure.
Test your changes to ensure they do not break existing functionality.
Update documentation as necessary.
For major changes, please open an issue first to discuss what you would like to change.

Thank you for your interest in contributing!