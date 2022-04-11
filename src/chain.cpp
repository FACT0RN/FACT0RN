// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <math.h>

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex) {
    if (pindex == nullptr) {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == nullptr) {
        return nullptr;
    }
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

CBlockIndex* CChain::FindEarliestAtLeast(int64_t nTime, int height) const
{
    std::pair<int64_t, int> blockparams = std::make_pair(nTime, height);
    std::vector<CBlockIndex*>::const_iterator lower = std::lower_bound(vChain.begin(), vChain.end(), blockparams,
        [](CBlockIndex* pBlock, const std::pair<int64_t, int>& blockparams) -> bool { return pBlock->GetBlockTimeMax() < blockparams.first || pBlock->nHeight < blockparams.second; });
    return (lower == vChain.end() ? nullptr : *lower);
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    const CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != nullptr &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    // The best estimate for the actual difficulty based on the factor sizes comes
    // the ECM algorithm and is about f(p) = e^( sqrt(2*log(p)*log(log(p)))), for p the smallest prime factor,
    // but we just need to encode this information in integer form, so an orderpreserving bijection from 
    // the domain of this function restricted to the bitsize of p will be enough
    // for our purposes. In particular, we can replace log by bitsize, base e by base 2 and
    // we still encode the information we will need.
    //
    // Because we will be adding f(p_0)+f(p_1)+...+f(p_n) for blocks 0,1,..,n
    // we need to approximate the additive property of exponentials. So, we can't
    // just add the exponents for every block else we get Sum(f(p_k)) = Prod(f(p_k)).
    //
    // The difficulty of factoring a 512-bit number is not the same as the difficulty
    // of factoring two 256-bit numbers.
    //
    // The working solution is simple.
    // This will work for factoring numbers up to 2048-bits; which is the limit the blockhain
    // has been designed for at the protocol level, some internal items will need to be updated to handle
    // factoring numbers bigger than 1024-bits, namely the type that handles integers, but that's a long way away. 
    //
    //     Let a = sqrt(2*bitsize(p)*log2(bitsize(p)))
    //     
    //     Now we get the integer part and the fractional part:
    //         aInt = floor(a)
    //         aFra = a - aInt
    //
    //     Now we compute our approximation of f, preserving only the properties we need, using integers.
    //       
    //     F(p) = 2^(aInt) + floor(1024*aFra)*2^(aInt-11) 
    //
    //     Table of values for reference:
    //
    //     bitsize(p)    aInt  floor(16*aFra)  hex( F(p) )
    
    //Get bitsize of nP1
    double nP1_bitsize = block.nP1.bits();

    //If bitsize outside working range, return 0:
    if  (nP1_bitsize < 16)  
	    return arith_uint256("0x0");
 
    //Compute exponent values
    const double   a    = sqrt( 2*nP1_bitsize*((double)log2(nP1_bitsize))) ;
    const uint64_t aInt = floor(a);
    const double   aFra = a - (double)aInt;

    //Get the tail value 
    std::stringstream stream;
    stream << std::hex << (int)(floor(1024.0f*aFra));
    std::string trail( stream.str() );

    //Initialize
    arith_uint256 t1("0x1");
    arith_uint256 t2( trail );
    
    //Compute work
    arith_uint256 work = ( t1 << aInt ) + ( t2 << (aInt - 11) );

    return work;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-nullptr. */
const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb) {
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}
