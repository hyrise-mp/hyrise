#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../base_test.hpp"
#include "SQLParser.h"
#include "gtest/gtest.h"

#include "operators/abstract_operator.hpp"
#include "optimizer/abstract_syntax_tree/ast_to_operator_translator.hpp"
#include "scheduler/operator_task.hpp"
#include "sql/sql_to_ast_translator.hpp"
#include "storage/storage_manager.hpp"

namespace opossum {

class TPCHTest : public BaseTest {
 protected:
  void SetUp() override {
    std::shared_ptr<Table> customer = load_table("src/test/tables/tpch/customer.tbl", 2);
    std::shared_ptr<Table> lineitem = load_table("src/test/tables/tpch/lineitem.tbl", 2);
    std::shared_ptr<Table> nation = load_table("src/test/tables/tpch/nation.tbl", 2);
    std::shared_ptr<Table> orders = load_table("src/test/tables/tpch/orders.tbl", 2);
    std::shared_ptr<Table> part = load_table("src/test/tables/tpch/part.tbl", 2);
    std::shared_ptr<Table> partsupplier = load_table("src/test/tables/tpch/partsupplier.tbl", 2);
    std::shared_ptr<Table> region = load_table("src/test/tables/tpch/region.tbl", 2);
    std::shared_ptr<Table> supplier = load_table("src/test/tables/tpch/supplier.tbl", 2);
    StorageManager::get().add_table("customer", std::move(customer));
    StorageManager::get().add_table("lineitem", std::move(lineitem));
    StorageManager::get().add_table("nation", std::move(nation));
    StorageManager::get().add_table("orders", std::move(orders));
    StorageManager::get().add_table("part", std::move(part));
    StorageManager::get().add_table("partsupp", std::move(partsupplier));
    StorageManager::get().add_table("region", std::move(region));
    StorageManager::get().add_table("supplier", std::move(supplier));
  }

  std::shared_ptr<AbstractOperator> translate_query_to_operator(const std::string query) {
    hsql::SQLParserResult parse_result;
    hsql::SQLParser::parseSQLString(query, &parse_result);

    if (!parse_result.isValid()) {
      std::cout << parse_result.errorMsg() << std::endl;
      std::cout << "ErrorLine: " << parse_result.errorLine() << std::endl;
      std::cout << "ErrorColumn: " << parse_result.errorColumn() << std::endl;
      throw std::runtime_error("Query is not valid.");
    }

    auto result_node = SQLToASTTranslator::get().translate_parse_result(parse_result)[0];
    return ASTToOperatorTranslator::get().translate_node(result_node);
  }

  std::shared_ptr<OperatorTask> schedule_query_and_return_task(const std::string query) {
    auto result_operator = translate_query_to_operator(query);
    auto tasks = OperatorTask::make_tasks_from_operator(result_operator);
    for (auto& task : tasks) {
      task->schedule();
    }
    return tasks.back();
  }

