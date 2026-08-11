**Linux-side MCUmgr/SMP client communicating with a Zephyr MCUmgr server over UART**, with CBOR, Base64 framing, image upload, image state parsing, confirmation and activation.

````markdown
# MCUmgr SMP Linux Client in C

A lightweight C implementation of an **MCUmgr / SMP client running on Linux**, communicating with a Zephyr-based embedded target over a serial UART interface.

The project implements the communication required to perform firmware image management using the **Simple Management Protocol (SMP)**, including CBOR encoding/decoding, Base64 transport framing, image upload, image state parsing, image confirmation, and image activation.

> **Current status:** Firmware image upload, image confirmation, and image activation have been successfully tested with a Zephyr MCUmgr/SMP server over UART.

---

## Features

### SMP / MCUmgr

- SMP request/response packet handling
- Serial transport communication
- SMP packet framing
- Packet length handling
- Fragmentation of large SMP packets
- Reassembly of received packets
- Continuation frame handling
- CRC16 handling

### Base64

- Base64 encoding
- Base64 decoding
- Serial transport Base64 frame handling
- Handling of MCUmgr serial framing

### CBOR

- CBOR request generation
- CBOR response parsing
- CBOR map parsing
- CBOR array parsing
- CBOR byte-string handling
- Extraction of:
  - Image slot
  - Firmware version
  - Image hash
  - Bootable state
  - Pending state
  - Confirmed state
  - Active state
  - Permanent state
  - Split status

### Firmware Image Management

- Firmware image upload
- Image upload in chunks
- Image hash handling
- Image state retrieval
- Candidate image detection
- Image confirmation
- Image activation / boot-success handling

---

## Architecture

The communication flow is approximately:

```text
                    Linux Host
                         |
                         |
                    UART / Serial
                         |
                         v
              +---------------------+
              |   SMP Transport     |
              |                     |
              |  Framing            |
              |  Base64             |
              |  CRC16              |
              +----------+----------+
                         |
                         v
              +---------------------+
              |    SMP Protocol     |
              |                     |
              |  Header             |
              |  Group              |
              |  Command            |
              |  Sequence           |
              |  Length             |
              +----------+----------+
                         |
                         v
              +---------------------+
              |        CBOR         |
              |                     |
              |  Encode / Decode    |
              |  Map / Array        |
              |  Byte Strings       |
              +----------+----------+
                         |
                         v
              +---------------------+
              |  MCUmgr Image Mgmt  |
              |                     |
              |  Image Upload       |
              |  Image State        |
              |  Image Confirm      |
              |  Image Activate     |
              +----------+----------+
                         |
                         v
                Zephyr MCU Target
                MCUmgr SMP Server
````

---

## Serial Transport

The project communicates with the target using a UART serial interface.

The MCUmgr serial transport uses framing around the SMP payload. The implementation handles:

```text
SMP data
   ↓
CBOR encoding
   ↓
SMP packet
   ↓
Base64 encoding
   ↓
Serial framing
   ↓
UART
```

On reception, the process is reversed:

```text
UART
   ↓
Serial frame
   ↓
Base64 decoding
   ↓
SMP packet reassembly
   ↓
CBOR decoding
   ↓
Management response
```

---

## Packet Fragmentation

Large SMP packets cannot always be transmitted as a single UART frame.

The implementation therefore separates the complete SMP packet from the physical UART transport.

Conceptually:

```text
Complete SMP packet
        |
        +----------------+
        |                |
        v                v
     Frame 1          Frame 2 ...
```

The receiver collects the fragments and reconstructs the complete SMP packet before attempting CBOR decoding.

This was particularly important during firmware image management because the image-management CBOR payload can be significantly larger than a single serial transport frame.

---

## Firmware Image Upload

The image upload process uses the MCUmgr image management protocol.

The image is divided into manageable chunks:

```text
Firmware image
      |
      v
+-------------+
|   Chunk 0   |
+-------------+
|   Chunk 1   |
+-------------+
|   Chunk 2   |
+-------------+
|     ...     |
+-------------+
|   Chunk N   |
+-------------+
```

Each upload request contains information such as:

```text
image
len
off
sha
data
```

Where:

* `image` = target image slot
* `len` = complete firmware image length
* `off` = current upload offset
* `sha` = firmware image SHA-256 hash
* `data` = current firmware chunk

The upload continues by increasing the offset until the complete image has been transferred.

---

## Image State

After uploading the image, the client can request the image state from the target.

The response contains an `images` array containing information about the available image slots.

Example:

```text
images:
    slot 0
        version   = 1.2.0
        active    = true
        confirmed = true

    slot 1
        version   = 1.3.1
        active    = false
        confirmed = false
```

The client parses the CBOR response and extracts:

```text
slot
version
hash
bootable
pending
confirmed
active
permanent
```

This allows the client to determine which image is currently active and which image is the candidate firmware.

---

## Image Hash

The image hash is represented in CBOR as a byte string.

For example:

```text
58 20
```

represents a 32-byte byte string.

The client extracts the raw 32-byte SHA-256 hash and can use it for image identification and image-management operations.

Example:

```text
09 7B AA 2B FD 13 7F 2B 79 B5 0D E0 E7 19 D9 5D
F4 7F 8E 8E 29 DC 83 59 36 60 CF 84 3A C0 AA 93
```

---

## Image Confirmation and Activation

After the image is uploaded, the target can contain both the currently active image and the newly uploaded image.

The workflow is:

```text
             Current Firmware
                    |
                    v
             Upload new image
                    |
                    v
          +-------------------+
          | Secondary Slot    |
          | New Firmware      |
          +-------------------+
                    |
                    v
             Image State
                    |
                    v
             Confirm Image
                    |
                    v
             Activate Image
                    |
                    v
              MCU Reboot
                    |
                    v
          New Firmware Active
