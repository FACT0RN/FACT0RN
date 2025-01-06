#!/usr/bin/env python3
# Copyright (c) 2016-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from sympy import randprime
import time

from test_framework.messages import sha256

from test_framework.script import bn2vch

from test_framework.test_framework import BitcoinTestFramework

from test_framework.util import (
    assert_equal,
    try_rpc,
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

class DeadpoolTest(BitcoinTestFramework):
    def setup_network(self):
        super().setup_network()

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.rpc_timeout = 60
        self.default_bounty_bits = 84
        self.expiry_test_cache = None

    def wait_for_async_indexer(self, deadpoolid, num_entries=1, num_anns=0):
        def wait_for_index():
            try:
                res = self.nodes[0].getdeadpoolentry(deadpoolid)
                return len(res['entries']) >= num_entries and len(res['announcements']) >= num_anns
            except:
                return False
        self.wait_until(wait_for_index, timeout=10)

    def run_test(self):
        self.log.info("Create wallets")
        self.init_wallet(0)

        self.log.info("Check that rpc functions error out before activation")
        assert_raises_rpc_error(-1, "Deadpool feature is not yet activated", self.nodes[0].listdeadpoolentries)

        self.log.info("Generate 128 blocks to trigger the softfork.")
        for _ in range(4):
            self.nodes[0].generate(32)

        self.log.info("Run tests")
        self.end_to_end_claim_test()


    def end_to_end_claim_test(self):
        self.log.info("END-TO-END CLAIM TEST STARTS")
        entry_amount = 0.1
        burn_amount = 0.01   # minimum for regtest

        balance = self.nodes[0].getbalance()
        assert balance > (entry_amount * 4 + burn_amount) * 2

        self.log.info("Create integer for the deadpool")
        p, _, n = create_pqn(self.default_bounty_bits)
        hash_of_n = hash_number(n)[::-1].hex()

        self.log.info("Create and post entry transactions")
        entry_txids = []
        for _ in range(4):
            entry_template = self.nodes[0].createdeadpoolentry(entry_amount, str(n))
            funded_entry = self.nodes[0].fundrawtransaction(entry_template, {"fee_rate": 10})
            signed_entry = self.nodes[0].signrawtransactionwithwallet(funded_entry['hex'])
            entry_txid = self.nodes[0].sendrawtransaction(signed_entry['hex'], 0)
            entry_txids.append(entry_txid)
            self.nodes[0].generate(1)

        self.log.info("Check the recording of our entries")
        self.wait_for_async_indexer(hash_of_n, 4)
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == n
        len(n_entries['entries']) == 4

        entry_list = self.nodes[0].listdeadpoolentries()
        found_entry = False
        for listed_entry in entry_list:
            if listed_entry['deadpoolid'] == hash_of_n:
                found_entry = True
                assert listed_entry['entries'] == 4
                assert listed_entry['announcements'] == 0
        assert found_entry == True

        self.log.info("Create an announcement transaction and post it")
        claim_address = self.nodes[0].getnewaddress()
        ann_template = self.nodes[0].announcedeadpoolclaim(burn_amount, claim_address, str(n), str(p))
        funded_ann = self.nodes[0].fundrawtransaction(ann_template, {'fee_rate': 10})
        signed_ann = self.nodes[0].signrawtransactionwithwallet(funded_ann['hex'])
        ann_txid = self.nodes[0].sendrawtransaction(signed_ann['hex'], 0)
        self.nodes[0].generate(1)

        self.log.info("Check the recording of our announcement")
        self.wait_for_async_indexer(hash_of_n, 4, 1)
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == n
        len(n_entries['announcements']) == 1

        entry_list = self.nodes[0].listdeadpoolentries()
        found_entry = False
        for listed_entry in entry_list:
            if listed_entry['deadpoolid'] == hash_of_n:
                found_entry = True
                assert listed_entry['announcements'] == 1
        assert found_entry == True

        self.log.info("Mature the announcement")
        self.nodes[0].generate(ANNOUNCE_MATURITY)

        self.log.info("Create a claim transaction for 2 entries and post it")
        claim_tx = self.nodes[0].claimdeadpooltxs(n_entries['entries'][:2], claim_address, str(p))
        claim_txid = self.nodes[0].sendrawtransaction(claim_tx, 0)
        claim_blockhash = self.nodes[0].generate(1)[0]

        tx_from_wallet = self.nodes[0].gettransaction(claim_txid)

        assert tx_from_wallet['confirmations'] == 1

        entry_list = self.nodes[0].listdeadpoolentries()
        found_entry = False
        for listed_entry in entry_list:
            if listed_entry['deadpoolid'] == hash_of_n:
                found_entry = True
                assert listed_entry['entries'] == 2
                assert listed_entry['announcements'] == 1
        assert found_entry == True

        self.log.info("Create a claim transaction the remaining entries and post it")
        claim_tx1 = self.nodes[0].claimdeadpoolid(hash_of_n, claim_address, str(p))
        claim_txid1 = self.nodes[0].sendrawtransaction(claim_tx1, 0)
        claim_blockhash1 = self.nodes[0].generate(1)[0]

        tx_from_wallet = self.nodes[0].gettransaction(claim_txid1)

        assert tx_from_wallet['confirmations'] == 1

        self.log.info("Check that the entry is no longer listed by default")
        entry_list = self.nodes[0].listdeadpoolentries()
        found_entry = False
        for listed_entry in entry_list:
            if listed_entry['deadpoolid'] == hash_of_n:
                found_entry = True
        assert found_entry == False

        self.log.info("Check that the entry is still listed in historical view")
        entry_list = self.nodes[0].listdeadpoolentries(1000,1000,True)
        found_entry = False
        for listed_entry in entry_list:
            if listed_entry['deadpoolid'] == hash_of_n:
                found_entry = True
        assert found_entry == True

        self.log.info("Check that the entry now list the claim info")
        n_entries = self.nodes[0].getdeadpoolentry(hash_of_n)
        assert int(n_entries['n']) == n
        assert len(n_entries['announcements']) == 1
        assert len(n_entries['entries']) == 4
        for entry in n_entries['entries']:
            assert entry['claimed'] == True
            assert entry['claim_blockhash'] in [claim_blockhash, claim_blockhash1]
            assert entry['claim_txid'] in [claim_txid, claim_txid1]
            assert int(entry['solution']) == p

        self.log.info("END-TO-END CLAIM TEST ENDS")

if __name__ == '__main__':
    DeadpoolTest().main()
