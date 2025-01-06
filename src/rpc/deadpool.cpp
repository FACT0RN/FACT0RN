// Copyright (c) 2023 AUTHOR
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/params.h>
#include <deadpool/deadpool.h>
#include <deadpool/index_common.h>
#include <deploymentstatus.h>
#include <index/deadpoolindex.h>
#include <key_io.h>               // For DecodeDestination()
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/bignum.h>
#include <util/moneystr.h> // For FormatMoney
#include <gmp.h>
#include <validation.h>

#include <univalue.h>

class TxValidationState;

static void MakeClaimHash(const CTxDestination& destination, const CScriptBignum& solution, std::vector<unsigned char>& claim_hash)
{
    // hash p
    std::vector<unsigned char> p_serialized = solution.Serialize();
    std::vector<unsigned char> p_hash(32);
    CSHA256().Write(p_serialized.data(), p_serialized.size()).Finalize(p_hash.data());

    // hash the outscript
    CScript claim_script = GetScriptForDestination(destination);
    std::vector<unsigned char> claimscript_hash(32);
    CSHA256().Write(claim_script.data(), claim_script.size()).Finalize(claimscript_hash.data());

    // hash(hash(p) || hash(claimscript))
    CSHA256().Write(p_hash.data(), p_hash.size()).Write(claimscript_hash.data(), claimscript_hash.size()).Finalize(claim_hash.data());
}

static CTransaction createClaimTx(const std::vector<COutPoint>& entries,
                                  const CAmount& total_value,
                                  const CScriptBignum& solution,
                                  const CTxDestination& dest,
                                  const CAmount& fee_rate)
{
    CMutableTransaction rawTx;

    // create the claimhash from destination and p
    std::vector<unsigned char> claim_hash(32);
    MakeClaimHash(dest, solution, claim_hash);

    // create "signed" inputs for each entry
    CScript scriptSig = CScript() << claim_hash << solution.Serialize();
    for (auto entry : entries) {
        rawTx.vin.push_back(CTxIn(entry, scriptSig));
    }


    CScript claim_script = GetScriptForDestination(dest);

    // calculate size
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    size_t tx_size = ss.size() + claim_script.size() + 1 + 8 + 4; // 1, 8 and 4 are for size, amount and locktime

    CAmount amount_after_fee = total_value - (tx_size * fee_rate);
    rawTx.vout.push_back(CTxOut(amount_after_fee, claim_script));

    return CTransaction(rawTx);
}

static bool IsDeadpoolActivated(const ChainstateManager& chainman) {
    CChainState& active_chainstate = chainman.ActiveChainstate();
    const CBlockIndex* tip = active_chainstate.m_chain.Tip();
    const Consensus::Params& consensusParams = Params().GetConsensus();
    return DeploymentActiveAfter(tip, consensusParams, Consensus::DEPLOYMENT_DEADPOOL);
}

static RPCHelpMan getdeadpoolid()
{
    return RPCHelpMan{
        "getdeadpoolid",
        "\nReturns the deadpool id (hash) of a semiprime.\n",

        {
            {"n", RPCArg::Type::STR, RPCArg::Optional::NO, "The number to hash"},
        },

        RPCResult{
            RPCResult::Type::STR_HEX, "deadpoolid", "The hex-encoded hash for 'n'"
        },

        RPCExamples{
          HelpExampleCli("getdeadpoolid", "mysemiprime")
        },

        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    RPCTypeCheck(request.params, {UniValue::VSTR});

    CScriptBignum n(request.params[0].get_str());
    if (!n.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Invalid decimal number provided");
    }

    TxValidationState state;
    if (!CheckDeadpoolInteger(n, state)) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Invalid integer: %s", state.ToString()));
    }

    std::vector<uint8_t> dataN = n.Serialize();

    // hash it
    uint256 hash = HashNValue(dataN);
    return hash.GetHex();
}
    };
}

