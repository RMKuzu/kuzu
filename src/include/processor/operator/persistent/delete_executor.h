#pragma once

#include "processor/execution_context.h"
#include "processor/result/result_set.h"
#include "storage/store/node_table.h"

namespace kuzu {
namespace processor {

class NodeDeleteExecutor {
public:
    NodeDeleteExecutor(const DataPos& nodeIDPos) : nodeIDPos{nodeIDPos} {}
    virtual ~NodeDeleteExecutor() = default;

    virtual void init(ResultSet* resultSet, ExecutionContext* context);

    virtual void delete_(ExecutionContext* context) = 0;

    virtual std::unique_ptr<NodeDeleteExecutor> copy() const = 0;

protected:
    DataPos nodeIDPos;
    common::ValueVector* nodeIDVector;
};

class SingleLabelNodeDeleteExecutor : public NodeDeleteExecutor {
public:
    SingleLabelNodeDeleteExecutor(storage::NodeTable* table, const DataPos& nodeIDPos)
        : NodeDeleteExecutor(nodeIDPos), table{table} {}
    SingleLabelNodeDeleteExecutor(const SingleLabelNodeDeleteExecutor& other)
        : NodeDeleteExecutor(other.nodeIDPos), table{other.table} {}

    void init(ResultSet* resultSet, ExecutionContext* context) final;
    void delete_(ExecutionContext* context) final;

    inline std::unique_ptr<NodeDeleteExecutor> copy() const final {
        return std::make_unique<SingleLabelNodeDeleteExecutor>(*this);
    }

private:
    storage::NodeTable* table;
    std::unique_ptr<storage::NodeTable::DeleteState> deleteState;
};

class MultiLabelNodeDeleteExecutor : public NodeDeleteExecutor {
public:
    MultiLabelNodeDeleteExecutor(
        std::unordered_map<common::table_id_t, storage::NodeTable*> tableIDToTableMap,
        const DataPos& nodeIDPos)
        : NodeDeleteExecutor(nodeIDPos), tableIDToTableMap{std::move(tableIDToTableMap)} {}
    MultiLabelNodeDeleteExecutor(const MultiLabelNodeDeleteExecutor& other)
        : NodeDeleteExecutor(other.nodeIDPos), tableIDToTableMap{other.tableIDToTableMap} {}

    void init(ResultSet* resultSet, ExecutionContext* context) final;
    void delete_(ExecutionContext* context) final;

    inline std::unique_ptr<NodeDeleteExecutor> copy() const final {
        return std::make_unique<MultiLabelNodeDeleteExecutor>(*this);
    }

private:
    std::unordered_map<common::table_id_t, storage::NodeTable*> tableIDToTableMap;
    std::unordered_map<common::table_id_t, std::unique_ptr<storage::NodeTable::DeleteState>>
        deleteStates;
};

class RelDeleteExecutor {
public:
    RelDeleteExecutor(
        const DataPos& srcNodeIDPos, const DataPos& dstNodeIDPos, const DataPos& relIDPos)
        : srcNodeIDPos{srcNodeIDPos}, dstNodeIDPos{dstNodeIDPos}, relIDPos{relIDPos} {}
    virtual ~RelDeleteExecutor() = default;

    void init(ResultSet* resultSet, ExecutionContext* context);

    virtual void delete_() = 0;

    virtual std::unique_ptr<RelDeleteExecutor> copy() const = 0;

protected:
    DataPos srcNodeIDPos;
    DataPos dstNodeIDPos;
    DataPos relIDPos;

    common::ValueVector* srcNodeIDVector;
    common::ValueVector* dstNodeIDVector;
    common::ValueVector* relIDVector;
};

class SingleLabelRelDeleteExecutor : public RelDeleteExecutor {
public:
    SingleLabelRelDeleteExecutor(storage::RelsStatistics* relsStatistic, storage::RelTable* table,
        const DataPos& srcNodeIDPos, const DataPos& dstNodeIDPos, const DataPos& relIDPos)
        : RelDeleteExecutor(srcNodeIDPos, dstNodeIDPos, relIDPos),
          relsStatistic{relsStatistic}, table{table} {}
    SingleLabelRelDeleteExecutor(const SingleLabelRelDeleteExecutor& other)
        : RelDeleteExecutor(other.srcNodeIDPos, other.dstNodeIDPos, other.relIDPos),
          relsStatistic{other.relsStatistic}, table{other.table} {}

    void delete_() final;

    inline std::unique_ptr<RelDeleteExecutor> copy() const final {
        return std::make_unique<SingleLabelRelDeleteExecutor>(*this);
    }

private:
    storage::RelsStatistics* relsStatistic;
    storage::RelTable* table;
};

class MultiLabelRelDeleteExecutor : public RelDeleteExecutor {
    using rel_table_statistic_pair = std::pair<storage::RelTable*, storage::RelsStatistics*>;

public:
    MultiLabelRelDeleteExecutor(
        std::unordered_map<common::table_id_t, rel_table_statistic_pair> tableIDToTableMap,
        const DataPos& srcNodeIDPos, const DataPos& dstNodeIDPos, const DataPos& relIDPos)
        : RelDeleteExecutor(srcNodeIDPos, dstNodeIDPos, relIDPos), tableIDToTableMap{std::move(
                                                                       tableIDToTableMap)} {}
    MultiLabelRelDeleteExecutor(const MultiLabelRelDeleteExecutor& other)
        : RelDeleteExecutor(other.srcNodeIDPos, other.dstNodeIDPos, other.relIDPos),
          tableIDToTableMap{other.tableIDToTableMap} {}

    void delete_() final;

    inline std::unique_ptr<RelDeleteExecutor> copy() const final {
        return std::make_unique<MultiLabelRelDeleteExecutor>(*this);
    }

private:
    std::unordered_map<common::table_id_t, rel_table_statistic_pair> tableIDToTableMap;
};

} // namespace processor
} // namespace kuzu
