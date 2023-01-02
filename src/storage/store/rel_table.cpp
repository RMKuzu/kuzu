#include "storage/store/rel_table.h"

#include "spdlog/spdlog.h"
#include "storage/storage_structure/lists/lists_update_iterator.h"

using namespace kuzu::catalog;

namespace kuzu {
namespace storage {

Column* DirectedRelTableData::getPropertyColumn(table_id_t boundNodeTableID, uint64_t propertyIdx) {
    if (propertyColumns.contains(boundNodeTableID) &&
        propertyIdx < propertyColumns[boundNodeTableID].size()) {
        return propertyColumns[boundNodeTableID][propertyIdx].get();
    }
    return nullptr;
}

Lists* DirectedRelTableData::getPropertyLists(table_id_t boundNodeTableID, uint64_t propertyIdx) {
    if (propertyLists.contains(boundNodeTableID) &&
        propertyIdx < propertyLists[boundNodeTableID].size()) {
        return propertyLists[boundNodeTableID][propertyIdx].get();
    }
    return nullptr;
}

AdjColumn* DirectedRelTableData::getAdjColumn(table_id_t boundNodeTableID) {
    if (adjColumns.contains(boundNodeTableID)) {
        return adjColumns[boundNodeTableID].get();
    }
    return nullptr;
}

AdjLists* DirectedRelTableData::getAdjLists(table_id_t boundNodeTableID) {
    if (adjLists.contains(boundNodeTableID)) {
        return adjLists[boundNodeTableID].get();
    }
    return nullptr;
}

void DirectedRelTableData::initializeData(
    RelTableSchema* tableSchema, BufferManager& bufferManager, WAL* wal) {
    for (auto& [srcTableID, dstTableID] : tableSchema->getSrcDstTableIDs()) {
        auto boundNodeTableID = direction == FWD ? srcTableID : dstTableID;
        NodeIDCompressionScheme nodeIDCompressionScheme(
            tableSchema->getUniqueNbrTableIDsForBoundTableIDDirection(direction, boundNodeTableID));
        if (tableSchema->isSingleMultiplicityInDirection(direction)) {
            initializeColumnsForBoundNodeTable(
                tableSchema, boundNodeTableID, nodeIDCompressionScheme, bufferManager, wal);
        } else {
            initializeListsForBoundNodeTabl(
                tableSchema, boundNodeTableID, nodeIDCompressionScheme, bufferManager, wal);
        }
    }
}

void DirectedRelTableData::initializeColumnsForBoundNodeTable(RelTableSchema* tableSchema,
    table_id_t boundNodeTableID, NodeIDCompressionScheme& nodeIDCompressionScheme,
    BufferManager& bufferManager, WAL* wal) {
    adjColumns[boundNodeTableID] =
        make_unique<AdjColumn>(StorageUtils::getAdjColumnStructureIDAndFName(wal->getDirectory(),
                                   tableSchema->tableID, boundNodeTableID, direction),
            bufferManager, nodeIDCompressionScheme, isInMemoryMode, wal);
    propertyColumns[boundNodeTableID].resize(tableSchema->getNumProperties());
    for (auto& property : tableSchema->properties) {
        propertyColumns.at(boundNodeTableID)[property.propertyID] = ColumnFactory::getColumn(
            StorageUtils::getRelPropertyColumnStructureIDAndFName(wal->getDirectory(),
                tableSchema->tableID, boundNodeTableID, direction, property.propertyID),
            property.dataType, bufferManager, isInMemoryMode, wal);
    }
}

void DirectedRelTableData::initializeListsForBoundNodeTabl(RelTableSchema* tableSchema,
    table_id_t boundNodeTableID, NodeIDCompressionScheme& nodeIDCompressionScheme,
    BufferManager& bufferManager, WAL* wal) {
    adjLists[boundNodeTableID] =
        make_unique<AdjLists>(StorageUtils::getAdjListsStructureIDAndFName(wal->getDirectory(),
                                  tableSchema->tableID, boundNodeTableID, direction),
            bufferManager, nodeIDCompressionScheme, isInMemoryMode, wal, listsUpdateStore);
    propertyLists[boundNodeTableID].resize(tableSchema->getNumProperties());
    for (auto& property : tableSchema->properties) {
        propertyLists.at(boundNodeTableID)[property.propertyID] = ListsFactory::getLists(
            StorageUtils::getRelPropertyListsStructureIDAndFName(
                wal->getDirectory(), tableSchema->tableID, boundNodeTableID, direction, property),
            property.dataType, adjLists[boundNodeTableID]->getHeaders(), bufferManager,
            isInMemoryMode, wal, listsUpdateStore);
    }
}

void DirectedRelTableData::scanColumns(Transaction* transaction, RelTableScanState& scanState,
    const shared_ptr<ValueVector>& inNodeIDVector, vector<shared_ptr<ValueVector>>& outputVectors) {
    auto adjColumn = adjColumns.at(scanState.boundNodeTableID).get();
    // Note: The scan operator should guarantee that the first property in the output is adj column.
    adjColumn->read(transaction, inNodeIDVector, outputVectors[0]);
    NodeIDVector::discardNull(*outputVectors[0]);
    if (outputVectors[0]->state->selVector->selectedSize == 0) {
        return;
    }
    for (auto i = 0u; i < scanState.propertyIds.size(); i++) {
        auto propertyId = scanState.propertyIds[i];
        auto outputVectorId = i + 1;
        if (propertyId == INVALID_PROPERTY_ID) {
            outputVectors[outputVectorId]->setAllNull();
            continue;
        }
        auto propertyColumn = getPropertyColumn(scanState.boundNodeTableID, propertyId);
        propertyColumn->read(transaction, inNodeIDVector, outputVectors[outputVectorId]);
    }
}

void DirectedRelTableData::scanLists(Transaction* transaction, RelTableScanState& scanState,
    const shared_ptr<ValueVector>& inNodeIDVector, vector<shared_ptr<ValueVector>>& outputVectors) {
    auto adjList = getAdjLists(scanState.boundNodeTableID);
    if (scanState.syncState->isBoundNodeOffsetInValid()) {
        auto currentIdx = inNodeIDVector->state->selVector->selectedPositions[0];
        if (inNodeIDVector->isNull(currentIdx)) {
            outputVectors[0]->state->selVector->selectedSize = 0;
            return;
        }
        auto currentNodeOffset = inNodeIDVector->readNodeOffset(currentIdx);
        adjList->initListReadingState(
            currentNodeOffset, *scanState.listHandles[0], transaction->getType());
    }
    adjList->readValues(outputVectors[0], *scanState.listHandles[0]);
    for (auto i = 0u; i < scanState.propertyIds.size(); i++) {
        auto propertyId = scanState.propertyIds[i];
        auto outputVectorId = i + 1;
        if (propertyId == INVALID_PROPERTY_ID) {
            outputVectors[outputVectorId]->setAllNull();
            continue;
        }
        auto propertyList = getPropertyLists(scanState.boundNodeTableID, propertyId);
        propertyList->readValues(
            outputVectors[outputVectorId], *scanState.listHandles[outputVectorId]);
        propertyList->setDeletedRelsIfNecessary(
            transaction, *scanState.listHandles[outputVectorId], outputVectors[outputVectorId]);
    }
}

void DirectedRelTableData::insertRel(table_id_t boundTableID,
    const shared_ptr<ValueVector>& boundVector, const shared_ptr<ValueVector>& nbrVector,
    const vector<shared_ptr<ValueVector>>& relPropertyVectors) {
    if (!adjColumns.contains(boundTableID)) {
        return;
    }
    auto adjColumn = adjColumns.at(boundTableID).get();
    auto nodeOffset =
        boundVector->readNodeOffset(boundVector->state->selVector->selectedPositions[0]);
    // TODO(Guodong): We should pass a write transaction pointer down.
    if (!adjColumn->isNull(nodeOffset, Transaction::getDummyWriteTrx().get())) {
        throw RuntimeException(
            StringUtils::string_format("Node(nodeOffset: %d, tableID: %d) in RelTable %d cannot "
                                       "have more than one neighbour in the %s direction.",
                nodeOffset, boundTableID, tableID, getRelDirectionAsString(direction).c_str()));
    }
    adjColumn->writeValues(boundVector, nbrVector);
    for (auto i = 0u; i < relPropertyVectors.size(); i++) {
        auto propertyColumn = getPropertyColumn(boundTableID, i);
        propertyColumn->writeValues(boundVector, relPropertyVectors[i]);
    }
}

void DirectedRelTableData::deleteRel(
    table_id_t boundTableID, const shared_ptr<ValueVector>& boundVector) {
    if (!adjColumns.contains(boundTableID)) {
        return;
    }
    auto adjColumn = adjColumns.at(boundTableID).get();
    auto nodeOffset =
        boundVector->readNodeOffset(boundVector->state->selVector->selectedPositions[0]);
    adjColumn->setNodeOffsetToNull(nodeOffset);
    for (auto& propertyColumn : propertyColumns.at(boundTableID)) {
        propertyColumn->setNodeOffsetToNull(nodeOffset);
    }
}

void DirectedRelTableData::performOpOnListsWithUpdates(
    const std::function<void(Lists*)>& opOnListsWithUpdates) {
    for (auto& [boundNodeTableID, listsUpdatePerTable] :
        listsUpdateStore->getListUpdatesPerBoundNodeTableOfDirection(direction)) {
        opOnListsWithUpdates(adjLists.at(boundNodeTableID).get());
        for (auto& propertyList : propertyLists.at(boundNodeTableID)) {
            opOnListsWithUpdates(propertyList.get());
        }
    }
}

vector<unique_ptr<ListsUpdateIterator>> DirectedRelTableData::getListsUpdateIterators(
    table_id_t boundNodeTableID) {
    vector<unique_ptr<ListsUpdateIterator>> listsUpdateIterators;
    listsUpdateIterators.push_back(
        ListsUpdateIteratorFactory::getListsUpdateIterator(adjLists.at(boundNodeTableID).get()));
    for (auto& propList : propertyLists.at(boundNodeTableID)) {
        listsUpdateIterators.push_back(
            ListsUpdateIteratorFactory::getListsUpdateIterator(propList.get()));
    }
    return listsUpdateIterators;
}

RelTable::RelTable(const Catalog& catalog, table_id_t tableID, BufferManager& bufferManager,
    MemoryManager& memoryManager, bool isInMemoryMode, WAL* wal)
    : tableID{tableID}, wal{wal} {
    auto tableSchema = catalog.getReadOnlyVersion()->getRelTableSchema(tableID);
    listsUpdateStore = make_unique<ListsUpdateStore>(memoryManager, *tableSchema);
    fwdRelTableData =
        make_unique<DirectedRelTableData>(tableID, FWD, listsUpdateStore.get(), isInMemoryMode);
    bwdRelTableData =
        make_unique<DirectedRelTableData>(tableID, BWD, listsUpdateStore.get(), isInMemoryMode);
    initializeData(tableSchema, bufferManager);
}

void RelTable::initializeData(RelTableSchema* tableSchema, BufferManager& bufferManager) {
    fwdRelTableData->initializeData(tableSchema, bufferManager, wal);
    bwdRelTableData->initializeData(tableSchema, bufferManager, wal);
}

vector<AdjLists*> RelTable::getAdjListsForNodeTable(table_id_t boundNodeTableID) {
    vector<AdjLists*> retVal;
    if (fwdRelTableData->hasAdjLists(boundNodeTableID)) {
        retVal.push_back(fwdRelTableData->getAdjLists(boundNodeTableID));
    }
    if (bwdRelTableData->hasAdjLists(boundNodeTableID)) {
        retVal.push_back(bwdRelTableData->getAdjLists(boundNodeTableID));
    }
    return retVal;
}

vector<AdjColumn*> RelTable::getAdjColumnsForNodeTable(table_id_t boundNodeTableID) {
    vector<AdjColumn*> retVal;
    if (fwdRelTableData->hasAdjColumn(boundNodeTableID)) {
        retVal.push_back(fwdRelTableData->getAdjColumn(boundNodeTableID));
    }
    if (bwdRelTableData->hasAdjColumn(boundNodeTableID)) {
        retVal.push_back(bwdRelTableData->getAdjColumn(boundNodeTableID));
    }
    return retVal;
}

// Prepares all the db file changes necessary to update the "persistent" store of lists with the
// listsUpdateStore, which stores the updates by the write transaction locally.
void RelTable::prepareCommitOrRollbackIfNecessary(bool isCommit) {
    if (isCommit) {
        prepareCommitForDirection(FWD);
        prepareCommitForDirection(BWD);
    }
    if (listsUpdateStore->hasUpdates()) {
        addToUpdatedRelTables();
    }
}

void RelTable::checkpointInMemoryIfNecessary() {
    performOpOnListsWithUpdates(
        std::bind(&Lists::checkpointInMemoryIfNecessary, std::placeholders::_1),
        std::bind(&RelTable::clearListsUpdateStore, this));
}

void RelTable::rollbackInMemoryIfNecessary() {
    performOpOnListsWithUpdates(
        std::bind(&Lists::rollbackInMemoryIfNecessary, std::placeholders::_1),
        std::bind(&RelTable::clearListsUpdateStore, this));
}

// This function assumes that the order of vectors in relPropertyVectorsPerRelTable as:
// [relProp1, relProp2, ..., relPropN] and all vectors are flat.
void RelTable::insertRel(const shared_ptr<ValueVector>& srcNodeIDVector,
    const shared_ptr<ValueVector>& dstNodeIDVector,
    const vector<shared_ptr<ValueVector>>& relPropertyVectors) {
    assert(srcNodeIDVector->state->isFlat() && dstNodeIDVector->state->isFlat());
    auto srcTableID =
        srcNodeIDVector->getValue<nodeID_t>(srcNodeIDVector->state->selVector->selectedPositions[0])
            .tableID;
    auto dstTableID =
        dstNodeIDVector->getValue<nodeID_t>(dstNodeIDVector->state->selVector->selectedPositions[0])
            .tableID;
    fwdRelTableData->insertRel(srcTableID, srcNodeIDVector, dstNodeIDVector, relPropertyVectors);
    bwdRelTableData->insertRel(dstTableID, dstNodeIDVector, srcNodeIDVector, relPropertyVectors);
    listsUpdateStore->insertRelIfNecessary(srcNodeIDVector, dstNodeIDVector, relPropertyVectors);
}

void RelTable::deleteRel(const shared_ptr<ValueVector>& srcNodeIDVector,
    const shared_ptr<ValueVector>& dstNodeIDVector, const shared_ptr<ValueVector>& relIDVector) {
    assert(srcNodeIDVector->state->isFlat() && dstNodeIDVector->state->isFlat() &&
           relIDVector->state->isFlat());
    auto srcTableID =
        srcNodeIDVector->getValue<nodeID_t>(srcNodeIDVector->state->selVector->selectedPositions[0])
            .tableID;
    auto dstTableID =
        dstNodeIDVector->getValue<nodeID_t>(dstNodeIDVector->state->selVector->selectedPositions[0])
            .tableID;
    fwdRelTableData->deleteRel(srcTableID, srcNodeIDVector);
    bwdRelTableData->deleteRel(dstTableID, dstNodeIDVector);
    listsUpdateStore->deleteRelIfNecessary(srcNodeIDVector, dstNodeIDVector, relIDVector);
}

void RelTable::initEmptyRelsForNewNode(nodeID_t& nodeID) {
    if (fwdRelTableData->hasAdjColumn(nodeID.tableID)) {
        fwdRelTableData->getAdjColumn(nodeID.tableID)->setNodeOffsetToNull(nodeID.offset);
    }
    if (bwdRelTableData->hasAdjColumn(nodeID.tableID)) {
        bwdRelTableData->getAdjColumn(nodeID.tableID)->setNodeOffsetToNull(nodeID.offset);
    }
    listsUpdateStore->initNewlyAddedNodes(nodeID);
}

void RelTable::performOpOnListsWithUpdates(const std::function<void(Lists*)>& opOnListsWithUpdates,
    const std::function<void()>& opIfHasUpdates) {
    fwdRelTableData->performOpOnListsWithUpdates(opOnListsWithUpdates);
    bwdRelTableData->performOpOnListsWithUpdates(opOnListsWithUpdates);
    if (listsUpdateStore->hasUpdates()) {
        opIfHasUpdates();
    }
}

vector<unique_ptr<ListsUpdateIterator>> RelTable::getListsUpdateIterators(
    RelDirection relDirection, table_id_t boundNodeTableID) const {
    return relDirection == FWD ? fwdRelTableData->getListsUpdateIterators(boundNodeTableID) :
                                 bwdRelTableData->getListsUpdateIterators(boundNodeTableID);
}

void RelTable::prepareCommitForDirection(RelDirection relDirection) {
    for (auto& [boundNodeTableID, listsUpdates] :
        listsUpdateStore->getListUpdatesPerBoundNodeTableOfDirection(relDirection)) {
        if (listsUpdates.empty()) {
            continue;
        }
        auto listsUpdateIterators = getListsUpdateIterators(relDirection, boundNodeTableID);
        // Note: call writeInMemListToListPages in ascending order of nodeOffsets is critical here.
        for (auto& [chunkId, listsUpdatesOfChunk] : listsUpdates) {
            for (auto& [nodeOffset, listUpdatesOfNode] : listsUpdatesOfChunk) {
                // Note: An empty listUpdates can exist for a nodeOffset, because we don't fix the
                // listUpdates, listUpdatesPerNode and ListUpdatesPerChunk indices after we insert
                // or delete a rel. For example: a user inserts 1 rel to nodeOffset1, and then
                // deletes that rel. We will end up getting an empty listUpdates for nodeOffset1.
                if (!listUpdatesOfNode.hasUpdates()) {
                    continue;
                }
                auto adjLists = getAdjLists(relDirection, boundNodeTableID);
                if (listUpdatesOfNode.isNewlyAddedNode) {
                    auto inMemAdjLists = adjLists->createInMemListWithDataFromUpdateStoreOnly(
                        nodeOffset, listUpdatesOfNode.insertedRelsTupleIdxInFT);
                    listsUpdateIterators[0]->updateList(nodeOffset, *inMemAdjLists);
                    auto numPropertyLists = getNumPropertyLists(relDirection, boundNodeTableID);
                    for (auto i = 0u; i < numPropertyLists; i++) {
                        auto inMemPropLists =
                            getPropertyLists(relDirection, boundNodeTableID, i)
                                ->createInMemListWithDataFromUpdateStoreOnly(
                                    nodeOffset, listUpdatesOfNode.insertedRelsTupleIdxInFT);
                        listsUpdateIterators[i + 1]->updateList(nodeOffset, *inMemPropLists);
                    }
                    // TODO(Guodong): Do we need to access the header in this way?
                } else if (ListHeaders::isALargeList(adjLists->getHeaders()->headersDiskArray->get(
                               nodeOffset, TransactionType::READ_ONLY)) &&
                           listUpdatesOfNode.deletedRelIDs.empty()) {
                    // We do an optimization for relPropertyList and adjList : If the initial list
                    // is a largeList and we didn't delete any rel from the persistentStore, we can
                    // simply append the newly inserted rels from the relUpdateStore to largeList.
                    // In this case, we can skip reading the data from persistentStore to InMemList
                    // and only need to read the data from relUpdateStore to InMemList.
                    auto inMemAdjLists = adjLists->createInMemListWithDataFromUpdateStoreOnly(
                        nodeOffset, listUpdatesOfNode.insertedRelsTupleIdxInFT);
                    listsUpdateIterators[0]->appendToLargeList(nodeOffset, *inMemAdjLists);
                    auto numPropertyLists = getNumPropertyLists(relDirection, boundNodeTableID);
                    for (auto i = 0u; i < numPropertyLists; i++) {
                        auto inMemPropLists =
                            getPropertyLists(relDirection, boundNodeTableID, i)
                                ->createInMemListWithDataFromUpdateStoreOnly(
                                    nodeOffset, listUpdatesOfNode.insertedRelsTupleIdxInFT);
                        listsUpdateIterators[i + 1]->appendToLargeList(nodeOffset, *inMemPropLists);
                    }
                } else {
                    auto relIDLists = (RelIDList*)getPropertyLists(relDirection, boundNodeTableID,
                        RelTableSchema::INTERNAL_REL_ID_PROPERTY_IDX);
                    auto deletedRelOffsets =
                        relIDLists->getDeletedRelOffsetsInListForNodeOffset(nodeOffset);
                    auto inMemAdjLists = adjLists->writeToInMemList(
                        nodeOffset, listUpdatesOfNode.insertedRelsTupleIdxInFT, deletedRelOffsets);
                    listsUpdateIterators[0]->updateList(nodeOffset, *inMemAdjLists);
                    auto numPropertyLists = getNumPropertyLists(relDirection, boundNodeTableID);
                    for (auto i = 0u; i < numPropertyLists; i++) {
                        auto inMemPropLists =
                            getPropertyLists(relDirection, boundNodeTableID, i)
                                ->writeToInMemList(nodeOffset,
                                    listUpdatesOfNode.insertedRelsTupleIdxInFT, deletedRelOffsets);
                        listsUpdateIterators[i + 1]->updateList(nodeOffset, *inMemPropLists);
                    }
                }
            }
            for (auto& listsUpdateIterator : listsUpdateIterators) {
                listsUpdateIterator->doneUpdating();
            }
        }
    }
}

} // namespace storage
} // namespace kuzu
