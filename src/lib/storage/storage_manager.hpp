#pragma once

#include <map>
#include <memory>
#include <string>

#include "table.hpp"

namespace opossum {

class StorageManager {
 public:
  static StorageManager &get();

  void add_table(const std::string &name, std::shared_ptr<Table> tp);
  std::shared_ptr<Table> get_table(const std::string &name) const;
  void print(std::ostream &out = std::cout) const;

 protected:
  StorageManager() {}
  StorageManager(StorageManager const &) = delete;
  StorageManager(StorageManager &&) = delete;

  std::map<std::string, std::shared_ptr<Table>> _tables;
};
}  // namespace opossum