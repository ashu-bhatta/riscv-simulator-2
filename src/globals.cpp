#include "globals.h"
#include <filesystem>

std::filesystem::path globals::invokation_path = std::filesystem::current_path();

std::filesystem::path globals::vm_state_directory = globals::invokation_path / "vm_state";
std::filesystem::path globals::config_file_path = (globals::invokation_path / "vm_state" / "config.ini");
std::filesystem::path globals::disassembly_file_path = (globals::invokation_path / "vm_state" / "disassembly.txt");
std::filesystem::path globals::errors_dump_file_path = (globals::invokation_path / "vm_state" / "errors_dump.json");
std::filesystem::path globals::registers_dump_file_path = (globals::invokation_path / "vm_state" / "registers_dump.json");
std::filesystem::path globals::memory_dump_file_path = (globals::invokation_path / "vm_state" / "memory_dump.json");
std::filesystem::path globals::cache_dump_file_path = (globals::invokation_path / "vm_state" / "cache_dump.json");
std::filesystem::path globals::vm_state_dump_file_path = (globals::invokation_path / "vm_state" / "vm_state_dump.json");

bool globals::verbose_errors_print = false;
bool globals::verbose_warnings = false;
bool globals::vm_as_backend = false;

unsigned int globals::text_section_start = 0x00000000;

// Pipeline feature flags
bool globals::pipeline_forwarding_enabled = false;
bool globals::pipeline_hazard_detection_enabled = false;

// Branch prediction defaults: 0 = none, 1 = static, 2 = dynamic
int globals::branch_prediction_mode = 0;
// Static branch policy default: predict-not-taken
int globals::static_branch_policy = 0;

// Number of bits for dynamic predictor counters (default 2)
int globals::branch_prediction_bits = 2;
