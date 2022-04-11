// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AMOUNT_H
#define BITCOIN_AMOUNT_H

#include <stdint.h>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

static const CAmount COIN = 100000000;

/** No amount larger than this (in satoshi) is valid.
 *
 * Original Comment:
 * 	Note that this constant is *not* the total money supply, which in Bitcoin
 * 	currently happens to be less than 21,000,000 BTC for various reasons, but
 * 	rather a sanity check. As this sanity check is used by consensus-critical
 * 	validation code, the exact value of the MAX_MONEY constant is consensus
 * 	critical; in unusual circumstances like a(nother) overflow bug that allowed
 * 	for the creation of coins out of thin air modification could lead to a fork.
 *
 * FACT comment:
 *
 *	Same reasoning as above. However, we set it to the maximum multiple of COIN
 *  that fits in a 64-bit register and is positive under a signed interpretation
 *	of the data on the register. The FACTor Blockchain does not have a cap
 *	on the maximum number of coins, but given the exponential difficulty of
 *	factoring, until quantum computers become commodity hardware the coin supply
 *	will grow at as fast as a snail.
 *
 *	The constant was chosen as floor( 2^62 / 10^8 ) = 92233720368
 *      REASON: COIN = 10^8
 *              A 64-bit register fits all signed positve integers less than 2^63.
 * 
* */
static const CAmount MAX_MONEY = 46116860184ULL * COIN;
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif //  BITCOIN_AMOUNT_H
