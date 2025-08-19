
#include <iostream>
#include <queue>
#include <vector>
#include <list>
#include <iomanip>
#include <unordered_map>
#include "sim_proc.h"

struct Instruction {
    uint64_t pc;
    int op_type;
    int dest;
    int src1;
    int src2;
    int original_dest;
    int original_src1;
    int original_src2;
    bool src1_ready;
    bool src2_ready;
    bool src1_awaken;
    bool src2_awaken;
    int seq_no;
    int fetch_cycle;
    int decode_cycle;
    int rename_cycle;
    int regread_cycle;
    int dispatch_cycle;
    int issue_cycle;
    int execute_cycle;
    int writeback_cycle;
    int retire_cycle;
    int execute_duration;
    int RT_cycle;
};

std::queue<Instruction> DE, RN, WB;
std::vector<Instruction> execute_list, completed_intstructions;
std::list<Instruction> IQ, RR, DI;
std::unordered_map<int, Instruction> retire_map; // Map rob tags to the rob_params
std::unordered_map<int, rob_params> reorder_buffer; // Map rob tags to the rob_params
std::unordered_map<int, int> RMT; // Map registers to their up-to-date rob_tag

// Statistics variables
int total_instructions = 0;
int total_cycles = 0;
int retired_instructions = 0;
int rob_tag = 0;
int rob_head = 0;
int rob_tail = 0;
bool rob_full = false;

int free_entries(int ROB_SIZE) {
    int entries;
    if (rob_tail == rob_head) {
        entries = rob_full ? 0 : ROB_SIZE;
    } else if (rob_tail > rob_head) {
        entries = ROB_SIZE - (rob_tail - rob_head);
    } else {
        entries = rob_head - rob_tail;
    }
    return entries;
}

void PrintRMT() {
    std::cout << "=== Register Mapping Table (RMT) ===" << std::endl;
    for (const auto &entry : RMT) {
        std::cout << "Register: " << entry.first 
                  << " -> ROB Tag: " << entry.second << std::endl;
    }
    std::cout << "====================================" << std::endl;
}

void PrintROB() {
    std::cout << "=== Reorder Buffer (ROB) ===" << std::endl;
    for (const auto &entry : reorder_buffer) {
        std::cout << "ROB Tag: " << entry.first 
                  << " -> ROB dst: " << entry.second.dst
                  << " -> ROB ready: " << entry.second.ready << std::endl;
    }
    std::cout << "====================================" << std::endl;
}

// Fetch stage: Fetch instructions from trace
void Fetch(FILE *trace_file, int WIDTH) {
    if (DE.empty()) {
        for (int i = 0; i < WIDTH && !feof(trace_file); ++i) {
            Instruction instr;
            if (fscanf(trace_file, "%llx %d %d %d %d", &instr.pc, &instr.op_type, &instr.dest, &instr.src1, &instr.src2) != EOF) {
                instr.seq_no = total_instructions++; // Assign sequence number and increment instruction count
                instr.fetch_cycle = total_cycles;
                instr.original_dest = instr.dest;
                instr.original_src1 = instr.src1;
                instr.original_src2 = instr.src2;
                instr.decode_cycle = total_cycles+1;
                DE.push(instr);
            }
        }
    }
}

// Decode stage: Move instructions from fetch to decode buffer
void Decode() {
    if (RN.empty()) {
        while (!DE.empty()) {
            Instruction instr = DE.front();
            DE.pop();
            instr.rename_cycle = total_cycles+1;
            RN.push(instr);
        }
    }
}

// Rename stage: Allocate ROB entries and rename registers
void Rename(int ROB_SIZE) {
    int entries = free_entries(ROB_SIZE);
    if (RR.empty() && (entries >= RN.size())) {
        while (!RN.empty()) {
            Instruction instr = RN.front();
            RN.pop();

            // Allocate an entry in the ROB
            rob_params ROB;
            ROB.ready = false;
            ROB.dst = instr.dest;
            ROB.tag = rob_tail;
            rob_tail = (rob_tail + 1) % ROB_SIZE;
            if (rob_tail == rob_head) {
                rob_full = true; // Mark ROB as full
            }
            reorder_buffer[ROB.tag] = ROB;

            // Rename source registers, if positive then it is in RMT
            if (instr.src1 != -1) {
                if (RMT.find(instr.src1) != RMT.end()) {
                    instr.src1 = RMT[instr.src1];
                } else {
                    instr.src1 = -2;
                }
            }
            if (instr.src2 != -1) {
                if (RMT.find(instr.src2) != RMT.end()) {
                    instr.src2 = RMT[instr.src2];
                } else {
                    instr.src2 = -2;
                }
            }
            instr.src1_awaken = false;
            instr.src2_awaken = false;
            
            // Rename destination register
            RMT[ROB.dst] = ROB.tag;
            instr.dest = ROB.tag;

            instr.regread_cycle = total_cycles+1;
            RR.push_back(instr);
        }
    }
}