static RPCHelpMan getdeadpoolentry()
{
    return RPCHelpMan{
        "getdeadpoolentry",
        "\nReturns the deadpool entry for a given deadpoolid.\n",

        {
            {"deadpoolid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The id (hash) of the deadpool number."},
        },

        RPCResult{
          RPCResult::Type::OBJ, "", "",
          {
              {RPCResult::Type::STR, "n", "The value of N"},
              {RPCResult::Type::NUM, "bits", "Size of N in bits"},
              {RPCResult::Type::STR_HEX, "deadpoolid", "The deadpool id (same as provided)"},
              {RPCResult::Type::NUM, "bounty", "The total bounty in " + CURRENCY_UNIT},
              {RPCResult::Type::ARR, "entries", "",
              {
                  {RPCResult::Type::OBJ, "", "",
                  {
                      {RPCResult::Type::STR_HEX, "txid", "The entry transaction id"},
                      {RPCResult::Type::NUM, "vout", "The entry output number"},
                      {RPCResult::Type::NUM, "amount", "The claimable amount in " + CURRENCY_UNIT},
                      {RPCResult::Type::NUM, "height", "The block height of the entry"},
                      {RPCResult::Type::BOOL, "claimed", "Whether this entry was claimed"},
                      {RPCResult::Type::NUM, "claim_height", "The block height of the claim (optional)"},
                      {RPCResult::Type::STR_HEX, "claim_blockhash", "The block the claim transaction was mined in (optional)"},
                      {RPCResult::Type::STR_HEX, "claim_txid", "The claim transaction id (optional)"},
                      {RPCResult::Type::STR, "solution", "The solution provided (optional)"},
                  }},
              }},
              {RPCResult::Type::ARR, "announcements", "",
              {
                  {RPCResult::Type::OBJ, "", "",
                  {
                      {RPCResult::Type::STR_HEX, "txid", "The announcement transaction id"},
                      {RPCResult::Type::NUM, "vout", "The announcement output number"},
                      {RPCResult::Type::NUM, "burn_amount", "The burned amount in " + CURRENCY_UNIT},
                      {RPCResult::Type::NUM, "height", "The block height of the announcement"},
                  }},
              }},
            },
        },

        RPCExamples{
          HelpExampleCli("getdeadpoolentry", "mydeadpoolid")
        },

        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);

    if (!IsDeadpoolActivated(chainman)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Deadpool feature is not yet activated");
    }

    uint256 deadpoolId = ParseHashV(request.params[0], "parameter 1");

    if (!g_deadpoolindex) {
      throw JSONRPCError(RPC_MISC_ERROR, "Deadpool index not available");
    }

    std::vector<DeadpoolIndexEntry> entries;
    if (!g_deadpoolindex->FindEntries(deadpoolId, entries)) {
      throw JSONRPCError(RPC_MISC_ERROR, "Unable to query deadpool index.");
    }

    if (entries.size() == 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "No entries found.");
    }

    std::vector<DeadpoolIndexEntry> anns;
    if (!g_deadpoolindex->FindAnnounces(deadpoolId, anns)) {
        anns = std::vector<DeadpoolIndexEntry>{};
    }

    auto nTotal = CAmount(0);
    UniValue resEntries(UniValue::VARR);
    std::vector<uint8_t> dataN;
    bool extracted_n = false;
    for (auto entry : entries) {
      if (!extracted_n) {
          GetEntryN(entry.txOut, dataN);
          extracted_n = true;
      }
      UniValue obj(UniValue::VOBJ);
      obj.pushKV("txid", entry.locator.hash.GetHex());
      obj.pushKV("vout", uint64_t(entry.locator.n));
      obj.pushKV("amount", ValueFromAmount(entry.txOut.nValue));
      obj.pushKV("height", entry.height);

      DeadpoolIndexClaim result{COutPoint(), uint256::ZERO, -1, uint256::ZERO, uint256::ZERO, std::vector<unsigned char>({})};
      if (g_deadpoolindex->FindClaim(entry.locator, result) && result.claimHeight > 0) {
          obj.pushKV("claimed", true);
          obj.pushKV("claim_height", result.claimHeight);
          obj.pushKV("claim_blockhash", result.claimBlockHash.GetHex());
          obj.pushKV("claim_txid", result.claimTxHash.GetHex());
          obj.pushKV("solution", CScriptBignum(result.solution).GetDec());
      } else {
          obj.pushKV("claimed", false);
      }

      resEntries.push_back(obj);
      nTotal += entry.txOut.nValue;
    }

    UniValue resAnns(UniValue::VARR);
    for (auto ann : anns) {
      UniValue obj(UniValue::VOBJ);
      obj.pushKV("txid", ann.locator.hash.GetHex());
      obj.pushKV("vout", uint64_t(ann.locator.n));
      obj.pushKV("burn_amount", ValueFromAmount(ann.txOut.nValue));
      obj.pushKV("height", ann.height);
      resAnns.push_back(obj);
    }

    UniValue result(UniValue::VOBJ);

    CScriptBignum n(dataN);

    result.pushKV("n", n.GetDec());
    result.pushKV("bits", (uint64_t)n.bits());
    result.pushKV("deadpoolid", deadpoolId.GetHex());
    result.pushKV("bounty", ValueFromAmount(nTotal));
    result.pushKV("entries", resEntries);
    result.pushKV("announcements", resAnns);

    return result;
}
    };
}

