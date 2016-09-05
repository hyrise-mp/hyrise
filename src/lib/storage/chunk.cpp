#include <iomanip>
#include <memory>
#include <string>
#include <vector>

#include "chunk.hpp"
#include "value_column.hpp"

namespace opossum {

Chunk::Chunk() {}

Chunk::Chunk(const std::vector<std::string> &column_types) {
  for (auto &column_type : column_types) {
    add_column(column_type);
  }
}

void Chunk::add_column(std::string type) {
  if (DEBUG && _columns.size() > 0 && size() > 0) throw std::runtime_error("Cannot add a column to a non-empty Chunk");
  _columns.emplace_back(make_shared_by_column_type<BaseColumn, ValueColumn>(type));
}

void Chunk::add_column(std::shared_ptr<BaseColumn> column) { _columns.emplace_back(column); }

void Chunk::append(std::initializer_list<AllTypeVariant> values) {
  if (DEBUG && _columns.size() != values.size()) {
    throw std::runtime_error("append: number of columns (" + to_string(_columns.size()) +
                             ") does not match value list (" + to_string(values.size()) + ")");
  }

  auto column_it = _columns.begin();
  auto value_it = values.begin();
  for (; column_it != _columns.end(); column_it++, value_it++) {
    (*column_it)->append(*value_it);
  }
}

std::vector<int> Chunk::column_string_widths(int max) const {
  std::vector<int> widths(_columns.size());
  for (size_t col = 0; col < _columns.size(); ++col) {
    for (size_t row = 0; row < size(); ++row) {
      int width = to_string((*_columns[col])[row]).size();
      if (width > widths[col]) {
        if (width >= max) {
          widths[col] = max;
          break;
        }
        widths[col] = width;
      }
    }
  }
  return widths;
}

std::shared_ptr<BaseColumn> Chunk::get_column(size_t column_id) const { return _columns[column_id]; }

void Chunk::print(std::ostream &out, const std::vector<int> &widths_in) const {
  auto widths = widths_in.size() > 0 ? widths_in : column_string_widths(20);
  for (size_t row = 0; row < size(); ++row) {
    out << "|";
    for (size_t col = 0; col < _columns.size(); ++col) {
      out << std::setw(widths[col]) << (*_columns[col])[row] << "|" << std::setw(0);
    }
    out << std::endl;
  }
}

size_t Chunk::size() const {
  if (DEBUG && _columns.size() == 0) {
    throw std::runtime_error("Can't calculate size on table without columns");
  }
  return _columns.front()->size();
}
}  // namespace opossum