// Copyright (c) 2023 AUTHOR
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <deadpool/deadpool.h>
#include <script/script.h>
#include <util/strencodings.h>
#include <script/sign.h>

#include <test/util/setup_common.h>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(deadpool_tests, BasicTestingSetup)

const std::vector<uint8_t> valid_N = ParseHex("000000000000000000000000000000000000013f");
const uint256 valid_N_hash = uint256S("cadb7d0d071506edc955a377b26875136bd74bbaa48eb85bf3f090dfeddb17b3");

BOOST_AUTO_TEST_CASE(deadpool_entry)
{
  std::vector<CTxOut> vout;
  UniqueDeadpoolIds ids;

  // sunny case
  auto s = CScript();
  s << valid_N;
  s << OP_CHECKDIVVERIFY << OP_DROP << OP_ANNOUNCEVERIFY << OP_DROP << OP_DROP << OP_TRUE;

  const auto valid_out = CTxOut(CAmount(1000), s);

  BOOST_CHECK_EQUAL("14000000000000000000000000000000000000013fb975b8757551", HexStr(valid_out.scriptPubKey));
  std::vector<std::vector<unsigned char>> dummy;
  auto type_valid = Solver(valid_out.scriptPubKey, dummy);
  BOOST_CHECK(type_valid == TxoutType::DEADPOOL_ENTRY);
  BOOST_CHECK(IsDeadpoolEntry(valid_out));
  BOOST_CHECK_EQUAL(HashNValue(valid_N).GetHex(), valid_N_hash.GetHex());
  BOOST_CHECK(GetEntryNHash(valid_out).GetHex() == valid_N_hash.GetHex());

  vout.clear();
  ids.clear();
  vout.push_back(valid_out);
  BOOST_CHECK(ExtractDeadpoolEntryIds(vout, ids));
  BOOST_CHECK(ids.count(valid_N_hash) == 1);

  // extra padding
  const std::vector<uint8_t> extrapadded_N = ParseHex("0000000000000000000000000000000000000000013f");
  s.clear();
  s << extrapadded_N;
  s << OP_CHECKDIVVERIFY << OP_DROP << OP_ANNOUNCEVERIFY << OP_DROP << OP_DROP << OP_TRUE;
  const auto padded_out = CTxOut(CAmount(1000), s);

  auto type_padded = Solver(padded_out.scriptPubKey, dummy);
  BOOST_CHECK(type_padded == TxoutType::DEADPOOL_ENTRY);
  BOOST_CHECK(IsDeadpoolEntry(padded_out));
  BOOST_CHECK_EQUAL(HashNValue(extrapadded_N).GetHex(), valid_N_hash.GetHex());
  BOOST_CHECK(GetEntryNHash(padded_out).GetHex() == valid_N_hash.GetHex());

  vout.clear();
  ids.clear();
  vout.push_back(padded_out);
  BOOST_CHECK(ExtractDeadpoolEntryIds(vout, ids));
  BOOST_CHECK(ids.count(valid_N_hash) == 1);

  // rainy case
  s.clear();
  s << OP_CHECKDIVVERIFY << OP_DROP << OP_ANNOUNCEVERIFY << OP_DROP << OP_DROP << OP_TRUE; // No N
  const auto invalid_out = CTxOut(CAmount(1000), s);

  BOOST_CHECK_EQUAL("b975b8757551", HexStr(invalid_out.scriptPubKey));
  auto type_invalid = Solver(invalid_out.scriptPubKey, dummy);
  BOOST_CHECK(type_invalid == TxoutType::NONSTANDARD);
  BOOST_CHECK(!IsDeadpoolEntry(invalid_out));

  vout.clear();
  ids.clear();
  vout.push_back(invalid_out);
  BOOST_CHECK(!ExtractDeadpoolEntryIds(vout, ids));
  BOOST_CHECK(ids.size() == 0);

  // insufficient padding
  const std::vector<uint8_t> unpadded_N = ParseHex("013f");
  s.clear();
  s << unpadded_N;
  s << OP_CHECKDIVVERIFY << OP_DROP << OP_ANNOUNCEVERIFY << OP_DROP << OP_DROP << OP_TRUE;
  const auto unpadded_out = CTxOut(CAmount(1000), s);

  auto type_unpadded = Solver(unpadded_out.scriptPubKey, dummy);
  BOOST_CHECK(type_unpadded == TxoutType::NONSTANDARD);                    // fails standard check
  BOOST_CHECK(!IsDeadpoolEntry(unpadded_out));
  BOOST_CHECK_EQUAL(HashNValue(unpadded_N).GetHex(), valid_N_hash.GetHex()); // hash does equal
  BOOST_CHECK(GetEntryNHash(unpadded_out).GetHex() == valid_N_hash.GetHex());

  vout.clear();
  ids.clear();
  vout.push_back(invalid_out);
  BOOST_CHECK(!ExtractDeadpoolEntryIds(vout, ids));
  BOOST_CHECK(ids.size() == 0);

}

BOOST_AUTO_TEST_CASE(deadpool_announce)
{
  std::vector<CTxOut> vout;
  UniqueDeadpoolIds ids;

  auto dummy_hash = ParseHex("0100000000000000000000000000000000000000000000000000000000000001");
  auto s = CScript();
  s << OP_ANNOUNCE;
  s << dummy_hash;
  s << valid_N; // add N=319;

  // check that this is an announcement
  std::vector<std::vector<unsigned char>> dummy;
  auto type = Solver(s, dummy);
  BOOST_CHECK(type == TxoutType::DEADPOOL_ANNOUNCE);

  //announcements must be unspendable
  BOOST_CHECK(s.IsUnspendable());

  //well-formed announcements must have valid ops
  BOOST_CHECK(s.HasValidOps());

  const auto valid_burn = CTxOut(CAmount(1000), s);
  auto valid_announce = CAnnounce(valid_burn, 1);

  BOOST_CHECK(uint256(dummy_hash) == valid_announce.ClaimHash());

  std::vector<unsigned char> nFromScript;
  BOOST_CHECK(valid_announce.ReadN(nFromScript));
  BOOST_CHECK(nFromScript == valid_N);

  BOOST_CHECK(valid_N_hash == valid_announce.NHash());

  vout.clear();
  ids.clear();
  vout.push_back(valid_burn);
  BOOST_CHECK(ExtractDeadpoolAnnounceIds(vout, ids));
  BOOST_CHECK(ids.count(valid_N_hash) == 1);

}

BOOST_AUTO_TEST_SUITE_END()
