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
)
from test_framework.script import (
    CScript,
    CScriptNum,
    OP_CHECKMULTISIG,
    OP_CHECKSIG,
    OP_DROP,
    OP_TRUE,
    OP_CHECKDIVVERIFY,
    OP_ANNOUNCEVERIFY,
)

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    try_rpc,
)

class CheckDivTest(BitcoinTestFramework):
    def setup_nodes(self):
        self.add_nodes(1, extra_args=[['-acceptnonstdtxn=1']])
        self.start_nodes()

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.rpc_timeout = 60

    def run_test(self):
        #Create wallets
        self.init_wallet(0)

        #Get wallet
        wallet_address0 = self.nodes[0].getnewaddress()

        #Generate enough blocks to activate checkdiv and have coins to spend.
        for _ in range(4):
            self.nodes[0].generate(32)

        #Send amount
        send_amount = 0.05

        #Send coins to wallet
        txid = self.nodes[0].sendtoaddress( wallet_address0, send_amount)
        self.nodes[0].generate(10)

        balance = self.nodes[0].getbalance()
        print("Balance: ", balance )

        #Create integer for the deadpool
        MAINNET_MINIMUM_DIFFICULTY = 230
        p1 = randprime(1 << (MAINNET_MINIMUM_DIFFICULTY-1), 1 << MAINNET_MINIMUM_DIFFICULTY)
        q1 = randprime(1 << (MAINNET_MINIMUM_DIFFICULTY-1), 1 << MAINNET_MINIMUM_DIFFICULTY)
        n  = p1*q1

        #Create transaction
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(txid, 16), 0), b''))
        tx.vout.append( CTxOut(int( (send_amount - 0.00000226) * COIN), CScript( [ CScriptNum(n) , OP_CHECKDIVVERIFY, OP_DROP, OP_DROP, OP_TRUE])) )
        tx_hex = self.nodes[0].signrawtransactionwithwallet(tx.serialize().hex())['hex']
        tx     = self.nodes[0].decoderawtransaction( tx_hex )
        txid1  = self.nodes[0].sendrawtransaction(tx_hex,0)
        tx     = tx_from_hex(tx_hex)
        self.nodes[0].generate(1)
        tx = self.nodes[0].decoderawtransaction( tx_hex )
        print(tx["vin"])
        print(tx["vout"])

        #Prepare scriptsig
        ans_scriptSig     = bytes.fromhex(hex(p1)[2:])
        ans_scriptSig_len = bytes.fromhex( hex( len(ans_scriptSig) )[2:] )
        scriptSig         = ans_scriptSig_len + ans_scriptSig[::-1]

        print()
        print("Hexadecimal")
        print( "P1: ", hex(p1) )
        print( "Q1: ", hex(q1) )
        print( " N: ", hex(n) )
        print("Decimal")
        print(  "N: ", n)
        print( "P1: ", p1 )
        print( "Q1: ", p1 )
        print()


        #Spend transaction.
        tx2 = CTransaction()
        tx2.vin.append( CTxIn(COutPoint(int(txid1, 16), 0), scriptSig ) )
        tx2.vout.append( CTxOut(int( (send_amount - 2*0.00000226) * COIN), CScript( [ CScriptNum(2**150) , OP_CHECKDIVVERIFY, OP_DROP, OP_DROP, OP_TRUE])) )
        txr = self.nodes[0].decoderawtransaction( tx2.serialize().hex() )
        print(txr["vin"])
        print(txr["vout"])

        txid2   = self.nodes[0].sendrawtransaction(  tx2.serialize().hex(),0  )
        self.nodes[0].generate(1)
        print(txid2)





        print("Done")


if __name__ == '__main__':
    CheckDivTest().main()