  void execute_and_check(const std::string query, std::shared_ptr<Table> expected_result,
                         bool order_sensitive = false) {
    auto result_task = schedule_query_and_return_task(query);
    EXPECT_TABLE_EQ(result_task->get_operator()->get_output(), expected_result, order_sensitive);
  }
};

TEST_F(TPCHTest, TPCH1) {
  /**
   * Original:
   *
   * SELECT l_returnflag, l_linestatus, sum(l_quantity) as sum_qty, sum(l_extendedprice) as sum_base_price,
   * sum(l_extendedprice*(1-l_discount)) as sum_disc_price, sum(l_extendedprice*(1-l_discount)*(1+l_tax)) as sum_charge,
   * avg(l_quantity) as avg_qty, avg(l_extendedprice) as avg_price, avg(l_discount) as avg_disc, count(*) as count_order
   * FROM lineitem
   * WHERE l_shipdate <= date '1998-12-01' - interval '[DELTA]' day (3)
   * GROUP BY l_returnflag, l_linestatus
   * ORDER BY l_returnflag, l_linestatus
   *
   * Changes:
   *  1. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation
   *  2. implicit type conversions for arithmetic operations are not supported
   *    a. changed 1 to 1.0 explicitly
   */
  const auto query =
      R"(SELECT l_returnflag, l_linestatus, SUM(l_quantity) as sum_qty, SUM(l_extendedprice) as sum_base_price,
      SUM(l_extendedprice*(1.0-l_discount)) as sum_disc_price,
      SUM(l_extendedprice*(1.0-l_discount)*(1.0+l_tax)) as sum_charge, AVG(l_quantity) as avg_qty,
      AVG(l_extendedprice) as avg_price, AVG(l_discount) as avg_disc, COUNT(*) as count_order
      FROM lineitem
      WHERE l_shipdate <= '1998-12-01'
      GROUP BY l_returnflag, l_linestatus
      ORDER BY l_returnflag, l_linestatus;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch1.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Subselects in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH2) {
  /**
   * Original:
   *
   * SELECT s_acctbal, s_name, n_name, p_partkey, p_mfgr, s_address, s_phone, s_comment
   * FROM part, supplier, partsupp, nation, region
   * WHERE
   *    p_partkey = ps_partkey
   *    AND s_suppkey = ps_suppkey
   *    AND p_size = [SIZE]
   *    AND p_type like '%[TYPE]'
   *    AND s_nationkey = n_nationkey
   *    AND n_regionkey = r_regionkey
   *    AND r_name = '[REGION]'
   *    AND ps_supplycost = (
   *        SELECT min(ps_supplycost)
   *        FROM partsupp, supplier, nation, region
   *        WHERE
   *            p_partkey = ps_partkey
   *            AND s_suppkey = ps_suppkey
   *            AND s_nationkey = n_nationkey
   *            AND n_regionkey = r_regionkey
   *            AND r_name = '[REGION]'
   *        )
   * ORDER BY s_acctbal DESC, n_name, s_name, p_partkey;
   *
   * Changes:
   *  1. Random values are hardcoded
   */
  const auto query =
      R"(SELECT s_acctbal, s_name, n_name, p_partkey, p_mfgr, s_address, s_phone, s_comment
       FROM "part", supplier, partsupp, nation, region
       WHERE p_partkey = ps_partkey AND s_suppkey = ps_suppkey AND p_size = 15 AND p_type like '%BRASS' AND
       s_nationkey = n_nationkey AND n_regionkey = r_regionkey AND r_name = 'EUROPE' AND
       ps_supplycost = (SELECT min(ps_supplycost) FROM partsupp, supplier, nation, region
       WHERE p_partkey = ps_partkey AND s_suppkey = ps_suppkey AND s_nationkey = n_nationkey
       AND n_regionkey = r_regionkey AND r_name = 'EUROPE') ORDER BY s_acctbal DESC, n_name, s_name, p_partkey;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch2.tbl", 2);
  execute_and_check(query, expected_result, true);
}

TEST_F(TPCHTest, TPCH3) {
  /**
   * Original:
   *
   * SELECT l_orderkey, sum(l_extendedprice*(1-l_discount)) as revenue, o_orderdate, o_shippriority
   * FROM customer, orders, lineitem
   * WHERE c_mktsegment = '[SEGMENT]' AND c_custkey = o_custkey AND l_orderkey = o_orderkey
   * AND o_orderdate < date '[DATE]' AND l_shipdate > date '[DATE]'
   * GROUP BY l_orderkey, o_orderdate, o_shippriority
   * ORDER BY revenue DESC, o_orderdate;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. implicit type conversions for arithmetic operations are not supported
   *    a. changed 1 to 1.0 explicitly
   *  3. Be aware that we ignore the column ordering here
   */
  const auto query =
      R"(SELECT l_orderkey, SUM(l_extendedprice*(1.0-l_discount)) as revenue, o_orderdate, o_shippriority
      FROM customer, orders, lineitem
      WHERE c_mktsegment = 'BUILDING' AND c_custkey = o_custkey AND l_orderkey = o_orderkey
      AND o_orderdate < '1995-03-15' AND l_shipdate > '1995-03-15'
      GROUP BY l_orderkey, o_orderdate, o_shippriority
      ORDER BY revenue DESC, o_orderdate;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch3.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Exists and Subselects in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH4) {
  /**
   * Original:
   *
   * SELECT
   *    o_orderpriority,
   *    count(*) as order_count
   * FROM orders
   * WHERE
   *    o_orderdate >= date '[DATE]'
   *    AND o_orderdate < date '[DATE]' + interval '3' month
   *    AND exists (
   *        SELECT *
   *        FROM lineitem
   *        WHERE
   *            l_orderkey = o_orderkey
   *            AND l_commitdate < l_receiptdate
   *        )
   * GROUP BY o_orderpriority
   * ORDER BY o_orderpriority;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation
   */
  const auto query =
      R"(SELECT o_orderpriority, count(*) as order_count FROM orders WHERE o_orderdate >= '1993-07-01' AND
      o_orderdate < '1993-10-01' AND exists (
      SELECT *FROM lineitem WHERE l_orderkey = o_orderkey AND l_commitdate < l_receiptdate)
      GROUP BY o_orderpriority ORDER BY o_orderpriority;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch4.tbl", 2);
  execute_and_check(query, expected_result, true);
}

TEST_F(TPCHTest, TPCH5) {
  /**
   * Original:
   *
   * SELECT n_name, sum(l_extendedprice * (1 - l_discount)) as revenue
   * FROM customer, orders, lineitem, supplier, nation, region
   * WHERE c_custkey = o_custkey AND l_orderkey = o_orderkey AND l_suppkey = s_suppkey AND c_nationkey = s_nationkey
   * AND s_nationkey = n_nationkey AND n_regionkey = r_regionkey AND r_name = '[REGION]' AND o_orderdate >= date
   * '[DATE]'
   * AND o_orderdate < date '[DATE]' + interval '1' year
   * GROUP BY n_name
   * ORDER BY revenue DESC;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation
   *  3. implicit type conversions for arithmetic operations are not supported
   *    a. changed 1 to 1.0 explicitly
   */
  const auto query =
      R"(SELECT n_name, SUM(l_extendedprice * (1.0 - l_discount)) as revenue
      FROM customer, orders, lineitem, supplier, nation, region
      WHERE c_custkey = o_custkey AND l_orderkey = o_orderkey AND l_suppkey = s_suppkey AND c_nationkey = s_nationkey
      AND s_nationkey = n_nationkey AND n_regionkey = r_regionkey AND r_name = 'ASIA' AND o_orderdate >= '1994-01-01'
      AND o_orderdate < '1995-01-01'
      GROUP BY n_name
      ORDER BY revenue DESC;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch5.tbl", 2);
  execute_and_check(query, expected_result, true);
}

TEST_F(TPCHTest, TPCH6) {
  /**
   * Original:
   *
   * SELECT SUM(L_EXTENDEDPRICE*L_DISCOUNT) AS REVENUE
   * FROM LINEITEM
   * WHERE L_SHIPDATE >= '1994-01-01' AND L_SHIPDATE < dateadd(yy, 1, cast('1994-01-01' as datetime))
   * AND L_DISCOUNT BETWEEN .06 - 0.01 AND .06 + 0.01 AND L_QUANTITY < 24
   *
   * Changes:
   *  1. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation
   *  2. arithmetic expressions with constants are not resolved automatically yet, so pre-calculate them as well
   *  3. 'SUM' must be upper-case so far
   */
  const auto query =
      R"(SELECT SUM(l_extendedprice*l_discount) AS REVENUE
      FROM lineitem
      WHERE l_shipdate >= '1994-01-01' AND l_shipdate < '1995-01-01'
      AND l_discount BETWEEN .05 AND .07 AND l_quantity < 24;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch6.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once OR is supported in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH7) {
  /**
   * Original:
   *
   * SELECT supp_nation, cust_nation, l_year, sum(volume) as revenue
   * FROM (
   *   SELECT
   *       n1.n_name as supp_nation,
   *       n2.n_name as cust_nation,
   *       extract(year FROM l_shipdate) as l_year,
   *       l_extendedprice * (1 - l_discount) as volume
   *   FROM supplier, lineitem, orders, customer, nation n1, nation n2
   *   WHERE
   *       s_suppkey = l_suppkey
   *       AND o_orderkey = l_orderkey
   *       AND c_custkey = o_custkey
   *       AND s_nationkey = n1.n_nationkey
   *       AND c_nationkey = n2.n_nationkey
   *       AND (
   *           (n1.n_name = '[NATION1]' AND n2.n_name = '[NATION2]')
   *           or
   *           (n1.n_name = '[NATION2]' AND n2.n_name = '[NATION1]'))
   *       AND l_shipdate between date '1995-01-01' AND date '1996-12-31'
   *   ) as shipping
   * GROUP BY supp_nation, cust_nation, l_year
   * ORDER BY supp_nation, cust_nation, l_year;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation
   *  3. Extract is not supported
   *    a. Use full date instead
   */
  const auto query =
      R"(SELECT supp_nation, cust_nation, l_year, SUM(volume) as revenue FROM (SELECT n1.n_name as supp_nation,
      n2.n_name as cust_nation, l_shipdate as l_year, l_extendedprice * (1 - l_discount) as volume
      FROM supplier, lineitem, orders, customer, nation n1, nation n2 WHERE s_suppkey = l_suppkey AND
      o_orderkey = l_orderkey AND c_custkey = o_custkey AND s_nationkey = n1.n_nationkey AND
      c_nationkey = n2.n_nationkey AND ((n1.n_name = 'GERMANY' AND n2.n_name = 'FRANCE') or
      (n1.n_name = 'FRANCE' AND n2.n_name = 'GERMANY')) AND l_shipdate between '1995-01-01' AND
      '1996-12-31') as shipping GROUP BY supp_nation, cust_nation, l_year
      ORDER BY supp_nation, cust_nation, l_year;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch7.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once CASE and arithmetic operations of Aggregations are supported
TEST_F(TPCHTest, DISABLED_TPCH8) {
  /**
   * Original:
   *
   * SELECT o_year,
   *      sum(case
   *              when nation = '[NATION]'
   *              then volume
   *              else 0
   *           end)
   *           / sum(volume) as mkt_share
   * FROM (
   *      SELECT
   *          extract(year FROM o_orderdate) as o_year,
   *          l_extendedprice * (1-l_discount) as volume,
   *          n2.n_name as nation
   *      FROM
   *          part,
   *          supplier,
   *          lineitem,
   *          orders,
   *          customer,
   *          nation n1,
   *          nation n2,
   *          region
   *      WHERE
   *          p_partkey = l_partkey
   *          AND s_suppkey = l_suppkey
   *          AND l_orderkey = o_orderkey
   *          AND o_custkey = c_custkey
   *          AND c_nationkey = n1.n_nationkey
   *          AND n1.n_regionkey = r_regionkey
   *          AND r_name = '[REGION]'
   *          AND s_nationkey = n2.n_nationkey
   *          AND o_orderdate between date '1995-01-01' AND date '1996-12-31'
   *          AND p_type = '[TYPE]'
   *      ) as all_nations
   * GROUP BY o_year
   * ORDER BY o_year;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *  3. Extract is not supported
   *    a. Use full date instead
   */
  const auto query =
      R"(SELECT o_year, SUM(case when nation = 'BRAZIL' then volume else 0 end) / SUM(volume) as mkt_share
      FROM (SELECT o_orderdate as o_year, l_extendedprice * (1-l_discount) as volume,
      n2.n_name as nation FROM "part", supplier, lineitem, orders, customer, nation n1, nation n2, region
      WHERE p_partkey = l_partkey AND s_suppkey = l_suppkey AND l_orderkey = o_orderkey AND
      o_custkey = c_custkey AND c_nationkey = n1.n_nationkey AND n1.n_regionkey = r_regionkey AND
      r_name = 'AMERICA' AND s_nationkey = n2.n_nationkey AND o_orderdate between '1995-01-01'
      AND '1996-12-31' AND p_type = 'ECONOMY ANODIZED STEEL') as all_nations GROUP BY o_year ORDER BY o_year;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch8.tbl", 2);
  execute_and_check(query, expected_result, true);
}

TEST_F(TPCHTest, TPCH9) {
  /**
   * Original:
   *
   * SELECT
   *    nation,
   *    o_year,
   *    sum(amount) as sum_profit
   * FROM (
   *    SELECT
   *        n_name as nation,
   *        extract(year FROM o_orderdate) as o_year,
   *        l_extendedprice * (1 - l_discount) - ps_supplycost * l_quantity as amount
   *    FROM part, supplier, lineitem, partsupp, orders, nation
   *    WHERE
   *        s_suppkey = l_suppkey
   *        AND ps_suppkey = l_suppkey
   *        AND ps_partkey = l_partkey
   *        AND p_partkey = l_partkey
   *        AND o_orderkey = l_orderkey
   *        AND s_nationkey = n_nationkey
   *        AND p_name like '%[COLOR]%'
   *    ) as profit
   * GROUP BY nation, o_year
   * ORDER BY nation, o_year DESC;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. Extract is not supported
   *    a. Use full date instead
   *  3. implicit type conversions for arithmetic operations are not supported
   *    a. changed 1 to 1.0 explicitly
   */
  const auto query =
      R"(SELECT nation, o_year, SUM(amount) as sum_profit FROM (SELECT n_name as nation, o_orderdate as o_year,
      l_extendedprice * (1.0 - l_discount) - ps_supplycost * l_quantity as amount
      FROM "part", supplier, lineitem, partsupp, orders, nation WHERE s_suppkey = l_suppkey
      AND ps_suppkey = l_suppkey AND ps_partkey = l_partkey AND p_partkey = l_partkey AND o_orderkey = l_orderkey
      AND s_nationkey = n_nationkey AND p_name like '%green%') as profit
      GROUP BY nation, o_year ORDER BY nation, o_year DESC;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch9.tbl", 2);
  execute_and_check(query, expected_result, true);
}

