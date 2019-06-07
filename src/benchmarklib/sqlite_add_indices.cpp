#include "sqlite_add_indices.hpp"

#include <fstream>
#include <iostream>
#include <string>

#include "benchmark_runner.hpp"
#include "storage/storage_manager.hpp"
#include "utils/sqlite_wrapper.hpp"
#include "utils/timer.hpp"

namespace opossum {

void add_indices_to_sqlite(std::string schema_file_path, std::string create_indices_file_path,
                           BenchmarkRunner& benchmark_runner) {
  std::cout << "- Adding indexes to SQLite" << std::endl;
  Timer timer;

  // SQLite does not support adding primary keys, so we rename the table, create an empty one from the provided
  // schema and copy the data.
  for (const auto& table_name : StorageManager::get().table_names()) {
    benchmark_runner.sqlite_wrapper->raw_execute_query(std::string{"ALTER TABLE "} + table_name +  // NOLINT
                                                       " RENAME TO " + table_name + "_unindexed");
  }

  // Recreate tables from schema.sql
  std::ifstream schema_file(schema_file_path);
  std::string schema_sql((std::istreambuf_iterator<char>(schema_file)), std::istreambuf_iterator<char>());
  benchmark_runner.sqlite_wrapper->raw_execute_query(schema_sql);

  // Add foreign keys
  std::ifstream create_indices_file(create_indices_file_path);
  std::string create_indices_sql((std::istreambuf_iterator<char>(create_indices_file)),
                                 std::istreambuf_iterator<char>());
  benchmark_runner.sqlite_wrapper->raw_execute_query(create_indices_sql);

  // Copy over data
  for (const auto& table_name : StorageManager::get().table_names()) {
    Timer per_table_time;
    std::cout << "-  Adding indexes to SQLite table " << table_name << std::flush;

    benchmark_runner.sqlite_wrapper->raw_execute_query(std::string{"INSERT INTO "} + table_name +  // NOLINT
                                                       " SELECT * FROM " + table_name + "_unindexed");

    std::cout << " (" << per_table_time.lap_formatted() << ")" << std::endl;
  }

  std::cout << "- Added indexes to SQLite (" << timer.lap_formatted() << ")" << std::endl;
}

}  // namespace opossum
