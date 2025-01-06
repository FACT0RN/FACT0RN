// Copyright (c) 2024 AUTHOR
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FACTORN_ANNOUNCEDB_H
#define FACTORN_ANNOUNCEDB_H

#include <deadpool/deadpool.h>
#include <deadpool/index_common.h>
#include <primitives/transaction.h> // for COutPoint
#include <dbwrapper.h>
#include <uint256.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

//! maximum cache for announcedb in MiB
static const int64_t nMaxAnnounceDbCache = 16;

class CClaimValue {
public:
  int32_t height;
  uint256 claimHash;

  SERIALIZE_METHODS(CClaimValue, obj) {
    READWRITE(obj.height);
    READWRITE(obj.claimHash);
  }
};

class CAnnounceDB : public CDBWrapper
{
public:
    explicit CAnnounceDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool AddAnnouncements(const std::vector<CLocdAnnouncement> &list);
    bool RemoveAnnouncements(const std::vector<CLocdAnnouncement> &list);
    bool ClaimExists(const uint256 &hash, const uint256 &claim, const int32_t minHeight, const int32_t maxHeight) const;
};

#endif // FACTORN_ANNOUNCEDB_H
