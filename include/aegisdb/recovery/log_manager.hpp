#pragma once

#include "aegisdb/common/status.hpp"
#include "aegisdb/recovery/log_record.hpp"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace aegisdb {

class LogManager {
 public:
  explicit LogManager(std::string path) : path_(std::move(path)) {
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!file_.is_open()) {
      std::ofstream create(path_, std::ios::out | std::ios::binary);
      create.close();
      file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    }
    if (!file_.is_open()) throw AegisError("cannot open WAL: " + path_);
    file_.seekp(0, std::ios::end);
    durable_lsn_ = static_cast<std::uint64_t>(file_.tellp());
    next_lsn_ = durable_lsn_ == 0 ? 1 : durable_lsn_;
  }

  ~LogManager() {
    Flush();
    if (file_.is_open()) file_.close();
  }

  std::uint64_t Append(LogRecord rec) {
    std::lock_guard<std::mutex> g(mu_);
    rec.lsn = next_lsn_;
    auto bytes = rec.Encode();
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    next_lsn_ += bytes.size();
    if (buffer_.size() >= kLogBufferPages * kPageSize) FlushLocked();
    return rec.lsn;
  }

  void FlushTo(std::uint64_t lsn) {
    std::lock_guard<std::mutex> g(mu_);
    if (lsn > durable_lsn_) FlushLocked();
  }

  void Flush() {
    std::lock_guard<std::mutex> g(mu_);
    FlushLocked();
  }

  std::uint64_t DurableLsn() const { return durable_lsn_; }
  std::uint64_t NextLsn() const { return next_lsn_; }
  const std::string& Path() const { return path_; }

  std::vector<LogRecord> ReadAll() {
    Flush();
    std::ifstream in(path_, std::ios::binary);
    std::vector<std::uint8_t> all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<LogRecord> recs;
    std::size_t off = 0;
    while (off < all.size()) {
      std::size_t consumed = 0;
      auto r = LogRecord::Decode(all.data() + off, all.size() - off, &consumed);
      if (consumed == 0) break;
      recs.push_back(std::move(r));
      off += consumed;
    }
    return recs;
  }

  void Truncate() {
    std::lock_guard<std::mutex> g(mu_);
    buffer_.clear();
    file_.close();
    std::ofstream clobber(path_, std::ios::out | std::ios::binary | std::ios::trunc);
    clobber.close();
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    durable_lsn_ = 0;
    next_lsn_ = 1;
  }

 private:
  void FlushLocked() {
    if (buffer_.empty()) return;
    file_.write(reinterpret_cast<const char*>(buffer_.data()), static_cast<std::streamsize>(buffer_.size()));
    file_.flush();
    durable_lsn_ = next_lsn_ - 1;
    buffer_.clear();
  }

  std::string path_;
  std::fstream file_;
  std::mutex mu_;
  std::vector<std::uint8_t> buffer_;
  std::uint64_t next_lsn_ = 1;
  std::uint64_t durable_lsn_ = 0;
};

}  // namespace aegisdb
