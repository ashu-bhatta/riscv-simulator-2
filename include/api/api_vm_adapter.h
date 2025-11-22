#pragma once

#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <string>
#include <atomic>

#include "vm/vm_base.h"
#include "config.h"

// Minimal, thread-safe adapter for exposing VM operations to an API layer.
// This adapter owns its own VM instance and serializes access.
class ApiVmAdapter {
public:
    ApiVmAdapter();
    ~ApiVmAdapter();

    // Lifecycle
    bool CreateVm();
    bool LoadProgram(const std::string &filename);

    // Control
    bool Step();
    bool Run();   // starts background run loop
    bool Pause(); // pauses background run
    bool Stop();  // stops run loop

    // Undo/Redo
    bool Undo();
    bool Redo();

    // Queries
    std::string GetStateJson(); // small JSON snapshot

    // singleton accessor (convenience for controllers)
    static ApiVmAdapter &Instance();

private:
    void RunLoop();

    std::mutex mu_;
    std::condition_variable cv_;
    std::unique_ptr<VmBase> vm_;
    std::thread run_thread_;
    std::atomic<bool> run_requested_{false};
    std::atomic<bool> running_{false};
};