// Register Read stage: Check readiness of source operands
void RegRead() {
    if (DI.empty()) {
        auto it = RR.begin();
        while (it != RR.end()) {
            Instruction instr = *it;
            if (instr.src1_awaken == false) {
                if (instr.src1 < 0) {
                    instr.src1_ready = true;
                }
                else if (reorder_buffer.find(instr.src1) != reorder_buffer.end()) {
                    instr.src1_ready = reorder_buffer[instr.src1].ready;
                }
                else {
                    instr.src1_ready = true;
                }
            }

            if (instr.src2_awaken == false) {
                if (instr.src2 < 0) {
                    instr.src2_ready = true;
                }
                else if (reorder_buffer.find(instr.src2) != reorder_buffer.end()) {
                    instr.src2_ready = reorder_buffer[instr.src2].ready;
                }
                else {
                    instr.src2_ready = true;
                }
            }
            instr.dispatch_cycle = total_cycles+1;
            DI.push_back(instr); // Add to the DI list
            it = RR.erase(it);   // Remove from the RR list
        }
    }
}

// Dispatch stage: Move instructions to the issue queue
void Dispatch(int IQ_SIZE) {
    auto it = DI.begin();
    while (it != DI.end() && (IQ_SIZE - IQ.size()) >= DI.size()) {
        Instruction instr = *it;
        instr.issue_cycle = total_cycles+1;
        IQ.push_back(instr); // Add to issue queue
        it = DI.erase(it);   // Remove from dispatch list
    }
}

// Issue stage: Issue oldest ready instructions to execute
void Issue(int WIDTH) {
    auto it = IQ.begin();
    int issued_insts = 0;
    while (issued_insts < WIDTH && it != IQ.end()) {
        if (it->src1_ready == true && it->src2_ready == true) {
            it->execute_duration = 0;
            execute_list.push_back(*it);
            it = IQ.erase(it);
            issued_insts++;
        }
        else {
            it++;
        }
    }
}

// Execute stage: Simulate execution latency
void Execute() {
    for (auto it = execute_list.begin(); it != execute_list.end(); ) {
        if (it->execute_duration == 0) { // First cycle in execution
            it->execute_cycle = total_cycles;
        }
        it->execute_duration++;
        bool completed = false;

        if (it->op_type == 0) { // Execution complete
            completed = true;
        } else if (it->op_type == 1 && it->execute_duration == 2) { // Execution complete
            completed = true;
        } else if (it->op_type == 2 && it->execute_duration == 5) { // Execution complete
            completed = true;
        }

        if (completed) { // Execution complete
            WB.push(*it);
            // Wake up dependent instructions in IQ, DI, and RR
            for (auto &instr : IQ) {
                if (instr.src1 == it->dest) {
                    instr.src1_ready = true; // Mark as ready
                }
                if (instr.src2 == it->dest) {
                    instr.src2_ready = true; // Mark as ready
                }
            }
            for (auto &instr : DI) {
                if (instr.src1 == it->dest) {
                    instr.src1_ready = true; // Mark as ready
                }
                if (instr.src2 == it->dest) {
                    instr.src2_ready = true; // Mark as ready
                }
            }
            for (auto &instr : RR) {
                if (instr.src1 == it->dest) {
                    instr.src1_ready = true; // Mark as ready
                    instr.src1_awaken = true;
                }
                if (instr.src2 == it->dest) {
                    instr.src2_ready = true; // Mark as ready
                    instr.src2_awaken = true;
                }
            }
            it = execute_list.erase(it); // Remove from execution list
        } 
        else {
            ++it;
        }
    }
}

// Writeback stage: Mark instructions as ready for retirement
void Writeback() {
    while (!WB.empty()) {
        Instruction instr = WB.front();
        WB.pop();
        instr.writeback_cycle = total_cycles;
        instr.retire_cycle = total_cycles + 1;
        reorder_buffer[instr.dest].ready = true;
        retire_map[instr.dest] = instr;
    }
}

// Retire stage: Retire instructions in program order
void Retire(int WIDTH, int ROB_SIZE) {
    int i = 0;
    while (i < WIDTH) {
        if (reorder_buffer[rob_head].ready == true) {
            Instruction instr = retire_map[rob_head];
            instr.RT_cycle = total_cycles+1;
            completed_intstructions.push_back(instr);
            retire_map.erase(rob_head);
            if (RMT[reorder_buffer[rob_head].dst] == rob_head) {
                RMT.erase(reorder_buffer[rob_head].dst);
            }
            reorder_buffer.erase(rob_head);
            rob_head = (rob_head + 1) % ROB_SIZE;
            rob_full = false;
            retired_instructions++;
            i++;
        }
        else {
            break;
        }
    }
}

