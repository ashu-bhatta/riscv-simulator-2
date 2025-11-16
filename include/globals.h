/**
 * @file globals.h
 * @brief Contains global definitions and includes for the assembler.
 * @author Vishank Singh, https://github.com/VishankSingh
 */
#ifndef GLOBALS_H
#define GLOBALS_H

#include <filesystem>

namespace globals {
extern std::filesystem::path invokation_path;
extern std::filesystem::path vm_state_directory;
extern std::filesystem::path config_file_path;
extern std::filesystem::path disassembly_file_path;
extern std::filesystem::path errors_dump_file_path;
extern std::filesystem::path registers_dump_file_path;
extern std::filesystem::path memory_dump_file_path;
extern std::filesystem::path cache_dump_file_path;
extern std::filesystem::path vm_state_dump_file_path;
//extern std::string output_file;

extern bool verbose_errors_print;
extern bool verbose_warnings;
extern bool vm_as_backend;

// Pipeline feature flags (set at startup based on config)
extern bool pipeline_forwarding_enabled;
extern bool pipeline_hazard_detection_enabled;

// Branch prediction mode: 0 = none/static, 1 = static (predict-not-taken/taken configurable), 2 = dynamic (2-bit bimodal)
extern int branch_prediction_mode;

// Static branch prediction policy: 0 = predict-not-taken, 1 = predict-taken
extern int static_branch_policy;

// Number of bits used by dynamic branch predictor counters (n). Default 2.
extern int branch_prediction_bits;

extern unsigned int text_section_start;

void initGlobals();
}

#endif // GLOBALS_H