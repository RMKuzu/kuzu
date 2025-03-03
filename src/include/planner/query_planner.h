#pragma once

#include "binder/bound_statement.h"
#include "binder/query/normalized_single_query.h"
#include "common/join_type.h"
#include "join_order_enumerator_context.h"
#include "planner/join_order/cardinality_estimator.h"
#include "planner/logical_plan/extend/extend_direction.h"

namespace kuzu {
namespace binder {
class BoundCreateInfo;
class BoundSetPropertyInfo;
class BoundDeleteInfo;
} // namespace binder

namespace planner {

class LogicalCreateNodeInfo;
class LogicalCreateRelInfo;
class LogicalSetPropertyInfo;

class QueryPlanner {
public:
    QueryPlanner(const catalog::Catalog& catalog,
        const storage::NodesStatisticsAndDeletedIDs& nodesStatistics,
        const storage::RelsStatistics& relsStatistics)
        : catalog{catalog} {
        cardinalityEstimator =
            std::make_unique<CardinalityEstimator>(nodesStatistics, relsStatistics);
        context = std::make_unique<JoinOrderEnumeratorContext>();
    }

    std::vector<std::unique_ptr<LogicalPlan>> getAllPlans(
        const binder::BoundStatement& boundStatement);

    inline std::unique_ptr<LogicalPlan> getBestPlan(const binder::BoundStatement& boundStatement) {
        return getBestPlan(getAllPlans(boundStatement));
    }

private:
    std::unique_ptr<LogicalPlan> getBestPlan(std::vector<std::unique_ptr<LogicalPlan>> plans);

    // Plan query
    std::vector<std::unique_ptr<LogicalPlan>> planSingleQuery(
        const binder::NormalizedSingleQuery& singleQuery);
    std::vector<std::unique_ptr<LogicalPlan>> planQueryPart(
        const binder::NormalizedQueryPart& queryPart,
        std::vector<std::unique_ptr<LogicalPlan>> prevPlans);

    // Plan reading
    void planReadingClause(binder::BoundReadingClause* readingClause,
        std::vector<std::unique_ptr<LogicalPlan>>& prevPlans);
    void planMatchClause(binder::BoundReadingClause* readingClause,
        std::vector<std::unique_ptr<LogicalPlan>>& plans);
    void planUnwindClause(binder::BoundReadingClause* readingClause,
        std::vector<std::unique_ptr<LogicalPlan>>& plans);
    void planInQueryCall(binder::BoundReadingClause* readingClause,
        std::vector<std::unique_ptr<LogicalPlan>>& plans);

    // Plan updating
    void planUpdatingClause(binder::BoundUpdatingClause& updatingClause,
        std::vector<std::unique_ptr<LogicalPlan>>& plans);
    void planUpdatingClause(binder::BoundUpdatingClause& updatingClause, LogicalPlan& plan);
    void planCreateClause(binder::BoundUpdatingClause& updatingClause, LogicalPlan& plan);
    void planMergeClause(binder::BoundUpdatingClause& updatingClause, LogicalPlan& plan);
    void planSetClause(binder::BoundUpdatingClause& updatingClause, LogicalPlan& plan);
    void planDeleteClause(binder::BoundUpdatingClause& updatingClause, LogicalPlan& plan);

    // Plan projection
    void planProjectionBody(const binder::BoundProjectionBody& projectionBody,
        const std::vector<std::unique_ptr<LogicalPlan>>& plans);
    void planProjectionBody(const binder::BoundProjectionBody& projectionBody, LogicalPlan& plan);
    void planAggregate(const binder::expression_vector& expressionsToAggregate,
        const binder::expression_vector& expressionsToGroupBy, LogicalPlan& plan);
    void planOrderBy(const binder::expression_vector& expressionsToProject,
        const binder::expression_vector& expressionsToOrderBy, const std::vector<bool>& isAscOrders,
        LogicalPlan& plan);

    // Plan subquery
    void planOptionalMatch(const binder::QueryGraphCollection& queryGraphCollection,
        const binder::expression_vector& predicates, LogicalPlan& leftPlan);
    void planRegularMatch(const binder::QueryGraphCollection& queryGraphCollection,
        const binder::expression_vector& predicates, LogicalPlan& leftPlan);
    void planExistsSubquery(std::shared_ptr<binder::Expression> subquery, LogicalPlan& outerPlan);
    void planSubqueryIfNecessary(
        const std::shared_ptr<binder::Expression>& expression, LogicalPlan& plan);

