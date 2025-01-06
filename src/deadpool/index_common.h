// Copyright (c) 2023-2024 AUTHOR
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FACTORN_DEADPOOL_INDEX_COMMON_H
#define FACTORN_DEADPOOL_INDEX_COMMON_H

#include <uint256.h>
#include <primitives/transaction.h> // for COutPoint


// Shared structs between deadpool entry index and announcedb

/**
 * Search a deadpool index by deadpool id (sha256 hash of N).
 */
class DeadpoolIndexSearchKey {
public:
  uint8_t type;
  uint256 deadpoolId;

  SERIALIZE_METHODS(DeadpoolIndexSearchKey, obj) {
    READWRITE(obj.type);
    READWRITE(obj.deadpoolId);
  }
};

/**
 * Key for all deadpool indices.
 */
class DeadpoolIndexKey {
public:
    uint8_t type;
    uint256 deadpoolId;
    COutPoint locator;

    SERIALIZE_METHODS(DeadpoolIndexKey, obj) {
      READWRITE(obj.type);
      READWRITE(obj.deadpoolId);
      READWRITE(obj.locator);
    }
};

#endif //FACTORN_DEADPOOL_INDEX_COMMON_H
