// Copyright (c) 2023-2024 FactorN Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <deadpool/deadpool.h>

#include <consensus/validation.h>
#include <crypto/sha256.h>
#include <script/bignum.h>
#include <script/script.h>
#include <script/standard.h>

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

/** Hashes whatever data is in the scriptPubKey from pos 34 to end */
uint256 CAnnounce::NHash() const
{
    std::vector<uint8_t> dataN;
    if(!ReadN(dataN)) {
        return uint256(0);
    }
    return HashNValue(dataN);
}

bool CAnnounce::ReadN(std::vector<unsigned char> &n) const
{
  CScript::const_iterator it = out.scriptPubKey.begin()+34;
  opcodetype opcode;
  return (out.scriptPubKey.GetOp(it, opcode, n) && n.size() <= MAX_SCRIPT_ELEMENT_SIZE);
}

/** Returns the contents of scriptPubKey position 2 to 34 */
uint256 CAnnounce::ClaimHash() const
{
    CScript::const_iterator it = out.scriptPubKey.begin()+1;
    std::vector<uint8_t> claimHash;
    opcodetype opcode;
    out.scriptPubKey.GetOp(it, opcode, claimHash);
    return uint256(claimHash);
}

std::pair<uint256, uint256> CAnnounce::GetCompact() const
{
    return std::make_pair<uint256, uint256>(NHash(), ClaimHash());
}

bool ExtractAnnouncements(const CTransaction& tx, const int32_t nHeight, std::vector<CLocdAnnouncement> &anns) {
    bool fHasAnnouncement = false;
    std::vector<std::vector<unsigned char>> dummy;

    for (size_t i = 0; i < tx.vout.size(); i++) {
        TxoutType type = Solver(tx.vout[i].scriptPubKey, dummy);
        if (type == TxoutType::DEADPOOL_ANNOUNCE) {
            fHasAnnouncement = true;
            anns.push_back(CLocdAnnouncement({COutPoint(tx.GetHash(), i), CAnnounce(tx.vout[i], nHeight)}));
        }
    }

    return fHasAnnouncement;
}

bool ExtractDeadpoolAnnounceIds(const std::vector<CTxOut>& txouts, UniqueDeadpoolIds &ids) {
    bool fHasAnnouncement = false;
    for (auto txout : txouts) {
        if (IsDeadpoolAnnouncement(txout)) {
          fHasAnnouncement = true;
          CAnnounce ann(txout, 0);
          ids.emplace(ann.NHash());
        }
    }

    return fHasAnnouncement;
}

bool ExtractDeadpoolEntryIds(const std::vector<CTxOut>& txouts, UniqueDeadpoolIds &ids) {
    bool fHasEntries = false;
    for (auto txout : txouts) {
        if (IsDeadpoolEntry(txout)) {
          fHasEntries = true;
          ids.emplace(GetEntryNHash(txout));
        }
    }

    return fHasEntries;
}

/** Solve a script and compare against the expected type */
bool IsScriptOfType(const CScript script, const TxoutType expected) {
    std::vector<std::vector<unsigned char>> dummy;
    TxoutType type = Solver(script, dummy);
    return type == expected;
}

bool IsDeadpoolEntry(const CTxOut& txout) {
    return IsScriptOfType(txout.scriptPubKey, TxoutType::DEADPOOL_ENTRY);
}

bool IsDeadpoolAnnouncement(const CTxOut& txout) {
    return IsScriptOfType(txout.scriptPubKey, TxoutType::DEADPOOL_ANNOUNCE);
}

uint256 GetClaimHashFromScriptSig(const CTxIn& txin) {
    CScript::const_iterator it = txin.scriptSig.begin();
    opcodetype opcode;
    std::vector<uint8_t> firstPushdata;
    txin.scriptSig.GetOp(it, opcode, firstPushdata);

    if (firstPushdata.size() == 32) {
      return uint256(firstPushdata);
    }

    return uint256::ZERO;
}

