#!/usr/bin/env python3
# Copyright (c) 2016-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from decimal import Decimal
from sympy import randprime

from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    tx_from_hex,
    from_hex,
    sha256,
)

from test_framework.script import (
    bn2vch,
    CScript,
    CScriptNum,
    OP_CHECKMULTISIG,
    OP_CHECKSIG,
    OP_DROP,
    OP_TRUE,
    OP_CHECKDIVVERIFY,
    OP_ANNOUNCEVERIFY,
    OP_ANNOUNCE
)

from test_framework.test_framework import BitcoinTestFramework

from test_framework.util import (
    assert_equal,
    try_rpc,
    hex_str_to_bytes,
    assert_raises_rpc_error
)

ANNOUNCE_MATURITY = 5 # regtest confirmations needed to mature an announcement
ANNOUNCE_EXPIRY = 100 # regtest confirmations needed to expire an announcement after maturity

def create_pqn(bits):
    p = randprime(1 << (bits-1), 1 << bits)
    q = randprime(1 << (bits-1), 1 << bits)
    n = p*q
    return p, q, n

def hash_number(n):
    return sha256(bn2vch(n))

def create_claim_hash(solution, claim_script):
    solution_hash = hash_number(solution)
    claim_script_hash = sha256(hex_str_to_bytes(claim_script))
    claim = bytearray(solution_hash) + bytearray(claim_script_hash)
    return sha256(bytes(claim))

def create_deadpool_entry(n):
    return CScript([n, OP_CHECKDIVVERIFY, OP_DROP, OP_ANNOUNCEVERIFY, OP_DROP, OP_DROP, OP_TRUE])

def create_deadpool_ann(n, claim_hash):
    return CScript([OP_ANNOUNCE, claim_hash, n])

class ExpiryTestCache():
    def is_valid(self):
        return (self.n is not None and
                self.q is not None and
                self.ann_height is not None and
                self.claim_script is not None)

