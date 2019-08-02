#include "duplicate_elimination_rule.hpp"

#include <map>
#include <queue>
#include <string>

#include "boost/functional/hash.hpp"
#include "expression/expression_utils.hpp"
#include "expression/lqp_column_expression.hpp"
#include "logical_query_plan/abstract_lqp_node.hpp"
#include "logical_query_plan/lqp_utils.hpp"

namespace opossum {

std::string DuplicateEliminationRule::name() const { return "Duplicate Elimination Rule"; }

void DuplicateEliminationRule::_print_traversal(const std::shared_ptr<AbstractLQPNode>& node) const {
  if(node){
    // if(true || node->type == LQPNodeType::StoredTable){
      std::cout<< node->description() << ", " << node << "\n";
    // }
    _print_traversal(node->left_input());
    _print_traversal(node->right_input());
  }
}

void DuplicateEliminationRule::apply_to(const std::shared_ptr<AbstractLQPNode>& node) const {
  std::cout << "DuplicateEliminationRule\n";
  _possible_replacement_mapping.clear();
  _sub_plans.clear();

  // PHASE 1 - identify where placements could be done, depth first traversal
  _create_possible_replacement_mapping(node);
  // PHASE 2 - replace sub-trees at the lowest level possible, bredth first traversal
  //         - build mapping structure 
  LQPNodeMapping node_mapping;
  // std::cout << "pre mapping size: " << node_mapping.size() << "\n";
  _replace_nodes_traversal(node, node_mapping);
  // std::cout << "post mapping size: " << node_mapping.size() << "\n";
  // PHASE 3 - correct the references for all lqp column expressions of nodes which
  //           were not replaces.

  std::cout << *node << "\n";

  // const auto node_mapping = lqp_create_node_mapping(duplicate_afflicted_lqp, node);

  // std::cout << "\n### mapping:\n";
  // for(const auto& [orig, repl] : node_mapping){
  //   std::cout << orig << " -> " << repl << "\n";
  // }

  if(!_possible_replacement_mapping.empty()){
    _adapt_expressions_traversal(node, node_mapping);
  }


}

// Alias [ca_county, d_year, (SUM(ws_ext_sales_price) * 1) / SUM(ws_ext_sales_price) AS web_q1_q2_increase, (SUM(ss_ext_sales_price) * 1) / SUM(ss_ext_sales_price) AS store_q1_q2_increase, (SUM(ws_ext_sales_price) * 1) / SUM(ws_ext_sales_price) AS web_q2_q3_increase, (SUM(ss_ext_sales_price) * 1) / SUM(ss_ext_sales_price) AS store_q2_q3_increase]
// rhs:
// Alias [ca_county, d_year, (SUM(ws_ext_sales_price) * 1) / SUM(ws_ext_sales_price) AS web_q1_q2_increase, (SUM(ss_ext_sales_price) * 1) / SUM(ss_ext_sales_price) AS store_q1_q2_increase, (SUM(ws_ext_sales_price) * 1) / SUM(ws_ext_sales_price) AS web_q2_q3_increase, (SUM(ss_ext_sales_price) * 1) / SUM(ss_ext_sales_price) AS store_q2_q3_increase]


// Alias [ca_county, d_year, (SUM(ws_ext_sales_price) * 1) / SUM(ws_ext_sales_price) AS web_q1_q2_increase, (SUM(ss_ext_sales_price) * 1) / SUM(ss_ext_sales_price) AS store_q1_q2_increase, (SUM(ws_ext_sales_price) * 1) / SUM(ws_ext_sales_price) AS web_q2_q3_increase, (SUM(ss_ext_sales_price) * 1) / SUM(ss_ext_sales_price) AS store_q2_q3_increase]
// rhs:
// Alias [ca_county, d_year, (SUM(ws_ext_sales_price) * 1) / SUM(ws_ext_sales_price) AS web_q1_q2_increase, (SUM(ss_ext_sales_price) * 1) / SUM(ss_ext_sales_price) AS store_q1_q2_increase, (SUM(ws_ext_sales_price) * 1) / SUM(ws_ext_sales_price) AS web_q2_q3_increase, (SUM(ss_ext_sales_price) * 1) / SUM(ss_ext_sales_price) AS store_q2_q3_increase]


void DuplicateEliminationRule::_create_possible_replacement_mapping(
    const std::shared_ptr<AbstractLQPNode>& node) const {
  const auto left_node = node->left_input();
  const auto right_node = node->right_input();

  if (left_node) {
    _create_possible_replacement_mapping(left_node);
  }
  if (right_node) {
    _create_possible_replacement_mapping(right_node);
  }

  if(node->type == LQPNodeType::StoredTable || node->type == LQPNodeType::Validate){
    return;
  }

  const auto duplicate_iter = std::find_if(_sub_plans.cbegin(), _sub_plans.cend(),
                               [&node](const auto& sub_plan) { return *node == *sub_plan; });
  // std::cout << "processing " << node->description() << ", " << node << "\n";
  if (duplicate_iter == _sub_plans.end()) {
    _sub_plans.emplace_back(node);
    // std::cout << "...stored in sub plans\n";
  } else {
    _possible_replacement_mapping.insert({node, *duplicate_iter});
    // std::cout << "... found duplicate: " << node->description() << ", " << *duplicate_iter << "\n";
  }
}

void DuplicateEliminationRule::_replace_nodes_traversal(
        const std::shared_ptr<AbstractLQPNode>& start_node, LQPNodeMapping& node_mapping) const {
  std::queue<std::shared_ptr<AbstractLQPNode>> queue{};
  std::unordered_set<std::shared_ptr<AbstractLQPNode>> visited{};

  queue.emplace(start_node);

  while(!queue.empty()){
    // traversal management
    const auto current_node = queue.front();
    queue.pop();

    const auto visited_iter = visited.find(current_node);
    if (visited_iter != visited.end()) {
      continue;
    }
    visited.emplace(current_node);

    const auto replacement_iter = _possible_replacement_mapping.find(current_node);
    if(replacement_iter != _possible_replacement_mapping.end()){
      node_mapping = lqp_create_node_mapping(current_node, replacement_iter->second, std::move(node_mapping));
      // std::cout << "mapping size increased: " << node_mapping.size() << "\n";
      for (const auto& output : current_node->outputs()) {
        const auto& input_side = current_node->get_input_side(output);
        output->set_input(input_side, replacement_iter->second);
      }
    } else {
      if(current_node->left_input()){
      queue.emplace(current_node->left_input());
    }
      if(current_node->right_input()){
        queue.emplace(current_node->right_input());
      }
    }
  }
}



void DuplicateEliminationRule::_adapt_expressions_traversal(
  const std::shared_ptr<AbstractLQPNode>& node, const LQPNodeMapping& node_mapping) const {
  if(node){
    for(auto& expression : node->node_expressions){
      expression_adapt_to_different_lqp(expression, node_mapping);
    }
    _adapt_expressions_traversal(node->left_input(), node_mapping);
    _adapt_expressions_traversal(node->right_input(),node_mapping);
  }
}

}  // namespace opossum
