// Copyright (c) 2023 AUTHOR
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/deadpoolindex.h>

#include <deadpool/deadpool.h>
#include <deadpool/index_common.h>
#include <logging.h>
#include <script/standard.h>
#include <script/bignum.h>

constexpr uint8_t DB_DEADPOOL_ENTRY{'d'};
constexpr uint8_t DB_DEADPOOL_ANNOUNCE{'a'};
constexpr uint8_t DB_DEADPOOL_CLAIMS{'c'};

std::unique_ptr<DeadpoolIndex> g_deadpoolindex;

/** Access to the deadpool index database (indexes/deadpool/) */
class DeadpoolIndex::DB : public BaseIndex::DB
{
public:
    explicit DB(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    // Read all entry txOut data for a given hash of N. Returns false if the
    // given N is not indexed.
    bool ReadEntries(const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const;

    // Write a deadpool entry to the index.
    bool WriteEntry(const uint256& deadpoolId, const COutPoint& outPoint, const int height, const CTxOut& txOut);

    // Read all announcement txOut and blockheight pairs for a deadpool entry hash. Returns false if the
    // given N does not have any indexed announcements.
    bool ReadAnnounces(const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const;

    // Write a deadpool announcement to the index
    bool WriteAnnounce(const uint256& deadpoolId, const COutPoint& outPoint, const CAnnounce& ann);

    bool FindEntriesSinceHeight(const int min_height, std::vector<DeadpoolIndexEntry> &list) const;

    // Write a locator-indexed entry to the index
    bool WriteUnclaimedEntry(const COutPoint& outpoint, const uint256& deadpoolId);

    // Rewrite the locator-indexed entry with claim details
    bool WriteClaimedEntry(const COutPoint& outpoint,
                           const uint256& deadpoolId,
                           const int claimHeight,
                           const uint256& claimBlockHash,
                           const uint256& claimTxHash,
                           const std::vector<unsigned char> solution);

    // return a claim record based on entry outpoint
    bool ReadClaimRecord(const COutPoint& outpoint, DeadpoolIndexClaim& claim_record) const;

    // determine whether an entry remains unclaimed
    bool IsUnclaimedEntry(const COutPoint& outpoint, uint256& deadpoolId) const;

private:

    // generic writer and reader for entries and announcements as these store the same data
    bool WriteEntryOrAnnounce(const uint8_t type, const uint256& deadpoolId, const COutPoint& outPoint, const int height, const CTxOut& txOut);
    bool ReadEntryOrAnnounce(const uint8_t type, const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const;

    // specific writer for claims
    bool WriteClaimRecord(const COutPoint& outpoint,
                          const uint256& deadpoolId,
                          const int claimHeight,
                          const uint256& claimBlockHash,
                          const uint256& claimTxHash,
                          const std::vector<unsigned char> solution);
};

DeadpoolIndex::DB::DB(size_t n_cache_size, bool f_memory, bool f_wipe) :
    BaseIndex::DB(gArgs.GetDataDirNet() / "indexes" / "deadpool", n_cache_size, f_memory, f_wipe)
{}

bool DeadpoolIndex::DB::ReadEntries(const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const
{
    return ReadEntryOrAnnounce(DB_DEADPOOL_ENTRY, deadpoolId, list);
}

bool DeadpoolIndex::DB::WriteEntry(const uint256& deadpoolId, const COutPoint& outPoint, const int height, const CTxOut& txOut)
{
    return WriteEntryOrAnnounce(DB_DEADPOOL_ENTRY, deadpoolId, outPoint, height, txOut);
}

bool DeadpoolIndex::DB::ReadAnnounces(const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const
{
    return ReadEntryOrAnnounce(DB_DEADPOOL_ANNOUNCE, deadpoolId, list);
}

bool DeadpoolIndex::DB::WriteAnnounce(const uint256& deadpoolId, const COutPoint& outPoint, const CAnnounce& ann)
{
    return WriteEntryOrAnnounce(DB_DEADPOOL_ANNOUNCE, deadpoolId, outPoint, ann.nHeight, ann.out);
}

bool DeadpoolIndex::DB::WriteEntryOrAnnounce(const uint8_t type, const uint256& deadpoolId, const COutPoint& outPoint, const int height, const CTxOut& txOut)
{
    CDBBatch batch(*this);

    // store entries and announcements as:
    //    key = ((type, deadpoolId), COutPoint)
    //    value = (blockHeight, CTxOut)
    batch.Write(std::make_pair(std::make_pair(type, deadpoolId), outPoint), std::make_pair(height, txOut));
    return WriteBatch(batch, true);
}

bool DeadpoolIndex::DB::ReadEntryOrAnnounce(const uint8_t type, const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const
{
  // Note: need to const_cast DB for read operations because leveldb does not provide a
  //       const iterator.
  std::unique_ptr<CDBIterator> iter(const_cast<DeadpoolIndex::DB&>(*this).NewIterator());
  iter->Seek(std::make_pair(type, deadpoolId));

  while (iter->Valid()) {
      std::pair<std::pair<char, uint256>, COutPoint> key;
      std::pair<int, CTxOut> value;
      if (iter->GetKey(key) && key.first.first == type && key.first.second == deadpoolId) {
          if (iter->GetValue(value)) {
              list.push_back(DeadpoolIndexEntry({ key.first.second, key.second, value.first, value.second }));
          }
      } else {
          break;
      }
      iter->Next();
  }
  return true;
}

bool DeadpoolIndex::DB::FindEntriesSinceHeight(const int min_height, std::vector<DeadpoolIndexEntry> &list) const
{
    std::unique_ptr<CDBIterator> iter(const_cast<DeadpoolIndex::DB&>(*this).NewIterator());
    iter->Seek(DB_DEADPOOL_ENTRY);

    while (iter->Valid()) {
        std::pair<std::pair<char, uint256>, COutPoint> key;
        std::pair<int, CTxOut> value;
        if (iter->GetKey(key) && key.first.first == DB_DEADPOOL_ENTRY) {
            if (iter->GetValue(value)) {
                if (value.first < min_height) {
                    iter->Next();
                    continue;
                }
                list.push_back(DeadpoolIndexEntry({ key.first.second, key.second, value.first, value.second }));
            }
        } else {
            break;
        }
        iter->Next();
    }
    return true;
}

bool DeadpoolIndex::DB::WriteClaimRecord(const COutPoint& outpoint,
                                         const uint256& deadpoolId,
                                         const int claimHeight,
                                         const uint256& claimBlockHash,
                                         const uint256& claimTxHash,
                                         const std::vector<unsigned char> solution)
{
    CDBBatch batch(*this);
    const auto key = std::make_pair(DB_DEADPOOL_CLAIMS, std::make_pair(outpoint, deadpoolId));
    const auto value = std::make_pair(std::make_pair(claimHeight, claimBlockHash), std::make_pair(claimTxHash, solution));
    batch.Write(key, value);
    return WriteBatch(batch, true);
}

bool DeadpoolIndex::DB::WriteUnclaimedEntry(const COutPoint& outpoint, const uint256& deadpoolId)
{
    return WriteClaimRecord(outpoint, deadpoolId, 0, uint256::ZERO, uint256::ZERO, std::vector<unsigned char>({}));
}

bool DeadpoolIndex::DB::WriteClaimedEntry(const COutPoint& outpoint,
                                          const uint256& deadpoolId,
                                          const int claimHeight,
                                          const uint256& claimBlockHash,
                                          const uint256& claimTxHash,
                                          const std::vector<unsigned char> solution)
{
    return WriteClaimRecord(outpoint, deadpoolId, claimHeight, claimBlockHash, claimTxHash, solution);
}

bool DeadpoolIndex::DB::ReadClaimRecord(const COutPoint& outpoint, DeadpoolIndexClaim& claim_record) const
{
    std::unique_ptr<CDBIterator> iter(const_cast<DeadpoolIndex::DB&>(*this).NewIterator());
    iter->Seek(std::make_pair(DB_DEADPOOL_CLAIMS, outpoint));

    if (iter->Valid()) {
        std::pair<char, std::pair<COutPoint, uint256>> key;
        std::pair<std::pair<int, uint256>, std::pair<uint256, std::vector<unsigned char>>> value;

        if (iter->GetKey(key) && key.first == DB_DEADPOOL_CLAIMS && key.second.first.hash == outpoint.hash && key.second.first.n == outpoint.n) {
            if (!iter->GetValue(value)) {
                return false;
            }

            claim_record = {
                key.second.first, // Entry Locator
                key.second.second, // Deadpool ID
                value.first.first, // Claim Height
                value.first.second, // Claim blockhash
                value.second.first, // Claim txhash
                value.second.second // Solution
            };

            return true;
        }
    }

    return false;
}

bool DeadpoolIndex::DB::IsUnclaimedEntry(const COutPoint& outpoint, uint256& deadpoolId) const
{
    DeadpoolIndexClaim result{COutPoint(), uint256::ZERO, -1, uint256::ZERO, uint256::ZERO, std::vector<unsigned char>({})};

    if (ReadClaimRecord(outpoint, result)) {
        if (result.claimHeight == 0 && result.deadpoolId != uint256::ZERO) {
            deadpoolId = result.deadpoolId;
            return true;
        }
    }

    return false;
}

DeadpoolIndex::DeadpoolIndex(size_t n_cache_size, bool f_memory, bool f_wipe)
    : m_db(std::make_unique<DeadpoolIndex::DB>(n_cache_size, f_memory, f_wipe))
{}

DeadpoolIndex::~DeadpoolIndex() {}

bool DeadpoolIndex::WriteBlock(const CBlock& block, const CBlockIndex* pindex)
{
    // Exclude genesis block transaction because outputs are not spendable.
    if (pindex->nHeight == 0) return true;

    size_t nAnns = 0;
    size_t nEntries = 0;
    size_t nClaims = 0;

    for (const auto& tx : block.vtx) {
        for (size_t i = 0; i < tx->vout.size(); i++) {
            std::vector<std::vector<unsigned char>> dummy;
            TxoutType type = Solver(tx->vout[i].scriptPubKey, dummy);

            switch (type) {
                case TxoutType::DEADPOOL_ANNOUNCE: {
                    auto ann = CAnnounce(tx->vout[i], pindex->nHeight);
                    uint256 nhash = ann.NHash();
                    if (!m_db->WriteAnnounce(nhash, COutPoint(tx->GetHash(), i), ann)) {
                        return false;
                    }
                    LogPrint(BCLog::IDX, "DeadpoolIndex found announcement: txid=%s height=%d nHash=%s claim=%s\n",
                        tx->GetHash().ToString(), pindex->nHeight, nhash.ToString(), ann.ClaimHash().ToString());
                    nAnns += 1;
                    break;
                }
                case TxoutType::DEADPOOL_ENTRY: {
                    uint256 nhash = GetEntryNHash(tx->vout[i]);
                    // log the entry
                    if (!m_db->WriteEntry(nhash, COutPoint(tx->GetHash(), i), pindex->nHeight, tx->vout[i])) {
                        return false;
                    }

                    // log the claim entry (as unclaimed)
                    if (!m_db->WriteUnclaimedEntry(COutPoint(tx->GetHash(), i), nhash)) {
                        return false;
                    }

                    LogPrint(BCLog::IDX, "DeadpoolIndex found entry: txid=%s height=%d nHash=%s\n",
                        tx->GetHash().ToString(), pindex->nHeight, nhash.ToString());
                    nEntries += 1;
                    break;
                }
                default:
                    break;
            }
        }

        for (const auto& txin : tx->vin) {
            uint256 nhash_prevout;
            if (m_db->IsUnclaimedEntry(txin.prevout, nhash_prevout)) {

                const CScriptBignum solution = GetSolutionFromScriptSig(txin);
                if (!m_db->WriteClaimedEntry(txin.prevout, nhash_prevout, pindex->nHeight, pindex->GetBlockHash(), tx->GetHash(), solution.Serialize())) {
                    return false;
                }

                LogPrint(BCLog::IDX, "DeadpoolIndex found claim: txid=%s height=%d nHash=%s\n",
                    tx->GetHash().ToString(), pindex->nHeight, nhash_prevout.ToString());

                nClaims += 1;
            }
        }
    }

    LogPrint(BCLog::IDX, "DeadpoolIndex: hash=%s height=%d anns=%d entries=%d claims=%d\n",
        pindex->GetBlockHash().ToString(), pindex->nHeight, nAnns, nEntries, nClaims);

    return true;
}

BaseIndex::DB& DeadpoolIndex::GetDB() const { return *m_db; }

bool DeadpoolIndex::FindEntries(const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const
{
  return m_db->ReadEntries(deadpoolId, list);
}

bool DeadpoolIndex::FindAnnounces(const uint256& deadpoolId, std::vector<DeadpoolIndexEntry> &list) const
{
  return m_db->ReadAnnounces(deadpoolId, list);
}

bool DeadpoolIndex::FindEntriesSinceHeight(const int min_height, std::vector<DeadpoolIndexEntry> &list) const
{
    return m_db->FindEntriesSinceHeight(min_height, list);
}

bool DeadpoolIndex::FindClaim(const COutPoint& outpoint, DeadpoolIndexClaim& claim) const
{
    return m_db->ReadClaimRecord(outpoint, claim);
}