TEST_F(TPCHTest, TPCH10) {
  /**
   * Original:
   *
   * SELECT c_custkey, c_name, sum(l_extendedprice * (1 - l_discount)) as revenue, c_acctbal, n_name, c_address,
   * c_phone, c_comment
   * FROM customer, orders, lineitem, nation
   * WHERE c_custkey = o_custkey AND l_orderkey = o_orderkey AND o_orderdate >= date '[DATE]'
   * AND o_orderdate < date '[DATE]' + interval '3' month AND l_returnflag = 'R' AND c_nationkey = n_nationkey
   * GROUP BY c_custkey, c_name, c_acctbal, c_phone, n_name, c_address, c_comment
   * ORDER BY revenue DESC;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation
   *  3. implicit type conversions for arithmetic operations are not supported
   *    a. changed 1 to 1.0 explicitly
   *  4. Be aware that we ignore the column ordering here
   */
  const auto query =
      R"(SELECT c_custkey, c_name, SUM(l_extendedprice * (1.0 - l_discount)) as revenue, c_acctbal, n_name, c_address,
      c_phone, c_comment
      FROM customer, orders, lineitem, nation
      WHERE c_custkey = o_custkey AND l_orderkey = o_orderkey AND o_orderdate >= '1993-10-01'
      AND o_orderdate < '1994-01-01' AND l_returnflag = 'R' AND c_nationkey = n_nationkey
      GROUP BY c_custkey, c_name, c_acctbal, c_phone, n_name, c_address, c_comment
      ORDER BY revenue DESC;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch10.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Subselects in Having clause
TEST_F(TPCHTest, DISABLED_TPCH11) {
  /**
   * Original:
   *
   * SELECT
   *    ps_partkey,
   *    sum(ps_supplycost * ps_availqty) as value
   * FROM partsupp, supplier, nation
   * WHERE
   *    ps_suppkey = s_suppkey
   *    AND s_nationkey = n_nationkey
   *    AND n_name = '[NATION]'
   * GROUP BY ps_partkey
   * having sum(ps_supplycost * ps_availqty) > (
   *    SELECT sum(ps_supplycost * ps_availqty) * [FRACTION]
   *    FROM partsupp, supplier, nation
   *    WHERE
   *        ps_suppkey = s_suppkey
   *        AND s_nationkey = n_nationkey
   *        AND n_name = '[NATION]'
   *    )
   * ORDER BY value DESC;
   *
   * Changes:
   *  1. Random values are hardcoded

   */
  const auto query =
      R"(SELECT ps_partkey, SUM(ps_supplycost * ps_availqty) as value FROM partsupp, supplier, nation
      WHERE ps_suppkey = s_suppkey AND s_nationkey = n_nationkey AND n_name = 'GERMANY'
      GROUP BY ps_partkey having SUM(ps_supplycost * ps_availqty) > (
      SELECT SUM(ps_supplycost * ps_availqty) * 0.0001 FROM partsupp, supplier, nation
      WHERE ps_suppkey = s_suppkey AND s_nationkey = n_nationkey AND n_name = 'GERMANY') ORDER BY value DESC;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch11.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support IN
TEST_F(TPCHTest, DISABLED_TPCH12) {
  /**
   * Original:
   *
   * SELECT
   *    l_shipmode,
   *    sum(case
   *            when o_orderpriority ='1-URGENT' or o_orderpriority ='2-HIGH'
   *            then 1
   *            else 0
   *        end) as high_line_count,
   *    sum(case
   *            when o_orderpriority <> '1-URGENT' AND o_orderpriority <> '2-HIGH'
   *            then 1
   *            else 0
   *        end) as low_line_count
   * FROM orders, lineitem
   * WHERE
   *    o_orderkey = l_orderkey
   *    AND l_shipmode in ('[SHIPMODE1]', '[SHIPMODE2]')
   *    AND l_commitdate < l_receiptdate
   *    AND l_shipdate < l_commitdate
   *    AND l_receiptdate >= date '[DATE]'
   *    AND l_receiptdate < date '[DATE]' + interval '1' year
   * GROUP BY l_shipmode
   * ORDER BY l_shipmode;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation

   */
  const auto query =
      R"(SELECT l_shipmode, SUM(case when o_orderpriority ='1-URGENT' or o_orderpriority ='2-HIGH' then 1 else 0 end)
      as high_line_count, SUM(case when o_orderpriority <> '1-URGENT' AND
      o_orderpriority <> '2-HIGH' then 1 else 0 end) as low_line_count FROM orders, lineitem
      WHERE o_orderkey = l_orderkey AND l_shipmode IN ('MAIL','SHIP') AND l_commitdate < l_receiptdate
      AND l_shipdate < l_commitdate AND l_receiptdate >= '1994-01-01' AND
      l_receiptdate < '1995-01-01' GROUP BY l_shipmode ORDER BY l_shipmode;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch12.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support nested expressions in Join Condition
TEST_F(TPCHTest, DISABLED_TPCH13) {
  /**
   * Original:
   *
   * SELECT c_count, count(*) as custdist
   * FROM (
   *    SELECT c_custkey, count(o_orderkey)
   *    FROM customer left outer join orders
   *    on
   *        c_custkey = o_custkey
   *        AND o_comment not like '%special%requests%'
   *    GROUP BY c_custkey
   *    ) as c_orders (c_custkey, c_count)
   * GROUP BY c_count
   * ORDER BY custdist DESC, c_count DESC;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. Variable binding in alias not supported by SQLParser
   *    a. removed it
   *  3. 'COUNT' must be upper-case for now
   */
  const auto query =
      R"(SELECT c_count, COUNT(*) as custdist FROM (SELECT c_custkey, count(o_orderkey)
      FROM customer left outer join orders on c_custkey = o_custkey AND o_comment not like '%[WORD1]%[WORD2]%'
      GROUP BY c_custkey) as c_orders GROUP BY c_count ORDER BY custdist DESC, c_count DESC;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch13.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Case
TEST_F(TPCHTest, DISABLED_TPCH14) {
  /**
   * Original:
   *
   * SELECT 100.00 * sum(case
   *                        when p_type like 'PROMO%'
   *                        then l_extendedprice*(1-l_discount)
   *                        else 0
   *                     end) / sum(l_extendedprice * (1 - l_discount)) as promo_revenue
   * FROM lineitem, part
   * WHERE
   *    l_partkey = p_partkey
   *    AND l_shipdate >= date '[DATE]'
   *    AND l_shipdate < date '[DATE]' + interval '1' month;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation
   *  3. implicit type conversions for arithmetic operations are not supported
   *    a. changed 1 to 1.0 explicitly
   */
  const auto query =
      R"(SELECT 100.00 * SUM(case when p_type like 'PROMO%' then l_extendedprice*(1.0-l_discount) else 0 end)
      / SUM(l_extendedprice * (1.0 - l_discount)) as promo_revenue FROM lineitem, "part" WHERE l_partkey = p_partkey
      AND l_shipdate >= '1995-09-01' AND l_shipdate < '1995-10-01';)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch14.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// We do not support Views yet
TEST_F(TPCHTest, DISABLED_TPCH15) {
  /**
   * Original:
   *
   * create view revenue[STREAM_ID] (supplier_no, total_revenue) as
   *    SELECT l_suppkey, sum(l_extendedprice * (1 - l_discount))
   *    FROM lineitem
   *    WHERE
   *        l_shipdate >= date '[DATE]'
   *        AND l_shipdate < date '[DATE]' + interval '3' month
   *    GROUP BY l_suppkey;
   *
   * SELECT s_suppkey, s_name, s_address, s_phone, total_revenue
   * FROM supplier, revenue[STREAM_ID]
   * WHERE
   *    s_suppkey = supplier_no
   *    AND total_revenue = (
   *        SELECT max(total_revenue)
   *        FROM revenue[STREAM_ID]
   *    )
   * ORDER BY s_suppkey;
   *
   * drop view revenue[STREAM_ID];
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation
   */
  const auto query =
      R"(create view revenue[STREAM_ID] (supplier_no, total_revenue) as SELECT l_suppkey,
      SUM(l_extendedprice * (1.0 - l_discount)) FROM lineitem WHERE l_shipdate >= date '[DATE]'
      AND l_shipdate < date '[DATE]' + interval '3' month GROUP BY l_suppkey;

      SELECT s_suppkey, s_name, s_address, s_phone, total_revenue FROM supplier, revenue[STREAM_ID]
      WHERE s_suppkey = supplier_no AND total_revenue = (SELECT max(total_revenue)
      FROM revenue[STREAM_ID]) ORDER BY s_suppkey;

      drop view revenue[STREAM_ID];)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch15.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Subselects in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH16) {
  /**
   * Original:
   *
   * SELECT p_brand, p_type, p_size, count(distinct ps_suppkey) as supplier_cnt
   * FROM partsupp, part
   * WHERE
   *    p_partkey = ps_partkey
   *    AND p_brand <> '[BRAND]'
   *    AND p_type not like '[TYPE]%'
   *    AND p_size in ([SIZE1], [SIZE2], [SIZE3], [SIZE4], [SIZE5], [SIZE6], [SIZE7], [SIZE8])
   *    AND ps_suppkey not in (
   *        SELECT s_suppkey
   *        FROM supplier
   *        WHERE s_comment like '%Customer%Complaints%'
   *    )
   * GROUP BY p_brand, p_type, p_size
   * ORDER BY supplier_cnt DESC, p_brand, p_type, p_size;
   *
   * Changes:
   *  1. Random values are hardcoded

   */
  const auto query =
      R"(SELECT p_brand, p_type, p_size, count(distinct ps_suppkey) as supplier_cnt
      FROM partsupp, "part" WHERE p_partkey = ps_partkey AND p_brand <> 'Brand#45' AND p_type not like 'MEDIUM POLISHED%'
      AND p_size in (49, 14, 23, 45, 19, 3, 36, 9)
      AND ps_suppkey not in (SELECT s_suppkey FROM supplier WHERE s_comment like '%Customer%Complaints%')
      GROUP BY p_brand, p_type, p_size ORDER BY supplier_cnt DESC, p_brand, p_type, p_size;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch16.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Subselect in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH17) {
  /**
   * Original:
   *
   * SELECT sum(l_extendedprice) / 7.0 as avg_yearly
   * FROM lineitem, part
   * WHERE
   *    p_partkey = l_partkey
   *    AND p_brand = '[BRAND]'
   *    AND p_container = '[CONTAINER]'
   *    AND l_quantity < (
   *        SELECT 0.2 * avg(l_quantity)
   *        FROM lineitem
   *        WHERE l_partkey = p_partkey
   *    );
   *
   * Changes:
   *  1. Random values are hardcoded

   */
  const auto query =
      R"(SELECT SUM(l_extendedprice) / 7.0 as avg_yearly FROM lineitem, \"part\" WHERE p_partkey = l_partkey
      AND p_brand = 'Brand#23' AND p_container = 'MED BOX' AND l_quantity < (SELECT 0.2 * avg(l_quantity)
      FROM lineitem WHERE l_partkey = p_partkey);)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch17.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Subselects in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH18) {
  /**
   * Original:
   *
   * SELECT c_name, c_custkey, o_orderkey, o_orderdate, o_totalprice, sum(l_quantity)
   * FROM customer, orders, lineitem
   * WHERE
   *    o_orderkey in (
   *        SELECT l_orderkey
   *        FROM lineitem
   *        GROUP BY l_orderkey
   *        having sum(l_quantity) > [QUANTITY]
   *    )
   *    AND c_custkey = o_custkey
   *    AND o_orderkey = l_orderkey
   * GROUP BY c_name, c_custkey, o_orderkey, o_orderdate, o_totalprice
   * ORDER BY o_totalprice DESC, o_orderdate;
   *
   * Changes:
   *  1. Random values are hardcoded

   */
  const auto query =
      R"(SELECT c_name, c_custkey, o_orderkey, o_orderdate, o_totalprice, SUM(l_quantity)
      FROM customer, orders, lineitem WHERE o_orderkey in (SELECT l_orderkey FROM lineitem
      GROUP BY l_orderkey having SUM(l_quantity) > 300) AND c_custkey = o_custkey AND o_orderkey = l_orderkey
      GROUP BY c_name, c_custkey, o_orderkey, o_orderdate, o_totalprice ORDER BY o_totalprice DESC, o_orderdate;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch18.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support OR in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH19) {
  /**
   * Original:
   *
   * SELECT sum(l_extendedprice * (1 - l_discount) ) as revenue
   * FROM lineitem, part
   * WHERE (
   *        p_partkey = l_partkey
   *        AND p_brand = '[BRAND1]'
   *        AND p_container in ( 'SM CASE', 'SM BOX', 'SM PACK', 'SM PKG')
   *        AND l_quantity >= [QUANTITY1]
   *        AND l_quantity <= [QUANTITY1] + 10
   *        AND p_size between 1 AND 5
   *        AND l_shipmode in ('AIR', 'AIR REG')
   *        AND l_shipinstruct = 'DELIVER IN PERSON'
   *    ) or (
   *        p_partkey = l_partkey
   *        AND p_brand = '[BRAND2]'
   *        AND p_container in ('MED BAG', 'MED BOX', 'MED PKG', 'MED PACK')
   *        AND l_quantity >= [QUANTITY2]
   *        AND l_quantity <= [QUANTITY2] + 10
   *        AND p_size between 1 AND 10
   *        AND l_shipmode in ('AIR', 'AIR REG')
   *        AND l_shipinstruct = 'DELIVER IN PERSON'
   *    ) or (
   *        p_partkey = l_partkey
   *        AND p_brand = '[BRAND3]'
   *        AND p_container in ( 'LG CASE', 'LG BOX', 'LG PACK', 'LG PKG')
   *        AND l_quantity >= [QUANTITY3]
   *        AND l_quantity <= [QUANTITY3] + 10
   *        AND p_size between 1 AND 15
   *        AND l_shipmode in ('AIR', 'AIR REG')
   *        AND l_shipinstruct = 'DELIVER IN PERSON'
   *    );
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. implicit type conversions for arithmetic operations are not supported
   *    a. changed 1 to 1.0 explicitly
   */
  const auto query =
      R"(SELECT SUM(l_extendedprice * (1.0 - l_discount) ) as revenue FROM lineitem, "part" WHERE (p_partkey = l_partkey
      AND p_brand = 'Brand#12' AND p_container in ( 'SM CASE', 'SM BOX', 'SM PACK', 'SM PKG') AND
      l_quantity >= 1 AND l_quantity <= 1 + 10 AND p_size between 1 AND 5 AND l_shipmode
      in ('AIR', 'AIR REG') AND l_shipinstruct = 'DELIVER IN PERSON') or (p_partkey = l_partkey
      AND p_brand = 'Brand#23' AND p_container in ('MED BAG', 'MED BOX', 'MED PKG', 'MED PACK')
      AND l_quantity >= 10 AND l_quantity <= 10 + 10 AND p_size between 1 AND 10
      AND l_shipmode in ('AIR', 'AIR REG') AND l_shipinstruct = 'DELIVER IN PERSON') or
      (p_partkey = l_partkey AND p_brand = 'Brand#34' AND p_container in ( 'LG CASE', 'LG BOX', 'LG PACK', 'LG PKG')
      AND l_quantity >= 20 AND l_quantity <= 20 + 10 AND p_size between 1 AND 15 AND l_shipmode in
      ('AIR', 'AIR REG') AND l_shipinstruct = 'DELIVER IN PERSON');)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch19.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Subselects in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH20) {
  /**
   * Original:
   *
   * SELECT s_name, s_address
   * FROM supplier, nation
   * WHERE
   *    s_suppkey in (
   *        SELECT ps_suppkey
   *        FROM partsupp
   *        WHERE ps_partkey in (
   *          SELECT p_partkey
   *          FROM part
   *          WHERE
   *            p_name like '[COLOR]%')
   *            AND ps_availqty > (
   *                SELECT 0.5 * sum(l_quantity)
   *                FROM lineitem
   *                WHERE
   *                    l_partkey = ps_partkey
   *                    AND l_suppkey = ps_suppkey
   *                    AND l_shipdate >= date('[DATE]')
   *                    AND l_shipdate < date('[DATE]') + interval '1' year
   *            )
   *        )
   *    AND s_nationkey = n_nationkey
   *    AND n_name = '[NATION]'
   * ORDER BY s_name;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation

   */
  const auto query =
      R"(SELECT s_name, s_address FROM supplier, nation WHERE s_suppkey in (SELECT ps_suppkey FROM partsupp
      WHERE ps_partkey in (SELECT p_partkey FROM "part" WHERE p_name like 'forest%') AND ps_availqty >
      (SELECT 0.5 * SUM(l_quantity) FROM lineitem WHERE l_partkey = ps_partkey AND l_suppkey = ps_suppkey AND
      l_shipdate >= '1994-01-01' AND l_shipdate < '1995-01-01')) AND s_nationkey = n_nationkey
      AND n_name = 'CANADA' ORDER BY s_name;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch20.tbl", 2);
  execute_and_check(query, expected_result, true);
}

