# TCP-Protocol

This repository contains a simulation of the TCP protocol using UDP. The goal of this project is to demonstrate the working principles of TCP by implementing a reliable data transfer protocol over UDP.

## Table of Contents

- [Installation](#installation)
- [Usage](#usage)
- [Files](#files)
  - [file-receiver.c](#file-receiverc)
  - [file-sender.c](#file-senderc)
  - [generate-msc.sh](#generate-mscbash)
  - [log-packets.c](#log-packetsc)
  - [packet-format.h](#packet-formath)

## Installation

1. Clone the repository:

    ```sh
    git clone https://github.com/Dredegui/TCP-Protocol.git
    ```

2. Navigate to the repository directory:

    ```sh
    cd TCP-Protocol
    ```

3. Compile the source files:

    ```sh
    make
    ```

## Usage

### Running the File Sender

To run the file sender, execute the following command:

```sh
./file-sender <port> <window_size>
```

- `<port>`: The port number on which the sender will listen.
- `<window_size>`: The size of the sliding window.

### Running the File Receiver

To run the file receiver, execute the following command:

```sh
./file-receiver <file_path> <host> <port> <window_size>
```

- `<file_path>`: The path to the file to be received.
- `<host>`: The hostname or IP address of the sender.
- `<port>`: The port number on which the receiver will listen.
- `<window_size>`: The size of the sliding window.

## Files

### file-receiver.c

This file contains the implementation of the file receiver. The receiver requests a file from the sender, receives data packets, acknowledges them, and writes the received data to a file. It uses a sliding window protocol to manage packet reception and acknowledgments.

### file-sender.c

This file contains the implementation of the file sender. The sender reads the requested file, splits it into data packets, and sends them to the receiver. It waits for acknowledgments and uses a sliding window protocol to manage packet transmission and retransmission in case of timeouts.

### generate-msc.sh

This script generates a message sequence chart (MSC) from log files. The script sorts the log entries and computes the time differences between sent and received messages. It uses `mscgen` to create a visual representation of the message flow.

### log-packets.c

This file overrides the `sendto` and `recvfrom` functions to log packet transmissions. It logs the packet details, including timestamps, source and destination addresses, and packet data. The logs can be used to analyze the behavior of the protocol and debug issues.

### packet-format.h

This header file defines the structures used for packet communication between the sender and receiver. It includes definitions for request packets, data packets, and acknowledgment packets, along with constants for maximum window size, timeout duration, retries, and chunk size.