// Advance Cycle: Update the cycle counter
bool Advance_Cycle() {
    return !DE.empty() || !RN.empty() || !RR.empty() ||
           !DI.empty() || !IQ.empty() || !retire_map.empty() ||
           !execute_list.empty() || !WB.empty(); // !reorder_buffer.empty() ||
}



// Print final statistics
void PrintStatistics(int ROB_SIZE, int IQ_SIZE, int WIDTH, char *TRACE) {
    std::cout << "# === Simulator Command =========\n";
    std::cout << "# ./sim " << ROB_SIZE << " " << IQ_SIZE << " " << WIDTH << " " << TRACE << "\n";
    std::cout << "# === Processor Configuration ===\n";
    std::cout << "# ROB_SIZE = " << ROB_SIZE << "\n";
    std::cout << "# IQ_SIZE  = " << IQ_SIZE << "\n";
    std::cout << "# WIDTH    = " << WIDTH << "\n";
    std::cout << "# === Simulation Results ========\n";
    std::cout << "# Dynamic Instruction Count    = " << retired_instructions << "\n";
    std::cout << "# Cycles                       = " << total_cycles << "\n";
    std::cout << "# Instructions Per Cycle (IPC) = " 
              << std::fixed << std::setprecision(2) 
              << static_cast<double>(retired_instructions) / total_cycles << "\n";
}

void PrintInstructionTiming(const std::vector<Instruction>& instructions) {
    for (const auto& inst : instructions) {
        std::cout << inst.seq_no << " fu{" << inst.op_type << "} src{" << inst.original_src1 << "," 
             << inst.original_src2 << "} dst{" << inst.original_dest << "} ";
        std::cout << "FE{" << inst.fetch_cycle << "," << (inst.decode_cycle - inst.fetch_cycle) << "} ";
        std::cout << "DE{" << inst.decode_cycle << "," << (inst.rename_cycle - inst.decode_cycle) << "} ";
        std::cout << "RN{" << inst.rename_cycle << "," << (inst.regread_cycle - inst.rename_cycle) << "} ";
        std::cout << "RR{" << inst.regread_cycle << "," << (inst.dispatch_cycle - inst.regread_cycle) << "} ";
        std::cout << "DI{" << inst.dispatch_cycle << "," << (inst.issue_cycle - inst.dispatch_cycle) << "} ";
        std::cout << "IS{" << inst.issue_cycle << "," << (inst.execute_cycle - inst.issue_cycle) << "} ";
        std::cout << "EX{" << inst.execute_cycle << "," << (inst.writeback_cycle - inst.execute_cycle) << "} ";
        std::cout << "WB{" << inst.writeback_cycle << "," << (inst.retire_cycle - inst.writeback_cycle) << "} ";
        std::cout << "RT{" << inst.retire_cycle << "," << (inst.RT_cycle - inst.retire_cycle) << "}" << "\n";
    }
}

/*  argc holds the number of command line arguments
    argv[] holds the commands themselves

    Example:-
    sim 256 32 4 gcc_trace.txt
    argc = 5
    argv[0] = "sim"
    argv[1] = "256"
    argv[2] = "32"
    ... and so on
*/
int main (int argc, char* argv[])
{
    FILE *FP;               // File handler
    char *trace_file;       // Variable that holds trace file name;
    proc_params params;       // look at sim_bp.h header file for the the definition of struct proc_params
    int op_type, dest, src1, src2;  // Variables are read from trace file
    uint64_t pc; // Variable holds the pc read from input file
    
    if (argc != 5)
    {
        printf("Error: Wrong number of inputs:%d\n", argc-1);
        exit(EXIT_FAILURE);
    }
    
    params.rob_size     = strtoul(argv[1], NULL, 10);
    params.iq_size      = strtoul(argv[2], NULL, 10);
    params.width        = strtoul(argv[3], NULL, 10);
    trace_file          = argv[4];

    FP = fopen(trace_file, "r");
    if(FP == NULL)
    {
        // Throw error and exit if fopen() failed
        printf("Error: Unable to open file %s\n", trace_file);
        exit(EXIT_FAILURE);
    }
    
    do {
        Retire(params.width, params.rob_size);
        Writeback();
        Execute();
        Issue(params.width);
        Dispatch(params.iq_size);
        RegRead();
        Rename(params.rob_size);
        Decode();
        Fetch(FP, params.width);
        ++total_cycles;
    } while (Advance_Cycle());
    
    PrintInstructionTiming(completed_intstructions);
    PrintStatistics(params.rob_size, params.iq_size, params.width, trace_file);
    fclose(FP);
    return 0;
}
