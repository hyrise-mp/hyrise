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
#include "file_based_benchmark_item_runner.hpp"
#include "file_based_table_generator.hpp"
#include "json.hpp"
#include "scheduler/current_scheduler.hpp"
#include "scheduler/node_queue_scheduler.hpp"
#include "scheduler/topology.hpp"
#include "sql/sql_pipeline.hpp"
#include "sql/sql_pipeline_builder.hpp"
#include "sqlite_add_indices.hpp"
#include "storage/chunk_encoder.hpp"
#include "utils/assert.hpp"
#include "visualization/lqp_visualizer.hpp"
#include "visualization/pqp_visualizer.hpp"

using namespace opossum;  // NOLINT

namespace {

bool data_files_available(const std::string& table_path);
const std::unordered_set<std::string> filename_blacklist();

}  // namespace

int main(int argc, char* argv[]) {
  const std::string binary_path = argv[0];
  const std::string binary_directory = binary_path.substr(0, binary_path.find_last_of("/"));

  auto cli_options = opossum::BenchmarkRunner::get_basic_cli_options("TPC-DS Benchmark");

  // clang-format off
  cli_options.add_options()
    ("s,scale", "Database scale factor (1 ~ 1GB)", cxxopts::value<int32_t>()->default_value("1"));
  // clang-format on

  std::shared_ptr<opossum::BenchmarkConfig> config;
  int32_t scale_factor{};

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
    scale_factor = cli_parse_result["scale"].as<int32_t>();

    config =
        std::make_shared<opossum::BenchmarkConfig>(opossum::CLIConfigParser::parse_basic_cli_options(cli_parse_result));
  }

  const std::vector<int32_t> valid_scale_factors{1, 1000, 3000, 10000, 30000, 100000};

  const auto& find_result = std::find(valid_scale_factors.begin(), valid_scale_factors.end(), scale_factor);
  Assert(find_result != valid_scale_factors.end(),
         "TPC-DS benchmark only supports scale factor 1 (qualification only), 1000, 3000, 10000, 30000 and 100000.");

  auto context = opossum::BenchmarkRunner::create_context(*config);

  std::cout << "- TPC-DS scale factor is " << scale_factor << std::endl;

  std::string query_path = "third_party/tpcds-result-reproduction/query_qualification";
  std::string table_path = "resources/benchmark/tpcds/tables";

  Assert(std::filesystem::is_directory(query_path), "Query path (" + query_path + ") has to be a directory.");
  Assert(std::filesystem::is_directory(table_path), "Table path (" + table_path + ") has to be a directory.");
  Assert(std::filesystem::exists(std::filesystem::path{query_path + "/01.sql"}), "Queries have to be available.");
  Assert(std::filesystem::exists(std::filesystem::path{table_path + "/call_center.csv.json"}),
         "Table schemes have to be available.");

  if (!data_files_available(table_path)) {
    if (std::filesystem::exists(std::filesystem::path{binary_directory + "/dsdgen"})) {
      // const auto files_setup_return =
      //     system(("cd " + binary_directory + " && "
      //       ""
      //       " ./dsdgen -scale " + std::to_string(scale_factor) +
      //             " -dir ../resources/benchmark/tpcds/tables -terminate n -verbose -suffix .csv -f")
      //                .c_str());

      // Assert(files_setup_return == 0, "Generating table data files failed.");
      Fail("IN PROGRESS");
    } else {
      Fail("Could not find 'dsdgen' in your build directory. Did you run the benchmark from the project root dir?");
    }
  }

  Assert(data_files_available(table_path), "Generating table data files failed.");

  auto query_generator = std::make_unique<FileBasedBenchmarkItemRunner>(config, query_path, filename_blacklist());
  auto table_generator = std::make_unique<FileBasedTableGenerator>(config, table_path);
  auto benchmark_runner = BenchmarkRunner{*config, std::move(query_generator), std::move(table_generator), context};

  if (config->verify) {
    add_indices_to_sqlite("resources/benchmark/tpcds/schema.sql", "resources/benchmark/tpcds/create_indices.sql",
                          benchmark_runner);
  }

  std::cout << "done." << std::endl;

  benchmark_runner.run();
}

namespace {

bool data_files_available(const std::string& table_path) {
  for (const auto& table : {"call_center",
                            "catalog_page",
                            "catalog_returns",
                            "catalog_sales",
                            "customer_address",
                            "customer",
                            "customer_demographics",
                            "date_dim",
                            "household_demographics",
                            "income_band",
                            "inventory",
                            "item",
                            "promotion",
                            "reason",
                            "ship_mode",
                            "store",
                            "store_returns",
                            "store_sales",
                            "time_dim",
                            "warehouse",
                            "web_page",
                            "web_returns",
                            "web_sales",
                            "web_site"}) {
    if (!std::filesystem::exists(table_path + "/" + table + ".csv")) {
      return false;
    }
  }
  return true;
}

const std::unordered_set<std::string> filename_blacklist() {
  return std::unordered_set<std::string>{
      "01.sql", "02.sql", "03.sql", "04.sql", "05.sql", "06.sql", "08.sql",  "11.sql",  "12.sql",  "14a.sql", "14b.sql",
      "16.sql", "18.sql", "19.sql", "20.sql", "21.sql", "22.sql", "23a.sql", "23b.sql", "24a.sql", "24b.sql", "27.sql",
      "30.sql", "31.sql", "32.sql", "33.sql", "36.sql", "37.sql", "38.sql",  "39a.sql", "39b.sql", "40.sql",  "44.sql",
      "46.sql", "47.sql", "49.sql", "51.sql", "52.sql", "53.sql", "54.sql",  "55.sql",  "56.sql",  "57.sql",  "58.sql",
      "59.sql", "60.sql", "61.sql", "63.sql", "64.sql", "66.sql", "67.sql",  "68.sql",  "70.sql",  "71.sql",  "72.sql",
      "74.sql", "75.sql", "76.sql", "77.sql", "78.sql", "80.sql", "81.sql",  "82.sql",  "83.sql",  "84.sql",  "86.sql",
      "87.sql", "89.sql", "90.sql", "91.sql", "92.sql", "94.sql", "95.sql",  "97.sql",  "98.sql"};
}

}  // namespace
