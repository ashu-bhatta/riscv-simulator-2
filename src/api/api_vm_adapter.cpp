#include "api/api_vm_adapter.h"

#include "assembler/assembler.h"
#include "vm/rvss/rvss_vm.h"
#include "vm/rv5s/rv5s_vm.h"
#include "globals.h"
#include "utils.h"

#include <sstream>
#include <iostream>

ApiVmAdapter::ApiVmAdapter() {
}

ApiVmAdapter::~ApiVmAdapter() {
    Stop();
}

ApiVmAdapter &ApiVmAdapter::Instance() {
    static ApiVmAdapter instance;
    return instance;
}

bool ApiVmAdapter::CreateVm() {
    std::lock_guard<std::mutex> lock(mu_);
    try {
        auto vmtype = vm_config::config.getVmType();
        switch (vmtype) {
            case vm_config::VmTypes::SINGLE_STAGE:
                vm_ = std::make_unique<RVSSVM>();
                break;
            default:
            case vm_config::VmTypes::MULTI_STAGE:
            case vm_config::VmTypes::MULTI_STAGE_WITH_FORWARDING:
            case vm_config::VmTypes::MULTI_STAGE_WITH_HAZARD_DETECTION:
            case vm_config::VmTypes::MULTI_STAGE_WITH_BOTH:
                vm_ = std::make_unique<RV5SVM>();
                break;
        }
    } catch (const std::exception &e) {
        std::cerr << "ApiVmAdapter::CreateVm error: " << e.what() << std::endl;
        return false;
    }
    return (vm_ != nullptr);
}

bool ApiVmAdapter::LoadProgram(const std::string &filename) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!vm_) CreateVm();
    try {
        AssembledProgram p = assemble(filename);
        vm_->LoadProgram(p);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "ApiVmAdapter::LoadProgram error: " << e.what() << std::endl;
        return false;
    }
}

bool ApiVmAdapter::Step() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!vm_) CreateVm();
    try {
        vm_->Step();
        return true;
    } catch (const std::exception &e) {
        std::cerr << "ApiVmAdapter::Step error: " << e.what() << std::endl;
        return false;
    }
}

bool ApiVmAdapter::Run() {
    bool expected = false;
    if (!run_requested_.compare_exchange_strong(expected, true)) return false; // already requested
    if (running_) return true;
    // start thread
    run_thread_ = std::thread(&ApiVmAdapter::RunLoop, this);
    return true;
}

bool ApiVmAdapter::Pause() {
    run_requested_ = false;
    return true;
}

bool ApiVmAdapter::Stop() {
    run_requested_ = false;
    if (run_thread_.joinable()) {
        run_thread_.join();
    }
    return true;
}

void ApiVmAdapter::RunLoop() {
    running_ = true;
    while (run_requested_) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!vm_) CreateVm();
            try {
                vm_->Step();
            } catch (...) {
                // swallow for now
            }
        }
        // small sleep to avoid tight loop; real code should support configurable frequency
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    running_ = false;
}

bool ApiVmAdapter::Undo() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!vm_) return false;
    try { vm_->Undo(); return true; } catch (...) { return false; }
}

bool ApiVmAdapter::Redo() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!vm_) return false;
    try { vm_->Redo(); return true; } catch (...) { return false; }
}

std::string ApiVmAdapter::GetStateJson() {
    std::lock_guard<std::mutex> lock(mu_);
    std::ostringstream oss;
    if (!vm_) {
        oss << "{\"ok\":false,\"error\":\"no_vm\"}";
        return oss.str();
    }
    // Build a small JSON snapshot (manual to avoid external deps)
    oss << "{";
    oss << "\"pc\":" << vm_->GetProgramCounter() << ",";
    oss << "\"cycle\":" << vm_->cycle_s_ << ",";
    oss << "\"status\":\"" << (run_requested_?"running":"stopped") << "\",";
    // registers (GPRs)
    auto gprs = vm_->registers_.GetGprValues();
    oss << "\"regs\": [";
    for (size_t i = 0; i < gprs.size(); ++i) {
        if (i) oss << ",";
        oss << gprs[i];
    }
    oss << "]";
    oss << "}";
    return oss.str();
}