static RPCHelpMan listdeadpoolentries()
{
  return RPCHelpMan{
      "listdeadpoolentries",
      "\nReturns a list of deadpool entries and their stats.\n",

      {
          {"num_blocks", RPCArg::Type::NUM, RPCArg::Default{1000}, "The number of blocks to crawl back"},
          {"limit", RPCArg::Type::NUM, RPCArg::Default{1000}, "The maximum number of results"},
          {"include_claimed", RPCArg::Type::BOOL, RPCArg::Default{false}, "Include entries that have been claimed"},
          {"include_announced", RPCArg::Type::BOOL, RPCArg::Default{true}, "Include entries that have an announcement"}
      },
      RPCResult{
        RPCResult::Type::ARR, "results", "",
        {
            {RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "deadpoolid", "The deadpool id."},
                {RPCResult::Type::NUM, "bounty", "The total bounty in " + CURRENCY_UNIT},
                {RPCResult::Type::NUM, "entries", "The number of entries to this deadpoolid."},
                {RPCResult::Type::NUM, "total_announcements", "The total number of announcements to this entry."}
            }}
        }
      },
      RPCExamples{
        HelpExampleCli("listdeadpoolentries", "") +
        HelpExampleCli("listdeadpoolentries", "100 1000 0")
      },

      [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    const NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);

    if (!IsDeadpoolActivated(chainman)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Deadpool feature is not yet activated");
    }

    int num_blocks = 1000;
    if (!request.params[0].isNull()) {
        num_blocks = request.params[0].get_int();
    }

    int num_results = 1000;
    if (!request.params[1].isNull()) {
        num_results = request.params[1].get_int();
    }

    bool include_claimed = false;
    if (!request.params[2].isNull()) {
        include_claimed = request.params[2].get_bool();
    }

    bool include_announced = true;
    if (!request.params[3].isNull()) {
        include_announced = request.params[3].get_bool();
    }

    if (!g_deadpoolindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Deadpool index not available");
    }

    int target_height;
    {
        LOCK(cs_main);
        CChainState& active_chainstate = chainman.ActiveChainstate();
        CCoinsViewCache* coins_view = &active_chainstate.CoinsTip();
        CBlockIndex* pindex = active_chainstate.m_blockman.LookupBlockIndex(coins_view->GetBestBlock());
        target_height = std::max(pindex->nHeight - num_blocks, 1);
    }

    std::vector<DeadpoolIndexEntry> foundEntries;
    if (!g_deadpoolindex->FindEntriesSinceHeight(target_height, foundEntries)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unable to query deadpool index.");
    }

    size_t results = 0;
    UniValue res(UniValue::VARR);
    UniqueDeadpoolIds processed_ids;
    for (auto entry : foundEntries) {

      if (results >= num_results) break;

      auto nTotal = CAmount(0);
      uint64_t num_entries = 0;
      uint64_t num_anns;

      // only process each entry once;
      auto searchId = processed_ids.find(entry.deadpoolId);
      if (searchId != processed_ids.end()) {
          continue;
      }

      processed_ids.insert(entry.deadpoolId);

      // first check announcements, in case this is filtered
      std::vector<DeadpoolIndexEntry> anns;
      if (g_deadpoolindex->FindAnnounces(entry.deadpoolId, anns)) {
          if (!include_announced && anns.size() > 0) {
              continue;
          }
          num_anns = anns.size();
      }

      // query the index again for all relevant entries to a crawled deadpoolid
      std::vector<DeadpoolIndexEntry> all_entries;
      if (g_deadpoolindex->FindEntries(entry.deadpoolId, all_entries)) {
          for (auto other_entry : all_entries) {
              if (!include_claimed) {
                  DeadpoolIndexClaim result{COutPoint(), uint256::ZERO, -1, uint256::ZERO, uint256::ZERO, std::vector<unsigned char>({})};
                  if (g_deadpoolindex->FindClaim(other_entry.locator, result) && result.claimHeight > 0) {
                      continue;
                  }
              }

              nTotal += other_entry.txOut.nValue;
              num_entries += 1;

          }
      }

      if (num_entries > 0) {
          UniValue obj(UniValue::VOBJ);
          obj.pushKV("deadpoolid", entry.deadpoolId.GetHex());
          obj.pushKV("bounty", ValueFromAmount(nTotal));
          obj.pushKV("entries", num_entries);
          obj.pushKV("announcements", num_anns);
          res.push_back(obj);
          results += 1;
      }
    }

    return res;
}
    };
}

