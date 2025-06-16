# Project: Reliable Transmission and TCP Congestion Control Protocol in C

## Overview

This project simulates a custom networking protocol using a sliding window mechanism, selective retransmission, and TCP-inspired congestion control — all implemented in C inside a Docker-contained virtual environment. It mimics real-world networking behavior including frame corruption, out-of-order arrival, and packet drops. The system uses a **64-byte MTU**, enforces **frame CRC validation**, and applies **TCP congestion strategies** like Slow Start, AIMD, Fast Retransmit, and Fast Recovery.

> This implementation emphasizes robustness, low-level performance control, and precise synchronization between multiple sender and receiver pairs.

---

## Protocol Architecture

### 🔄 Layered Design

| Layer              | Functionality                                                           |
|--------------------|-------------------------------------------------------------------------|
| Application Layer  | Message typing and command-line transmission                            |
| Transport Layer    | Sliding Window Protocol, Congestion Control, Acknowledgment Tracking    |
| Link Layer         | CRC-8 Error Detection, Frame Construction, Timeout Handling             |
| Physical Layer     | Simulated in `switch.c`, handles actual frame delivery between hosts     |

---

## Core Features

### 1. Sliding Window Protocol
- Window size: `glb_sysconfig.window_size` (≤ 8 for Part 1, > 8 for Part 2)
- Implements:
  - Selective retransmission
  - Cumulative ACKs
  - Sequence number wraparound handling (`seq_num_diff`)
- Custom frame format with:
  - 2-byte `remaining_msg_bytes`
  - 1-byte `src_id`, `dst_id`, `seq_num`, and `crc8`
- Frames exceeding MTU (64 bytes) are fragmented and reassembled.

### 2. Congestion Control (PA1b)
- Tracks per-host pair congestion state:
  - `SS` (Slow Start)
  - `AIMD` (Additive Increase Multiplicative Decrease)
  - `FRFT` (Fast Retransmission & Fast Recovery)
- Implements:
  - Linear growth above `ssthresh` (AIMD)
  - Exponential growth below `ssthresh` (Slow Start)
  - Immediate retransmission after 3 dup ACKs (Fast Retransmission)
  - cwnd inflation on dup ACKs and recovery to `ssthresh` on valid ACK (Fast Recovery)
- All state transitions are tracked via a `CongestionControl` struct for each receiver.

---

## 📦 Frame Format

| Field                | Bytes | Description                          |
|---------------------|--------|--------------------------------------|
| `remaining_msg_bytes` | 2    | Bytes remaining in full message      |
| `dst_id`            | 1      | Destination Host ID                  |
| `src_id`            | 1      | Sender Host ID                       |
| `seq_num`           | 1      | Sequence number of this frame        |
| `data`              | ~58    | Payload                              |
| `crc_val`           | 1      | CRC-8 checksum (must be last field)  |

---

## 🧠 Algorithmic Breakdown

### Host-Side Logic

#### `handle_input_cmds`
- Parses command-line messages.
- Breaks long messages into frames using `FRAME_PAYLOAD_SIZE`.
- Fills out header, calculates CRC, and stores frames in `buffered_outframes_head`.

#### `handle_outgoing_frames`
- Sends `min(cwnd, available slots)` frames from buffered queue.
- Maintains per-frame timeout (`send_window[i].timeout`).
- Sets gaps of 10ms between timeouts for fairness.
- Updates `latest_timeout` to prevent timeout misalignment.

#### `handle_incoming_acks`
- Handles incoming ACKs and invokes:
  - `TCPCongestionControl()` to update congestion state.
  - Frame cleanup (if ACKed) and duplicate ACK tracking.
- Shifts `send_window` left after freeing ACKed frames (`shiftLeft`).

#### `handle_timedout_frames`
- On timeout detection, resets **all** frame timeouts.
- Drops back to Slow Start with:
  - `ssthresh = cwnd / 2` (min 2)
  - `cwnd = 1`
  - `state = cc_SS`

#### `FastRetransmission`
- Resends `LAR + 1` on third dup ACK.
- Manages `timeout`, increments cwnd (FRFT behavior).

#### `TCPCongestionControl`
- State machine for SS, AIMD, FRFT.
- Correctly increments, shrinks, and transitions `cwnd` per TCP standards.
- Seamless handling of duplicate vs new ACKs.
- Ensures `ssthresh` never falls below 2.

---

## 📥 Receiver-Side Logic

### `handle_incoming_frames`
- Pops incoming frames, validates via CRC.
- Rejects corrupted frames early.
- Stores valid frames in a **min-heap** indexed by sequence number (per sender).
- When the minimum frame is in order (`seq == LFR + 1`), it:
  - Appends to message buffer,
  - Checks `remaining_msg_bytes`,
  - Prints full message via `printf()` on final frame.

### `send_ack`
- Called after every valid (even duplicate) frame.
- Cumulative: always ACKs the latest `LFR`.

---

## 📊 Diagnostics Output

CSV diagnostics file (`diagnostics.csv`) logs per-RTT congestion behavior.

| Field                    | Description                            |
|--------------------------|----------------------------------------|
| `rtt`                    | Round-trip number                      |
| `ack_received`           | Valid ACKs received                    |
| `dup_acks`               | Count of duplicate ACKs received       |
| `state`                  | Congestion control state               |
| `cwnd`                   | Congestion window size                 |
| `ssthresh`               | Slow start threshold                   |
| `frames_sent`            | Frames sent this RTT                   |
| `frames_dropped`         | Frames dropped                         |
| `frames_in_sender_window` | Outstanding unacknowledged frames     |
| `timedout_frames`        | Timed-out frames awaiting retransmit   |

Run example:
```bash
./tritontalk -s 0 -r 2 -p ./test_suite/cc_basic.cfg < long_msg.txt
```

---

## ⚙️ Min-Heap Buffering (Receiver)

Used in `receiver.c` to hold out-of-order frames efficiently.

### Implementation Notes:
- Stored in `arrayMinQueue->minQueues[src_id]`
- Avoids insertion of duplicate `seq_num`
- Frames are only popped when `seq == LFR + 1`
- Ensures correct message reassembly even with reordering

### Utility Functions:
- `enqueue()` – Inserts frame into heap if not duplicate
- `getMin()` – Peeks at frame with lowest sequence number
- `popMin()` – Extracts the root of the heap
- `clearMinQueue()` – Wipes entire heap buffer

The comparison for ordering uses:
```c
seq_num_diff(seq1, seq2)
```
Which accounts for 8-bit wraparound (0–255) to ensure proper cyclic sequencing.

---

## 🛠 Utility and Support Tools

- CRC-8 implemented via `compute_crc8()` (based on 0x07 polynomial).
- Timeout handling with:
  - `timeval_usecdiff()`
  - `timeval_usecplus()`
- Circular linked list for buffering (`ll_*` functions).
- Sequence difference via `seq_num_diff()` for wraparound protection.

---

## 🧼 Clean Code & Safety

- Memory-safe: dynamically allocated frames and timeouts are `free()`d.
- Timeout spacing ensures deterministic ordering across RTTs.
- `latest_timeout` tracking ensures old frames don't preempt new ones.
- Frame sanity checks (`frame_sanity_check()`) prevent out-of-bounds access.

---

## 📌 Known Constraints

- CRC only detects corruption (not corrects it)
- ACKs always cumulative — no SACK support
- Each host sends to only one destination per command
- `minHeap` is custom — no standard heap lib used
- Receiver window capped at `window_size` frames per sender

---

## 👤 Author

**Name**: Mauricio Serrano  
