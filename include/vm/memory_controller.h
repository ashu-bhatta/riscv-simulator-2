/**
 * @file memory_controller.h
 * @brief Contains the declaration of the MemoryController class for managing memory in the VM.
 * @author Vishank Singh, https://github.com/VishankSingh
 */

#ifndef MEMORY_CONTROLLER_H
#define MEMORY_CONTROLLER_H

#include "../config.h"
#include "main_memory.h"
#include "cache/cache.h"

#include <iostream>
#include <string>
#include <vector>


/**
 * @brief The MemoryController class is responsible for managing memory in the VM.
 */
class MemoryController {
private:
  Memory memory_; ///< The main memory object.
  std::unique_ptr<cache::Cache> cache_ = nullptr;
public:
  MemoryController() = default;

    void Reset() {
        memory_.Reset();
    }

    void PrintCacheStatus() const {
      std::cout << "Cache Status:" << std::endl;
      if (cache_) {
        cache_->PrintStats();
      } else {
        std::cout << "Cache is disabled." << std::endl;
      }
    }

    std::vector<cache::CacheSet>& GetCache() {
      if (!cache_ && vm_config::config.cache_enabled) {
        cache::CacheConfig cfg{};
        cfg.size = vm_config::config.cache_capacity;
        cfg.associativity = vm_config::config.cache_associativity;
        cfg.lines = cfg.size / vm_config::config.cache_block_size;
        cfg.words_per_line = vm_config::config.cache_block_size / 4;
        cfg.cache_type = cache::CacheType::Data;
        cache_ = std::make_unique<cache::Cache>(cfg);
      }
      if (cache_) {
        return cache_->GetCacheSets();
      } else {
        throw std::runtime_error("Cache is disabled.");
      }
    }

    void WriteByte(uint64_t address, uint8_t value) {
      if (!cache_ && vm_config::config.cache_enabled) {
        cache::CacheConfig cfg{};
        cfg.size = vm_config::config.cache_capacity;
        cfg.associativity = vm_config::config.cache_associativity;
        cfg.lines = cfg.size / vm_config::config.cache_block_size;
        cfg.words_per_line = vm_config::config.cache_block_size / 4;
        cfg.cache_type = cache::CacheType::Data;
        cache_ = std::make_unique<cache::Cache>(cfg);
      }
      if (cache_) {
        cache_->WriteByte(address, value, memory_);
        return;
      }
      memory_.WriteByte(address, value);
    }

    void WriteHalfWord(uint64_t address, uint16_t value) {
      memory_.WriteHalfWord(address, value);
    }

    void WriteWord(uint64_t address, uint32_t value) {
      memory_.WriteWord(address, value);
    }

    void WriteDoubleWord(uint64_t address, uint64_t value) {
      memory_.WriteDoubleWord(address, value);
    }

    [[nodiscard]] uint8_t ReadByte(uint64_t address) {
      if (!cache_ && vm_config::config.cache_enabled) {
        cache::CacheConfig cfg{};
        cfg.size = vm_config::config.cache_capacity;
        cfg.associativity = vm_config::config.cache_associativity;
        cfg.lines = cfg.size / vm_config::config.cache_block_size;
        cfg.words_per_line = vm_config::config.cache_block_size / 4;
        cfg.cache_type = cache::CacheType::Data;
        cache_ = std::make_unique<cache::Cache>(cfg);
      }
      if (cache_) return cache_->ReadByte(address, memory_);
      return memory_.ReadByte(address);
    }

    [[nodiscard]] uint16_t ReadHalfWord(uint64_t address) {
      uint16_t v = 0;
      for (size_t i = 0; i < sizeof(uint16_t); ++i) v |= static_cast<uint16_t>(ReadByte(address + i)) << (8*i);
      return v;
    }

    [[nodiscard]] uint32_t ReadWord(uint64_t address) {
      uint32_t v = 0;
      for (size_t i = 0; i < sizeof(uint32_t); ++i) v |= static_cast<uint32_t>(ReadByte(address + i)) << (8*i);
      return v;
    }

    [[nodiscard]] uint64_t ReadDoubleWord(uint64_t address) {
      uint64_t v = 0;
      for (size_t i = 0; i < sizeof(uint64_t); ++i) v |= static_cast<uint64_t>(ReadByte(address + i)) << (8*i);
      return v;
    }

    // Functions to read memory directly with cache bypass

    [[nodiscard]] uint8_t ReadByte_d(uint64_t address) {
        return memory_.ReadByte(address);
    }

    [[nodiscard]] uint16_t ReadHalfWord_d(uint64_t address) {
        return memory_.ReadHalfWord(address);
    }

    [[nodiscard]] uint32_t ReadWord_d(uint64_t address) {
        return memory_.ReadWord(address);
    }

    [[nodiscard]] uint64_t ReadDoubleWord_d(uint64_t address) {
        return memory_.ReadDoubleWord(address);
    }

    void PrintMemory(const uint64_t address, unsigned int rows) {
      memory_.PrintMemory(address, rows);
    }

    void DumpMemory(std::vector<std::string> args) {
      memory_.DumpMemory(args);
    }

    void GetMemoryPoint(std::string address) {
      return memory_.GetMemoryPoint(address);
    }

};

#endif // MEMORY_CONTROLLER_H

