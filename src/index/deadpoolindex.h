// Copyright (c) 2023 AUTHOR
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FACTORN_INDEX_DEADPOOLINDEX_H
#define FACTORN_INDEX_DEADPOOLINDEX_H

#include <chain.h>
#include <deadpool/index_common.h>
#include <index/base.h>
#include <txdb.h>

/**
 * Deadpool entry / announce result from index lookup
 */
struct DeadpoolIndexEntry {
    uint256 deadpoolId;
    COutPoint locator;
    int height;
    CTxOut txOut;
};

struct DeadpoolIndexClaim {
    COutPoint entryLocator;
    uint256 deadpoolId;
    int claimHeight;
    uint256 claimBlockHash;
    uint256 claimTxHash;
    std::vector<unsigned char> solution;
};

/**
 * DeadpoolIndex is used to look up deadpool entries and announcements.
 * This index is not consensus critical, and could be made optional.
 */
class DeadpoolIndex final : public BaseIndex
{
protected:
    class DB;

private:
    const std::unique_ptr<DB> m_db;

protected:
    bool WriteBlock(const CBlock& block, const CBlockIndex* pindex) override;

    BaseIndex::DB& GetDB() const override;

    const char* GetName() const override { return "deadpoolindex"; }

public:
    /// Constructs the index, which becomes available to be queried.
    explicit DeadpoolIndex(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    // Destructor is declared because this class contains a unique_ptr to an incomplete type.
    virtual ~DeadpoolIndex() override;

    /// Find deadpool entries by hash of N.
    ///
    /// @param[in]    deadpoolId   The hash of N to search entries for.
    /// @param[out]   list         A list of transaction outputs for N entries.
    /// @return   true if entries are found.
    bool FindEntries(const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const;

    /// Find deadpool announcements by hash of N.
    ///
    /// @param[in]    deadpoolId   The hash of N to search entries for.
    /// @param[out]   list         A list of pairs of block heights and transaction outputs for N announcements.
    /// @return   true if announcements are found.
    bool FindAnnounces(const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const;

    /// Find deadpool entries after a specified height.
    /// @param[in]    min_height The minimum height to search entries for.
    /// @param[out]   list       A list of transaction outputs for N entries.
    /// @return   true if entries are found.
    bool FindEntriesSinceHeight(const int min_height, std::vector<DeadpoolIndexEntry> &list) const;

    /// Find deadpool claims by entry outpoint.
    /// @param[in]    outpoint   The txhash and vout index of the entry
    /// @param[out]   claim      The claim data including solution
    /// @return   true if a claim is found.
    bool FindClaim(const COutPoint& outpoint, DeadpoolIndexClaim& claim) const;

};

/// The global deadpool index.
extern std::unique_ptr<DeadpoolIndex> g_deadpoolindex;

#endif // FACTORN_INDEX_DEADPOOLINDEX_H
