# Out-of-Order Pipeline Simulator

This project implements a cycle-accurate **out-of-order (OOO) processor pipeline simulator** in C++.  
It models instruction execution through various pipeline stages, including fetch, decode, rename, register read, dispatch, issue, execute, writeback, and retire, while maintaining a **Reorder Buffer (ROB)**, **Issue Queue (IQ)**, and **Register Mapping Table (RMT)**.

## Features
- Simulates instruction flow through all pipeline stages:
  - **Fetch (FE)**
  - **Decode (DE)**
  - **Rename (RN)**
  - **Register Read (RR)**
  - **Dispatch (DI)**
  - **Issue (IS)**
  - **Execute (EX)**
  - **Writeback (WB)**
  - **Retire (RT)**
- Implements:
  - **Reorder Buffer (ROB)** for in-order retirement
  - **Register Mapping Table (RMT)** for register renaming
  - **Issue Queue (IQ)** for scheduling ready instructions
  - Execution latencies for different instruction types (INT, MUL, DIV etc.)
- Tracks per-instruction timing across pipeline stages
- Reports final statistics:
  - Total dynamic instructions executed
  - Total cycles
  - IPC (Instructions Per Cycle)

## Build Instructions
Compile with any C++11+ compiler:
    ```g++ -std=c++11 -o sim sim_proc.cc```

# Usage
Run the simulator with:
    ```./sim <ROB_SIZE> <IQ_SIZE> <WIDTH> <TRACE_FILE>```

Example:
    ```./sim 256 32 4 gcc_trace.txt```
Where:

ROB_SIZE = number of entries in the reorder buffer

IQ_SIZE = number of entries in the issue queue

WIDTH = processor width (instructions per cycle)

TRACE_FILE = instruction trace file

## Output
The simulator prints:

Per-instruction timeline, showing each stage and its cycle count:
    <seq_no> fu{<op>} src{<src1>,<src2>} dst{<dst>} FE{start,lat} DE{start,lat} ...

Final statistics:

/# === Simulator Command =========
/# ./sim 256 32 4 gcc_trace.txt
/# === Processor Configuration ===
/# ROB_SIZE = 256
/# IQ_SIZE  = 32
/# WIDTH    = 4
/# === Simulation Results ========
/# Dynamic Instruction Count    = 100000
/# Cycles                       = 50000
/# Instructions Per Cycle (IPC) = 2.00
## File Overview

sim_proc.cc – Main simulator source file, implements pipeline stages, instruction timing, and statistics.

sim_proc.h – Header file defining processor parameters and ROB structures (not included here, but required).

Notes
Instruction latencies are hardcoded:

op_type = 0: completes in 1 cycle

op_type = 1: completes in 2 cycles

op_type = 2: completes in 5 cycles

Assumes a valid trace file with format:

    <pc> <op_type> <dest> <src1> <src2>