    // Plan query graphs
    std::unique_ptr<LogicalPlan> planQueryGraphCollection(
        const binder::QueryGraphCollection& queryGraphCollection,
        const binder::expression_vector& predicates);
    std::unique_ptr<LogicalPlan> planQueryGraphCollectionInNewContext(SubqueryType subqueryType,
        const binder::expression_vector& correlatedExpressions, uint64_t cardinality,
        const binder::QueryGraphCollection& queryGraphCollection,
        const binder::expression_vector& predicates);
    std::vector<std::unique_ptr<LogicalPlan>> enumerateQueryGraphCollection(
        const binder::QueryGraphCollection& queryGraphCollection,
        const binder::expression_vector& predicates);
    std::vector<std::unique_ptr<LogicalPlan>> enumerateQueryGraph(SubqueryType subqueryType,
        const expression_vector& correlatedExpressions, binder::QueryGraph* queryGraph,
        binder::expression_vector& predicates);

    // Plan node/rel table scan
    void planBaseTableScans(
        SubqueryType subqueryType, const expression_vector& correlatedExpressions);
    void planCorrelatedExpressionsScan(const binder::expression_vector& correlatedExpressions);
    std::unique_ptr<LogicalPlan> getCorrelatedExpressionScanPlan(
        const binder::expression_vector& correlatedExpressions);
    void planNodeScan(uint32_t nodePos);
    void planNodeIDScan(uint32_t nodePos);
    void planRelScan(uint32_t relPos);
    void appendExtendAndFilter(std::shared_ptr<binder::NodeExpression> boundNode,
        std::shared_ptr<binder::NodeExpression> nbrNode, std::shared_ptr<binder::RelExpression> rel,
        ExtendDirection direction, const binder::expression_vector& predicates, LogicalPlan& plan);

    // Plan dp level
    void planLevel(uint32_t level);
    void planLevelExactly(uint32_t level);
    void planLevelApproximately(uint32_t level);

    // Plan worst case optimal join
    void planWCOJoin(uint32_t leftLevel, uint32_t rightLevel);
    void planWCOJoin(const SubqueryGraph& subgraph,
        std::vector<std::shared_ptr<binder::RelExpression>> rels,
        const std::shared_ptr<binder::NodeExpression>& intersectNode);

    // Plan index-nested-loop join / hash join
    void planInnerJoin(uint32_t leftLevel, uint32_t rightLevel);
    bool tryPlanINLJoin(const SubqueryGraph& subgraph, const SubqueryGraph& otherSubgraph,
        const std::vector<std::shared_ptr<NodeExpression>>& joinNodes);
    void planInnerHashJoin(const SubqueryGraph& subgraph, const SubqueryGraph& otherSubgraph,
        std::vector<std::shared_ptr<binder::NodeExpression>> joinNodes, bool flipPlan);

    std::vector<std::unique_ptr<LogicalPlan>> planCrossProduct(
        std::vector<std::unique_ptr<LogicalPlan>> leftPlans,
        std::vector<std::unique_ptr<LogicalPlan>> rightPlans);

    // Append updating operators
    void appendCreateNode(
        const std::vector<binder::BoundCreateInfo*>& boundCreateInfos, LogicalPlan& plan);
    void appendCreateRel(
        const std::vector<binder::BoundCreateInfo*>& boundCreateInfos, LogicalPlan& plan);
    void appendSetNodeProperty(
        const std::vector<binder::BoundSetPropertyInfo*>& boundInfos, LogicalPlan& plan);
    void appendSetRelProperty(
        const std::vector<binder::BoundSetPropertyInfo*>& boundInfos, LogicalPlan& plan);
    void appendDeleteNode(
        const std::vector<binder::BoundDeleteInfo*>& boundInfos, LogicalPlan& plan);
    void appendDeleteRel(
        const std::vector<binder::BoundDeleteInfo*>& boundInfos, LogicalPlan& plan);
    std::unique_ptr<LogicalCreateNodeInfo> createLogicalCreateNodeInfo(
        BoundCreateInfo* boundCreateInfo);
    std::unique_ptr<LogicalCreateRelInfo> createLogicalCreateRelInfo(
        BoundCreateInfo* boundCreateInfo);
    std::unique_ptr<LogicalSetPropertyInfo> createLogicalSetPropertyInfo(
        BoundSetPropertyInfo* boundSetPropertyInfo);
    // TODO(Xiyang): remove this after refactoring
    std::vector<std::unique_ptr<BoundSetPropertyInfo>> createLogicalSetPropertyInfo(
        const std::vector<binder::BoundCreateInfo*>& boundCreateInfos);

    // Append projection operators
    void appendProjection(const binder::expression_vector& expressionsToProject, LogicalPlan& plan);
    void appendAggregate(const binder::expression_vector& expressionsToGroupBy,
        const binder::expression_vector& expressionsToAggregate, LogicalPlan& plan);
    void appendOrderBy(const binder::expression_vector& expressions,
        const std::vector<bool>& isAscOrders, LogicalPlan& plan);
    void appendMultiplicityReducer(LogicalPlan& plan);
    void appendLimit(uint64_t skipNum, uint64_t limitNum, LogicalPlan& plan);

