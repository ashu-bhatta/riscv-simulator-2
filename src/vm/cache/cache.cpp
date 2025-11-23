/**
 * @file cache.cpp
 * @brief Basic set-associative cache implementation with LRU replacement.
 */

#include "vm/cache/cache.h"
#include "vm/main_memory.h"
#include <algorithm>
#include <iostream>

using namespace cache;

Cache::Cache(const CacheConfig &cfg)
  : enabled_(true), type_(cfg.cache_type), config_(cfg), stats_{0,0,0} {
  // deduce line size from config.words_per_line (words is ambiguous); use size/lines
  if (cfg.size == 0 || cfg.lines == 0) {
    // fallback: try to compute from associativity and size
    line_size_ = cfg.words_per_line ? cfg.words_per_line * 4 : 64;
  } else {
    line_size_ = cfg.size / cfg.lines;
  }
  if (line_size_ == 0) line_size_ = 64;

  // compute set_count
  unsigned long lines = cfg.lines ? cfg.lines : (cfg.size / line_size_);
  if (lines == 0) lines = 1;
  set_count_ = lines / std::max<unsigned long>(1, cfg.associativity);
  if (set_count_ == 0) set_count_ = 1;

  sets_.reserve(set_count_);
  for (unsigned long i = 0; i < set_count_; ++i) sets_.emplace_back(cfg.associativity);
}

unsigned long Cache::addressToTag(uint64_t address) const {
  return address / line_size_;
}

unsigned long Cache::addressToSetIndex(uint64_t address) const {
  return (address / line_size_) % set_count_;
}

void Cache::fetchLineToSet(unsigned long set_idx, unsigned long tag, uint64_t block_addr, Memory &memory, unsigned long line_idx) {
  // read line_size_ bytes from memory into the line
  CacheLine &line = sets_[set_idx].lines[line_idx];
  line.data.resize(line_size_);
  for (unsigned long i = 0; i < line_size_; ++i) {
    line.data[i] = memory.ReadByte(block_addr + i);
  }
  line.tag = tag;
  line.state = CacheLineState::Valid;
}

uint8_t Cache::ReadByte(uint64_t address, Memory &memory) {
  ++stats_.accesses;
  ++access_counter_;
  unsigned long tag = addressToTag(address);
  unsigned long set_idx = addressToSetIndex(address);
  unsigned long offset = address % line_size_;

  CacheSet &set = sets_[set_idx];
  // search for tag
  for (unsigned long i = 0; i < set.lines.size(); ++i) {
    CacheLine &line = set.lines[i];
    if (line.state != CacheLineState::Invalid && line.tag == tag) {
      ++stats_.hits;
      if (i != 0) std::rotate(set.lines.begin(), set.lines.begin() + i, set.lines.begin() + i + 1);
      return line.data[offset];
    }
  }

  ++stats_.misses;
  // pick victim: find invalid line first
  unsigned long victim = 0;
  bool found_invalid = false;
  for (unsigned long i = 0; i < set.lines.size(); ++i) {
    if (set.lines[i].state == CacheLineState::Invalid) { victim = i; found_invalid = true; break; }
  }
  if (!found_invalid) {

    victim = set.lines.size() - 1;
    // if dirty, write back
    CacheLine &vline = set.lines[victim];
    if (vline.state == CacheLineState::Dirty) {
      uint64_t base = vline.tag * line_size_;
      for (unsigned long k = 0; k < line_size_; ++k) memory.WriteByte(base + k, vline.data[k]);
    }
  }

  // fetch from memory into victim
  uint64_t block_addr = (address / line_size_) * line_size_;
  fetchLineToSet(set_idx, tag, block_addr, memory, victim);
  // move victim to front
  if (victim != 0) std::rotate(set.lines.begin(), set.lines.begin() + victim, set.lines.begin() + victim + 1);
  return set.lines[0].data[offset];
}

void Cache::WriteByte(uint64_t address, uint8_t value, Memory &memory) {
  ++stats_.accesses;
  ++access_counter_;
  unsigned long tag = addressToTag(address);
  unsigned long set_idx = addressToSetIndex(address);
  unsigned long offset = address % line_size_;

  CacheSet &set = sets_[set_idx];
  for (unsigned long i = 0; i < set.lines.size(); ++i) {
    CacheLine &line = set.lines[i];
    if (line.state != CacheLineState::Invalid && line.tag == tag) {
      ++stats_.hits;
      // write into cache
      line.data[offset] = value;
      // apply write-hit policy
      if (config_.write_hit_policy == WriteHitPolicy::WriteThrough) {
        // write through to main memory immediately
        uint64_t base = line.tag * line_size_;
        memory.WriteByte(base + offset, value);
        // keep line state as Valid (not dirty)
        line.state = CacheLineState::Valid;
      } else { // default: WriteBack
        line.state = CacheLineState::Dirty;
      }
      if (i != 0) std::rotate(set.lines.begin(), set.lines.begin() + i, set.lines.begin() + i + 1);
      return;
    }
  }

  ++stats_.misses;
  // on miss, behavior depends on write-miss policy
  if (config_.write_miss_policy == WriteMissPolicy::NoWriteAllocate) {
    // Do not bring block into cache: perform the write directly to memory.
    // If write-hit policy is WriteThrough, semantics are same for memory write on miss.
    memory.WriteByte(address, value);
    return;
  }

  // Otherwise, WriteAllocate: bring the block into cache, evict if needed
  unsigned long victim = 0;
  bool found_invalid = false;
  for (unsigned long i = 0; i < set.lines.size(); ++i) {
    if (set.lines[i].state == CacheLineState::Invalid) { victim = i; found_invalid = true; break; }
  }
  if (!found_invalid) {
    victim = set.lines.size() - 1;
    CacheLine &vline = set.lines[victim];
    if (vline.state == CacheLineState::Dirty) {
      uint64_t base = vline.tag * line_size_;
      for (unsigned long k = 0; k < line_size_; ++k) memory.WriteByte(base + k, vline.data[k]);
    }
  }

  uint64_t block_addr = (address / line_size_) * line_size_;
  fetchLineToSet(set_idx, tag, block_addr, memory, victim);
  // write the byte into cache line according to hit policy: if write-through, also write to memory
  set.lines[victim].data[offset] = value;
  if (config_.write_hit_policy == WriteHitPolicy::WriteThrough) {
    uint64_t base = set.lines[victim].tag * line_size_;
    memory.WriteByte(base + offset, value);
    set.lines[victim].state = CacheLineState::Valid;
  } else {
    set.lines[victim].state = CacheLineState::Dirty;
  }
  if (victim != 0) std::rotate(set.lines.begin(), set.lines.begin() + victim, set.lines.begin() + victim + 1);
}

void Cache::PrintStats() const {
  std::cout << "Cache stats: accesses = " << stats_.accesses << " hits = " << stats_.hits << " misses = " << stats_.misses << std::endl;
}
