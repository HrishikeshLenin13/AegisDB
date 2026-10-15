#include "aegisdb/engine.hpp"
#include "aegisdb/index/bloom_filter.hpp"
#include "aegisdb/index/bplus_tree.hpp"
#include "aegisdb/index/extendible_hash.hpp"
#include "aegisdb/storage/slotted_page.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <stdexcept>
#include <limits>

namespace fs = std::filesystem;
using namespace aegisdb;

static int g_fails = 0;

#define CHECK(cond)                                                                 \
  do {                                                                              \
    if (!(cond)) {                                                                  \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
      ++g_fails;                                                                    \
    }                                                                               \
  } while (0)

static std::string Tmp(const std::string& name) {
  auto d = fs::temp_directory_path() / ("aegis-test-" + name);
  fs::remove_all(d);
  fs::create_directories(d);
  return (d / "db").string();
}

static void TestSlottedPage() {
  Page p;
  SlottedPage::Init(&p, 1);
  std::string a = "hello";
  std::string b = "world!!";
  auto s1 = SlottedPage::Insert(&p, reinterpret_cast<const std::uint8_t*>(a.data()), static_cast<std::uint16_t>(a.size()));
  auto s2 = SlottedPage::Insert(&p, reinterpret_cast<const std::uint8_t*>(b.data()), static_cast<std::uint16_t>(b.size()));
  CHECK(s1 && s2);
  auto r1 = SlottedPage::Read(&p, *s1);
  auto r2 = SlottedPage::Read(&p, *s2);
  CHECK(r1 && std::string(r1->begin(), r1->end()) == a);
  CHECK(r2 && std::string(r2->begin(), r2->end()) == b);
  CHECK(SlottedPage::Delete(&p, *s1));
  CHECK(!SlottedPage::Read(&p, *s1));
}

static void TestBufferPool() {
  auto path = Tmp("bpm");
  DiskManager disk(path + ".aegis");
  BufferPool bpm(&disk, 4);
  std::uint32_t ids[8];
  for (int i = 0; i < 8; ++i) {
    Page* p = bpm.NewPage(&ids[i], PageType::Heap);
    p->Data()[kPageHeaderSize] = static_cast<char>(i);
    bpm.UnpinPage(ids[i], true);
  }
  // Clock/LRU should have evicted the earliest unpinned pages.
  Page* p = bpm.FetchPage(ids[7]);
  CHECK(p->Data()[kPageHeaderSize] == 7);
  bpm.UnpinPage(ids[7], false);
}

static void TestBPlus() {
  auto path = Tmp("bplus");
  DiskManager disk(path + ".aegis");
  BufferPool bpm(&disk, 64);
  auto root = BPlusTree::Create(&bpm);
  BPlusTree t(&bpm, root, TypeId::Integer);
  for (int i = 0; i < 400; ++i) {
    t.Insert(Value::Integer(i), RID{static_cast<std::uint32_t>(i), 0});
  }
  auto got = t.Search(Value::Integer(123));
  CHECK(got.size() == 1 && got[0].page_id == 123);
  auto rng = t.Range(Value::Integer(10), Value::Integer(20));
  CHECK(rng.size() == 10);
  CHECK(t.Remove(Value::Integer(123), RID{123, 0}));
  CHECK(t.Search(Value::Integer(123)).empty());
  // Merge path: delete a contiguous range.
  for (int i = 0; i < 50; ++i) t.Remove(Value::Integer(i), RID{static_cast<std::uint32_t>(i), 0});
  CHECK(t.Search(Value::Integer(5)).empty());
  CHECK(t.Search(Value::Integer(200)).size() == 1);
}

static void TestBloomInvalidProbability() {
  BloomFilter filter(100, 0.01);
  filter.Add("retained");
  for (double probability : {0.0, -0.1, 1.0, 1.1,
                             std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
    bool rejected = false;
    try {
      filter.Reset(100, probability);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    CHECK(rejected);
    CHECK(filter.Count() == 1);
    CHECK(filter.MaybeContains("retained"));
  }
}

static void TestHashAndBloom() {
  BloomFilter bf(1000, 0.01);
  bf.Add("alpha");
  CHECK(bf.MaybeContains("alpha"));
  CHECK(!bf.MaybeContains("zzz-not-present-zzz"));

  auto path = Tmp("hash");
  DiskManager disk(path + ".aegis");
  BufferPool bpm(&disk, 64);
  auto dir = ExtendibleHash::Create(&bpm);
  ExtendibleHash h(&bpm, dir);
  for (int i = 0; i < 300; ++i) h.Insert(Value::Integer(i), RID{static_cast<std::uint32_t>(i), 1});
  auto got = h.Search(Value::Integer(42));
  CHECK(got.size() == 1 && got[0].page_id == 42);
  CHECK(h.Search(Value::Integer(99999)).empty());
}

static void TestSqlAndOptimizer() {
  auto path = Tmp("sql");
  Database db(path);
  db.Execute("CREATE TABLE t (id INTEGER, name VARCHAR(32), v DOUBLE);");
  db.Execute("INSERT INTO t VALUES (1, 'a', 1.5), (2, 'b', 2.5), (3, 'c', 3.5);");
  db.Execute("CREATE INDEX t_id ON t(id);");
  auto r = db.Execute("SELECT id, name FROM t WHERE id = 2;");
  CHECK(r.rows.size() == 1);
  CHECK(r.rows[0].values[0].i64 == 2);
  auto plan = db.Explain("SELECT id FROM t WHERE id = 2;");
  CHECK(plan.find("IndexScan") != std::string::npos || plan.find("SeqScan") != std::string::npos);

  db.Execute("CREATE TABLE u (id INTEGER, t_id INTEGER);");
  db.Execute("INSERT INTO u VALUES (10, 1), (20, 2);");
  db.Execute("ANALYZE;");
  auto j2 = db.Execute("SELECT t.name, u.id FROM t JOIN u ON t.id = u.t_id;");
  CHECK(j2.rows.size() == 2);

  auto agg = db.Execute("SELECT COUNT(*), SUM(v) FROM t;");
  CHECK(agg.rows.size() == 1);
  CHECK(agg.rows[0].values[0].i64 == 3);

  db.Execute("CREATE HASH INDEX t_name ON t(name);");
  auto hr = db.Execute("SELECT id FROM t WHERE name = 'b';");
  CHECK(hr.rows.size() == 1);

  auto ordered = db.Execute("SELECT id FROM t ORDER BY id DESC;");
  CHECK(ordered.rows.size() == 3);
  CHECK(ordered.rows[0].values[0].i64 == 3);
}

static void TestMvccAndWal() {
  auto path = Tmp("mvcc");
  {
    Database db(path);
    db.Execute("CREATE TABLE kv (k INTEGER, v INTEGER);");
    db.Execute("INSERT INTO kv VALUES (1, 10), (2, 20);");
    db.Execute("COMMIT;");
    db.Execute("BEGIN;");
    db.Execute("INSERT INTO kv VALUES (3, 30);");
    db.Execute("ROLLBACK;");
    auto unseen = db.Execute("SELECT k FROM kv;");
    CHECK(unseen.rows.size() == 2);
    db.CrashSim();
  }
  {
    Database db(path);  // recover
    auto r = db.Execute("SELECT k, v FROM kv;");
    CHECK(r.rows.size() == 2);
  }
}

int main() {
  TestSlottedPage();
  TestBufferPool();
  TestBPlus();
  TestHashAndBloom();
  TestBloomInvalidProbability();
  TestSqlAndOptimizer();
  TestMvccAndWal();
  if (g_fails) {
    std::cerr << g_fails << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
