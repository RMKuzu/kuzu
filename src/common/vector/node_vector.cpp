#include "src/common/include/vector/node_vector.h"

#include <cassert>

namespace graphflow {
namespace common {

node_offset_t NodeIDVector::readNodeOffset(uint64_t pos) const {
    if (representation.isSequence) {
        return ((node_offset_t*)values)[0] + pos;
    }
    auto readOffset = values + (pos * representation.compressionScheme.getNumTotalBytes());
    node_offset_t nodeOffset = 0;
    memcpy(&nodeOffset, readOffset + representation.compressionScheme.getNumBytesForLabel(),
        representation.compressionScheme.getNumBytesForOffset());
    return nodeOffset;
}

void NodeIDVector::readNodeID(uint64_t pos, nodeID_t& nodeID) const {
    if (representation.isSequence) {
        nodeID.offset = ((node_offset_t*)values)[0] + pos;
        nodeID.label = representation.commonLabel;
        return;
    }
    auto readOffset = values + (pos * representation.compressionScheme.getNumTotalBytes());
    if (representation.compressionScheme.getNumBytesForLabel() == 0) {
        nodeID.label = representation.commonLabel;
    } else {
        nodeID.label = 0;
        memcpy(&nodeID.label, readOffset, representation.compressionScheme.getNumBytesForLabel());
    }
    nodeID.offset = 0;
    memcpy(&nodeID.offset, readOffset + representation.compressionScheme.getNumBytesForLabel(),
        representation.compressionScheme.getNumBytesForOffset());
}

bool NodeIDVector::discardNulls() {
    auto nullOffset = representation.compressionScheme.getNodeOffsetNullValue();
    node_offset_t nodeOffset;
    if (state->currPos == -1) {
        auto selectedValuesPos = state->selectedValuesPos;
        auto selectedPos = 0u;
        for (auto j = 0u; j < state->numSelectedValues; j++) {
            nodeOffset = readNodeOffset(state->selectedValuesPos[j]);
            if (nodeOffset != nullOffset) {
                selectedValuesPos[selectedPos++] = j;
            }
        }
        state->numSelectedValues = selectedPos;
        return state->numSelectedValues > 0;
    } else {
        nodeOffset = readNodeOffset(state->getCurrSelectedValuesPos());
        return nodeOffset != nullOffset;
    }
}

shared_ptr<ValueVector> NodeIDVector::clone() {
    auto newVector = make_shared<NodeIDVector>(
        representation.commonLabel, representation.compressionScheme, representation.isSequence);
    memcpy(newVector->nullMask, nullMask, vectorCapacity);
    memcpy(newVector->values, values, vectorCapacity * getNumBytesPerValue());
    return newVector;
}

} // namespace common
} // namespace graphflow