CScriptBignum GetSolutionFromScriptSig(const CTxIn& txin) {
    CScript::const_iterator it = txin.scriptSig.begin()+33;
    opcodetype opcode;
    std::vector<unsigned char> secondPushdata;
    txin.scriptSig.GetOp(it, opcode, secondPushdata);
    return CScriptBignum(secondPushdata);
}

void GetEntryN(const CTxOut& txout, std::vector<uint8_t>& dataN) {
  CScript::const_iterator it = txout.scriptPubKey.begin();
  opcodetype opcode;
  txout.scriptPubKey.GetOp(it, opcode, dataN);
}

uint256 GetEntryNHash(const CTxOut& txout)
{
  std::vector<uint8_t> dataN;
  GetEntryN(txout, dataN);
  return HashNValue(dataN);
}

uint256 HashNValue(std::vector<uint8_t> dataN) {
  uint256 hashOfN;
  CSHA256 hasher;
  hasher.Write(dataN.data(), dataN.size()).Finalize(hashOfN.begin());
  return hashOfN;
}

namespace {
bool CheckDeadpoolInteger(const std::vector<uint8_t>& dataN, bool fCheckEncoding, TxValidationState& state)
{
    // zero bytes is invalid
    if (dataN.size() == 0) {
        return state.Invalid(TxValidationResult::TX_RECENT_CONSENSUS_CHANGE, "bad-bigint-zero");
    }

    CScriptBignum n(dataN);
    size_t bitsz = n.bits();

    // Must have a valid internal state
    if (!n.IsValid()) {
        return state.Invalid(TxValidationResult::TX_RECENT_CONSENSUS_CHANGE, "bad-bigint-invalid-number");
    }

    // cannot be 0 or 1
    if (n == 0 || n == 1) {
        return state.Invalid(TxValidationResult::TX_RECENT_CONSENSUS_CHANGE, "bad-bigint-zero");
    }

    // cannot be negative
    if (n.sign()) {
        return state.Invalid(TxValidationResult::TX_RECENT_CONSENSUS_CHANGE, "bad-bigint-negative");
    }

    // cannot be under 160 bits
    if (bitsz < 160) {
        return state.Invalid(TxValidationResult::TX_RECENT_CONSENSUS_CHANGE, "bad-bigint-too-small");
    }

    // cannot be over 4159 bits (520 max script element byte size (*8 = 4160) minus the signing bit (=4159))
    if (bitsz > (520 * 8) - 1) {
        return state.Invalid(TxValidationResult::TX_RECENT_CONSENSUS_CHANGE, "bad-bigint-too-large");
    }

    // We don't need to check encoding if we've constructed the number from a CScriptBignum
    if (!fCheckEncoding) {
        return true;
    }

    std::vector<uint8_t> canonicalN = n.Serialize();

    // if the given byte vector has a different size, fail early (cheaper)
    if (dataN.size() != canonicalN.size()) {
        return state.Invalid(TxValidationResult::TX_RECENT_CONSENSUS_CHANGE, "bad-bigint-non-canonical-size");
    }

    // byte vector data must match exactly with the gmp LE encoded byte array
    if (dataN != canonicalN) {
        return state.Invalid(TxValidationResult::TX_RECENT_CONSENSUS_CHANGE, "bad-bigint-non-canonical");
    }

    return true;
}
}// anon namespace

bool CheckDeadpoolInteger(const CScriptBignum& n, TxValidationState& state) {
    return CheckDeadpoolInteger(n.Serialize(), false, state);
}

bool CheckTxOutDeadpoolIntegers(const CTxOut& txout, TxValidationState& state)
{
    std::vector<std::vector<unsigned char>> dummy;
    TxoutType type = Solver(txout.scriptPubKey, dummy);

    // Check deadpool entries and announces
    std::vector<uint8_t> entry_n;
    if (type == TxoutType::DEADPOOL_ENTRY) {
        GetEntryN(txout, entry_n);
    } else if (type == TxoutType::DEADPOOL_ANNOUNCE) {
        CAnnounce ann = CAnnounce(txout, 0); // use dummy height
        ann.ReadN(entry_n);
    } else {
        return true;
    }

    // Bigints must be within range and canonically encoded
    return CheckDeadpoolInteger(entry_n, true, state);
}
