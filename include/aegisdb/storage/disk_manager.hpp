#pragma once

#include "aegisdb/common/config.hpp"
#include "aegisdb/common/status.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

namespace aegisdb {

class DiskManager {
 public:
  explicit DiskManager(std::string path) : path_(std::move(path)) {
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
      std::ofstream create(path_, std::ios::out | std::ios::binary);
      create.close();
      file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_.is_open()) throw AegisError("cannot open database file: " + path_);
    file_.seekg(0, std::ios::end);
    auto sz = file_.tellg();
    next_page_id_ = static_cast<std::uint32_t>(sz / kPageSize);
  }

  ~DiskManager() {
    if (file_.is_open()) file_.close();
  }

  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  void ReadPage(std::uint32_t page_id, char* data) {
    std::lock_guard<std::mutex> g(mu_);
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(page_id) * kPageSize, std::ios::beg);
    file_.read(data, kPageSize);
    auto got = file_.gcount();
    if (got < static_cast<std::streamsize>(kPageSize)) {
      std::memset(data + got, 0, kPageSize - static_cast<std::size_t>(got));
    }
  }

  void WritePage(std::uint32_t page_id, const char* data) {
    std::lock_guard<std::mutex> g(mu_);
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(page_id) * kPageSize, std::ios::beg);
    file_.write(data, kPageSize);
    file_.flush();
    if (!file_) throw AegisError("page write failed");
  }

  std::uint32_t AllocatePage() {
    std::lock_guard<std::mutex> g(mu_);
    return next_page_id_++;
  }

  std::uint32_t NextPageId() const { return next_page_id_; }
  const std::string& Path() const { return path_; }

  void Sync() {
    std::lock_guard<std::mutex> g(mu_);
    file_.flush();
  }

 private:
  std::string path_;
  std::fstream file_;
  std::mutex mu_;
  std::uint32_t next_page_id_ = 0;
};

}  // namespace aegisdb