    // Append scan operators
    void appendExpressionsScan(const expression_vector& expressions, LogicalPlan& plan);
    void appendScanNodeID(std::shared_ptr<NodeExpression>& node, LogicalPlan& plan);
    void appendScanNodeProperties(const expression_vector& propertyExpressions,
        std::shared_ptr<NodeExpression> node, LogicalPlan& plan);

    // Append extend operators
    void appendNonRecursiveExtend(std::shared_ptr<NodeExpression> boundNode,
        std::shared_ptr<NodeExpression> nbrNode, std::shared_ptr<RelExpression> rel,
        ExtendDirection direction, const binder::expression_vector& properties, LogicalPlan& plan);
    void appendRecursiveExtend(std::shared_ptr<NodeExpression> boundNode,
        std::shared_ptr<NodeExpression> nbrNode, std::shared_ptr<RelExpression> rel,
        ExtendDirection direction, LogicalPlan& plan);
    void createRecursivePlan(std::shared_ptr<NodeExpression> boundNode,
        std::shared_ptr<NodeExpression> recursiveNode, std::shared_ptr<RelExpression> recursiveRel,
        ExtendDirection direction, const expression_vector& predicates, LogicalPlan& plan);
    void createPathNodePropertyScanPlan(
        std::shared_ptr<NodeExpression> recursiveNode, LogicalPlan& plan);
    void createPathRelPropertyScanPlan(std::shared_ptr<NodeExpression> recursiveNode,
        std::shared_ptr<NodeExpression> nbrNode, std::shared_ptr<RelExpression> recursiveRel,
        ExtendDirection direction, LogicalPlan& plan);
    void appendNodeLabelFilter(std::shared_ptr<Expression> nodeID,
        std::unordered_set<common::table_id_t> tableIDSet, LogicalPlan& plan);

    // Append Join operators
    void appendHashJoin(const binder::expression_vector& joinNodeIDs, common::JoinType joinType,
        LogicalPlan& probePlan, LogicalPlan& buildPlan);
    void appendMarkJoin(const binder::expression_vector& joinNodeIDs,
        const std::shared_ptr<Expression>& mark, LogicalPlan& probePlan, LogicalPlan& buildPlan);
    void appendIntersect(const std::shared_ptr<Expression>& intersectNodeID,
        binder::expression_vector& boundNodeIDs, LogicalPlan& probePlan,
        std::vector<std::unique_ptr<LogicalPlan>>& buildPlans);

    void appendCrossProduct(
        common::AccumulateType accumulateType, LogicalPlan& probePlan, LogicalPlan& buildPlan);

    inline void appendAccumulate(common::AccumulateType accumulateType, LogicalPlan& plan) {
        appendAccumulate(accumulateType, expression_vector{}, plan);
    }
    void appendAccumulate(common::AccumulateType accumulateType,
        const binder::expression_vector& expressionsToFlatten, LogicalPlan& plan);

    void appendDummyScan(LogicalPlan& plan);

    void appendDistinct(const expression_vector& expressionsToDistinct, LogicalPlan& plan);

    void appendUnwind(const BoundReadingClause& boundReadingClause, LogicalPlan& plan);

    void appendInQueryCall(const BoundReadingClause& boundReadingClause, LogicalPlan& plan);

    void appendFlattens(const f_group_pos_set& groupsPos, LogicalPlan& plan);
    void appendFlattenIfNecessary(f_group_pos groupPos, LogicalPlan& plan);

    void appendFilters(const binder::expression_vector& predicates, LogicalPlan& plan);
    void appendFilter(const std::shared_ptr<Expression>& predicate, LogicalPlan& plan);

    std::unique_ptr<LogicalPlan> createUnionPlan(
        std::vector<std::unique_ptr<LogicalPlan>>& childrenPlans, bool isUnionAll);

    static std::vector<std::unique_ptr<LogicalPlan>> getInitialEmptyPlans();

    expression_vector getProperties(const binder::Expression& nodeOrRel);

    std::unique_ptr<JoinOrderEnumeratorContext> enterContext(SubqueryType subqueryType,
        const expression_vector& correlatedExpressions, uint64_t cardinality);
    void exitContext(std::unique_ptr<JoinOrderEnumeratorContext> prevContext);

private:
    const catalog::Catalog& catalog;
    expression_vector propertiesToScan;
    std::unique_ptr<CardinalityEstimator> cardinalityEstimator;
    std::unique_ptr<JoinOrderEnumeratorContext> context;
};

} // namespace planner
} // namespace kuzu
