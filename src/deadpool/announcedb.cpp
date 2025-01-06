// Copyright (c) 2024 AUTHOR
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <deadpool/announcedb.h>

#include <deadpool/deadpool.h>
#include <deadpool/index_common.h>
#include <node/ui_interface.h>
#include <uint256.h>
#include <util/system.h>

#include <vector>
#include <stdint.h>

static constexpr uint8_t DB_DEADPOOL_ANN{'a'};

CAnnounceDB::CAnnounceDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(gArgs.GetDataDirNet() / "announcedb", nCacheSize, fMemory, fWipe) {}

bool CAnnounceDB::AddAnnouncements(const std::vector<CLocdAnnouncement> &list) {
    CDBBatch batch(*this);
    size_t count;

    for (std::vector<CLocdAnnouncement>::const_iterator it=list.begin(); it != list.end(); it++) {
        const uint256 entry = it->announcement.NHash();
        LogPrint(BCLog::COINDB, "Added announcement (%s:%u) at height %d for entry %s to db.\n", it->locator.hash.GetHex(), it->locator.n, it->announcement.nHeight, entry.GetHex());
        const DeadpoolIndexKey key({DB_DEADPOOL_ANN, entry, it->locator});
        const CClaimValue value({it->announcement.nHeight, it->announcement.ClaimHash()});
        batch.Write(key, value);
        count++;
    }

    bool ret = WriteBatch(batch);
    LogPrint(BCLog::COINDB, "Committed %u announcements to db.\n", count);
    return ret;
}

bool CAnnounceDB::RemoveAnnouncements(const std::vector<CLocdAnnouncement> &list) {
    CDBBatch batch(*this);
    size_t count;

    for (std::vector<CLocdAnnouncement>::const_iterator it=list.begin(); it != list.end(); it++) {
        const uint256 entry = it->announcement.NHash();
        LogPrint(BCLog::COINDB, "Removed announcement (%s:%u) for entry %s from db.\n", it->locator.hash.GetHex(), it->locator.n, entry.GetHex());
        const DeadpoolIndexKey key({DB_DEADPOOL_ANN, entry, it->locator});
        batch.Erase(key);
        count++;
    }

    bool ret = WriteBatch(batch);
    LogPrint(BCLog::COINDB, "Removed %u announcements from db.\n", count);
    return ret;
}

bool CAnnounceDB::ClaimExists(const uint256 &hash, const uint256 &claim, const int32_t minHeight, const int32_t maxHeight) const
{
    std::unique_ptr<CDBIterator> pcursor(const_cast<CAnnounceDB&>(*this).NewIterator());

    const DeadpoolIndexSearchKey searchKey({DB_DEADPOOL_ANN, hash});
    pcursor->Seek(searchKey);

    while (pcursor->Valid()) {
        DeadpoolIndexKey key = {};
        CClaimValue value = {};

        if (pcursor->GetKey(key) && key.type == DB_DEADPOOL_ANN && key.deadpoolId == hash) {
            if (pcursor->GetValue(value)) {
                if (value.height <= maxHeight && value.height >= minHeight && value.claimHash == claim) {
                    LogPrint(BCLog::COINDB, "Found claim %s for entry %s: %s:%u.\n", claim.GetHex(), hash.GetHex(), key.locator.hash.GetHex(), key.locator.n);
                    return true;
                }
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    return false;
}
