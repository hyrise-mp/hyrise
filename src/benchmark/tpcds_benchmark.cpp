#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

#include "SQLParser.h"
#include "SQLParserResult.h"
#include "benchmark_runner.hpp"
#include "cli_config_parser.hpp"
#include "cxxopts.hpp"
#include "file_based_query_generator.hpp"
#include "file_based_table_generator.hpp"
#include "json.hpp"
#include "sqlite_add_indices.hpp"
#include "scheduler/current_scheduler.hpp"
#include "scheduler/node_queue_scheduler.hpp"
#include "scheduler/topology.hpp"
#include "sql/sql_pipeline.hpp"
#include "sql/sql_pipeline_builder.hpp"
#include "storage/chunk_encoder.hpp"
#include "utils/assert.hpp"
#include "visualization/lqp_visualizer.hpp"
#include "visualization/pqp_visualizer.hpp"

using namespace opossum;  // NOLINT

int main(int argc, char* argv[]) {
  auto cli_options = opossum::BenchmarkRunner::get_basic_cli_options("TPC-DS Benchmark");

  // clang-format off
  cli_options.add_options()
    ("s,scale", "Database scale factor (1 ~ 1GB)", cxxopts::value<int>()->default_value("1"));
  // clang-format on

  std::shared_ptr<opossum::BenchmarkConfig> config;
  int scale_factor;

  if (opossum::CLIConfigParser::cli_has_json_config(argc, argv)) {
    // JSON config file was passed in
    const auto json_config = opossum::CLIConfigParser::parse_json_config_file(argv[1]);
    scale_factor = json_config.value("scale", 1);
    config = std::make_shared<opossum::BenchmarkConfig>(
        opossum::CLIConfigParser::parse_basic_options_json_config(json_config));

  } else {
    // Parse regular command line args
    const auto cli_parse_result = cli_options.parse(argc, argv);

    if (CLIConfigParser::print_help_if_requested(cli_options, cli_parse_result)) {
      return 0;
    }
    scale_factor = cli_parse_result["scale"].as<int>();

    config =
        std::make_shared<opossum::BenchmarkConfig>(opossum::CLIConfigParser::parse_basic_cli_options(cli_parse_result));
  }

  // For the data generation, the official tpc-ds toolkit is used.
  // This toolkit does not provide the option to generate data using a scale factor less than 1.
  Assert(scale_factor >= 1, "For now, TPC-DS benchmark only supports scale factor 1");

  auto context = opossum::BenchmarkRunner::create_context(*config);

  std::cout << "- TPC-DS scale factor is " << scale_factor << std::endl;

  // TPC-DS FileBasedQueryGenerator specification
  std::optional<std::unordered_set<std::string>> query_subset;
  const auto query_filename_blacklist = std::unordered_set<std::string>{};
  std::string query_path = "resources/benchmark/tpcds/queries/supported";
  std::string table_path = "resources/benchmark/tpcds/tables";
  std::filesystem::path example_query_path = query_path + "/query_07.sql";
  std::filesystem::path example_table_schema_path = table_path + "/call_center.csv.json";

  Assert(std::filesystem::is_directory(query_path), "Query path (" + query_path + ") has to be a directory.");
  Assert(std::filesystem::is_directory(table_path), "Table path (" + table_path + ") has to be a directory.");
  Assert(std::filesystem::exists(example_query_path), "Queries have to be available.");
  Assert(std::filesystem::exists(example_table_schema_path), "Table schemes have to be available.");

  auto query_generator =
      std::make_unique<FileBasedQueryGenerator>(*config, query_path, query_filename_blacklist, query_subset);
  auto table_generator = std::make_unique<FileBasedTableGenerator>(config, table_path);
  auto benchmark_runner = BenchmarkRunner{*config, std::move(query_generator), std::move(table_generator), context};

  if (config->verify) {
    add_indices_to_sqlite("resources/benchmark/tpcds/schema.sql", "resources/benchmark/tpcds/create_indices.sql",
                          benchmark_runner);
  }

  std::cout << "done." << std::endl;

  benchmark_runner.run();
}
