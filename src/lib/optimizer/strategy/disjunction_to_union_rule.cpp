#include "disjunction_to_union_rule.hpp"

#include <expression/expression_utils.hpp>

#include "expression/logical_expression.hpp"
#include "logical_query_plan/lqp_utils.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "logical_query_plan/union_node.hpp"

namespace opossum {

void DisjunctionToUnionRule::apply_to(const std::shared_ptr<AbstractLQPNode>& root) const {
  Assert(root->type == LQPNodeType::Root, "DisjunctionToUnionRule needs root to hold onto");

  /**
   * Step 1:
   *    - Collect PredicateNodes that can be split up into multiple ones into `predicate_nodes_to_flat_disjunctions`
   */
  auto predicate_nodes_to_flat_disjunctions =
      std::vector<std::pair<std::shared_ptr<PredicateNode>, std::vector<std::shared_ptr<AbstractExpression>>>>{};

  visit_lqp(root, [&](const auto& sub_node) {
    if (const auto predicate_node = std::dynamic_pointer_cast<PredicateNode>(sub_node)) {
      const auto flat_disjunction = flatten_logical_expressions(predicate_node->predicate(), LogicalOperator::Or);

      if (flat_disjunction.size() > 1) {
        predicate_nodes_to_flat_disjunctions.emplace_back(predicate_node, flat_disjunction);
      }
    }

    return LQPVisitation::VisitInputs;
  });

  /**
   * Step 2:
   *    - Split up qualifying PredicateNodes into n-1 consecutive UnionNodes and n PredicateNodes. We have to do this in
   *      a second pass because manipulating the LQP within `visit_lqp()`, while theoretically possible, is prone to
   *      bugs.
   */
  for (const auto& [predicate_node, flat_disjunction] : predicate_nodes_to_flat_disjunctions) {
    if (flat_disjunction.size() <= 1) {
      break;
    }

    auto previous_union_node = UnionNode::make(UnionMode::Positions);
    const auto left_input = predicate_node->left_input();
    lqp_replace_node(predicate_node, previous_union_node);
    previous_union_node->set_left_input(PredicateNode::make(flat_disjunction[0], left_input));
    previous_union_node->set_right_input(PredicateNode::make(flat_disjunction[1], left_input));

    for (auto disjunctionIdx = size_t{2}; disjunctionIdx < flat_disjunction.size(); ++disjunctionIdx) {
      const auto& predicate_expression = flat_disjunction[disjunctionIdx];
      auto next_union_node = UnionNode::make(UnionMode::Positions);
      lqp_insert_node(previous_union_node, LQPInputSide::Right, next_union_node);
      next_union_node->set_right_input(PredicateNode::make(predicate_expression, left_input));
      previous_union_node = next_union_node;
    }
  }
}

}  // namespace opossum