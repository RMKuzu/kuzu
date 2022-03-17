#pragma once

#include <functional>

#include "src/common/include/vector/value_vector.h"

using namespace graphflow::common;

namespace graphflow {
namespace function {

struct VectorListOperations {

    static void ListCreation(
        const vector<shared_ptr<ValueVector>>& parameters, ValueVector& result);
};

} // namespace function
} // namespace graphflow
