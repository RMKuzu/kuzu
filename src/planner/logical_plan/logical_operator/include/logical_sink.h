#pragma once

#include "base_logical_operator.h"
#include "schema.h"

namespace graphflow {
namespace planner {

class LogicalSink : public LogicalOperator {

public:
    LogicalSink(expression_vector expressions, vector<uint64_t> flatOutputGroupPositions,
        unique_ptr<Schema> schemaBeforeSink, shared_ptr<LogicalOperator> child)
        : LogicalOperator{move(child)}, expressions{move(expressions)},
          flatOutputGroupPositions{move(flatOutputGroupPositions)}, schemaBeforeSink{
                                                                        move(schemaBeforeSink)} {}

    LogicalOperatorType getLogicalOperatorType() const override {
        return LogicalOperatorType::LOGICAL_SINK;
    }

    string getExpressionsForPrinting() const override {
        string result;
        for (auto& expression : expressions) {
            result += expression->getRawName() + ",";
        }
        return result;
    }

    inline expression_vector getExpressions() const { return expressions; }
    inline vector<uint64_t> getFlatOutputGroupPositions() const { return flatOutputGroupPositions; }
    inline Schema* getSchemaBeforeSink() const { return schemaBeforeSink.get(); }

    unique_ptr<LogicalOperator> copy() override {
        return make_unique<LogicalSink>(
            expressions, flatOutputGroupPositions, schemaBeforeSink->copy(), children[0]->copy());
    }

private:
    // Expressions to materialize and scan
    expression_vector expressions;
    // TODO(Xiyang): remove this when fixing issue #606
    vector<uint64_t> flatOutputGroupPositions;
    unique_ptr<Schema> schemaBeforeSink;
};

} // namespace planner
} // namespace graphflow
