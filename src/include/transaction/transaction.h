#pragma once

#include <memory>

#include "storage/local_storage.h"

namespace kuzu {
namespace transaction {

class TransactionManager;

enum class TransactionType : uint8_t { READ_ONLY, WRITE };
enum class TransactionAction : uint8_t { COMMIT, ROLLBACK };

class Transaction {
    friend class TransactionManager;

public:
    Transaction(TransactionType transactionType, uint64_t transactionID,
        storage::StorageManager* storageManager, storage::MemoryManager* mm)
        : type{transactionType}, ID{transactionID} {
        localStorage = std::make_unique<storage::LocalStorage>(storageManager, mm);
    }

    constexpr explicit Transaction(TransactionType transactionType)
        : type{transactionType}, ID{UINT64_MAX} {}

public:
    inline TransactionType getType() const { return type; }
    inline bool isReadOnly() const { return TransactionType::READ_ONLY == type; }
    inline bool isWriteTransaction() const { return TransactionType::WRITE == type; }
    inline uint64_t getID() const { return ID; }
    inline storage::LocalStorage* getLocalStorage() { return localStorage.get(); }

    static inline std::unique_ptr<Transaction> getDummyWriteTrx() {
        return std::make_unique<Transaction>(TransactionType::WRITE);
    }
    static inline std::unique_ptr<Transaction> getDummyReadOnlyTrx() {
        return std::make_unique<Transaction>(TransactionType::READ_ONLY);
    }

private:
    TransactionType type;
    // TODO(Guodong): add type transaction_id_t.
    uint64_t ID;
    std::unique_ptr<storage::LocalStorage> localStorage;
};

static Transaction DUMMY_READ_TRANSACTION = Transaction(TransactionType::READ_ONLY);

} // namespace transaction
} // namespace kuzu