static RPCHelpMan createdeadpoolentry()
{
    return RPCHelpMan{
        "createdeadpoolentry",
        "\nCreates a transaction template for a deadpool entry.\n"
        "This template can subsequently be funded with fundrawtransaction\n",
        {
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The amount in " + CURRENCY_UNIT + " to be claimed with the entry."},
            {"n", RPCArg::Type::STR, RPCArg::Optional::NO, "The number to create the entry for, in decimal notation."}
        },
        RPCResult{
            RPCResult::Type::STR_HEX, "tx", "The unfunded transaction template."
        },
        RPCExamples{
            "\nCreate a deadpool entry for 1.0 " + CURRENCY_UNIT + "\n"
            + HelpExampleCli("createdeadpoolentry", "1.0 \"yoursemiprime\"") +
            "\nAdd sufficient unsigned inputs to meet the output value\n"
            + HelpExampleCli("fundrawtransaction", "\"entrytransactionhex\"") +
            "\nSign the transaction\n"
            + HelpExampleCli("signrawtransactionwithwallet", "\"fundedtransactionhex\"") +
            "\nSend the transaction\n"
            + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
        },

        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    RPCTypeCheck(request.params, {UniValueType(), UniValue::VSTR});

    const NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);

    if (!IsDeadpoolActivated(chainman)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Deadpool feature is not yet activated");
    }

    CAmount amount = AmountFromValue(request.params[0]);
    CScriptBignum n(request.params[1].get_str());

    if (!n.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Invalid decimal number provided");
    }

    TxValidationState state;
    if (!CheckDeadpoolInteger(n, state)) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Invalid integer: %s", state.ToString()));
    }

    CMutableTransaction rawTx;
    CScript outscript = CScript() << n.Serialize();
    outscript << OP_CHECKDIVVERIFY << OP_DROP << OP_ANNOUNCEVERIFY << OP_DROP << OP_DROP << OP_TRUE;
    CTxOut out(amount, outscript);
    rawTx.vout.push_back(out);

    return EncodeHexTx(CTransaction(rawTx));
}
    };
}