class DeadpoolTest(BitcoinTestFramework):
    def setup_network(self):
        super().setup_network()

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.rpc_timeout = 60
        self.default_bounty_bits = 84
        self.expiry_test_cache = None

    def wait_for_async_indexer(self, deadpoolid):
        def wait_for_index():
            try:
                res = self.nodes[0].getdeadpoolentry(deadpoolid)
                return len(res['entries']) > 0
            except:
                return False
        self.wait_until(wait_for_index, timeout=10)

    def get_inputs(self, min_amount):
        inputs = self.nodes[0].listunspent(query_options={"minimumSumAmount": min_amount})
        ctxins = []
        for inpt in inputs:
            ctxins.append(CTxIn(COutPoint(int(inpt['txid'], 16), inpt['vout']), b''))
        return ctxins

    def create_sign_post_tx(self, in_amount, txouts):
        tx = CTransaction()
        tx.vin = self.get_inputs(in_amount)
        tx.vout = txouts
        tx_hex = self.nodes[0].signrawtransactionwithwallet(tx.serialize().hex())['hex']
        return self.nodes[0].sendrawtransaction(tx_hex, 0)

    def run_test(self):
        self.log.info("Create wallets")
        self.init_wallet(0)

        self.log.info("Generate 128 blocks to trigger the softfork.")
        for _ in range(4):
            self.nodes[0].generate(32)

        self.log.info("Run tests")
        self.expiry_test_stage1()
        self.end_to_end_claim_test()
        self.no_announce_test()
        self.hijack_announce_test()
        self.multiclaim_test()
        self.expiry_test_stage2()
        self.low_burn_test()

    def end_to_end_claim_test(self):
        self.log.info("END-TO-END CLAIM TEST STARTS")
        entry_amount = 0.5
        burn_amount = 0.01   # minimum for regtest
        fee_amount = 0.00001 # use a fee on the high side
        bounty = int(entry_amount * COIN)
        ann_amount = int(burn_amount * COIN)
        claim_amount = bounty - 200 # claim tx size will stay under 200 bytes

        balance = self.nodes[0].getbalance()
        assert balance > (entry_amount + burn_amount + (2 * fee_amount)) * 2

        self.log.info("Create integer for the deadpool")
        _, q, n = create_pqn(self.default_bounty_bits)

        hash_of_n = self.nodes[0].getdeadpoolid(str(n))
        assert hash_of_n == hash_number(n)[::-1].hex()

        self.log.info(" Post entry transaction")
        entry_txid = self.create_sign_post_tx(entry_amount + fee_amount, [CTxOut(bounty, create_deadpool_entry(n))])
        self.nodes[0].generate(1)

        self.log.info("Allow async indexer to catch up (10s max)")
        self.wait_for_async_indexer(hash_of_n)

        self.log.info("Check the recording of our entry")
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == n
        assert len(n_entries['entries']) == 1
        assert int(Decimal(n_entries['bounty'])*COIN) == bounty
        assert n_entries['entries'][0]['txid'] == entry_txid
        assert n_entries['entries'][0]['vout'] == 0
        assert int(Decimal(n_entries['entries'][0]['amount'])*COIN) == bounty

        self.log.info("Create need address and outputscript for the announcement")
        claim_address = self.nodes[0].getnewaddress()
        claim_script = self.nodes[0].validateaddress(claim_address)['scriptPubKey']
        claim_hash = create_claim_hash(q, claim_script)

        self.log.info("Post announcement transaction")
        ann_txid = self.create_sign_post_tx(burn_amount + fee_amount, [CTxOut(ann_amount, create_deadpool_ann(n, claim_hash))])
        self.nodes[0].generate(1)

        self.log.info("Create the claim transaction")
        claim_tx = CTransaction()
        claim_tx.vin.append(CTxIn(COutPoint(int(entry_txid, 16), 0), CScript([claim_hash, q])))
        claim_tx.vout.append(CTxOut(claim_amount, CScript(hex_str_to_bytes(claim_script))))

        self.log.info("Attempting to claim early fails")
        assert_raises_rpc_error(-26, "deadpool-claim-no-announce", self.nodes[0].sendrawtransaction, claim_tx.serialize().hex(), 0)

        self.log.info("Mature the announcement")
        self.nodes[0].generate(ANNOUNCE_MATURITY)

        self.log.info("Claiming the same transaction at maturity works")
        claim_txid = self.nodes[0].sendrawtransaction(claim_tx.serialize().hex(), 0)
        self.nodes[0].generate(1)

        tx_from_wallet = self.nodes[0].gettransaction(claim_txid)

        assert int(Decimal(tx_from_wallet['amount'])*COIN) == claim_amount
        assert tx_from_wallet['confirmations'] == 1
        self.log.info("END-TO-END CLAIM TEST ENDS")

    def no_announce_test(self):
        self.log.info("NO ANNOUNCE TEST STARTS")
        entry_amount = 0.001
        fee_amount = 0.00001 # use a fee on the high side
        bounty = int(entry_amount * COIN)
        claim_amount = bounty - 200 # claim tx size will stay under 200 bytes

        balance = self.nodes[0].getbalance()
        assert balance > (entry_amount + fee_amount) * 2

        _, q, n = create_pqn(self.default_bounty_bits)
        hash_of_n = self.nodes[0].getdeadpoolid(str(n))

        self.log.info("Post entry transaction")
        entry_txid = self.create_sign_post_tx(entry_amount + fee_amount, [CTxOut(bounty, create_deadpool_entry(n))])
        self.nodes[0].generate(1)

        self.log.info("Allow async indexer to catch up (10s max)")
        self.wait_for_async_indexer(hash_of_n)

        self.log.info("Check the recording of our entry")
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == n
        assert len(n_entries['entries']) == 1
        assert int(Decimal(n_entries['bounty'])*COIN) == bounty
        assert n_entries['entries'][0]['txid'] == entry_txid
        assert n_entries['entries'][0]['vout'] == 0
        assert int(Decimal(n_entries['entries'][0]['amount'])*COIN) == bounty

        self.log.info("Create a claim address")
        claim_address = self.nodes[0].getnewaddress()
        claim_script = self.nodes[0].validateaddress(claim_address)['scriptPubKey']
        claim_hash = create_claim_hash(q, claim_script)

        self.log.info("Mature a non-existing announcement")
        self.nodes[0].generate(ANNOUNCE_MATURITY)

        self.log.info("Claim transaction without announcement")
        claim_tx = CTransaction()
        claim_tx.vin.append(CTxIn(COutPoint(int(entry_txid, 16), 0), CScript([claim_hash, q])))
        claim_tx.vout.append(CTxOut(claim_amount, CScript(hex_str_to_bytes(claim_script))))
        assert_raises_rpc_error(-26, "deadpool-claim-no-announce", self.nodes[0].sendrawtransaction, claim_tx.serialize().hex(), 0)
        self.log.info("NO ANNOUNCE TEST ENDS")

    def hijack_announce_test(self):
        self.log.info("HIJACK TEST STARTS")
        entry_amount = 0.5
        burn_amount = 0.01   # minimum for regtest
        fee_amount = 0.00001 # use a fee on the high side
        bounty = int(entry_amount * COIN)
        ann_amount = int(burn_amount * COIN)
        claim_amount = bounty - 200 # claim tx size will stay under 200 bytes

        balance = self.nodes[0].getbalance()
        assert balance > (entry_amount + burn_amount + (2 * fee_amount)) * 2

        _, q, n = create_pqn(self.default_bounty_bits)
        hash_of_n = self.nodes[0].getdeadpoolid(str(n))

        self.log.info("Post entry transaction")
        entry_txid = self.create_sign_post_tx(entry_amount + fee_amount, [CTxOut(bounty, create_deadpool_entry(n))])
        self.nodes[0].generate(1)

        self.log.info("Allow async indexer to catch up (10s max)")
        self.wait_for_async_indexer(hash_of_n)

        self.log.info("Check the recording of our entry")
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == n
        assert len(n_entries['entries']) == 1
        assert int(Decimal(n_entries['bounty'])*COIN) == bounty
        assert n_entries['entries'][0]['txid'] == entry_txid
        assert n_entries['entries'][0]['vout'] == 0
        assert int(Decimal(n_entries['entries'][0]['amount'])*COIN) == bounty

        self.log.info("Create address for the announcement")
        claim_address = self.nodes[0].getnewaddress()
        claim_script = self.nodes[0].validateaddress(claim_address)['scriptPubKey']
        claim_hash = create_claim_hash(q, claim_script)

        self.log.info("Post announcement transaction")
        ann_txid = self.create_sign_post_tx(burn_amount + fee_amount, [CTxOut(ann_amount, create_deadpool_ann(n, claim_hash))])
        self.nodes[0].generate(1)

        self.log.info("Mature the announcement...")
        self.nodes[0].generate(ANNOUNCE_MATURITY)

        self.log.info("Network participant tries to hijack the bounty")
        evil_address = self.nodes[0].getnewaddress()
        evil_script = self.nodes[0].validateaddress(evil_address)['scriptPubKey']

        self.log.info("Claim transaction with original claim_hash")
        evil_tx1 = CTransaction()
        evil_tx1.vin.append(CTxIn(COutPoint(int(entry_txid, 16), 0), CScript([claim_hash, q])))
        evil_tx1.vout.append(CTxOut(claim_amount, CScript(hex_str_to_bytes(evil_script))))
        assert_raises_rpc_error(-26, "OP_ANNOUNCEVERIFY Claim-Hash does not match", self.nodes[0].sendrawtransaction, evil_tx1.serialize().hex(), 0)

        self.log.info("Claiming transaction with a claim_hash that matches the evil outscript")
        evil_claim_hash = create_claim_hash(q, evil_script)
        evil_tx2 = CTransaction()
        evil_tx2.vin.append(CTxIn(COutPoint(int(entry_txid, 16), 0), CScript([evil_claim_hash, q])))
        evil_tx2.vout.append(CTxOut(claim_amount, CScript(hex_str_to_bytes(evil_script))))
        assert_raises_rpc_error(-26, "deadpool-claim-no-announce", self.nodes[0].sendrawtransaction, evil_tx2.serialize().hex(), 0)

        self.log.info("Claiming with the announced outscript actually works")
        honest_tx = CTransaction()
        honest_tx.vin.append(CTxIn(COutPoint(int(entry_txid, 16), 0), CScript([claim_hash, q])))
        honest_tx.vout.append(CTxOut(claim_amount, CScript(hex_str_to_bytes(claim_script))))
        honest_txid = self.nodes[0].sendrawtransaction(honest_tx.serialize().hex(), 0)
        self.nodes[0].generate(1)

        tx_from_wallet = self.nodes[0].gettransaction(honest_txid)

        assert int(Decimal(tx_from_wallet['amount'])*COIN) == claim_amount
        assert tx_from_wallet['confirmations'] == 1
        self.log.info("HIJACK TEST ENDS")

    def multiclaim_test(self):
        self.log.info("MULTICLAIM TEST STARTS")
        entry_amount = Decimal("0.5")
        burn_amount = Decimal("0.01")   # minimum for regtest
        fee_amount = Decimal("0.00001") # use a fee on the high side
        bounty = int(entry_amount * COIN)
        ann_amount = int(burn_amount * COIN)
        claim_amount = (3*bounty) - 600 # claim tx size will stay under 600 bytes

        balance = self.nodes[0].getbalance()
        assert balance > ((3 * entry_amount) + (2*burn_amount) + (3 * fee_amount)) * 2

        _, q1, n1 = create_pqn(self.default_bounty_bits)
        hash_of_n1 = self.nodes[0].getdeadpoolid(str(n1))
        _, q2, n2 = create_pqn(self.default_bounty_bits)
        hash_of_n2 = self.nodes[0].getdeadpoolid(str(n2))

        entry_txid1 = self.create_sign_post_tx(2*(entry_amount + fee_amount), [
          CTxOut(bounty, create_deadpool_entry(n1)),
          CTxOut(bounty, create_deadpool_entry(n2)),
        ])
        entry_txid2 = self.create_sign_post_tx(entry_amount + fee_amount, [CTxOut(bounty, create_deadpool_entry(n1))])
        self.nodes[0].generate(1)

        self.log.info("Allow async indexer to catch up (2*10s max)")
        for hn in [hash_of_n1, hash_of_n2]:
            self.wait_for_async_indexer(hn)

        self.log.info("Check the recording of our entries")
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n1)
        assert int(n_entries['n']) == n1
        assert len(n_entries['entries']) == 2
        assert Decimal(n_entries['bounty']) == Decimal(entry_amount) * 2
        assert n_entries['entries'][0]['txid'] in [entry_txid1, entry_txid2]
        assert n_entries['entries'][0]['vout'] == 0
        assert int(Decimal(n_entries['entries'][0]['amount'])*COIN) == bounty
        assert n_entries['entries'][1]['txid'] in [entry_txid1, entry_txid2]
        assert n_entries['entries'][1]['vout'] == 0
        assert int(Decimal(n_entries['entries'][1]['amount'])*COIN) == bounty

        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n2)
        assert int(n_entries['n']) == n2
        assert len(n_entries['entries']) == 1
        assert int(Decimal(n_entries['bounty'])*COIN) == bounty
        assert n_entries['entries'][0]['txid'] == entry_txid1
        assert n_entries['entries'][0]['vout'] == 1
        assert int(Decimal(n_entries['entries'][0]['amount'])*COIN) == bounty

        self.log.info("Create a single address for both announcements")
        claim_address = self.nodes[0].getnewaddress()
        claim_script = self.nodes[0].validateaddress(claim_address)['scriptPubKey']
        claim_hash1 = create_claim_hash(q1, claim_script)
        claim_hash2 = create_claim_hash(q2, claim_script)

        self.log.info("Post the first announcement transaction")
        ann_txid = self.create_sign_post_tx(burn_amount + fee_amount, [CTxOut(ann_amount, create_deadpool_ann(n1, claim_hash1))])
        self.nodes[0].generate(1)

        self.log.info("Post the second announcement transaction")
        ann_txid = self.create_sign_post_tx(burn_amount + fee_amount, [CTxOut(ann_amount, create_deadpool_ann(n2, claim_hash2))])
        self.nodes[0].generate(1)

        self.log.info("Mature both announcements...")
        self.nodes[0].generate(ANNOUNCE_MATURITY)

        self.log.info("Claim all 3 bounties at once")
        claim_tx = CTransaction()
        claim_tx.vin.append(CTxIn(COutPoint(int(entry_txid1, 16), 0), CScript([claim_hash1, q1])))
        claim_tx.vin.append(CTxIn(COutPoint(int(entry_txid1, 16), 1), CScript([claim_hash2, q2])))
        claim_tx.vin.append(CTxIn(COutPoint(int(entry_txid2, 16), 0), CScript([claim_hash1, q1])))
        claim_tx.vout.append(CTxOut(claim_amount, CScript(hex_str_to_bytes(claim_script))))
        claim_txid = self.nodes[0].sendrawtransaction(claim_tx.serialize().hex(), 0)
        self.nodes[0].generate(1)

        tx_from_wallet = self.nodes[0].gettransaction(claim_txid)

        assert int(Decimal(tx_from_wallet['amount'])*COIN) == claim_amount
        assert tx_from_wallet['confirmations'] == 1
        self.log.info("MULTICLAIM TEST ENDS")

    def expiry_test_stage1(self):
        self.log.info("EXPIRY TEST STAGE1 STARTS")

        self.expiry_test_cache = ExpiryTestCache() # cache essential data to claim later

        entry_amount = 0.1
        burn_amount = 0.01   # minimum for regtest
        fee_amount = 0.00001 # use a fee on the high side
        bounty = int(entry_amount * COIN)
        ann_amount = int(burn_amount * COIN)

        _, self.expiry_test_cache.q, self.expiry_test_cache.n = create_pqn(self.default_bounty_bits)
        hash_of_n = self.nodes[0].getdeadpoolid(str(self.expiry_test_cache.n))

        self.log.info("Post entry transaction")
        entry_txid = self.create_sign_post_tx(entry_amount + fee_amount, [CTxOut(bounty, create_deadpool_entry(self.expiry_test_cache.n))])
        self.nodes[0].generate(1)

        self.log.info("Allow async indexer to catch up (10s max)")
        self.wait_for_async_indexer(hash_of_n)

        self.log.info("Check the recording of our entry")
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == self.expiry_test_cache.n
        assert len(n_entries['entries']) == 1
        assert int(Decimal(n_entries['bounty'])*COIN) == bounty
        assert n_entries['entries'][0]['txid'] == entry_txid
        assert n_entries['entries'][0]['vout'] == 0
        assert int(Decimal(n_entries['entries'][0]['amount'])*COIN) == bounty

        self.log.info("Create a claim address")
        claim_address = self.nodes[0].getnewaddress()
        self.expiry_test_cache.claim_script = self.nodes[0].validateaddress(claim_address)['scriptPubKey']
        claim_hash = create_claim_hash(self.expiry_test_cache.q, self.expiry_test_cache.claim_script)

        self.log.info("Post announcement transaction")
        ann_txid = self.create_sign_post_tx(burn_amount + fee_amount, [CTxOut(ann_amount, create_deadpool_ann(self.expiry_test_cache.n, claim_hash))])
        self.nodes[0].generate(1)

        self.log.info("Mature the announcement...")
        self.nodes[0].generate(ANNOUNCE_MATURITY)

        chaininfo = self.nodes[0].getblockchaininfo()
        self.expiry_test_cache.ann_height = chaininfo['blocks']

        assert self.expiry_test_cache.is_valid()

        self.log.info("EXPIRY TEST STAGE1 ENDS")

    def expiry_test_stage2(self):
        self.log.info("EXPIRY TEST STAGE2 STARTS")

        assert self.expiry_test_cache.is_valid()

        hash_of_n = self.nodes[0].getdeadpoolid(str(self.expiry_test_cache.n))

        self.log.info("Check the recording of our entry")
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == self.expiry_test_cache.n
        assert len(n_entries['entries']) == 1

        entry_txid = n_entries['entries'][0]['txid']
        entry_vout = n_entries['entries'][0]['vout']
        entry_amount = n_entries['bounty']
        claim_amount = int(entry_amount * COIN) - 200 # claim tx size will stay under 200 bytes
        ann_amount = int(Decimal("0.01") * COIN)

        chaininfo = self.nodes[0].getblockchaininfo()
        curheight = chaininfo['blocks']
        blocks_to_mine = (self.expiry_test_cache.ann_height + ANNOUNCE_EXPIRY + 1) - curheight

        self.log.info("Mine blocks until our announcement has expired")
        assert blocks_to_mine > 0
        self.nodes[0].generate(blocks_to_mine)

        claim_hash = create_claim_hash(self.expiry_test_cache.q, self.expiry_test_cache.claim_script)

        self.log.info("Create a claim transaction based on the announcement fails")
        claim_tx = CTransaction()
        claim_tx.vin.append(CTxIn(COutPoint(int(entry_txid, 16), entry_vout), CScript([claim_hash, self.expiry_test_cache.q])))
        claim_tx.vout.append(CTxOut(claim_amount, CScript(hex_str_to_bytes(self.expiry_test_cache.claim_script))))
        assert_raises_rpc_error(-26, "deadpool-claim-no-announce", self.nodes[0].sendrawtransaction, claim_tx.serialize().hex(), 0)

        self.log.info("Re-post the announcement in a new transaction (burn more coin)")
        ann_txid = self.create_sign_post_tx(Decimal("0.0101"), [CTxOut(ann_amount, create_deadpool_ann(self.expiry_test_cache.n, claim_hash))])
        self.nodes[0].generate(1)

        self.log.info("Mature the reposted announcement...")
        self.nodes[0].generate(ANNOUNCE_MATURITY)

        self.log.info("Claiming after the reposted announcement is successful")
        claim_txid = self.nodes[0].sendrawtransaction(claim_tx.serialize().hex(), 0)
        self.nodes[0].generate(1)

        tx_from_wallet = self.nodes[0].gettransaction(claim_txid)

        assert int(Decimal(tx_from_wallet['amount'])*COIN) == claim_amount
        assert tx_from_wallet['confirmations'] == 1

        self.log.info("EXPIRY TEST STAGE2 ENDS")

    def low_burn_test(self):
        self.log.info("LOW BURN TEST STARTS")
        entry_amount = 0.5
        burn_amount = 0.001   # below minimum for regtest
        fee_amount = 0.00001 # use a fee on the high side
        bounty = int(entry_amount * COIN)
        ann_amount = int(burn_amount * COIN)
        claim_amount = bounty - 200 # claim tx size will stay under 200 bytes

        balance = self.nodes[0].getbalance()
        assert balance > (entry_amount + burn_amount + (2 * fee_amount)) * 2

        self.log.info("Create integer for the deadpool")
        _, q, n = create_pqn(self.default_bounty_bits)

        hash_of_n = self.nodes[0].getdeadpoolid(str(n))
        assert hash_of_n == hash_number(n)[::-1].hex()

        self.log.info(" Post entry transaction")
        entry_txid = self.create_sign_post_tx(entry_amount + fee_amount, [CTxOut(bounty, create_deadpool_entry(n))])
        self.nodes[0].generate(1)

        self.log.info("Allow async indexer to catch up (10s max)")
        self.wait_for_async_indexer(hash_of_n)

        self.log.info("Check the recording of our entry")
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == n
        assert len(n_entries['entries']) == 1
        assert int(Decimal(n_entries['bounty'])*COIN) == bounty
        assert n_entries['entries'][0]['txid'] == entry_txid
        assert n_entries['entries'][0]['vout'] == 0
        assert int(Decimal(n_entries['entries'][0]['amount'])*COIN) == bounty

        self.log.info("Create need address and outputscript for the announcement")
        claim_address = self.nodes[0].getnewaddress()
        claim_script = self.nodes[0].validateaddress(claim_address)['scriptPubKey']
        claim_hash = create_claim_hash(q, claim_script)

        self.log.info("Post announcement transaction")
        ann_txid = self.create_sign_post_tx(burn_amount + fee_amount, [CTxOut(ann_amount, create_deadpool_ann(n, claim_hash))])
        self.nodes[0].generate(1)

        self.log.info("Mature the announcement")
        self.nodes[0].generate(ANNOUNCE_MATURITY)

        self.log.info("Create the claim transaction")
        claim_tx = CTransaction()
        claim_tx.vin.append(CTxIn(COutPoint(int(entry_txid, 16), 0), CScript([claim_hash, q])))
        claim_tx.vout.append(CTxOut(claim_amount, CScript(hex_str_to_bytes(claim_script))))
        assert_raises_rpc_error(-26, "deadpool-claim-no-announce", self.nodes[0].sendrawtransaction, claim_tx.serialize().hex(), 0)

        self.log.info("LOW BURN TEST ENDS")

if __name__ == '__main__':
    DeadpoolTest().main()