// Enable once we support Exists and Subselect in WHERE condition
TEST_F(TPCHTest, DISABLED_TPCH21) {
  /**
   * Original:
   *
   * SELECT s_name, count(*) as numwait
   * FROM supplier, lineitem l1, orders, nation
   * WHERE
   *    s_suppkey = l1.l_suppkey
   *    AND o_orderkey = l1.l_orderkey
   *    AND o_orderstatus = 'F'
   *    AND l1.l_receiptdate > l1.l_commitdate
   *    AND exists (
   *        SELECT *
   *        FROM lineitem l2
   *        WHERE
   *            l2.l_orderkey = l1.l_orderkey
   *            AND l2.l_suppkey <> l1.l_suppkey
   *    ) AND not exists (
   *        SELECT *
   *        FROM lineitem l3
   *        WHERE
   *            l3.l_orderkey = l1.l_orderkey
   *            AND l3.l_suppkey <> l1.l_suppkey
   *            AND l3.l_receiptdate > l3.l_commitdate
   *    )
   *    AND s_nationkey = n_nationkey
   *    AND n_name = '[NATION]'
   * GROUP BY s_name
   * ORDER BY numwait DESC, s_name;
   *
   * Changes:
   *  1. Random values are hardcoded
   *  2. dates are not supported
   *    a. use strings as data type for now
   *    b. pre-calculate date operation

   */
  const auto query =
      R"(SELECT s_name, count(*) as numwait FROM supplier, lineitem l1, orders, nation WHERE s_suppkey = l1.l_suppkey
      AND o_orderkey = l1.l_orderkey AND o_orderstatus = 'F' AND l1.l_receiptdate > l1.l_commitdate AND exists
      (SELECT * FROM lineitem l2 WHERE l2.l_orderkey = l1.l_orderkey AND l2.l_suppkey <> l1.l_suppkey) AND not exists
      (SELECT * FROM lineitem l3 WHERE l3.l_orderkey = l1.l_orderkey AND l3.l_suppkey <> l1.l_suppkey AND
      l3.l_receiptdate > l3.l_commitdate ) AND s_nationkey = n_nationkey AND n_name = 'SAUDI ARABIA' GROUP BY s_name
      ORDER BY numwait DESC, s_name;)";
  const auto expected_result = load_table("src/test/tables/tpch/results/tpch21.tbl", 2);
  execute_and_check(query, expected_result, true);
}

}  // namespace opossum