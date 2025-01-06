// Copyright (c) 2023 AUTHOR
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FACTORN_DEADPOOL_H
#define FACTORN_DEADPOOL_H

#include <coins.h>                  // for Coin
#include <util/hasher.h>            // for SaltedTxidHasher
#include <primitives/transaction.h> // for CTxOut, COutPoint, CTransaction
#include <uint256.h>

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

class TxValidationState;
class CScriptBignum;

typedef std::unordered_set<uint256, SaltedTxidHasher> UniqueDeadpoolIds;

/** Helper class for deadpool announcements, extends Coin */
class CAnnounce : public Coin
{
public:
  explicit CAnnounce(CTxOut txOutIn, int32_t nHeightIn) : Coin(txOutIn, nHeightIn, false) {}

  /** Exposes the first pushdata element of the announcement */
  uint256 ClaimHash() const;
  /** Exposes the second pushdata element of the announcement */
  bool ReadN(std::vector<unsigned char> &n) const;
  /** Exposes hash of the second pushdata element of the announcement */
  uint256 NHash() const;
  /** Compact Announcement containing only deadpool Id and claim hash */
  std::pair<uint256, uint256> GetCompact() const;
};

class CLocdAnnouncement {
public:
    COutPoint locator;
    CAnnounce announcement;

    SERIALIZE_METHODS(CLocdAnnouncement, obj) {
      READWRITE(obj.locator);
      READWRITE(obj.announcement);
    }
};

//// Helper functions for announcements ////

/**
 * Extracts all announcements from a given transaction.
 *
 * @param [out] anns - list of located announcements (CLocdAnnouncement)
 * @returns boolean indicating if announcements were present in the transaction
 */
bool ExtractAnnouncements(const CTransaction& tx, const int32_t nHeight, std::vector<CLocdAnnouncement> &anns);

/**
 * Extracts all deadpool announcements from a list of txouts.
 *
 * @param [out] ids - unordered set of hash-of-N values that are attempted to be spent
 * @returns boolean indicating if any deadpool announcements were in the list of txouts
 */
bool ExtractDeadpoolAnnounceIds(const std::vector<CTxOut>& txouts, UniqueDeadpoolIds &ids);

/**
 * Extracts all deadpool entries from a list of txouts.
 *
 * @param [out] ids - unordered set of hash-of-N values that are attempted to be spent
 * @returns boolean indicating if any deadpool entries were in the list of utxo
 */
bool ExtractDeadpoolEntryIds(const std::vector<CTxOut>& txouts, UniqueDeadpoolIds &ids);

/**
 * Extract the claimhash from a txin that claims a deadpool entry.
 *
 * @returns uint256 with the claim hash, ZERO if no valid claimhash was found
 */
uint256 GetClaimHashFromScriptSig(const CTxIn& txin);

/**
 * Extract the solution from a txin that claims a deadpool entry.
 *
 * @returns CScriptBignum containing the solution
 */
CScriptBignum GetSolutionFromScriptSig(const CTxIn& txin);

/** Whether a transaction contains an Announcement. */
bool IsDeadpoolAnnouncement(const CTxOut& txout);

//// Helper functions for deadpool entries ////

bool IsDeadpoolEntry(const CTxOut& txout);

/** Get the N value of an entry txout */
void GetEntryN(const CTxOut& txout, std::vector<uint8_t>& dataN);

/** Get the hash of an entry's N value */
uint256 GetEntryNHash(const CTxOut& txout);

/** Hash an N into a uint256 using sha256 */
uint256 HashNValue(std::vector<uint8_t> dataN);

/** Consensus checks for deadpool integers (parsing, sizes, values and optionally canonical encoding) */
bool CheckDeadpoolInteger(const CScriptBignum& n, TxValidationState& state);

/** Consensus checks for deadpool integers (size, canonical encoding) on a TxOut*/
bool CheckTxOutDeadpoolIntegers(const CTxOut& txout, TxValidationState& state);

#endif //FACTORN_DEADPOOL_H