static RPCHelpMan announcedeadpoolclaim()
{
    return RPCHelpMan{
        "announcedeadpoolclaim",
        "\nCreates a transaction template for a deadpool announcement of a future claim.\n"
        "This template can subsequently be funded with fundrawtransaction\n",
        {
            {"burn_amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The amount in " + CURRENCY_UNIT + " to be burned with the announcement."},
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to claim to in the future." },
            {"entry_n", RPCArg::Type::STR, RPCArg::Optional::NO, "The number to claim, in decimal notation."},
            {"solution", RPCArg::Type::STR, RPCArg::Optional::NO, "The solution, in decimal notation."}
        },
        RPCResult{
            RPCResult::Type::STR_HEX, "tx", "The unfunded transaction template."
        },
        RPCExamples{
            "\nGenerate a new address (write this down)\n"
            + HelpExampleCli("getnewaddress", "") +
            "\nCreate a deadpool announcement burning 0.1 " + CURRENCY_UNIT + "\n"
            + HelpExampleCli("announcedeadpoolclaim", "0.1 \"address\" \"entry number\" \"solution\"") +
            "\nAdd sufficient unsigned inputs to meet the output value\n"
            + HelpExampleCli("fundrawtransaction", "\"entrytransactionhex\"") +
            "\nSign the transaction\n"
            + HelpExampleCli("signrawtransactionwithwallet", "\"fundedtransactionhex\"") +
            "\nSend the transaction\n"
            + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
        },

        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    RPCTypeCheck(request.params, {UniValueType(), UniValue::VSTR});

    const NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);

    if (!IsDeadpoolActivated(chainman)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Deadpool feature is not yet activated");
    }

    CAmount amount = AmountFromValue(request.params[0]);
    CAmount minBurn = CAmount(Params().GetConsensus().nDeadpoolAnnounceMinBurn);
    if (amount < minBurn) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Burn amount should be at least %s", FormatMoney(minBurn)));
    }

    // check address input
    const std::string address = request.params[1].get_str();
    CTxDestination destination = DecodeDestination(address);

    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + address);
    }

    // check n
    CScriptBignum n(request.params[2].get_str());

    if (!n.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Invalid decimal number provided for entry_n");
    }

    TxValidationState state;
    if (!CheckDeadpoolInteger(n, state)) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Invalid entry_n integer: %s", state.ToString()));
    }

    // check p
    CScriptBignum p(request.params[3].get_str());

    if (!p.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Invalid decimal number provided for solution");
    }

    // check solution
    if ((n % p) != 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Solution is not valid for given entry");
    }

    // create the claimhash from destination and p
    std::vector<unsigned char> claim_hash(32);
    MakeClaimHash(destination, p, claim_hash);

    CMutableTransaction rawTx;
    CScript outscript = CScript() << OP_ANNOUNCE << claim_hash << n.Serialize();
    CTxOut out(amount, outscript);
    rawTx.vout.push_back(out);

    return EncodeHexTx(CTransaction(rawTx));
}
    };
}

static RPCHelpMan claimdeadpooltxs()
{
    return RPCHelpMan{
        "claimdeadpooltxs",
        "\nCreates a transaction for a deadpool claim.\n",
        {
            {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The inputs",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            },
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to claim to." },
            {"solution", RPCArg::Type::STR, RPCArg::Optional::NO, "The solution, in decimal notation."},
        },
        RPCResult{
            RPCResult::Type::STR_HEX, "tx", "The claim transaction."
        },
        RPCExamples{
            HelpExampleCli("claimdeadpooltxs", "[{\"txid\": \"entry_txid\", \"vout\": entry_vout}] \"your_address\" \"solution\"")
        },

        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    LOCK(cs_main);
    const NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);

    if (!IsDeadpoolActivated(chainman)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Deadpool feature is not yet activated");
    }

    CChainState& active_chainstate = chainman.ActiveChainstate();
    CCoinsViewCache* coins_view = &active_chainstate.CoinsTip();
    CBlockIndex* pindex = active_chainstate.m_blockman.LookupBlockIndex(coins_view->GetBestBlock());

    // check address input
    const std::string address = request.params[1].get_str();
    CTxDestination destination = DecodeDestination(address);

    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + address);
    }

    // check p
    CScriptBignum p(request.params[2].get_str());

    if (!p.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Invalid decimal number provided for solution");
    }

    // parse and lookup inputs
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, inputs argument must be non-null");
    }

    UniValue inputs = request.params[0].get_array();
    std::vector<uint8_t> entry_n;
    std::vector<COutPoint> entries;
    CAmount total_bounty = 0;
    bool have_n = false;
    for (size_t idx = 0; idx < inputs.size(); ++idx) {
        const UniValue& p = inputs[idx];

        UniValue entry = p.get_obj();
        RPCTypeCheckObj(entry,
            {
                {"txid", UniValueType(UniValue::VSTR)},
                {"vout", UniValueType(UniValue::VNUM)}
            });

        uint256 txid = ParseHashO(entry, "txid");

        int nOut = find_value(entry, "vout").get_int();
        if (nOut < 0) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout cannot be negative");
        }

        COutPoint locator(txid, nOut);
        Coin coin;
        if (!coins_view->GetCoin(locator, coin)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("Unable to find entry for %s:%d", txid.GetHex(), nOut));
        }

        CTxOut entry_out = coin.out;

        if (!IsDeadpoolEntry(entry_out)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TxOut %s:%d is not a deadpool entry", txid.GetHex(), nOut));
        }

        if (!have_n) {
            GetEntryN(entry_out, entry_n);
            have_n = true;
        } else {
            std::vector<uint8_t> this_n;
            GetEntryN(entry_out, this_n);
            if (this_n != entry_n) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("Entry %s:%d mismatches other entries", txid.GetHex(), nOut));
            }
        }

        entries.push_back(locator);
        total_bounty += entry_out.nValue;
    }

    // read n
    CScriptBignum n(entry_n);

    // check solution
    if ((n % p) != 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Solution is not valid for given entry");
    }

    CAmount feerate = 10;

    CTransaction tx = createClaimTx(entries, total_bounty, p, destination, feerate);
    return EncodeHexTx(tx);
}
    };
}

