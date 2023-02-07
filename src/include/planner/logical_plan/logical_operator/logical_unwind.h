#pragma once

#include "base_logical_operator.h"

namespace kuzu {
namespace planner {

class LogicalUnwind : public LogicalOperator {
public:
    LogicalUnwind(std::shared_ptr<binder::Expression> expression,
        std::shared_ptr<binder::Expression> aliasExpression,
        std::shared_ptr<LogicalOperator> childOperator)
        : LogicalOperator{LogicalOperatorType::UNWIND, std::move(childOperator)},
          expression{std::move(expression)}, aliasExpression{std::move(aliasExpression)} {}

    void computeSchema() override;

    inline std::shared_ptr<binder::Expression> getExpression() { return expression; }

    inline std::shared_ptr<binder::Expression> getAliasExpression() { return aliasExpression; }

    inline std::string getExpressionsForPrinting() const override {
        return expression->getUniqueName();
    }

    inline std::unique_ptr<LogicalOperator> copy() override {
        return make_unique<LogicalUnwind>(expression, aliasExpression, children[0]->copy());
    }

private:
    std::shared_ptr<binder::Expression> expression;
    std::shared_ptr<binder::Expression> aliasExpression;
};

} // namespace planner
} // namespace kuzu