```

The project has successfully been tested through the image upload and activation flow.

---

## Example Image State

A decoded response can look like:

```text
Image 0
Slot    : 0
Version : 1.2.0
Active  : true
Confirmed : true

Image 1
Slot    : 1
Version : 1.3.1
Active  : false
Confirmed : false
```

The client can identify the candidate image and extract its associated SHA-256 hash.

---

## Dependencies

The project currently uses:

* GCC / C compiler
* Linux
* POSIX serial/UART APIs
* `libcbor`

Install libcbor on Debian/Ubuntu:

```bash
sudo apt install libcbor-dev
```

---

## Build

Clone the repository:

```bash
git clone https://github.com/dear-himanshu/mcumgr_smp_Linux_C.git
cd mcumgr_smp_Linux_C
```

Compile:

```bash
gcc enc_dec_fun.c app_stack_uart.c -lcbor -o mcumgr_smp
```

Depending on the project structure, additional source files may need to be added to the compilation command.

---

## Running

Connect the Linux host to the target MCU through the appropriate UART interface.

Example:

```bash
./mcumgr_smp
```

The UART configuration can be adjusted according to the target:

```text
Device : /dev/ttyS0
Baud   : 115200
```

Make sure the user has permission to access the serial device.

For example:

```bash
sudo usermod -aG dialout $USER
```

Log out and log back in after changing the group membership.

---

## Firmware Upload Flow

A typical firmware update sequence is:

```text
1. Calculate firmware SHA-256
          ↓
2. Prepare SMP image upload request
          ↓
3. Encode request using CBOR
          ↓
4. Build SMP packet
          ↓
5. Base64 encode packet
          ↓
6. Send over UART
          ↓
7. Receive response
          ↓
8. Decode Base64
          ↓
9. Reassemble SMP packet
          ↓
10. Decode CBOR
          ↓
11. Continue with next image offset
          ↓
12. Image upload complete
          ↓
13. Request image state
          ↓
14. Confirm image
          ↓
15. Activate image
          ↓
16. MCU boots new firmware
```

---

## Project Structure

The project is currently organized around the SMP transport, UART communication, CBOR processing and image-management functionality.

```text
.
├── enc_dec_fun.c
├── app_stack_uart.c
├── ...
└── README.md
```

The structure may evolve as the project is cleaned up and modularized.

---

## Technical Details

### SMP

The implementation works with the SMP header and management fields including:

```text
Op
Flags
Length
Group
Sequence
Command ID
```

### CBOR

CBOR is used as the payload encoding format for SMP management messages.

The implementation uses `libcbor` for:

```text
Map creation
Array creation
Byte strings
Text strings
CBOR decoding
CBOR map traversal
CBOR array traversal
```

### Hash

Firmware images use SHA-256 hashes:

```text
SHA-256 = 32 bytes = 256 bits
```

The hash is represented as a CBOR byte string during image management.

---

## Lessons / Development Notes

This project was developed while working through the MCUmgr/SMP protocol at the byte level.

Some of the important implementation challenges included:

* Understanding SMP packet length fields
* Handling serial transport fragmentation
* Base64 framing
* Continuation frames
* CRC16 calculation
* CBOR map and array traversal
* CBOR byte-string extraction
* Correct handling of SHA-256 hashes
* Managing image upload offsets
* Handling image state responses
* Image confirmation and activation
* Avoiding memory corruption while parsing asynchronous UART data

The implementation is intentionally kept close to the protocol to make the packet flow easy to inspect and debug.

---

## Current Status

### Working

* [x] UART communication
* [x] SMP packet construction
* [x] SMP packet parsing
* [x] Base64 encoding/decoding
* [x] Serial packet framing
* [x] Packet fragmentation/reassembly
* [x] CRC16 handling
* [x] CBOR encoding
* [x] CBOR decoding
* [x] Image upload
* [x] Image state parsing
* [x] Image hash extraction
* [x] Image confirmation
* [x] Image activation
* [x] Successful firmware boot after activation

### Planned

* [ ] Better error handling
* [ ] Cleaner module separation
* [ ] Command-line arguments
* [ ] Configurable UART parameters
* [ ] Better logging
* [ ] More complete MCUmgr command support
* [ ] Automated image upload testing
* [ ] Improved documentation
* [ ] Unit tests

---

## Disclaimer

This project is an independent implementation intended for learning, experimentation and development with the MCUmgr/SMP protocol.

It is not intended to replace the official MCUmgr tooling.

---

## License

Add your chosen open-source license here.

For example:

```text
MIT License
```

See `LICENSE` for details.

---

## Author

**Himanshu**

Embedded Firmware Engineer

Interests:

* Embedded C
* RTOS
* Zephyr
* Firmware Update / FOTA
* MCUmgr / SMP
* UART / SPI / I2C
* TCP/IP
* IoT
* Embedded Linux

---

## Acknowledgements

This project builds upon publicly documented technologies and open-source components including:

* Zephyr Project
* MCUmgr
* Simple Management Protocol (SMP)
* libcbor

Please refer to the respective projects and documentation for their licenses and implementation details.

```
## Installation

### Requirements

- Linux
- GCC
- libcbor
- OpenSSL/libcrypto
- POSIX-compatible serial interface

### Ubuntu/Debian

Install the required packages:

```bash
sudo apt update
sudo apt install -y build-essential libcbor-dev libssl-dev

## Compile using GCC command

```bash
 cc enc_dec_fun.c app_stack_uart.c -lcbor -pthread -o enc_dec_fun -lcrypto

```bash
./enc_dec_fun