static RPCHelpMan claimdeadpoolid()
{
    return RPCHelpMan{
        "claimdeadpoolid",
        "\nCreates a transaction for a deadpool claim by entry id.\n",
        {
            {"deadpoolid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The deadpool entry id to claim."},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to claim to." },
            {"solution", RPCArg::Type::STR, RPCArg::Optional::NO, "The solution, in decimal notation."},
        },
        RPCResult{
            RPCResult::Type::STR_HEX, "tx", "The claim transaction."
        },
        RPCExamples{
            HelpExampleCli("claimdeadpoolid", "\"deadpoolid\" \"your_address\" \"solution\"")
        },

        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    LOCK(cs_main);
    const NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);

    if (!IsDeadpoolActivated(chainman)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Deadpool feature is not yet activated");
    }

    CChainState& active_chainstate = chainman.ActiveChainstate();
    CCoinsViewCache* coins_view = &active_chainstate.CoinsTip();
    CBlockIndex* pindex = active_chainstate.m_blockman.LookupBlockIndex(coins_view->GetBestBlock());

    // check address input
    const std::string address = request.params[1].get_str();
    CTxDestination destination = DecodeDestination(address);

    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address: ") + address);
    }

    // check p
    CScriptBignum p(request.params[2].get_str());

    if (!p.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Invalid decimal number provided for solution");
    }

    // fetch deadpoolid
    uint256 deadpoolId = ParseHashV(request.params[0], "parameter 1");

    // query the index for all entries
    if (!g_deadpoolindex) {
      throw JSONRPCError(RPC_MISC_ERROR, "Deadpool index not available");
    }

    std::vector<DeadpoolIndexEntry> entries;
    if (!g_deadpoolindex->FindEntries(deadpoolId, entries)) {
      throw JSONRPCError(RPC_MISC_ERROR, "Unable to query deadpool index");
    }

    if (entries.size() == 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "No entries found");
    }


    std::vector<uint8_t> entry_n;
    std::vector<COutPoint> unclaimed_entries;
    CAmount total_bounty = 0;
    bool have_n = false;
    for (auto entry : entries) {
        Coin coin;
        if (!coins_view->GetCoin(entry.locator, coin)) {
            // indexed entry does not exist (already claimed), move on to the next
            continue;
        }

        if (!have_n) {
            GetEntryN(entry.txOut, entry_n);
            have_n = true;
        }

        unclaimed_entries.push_back(entry.locator);
        total_bounty += entry.txOut.nValue;
    }

    if (unclaimed_entries.size() < 1 || !have_n) {
        throw JSONRPCError(RPC_MISC_ERROR, "No entries found");
    }

    // read n
    CScriptBignum n(entry_n);

    // check solution
    if ((n % p) != 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Solution is not valid for given entry");
    }

    CAmount feerate = 10;
    CTransaction tx = createClaimTx(unclaimed_entries, total_bounty, p, destination, feerate);

    return EncodeHexTx(tx);
}
    };
}

void RegisterDeadpoolRPCCommands(CRPCTable &t)
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category                 actor (function)
  //  ---------------------    -----------------------
    { "deadpool",              &getdeadpoolid,              },
    { "deadpool",              &getdeadpoolentry,           },
    { "deadpool",              &listdeadpoolentries,        },
    { "deadpool",              &createdeadpoolentry,        },
    { "deadpool",              &announcedeadpoolclaim,      },
    { "deadpool",              &claimdeadpooltxs,           },
    { "deadpool",              &claimdeadpoolid,            },
};
// clang-format on
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
