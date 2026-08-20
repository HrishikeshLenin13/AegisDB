#include "aegisdb/engine.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  std::string path = "aegis";
  if (argc > 1) path = argv[1];
  try {
    aegisdb::Database db(path);
    std::cout << "AegisDB  (" << path << ".aegis)\n"
              << "SQL in, execution plan out.  Type EXPLAIN SELECT ... or .quit\n";
    std::string line, sql;
    std::cout << "aegisql> " << std::flush;
    while (std::getline(std::cin, line)) {
      if (line == ".quit" || line == ".exit") break;
      if (line == ".help") {
        std::cout << "Commands: EXPLAIN, CREATE TABLE/INDEX, INSERT, SELECT, BEGIN/COMMIT/ROLLBACK, ANALYZE\n";
        std::cout << "aegisql> " << std::flush;
        continue;
      }
      sql += line;
      if (line.find(';') == std::string::npos) {
        sql += " ";
        std::cout << "     -> " << std::flush;
        continue;
      }
      try {
        auto r = db.Execute(sql);
        if (!r.plan.empty() && r.rows.empty() && r.message == r.plan) {
          std::cout << r.plan;
        } else {
          if (!r.plan.empty() && sql.find("EXPLAIN") == std::string::npos && sql.find("explain") == std::string::npos) {
            // print nothing; plan is available via EXPLAIN
          }
          if (!r.schema.columns.empty()) {
            for (std::size_t c = 0; c < r.schema.columns.size(); ++c) {
              if (c) std::cout << " | ";
              std::cout << r.schema.columns[c].name;
            }
            std::cout << "\n";
            for (auto& row : r.rows) {
              for (std::size_t c = 0; c < row.values.size(); ++c) {
                if (c) std::cout << " | ";
                std::cout << row.values[c].ToString();
              }
              std::cout << "\n";
            }
            std::cout << r.rows.size() << " row(s)  " << r.elapsed_ms << " ms\n";
          } else if (!r.message.empty()) {
            std::cout << r.message << "  (" << r.elapsed_ms << " ms)\n";
          }
        }
      } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << "\n";
      }
      sql.clear();
      std::cout << "aegisql> " << std::flush;
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
  return 0;
}
