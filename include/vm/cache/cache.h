/**
 * @file cache.h
 * @brief File containing the cache class for the virtual machine
 * @author Vishank Singh, https://github.com/VishankSingh
 */
#ifndef CACHE_H
#define CACHE_H

#include "../main_memory.h"

#include <cstdint>
#include <vector>

namespace cache {

enum class ReplacementPolicy {
  LRU,    ///< Least Recently Used
  FIFO,   ///< First In First Out
  Random  ///< Random replacement
};

enum class CacheType {
  Instruction, ///< Cache for instructions
  Data         ///< Cache for data
};

enum class CacheLineState {
  Valid,       ///< Cache line is valid
  Invalid,     ///< Cache line is invalid
  Dirty        ///< Cache line has been modified
};

enum class WriteHitPolicy {
  WriteThrough, ///< Write through policy
  WriteBack     ///< Write back policy
};

enum class WriteMissPolicy {
  NoWriteAllocate, ///< Do not allocate on write miss
  WriteAllocate    ///< Allocate on write miss
};

struct CacheConfig {
  unsigned long lines = 0;  ///< Number of lines in the cache
  unsigned long associativity = 0; ///< Associativity of the cache
  unsigned long words_per_line = 0; ///< Number of words per line in the cache
  ReplacementPolicy replacement_policy = ReplacementPolicy::LRU; ///< Replacement policy for the cache
  CacheType cache_type = CacheType::Data; ///< Type of cache (instruction or data)
  WriteHitPolicy write_hit_policy = WriteHitPolicy::WriteBack; ///< Write hit policy
  WriteMissPolicy write_miss_policy = WriteMissPolicy::NoWriteAllocate; ///< Write miss policy
  unsigned long size = 0;   ///< Size of the cache in bytes
};

struct CacheLine {
  CacheLineState state = CacheLineState::Invalid; ///< State of the cache line
  unsigned long tag = 0;    ///< Tag for the cache line
  std::vector<uint8_t> data; ///< Data stored in the cache line
  
};

struct CacheStats {
  unsigned long accesses; ///< Total number of accesses to the cache
  unsigned long hits;     ///< Total number of hits in the cache
  unsigned long misses;   ///< Total number of misses in the cache


};

struct CacheSet {
  unsigned long associativity; ///< Associativity of the cache set
  std::vector<CacheLine> lines; ///< Lines in the cache set

  CacheSet(unsigned long assoc)
    : associativity(assoc), lines(assoc) {}
};

class Cache {
public:
  Cache() = delete;
  Cache(const CacheConfig &cfg);

  // Read/Write operations (memory is passed so cache can fetch/evict lines)
  uint8_t ReadByte(uint64_t address, Memory &memory);
  void WriteByte(uint64_t address, uint8_t value, Memory &memory);

  void PrintStats() const;

  std::vector<CacheSet>& GetCacheSets() {
    return sets_;
  }

private:
  bool enabled_; ///< Flag to indicate if the cache is enabled
  CacheType type_; ///< Type of cache (instruction or data)
  CacheConfig config_; ///< Configuration of the cache
  CacheStats stats_; ///< Statistics for the cache

  std::vector<CacheSet> sets_;
  unsigned long set_count_ = 0;
  unsigned long line_size_ = 0;

  // Simple LRU timestamp counter
  unsigned long access_counter_ = 0;

  // Helper utilities
  unsigned long addressToTag(uint64_t address) const;
  unsigned long addressToSetIndex(uint64_t address) const;
  void fetchLineToSet(unsigned long set_idx, unsigned long tag, uint64_t block_addr, Memory &memory, unsigned long line_idx);
};



} // namespace cache



#endif // CACHE_H