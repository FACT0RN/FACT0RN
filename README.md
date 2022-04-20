#  <div align="center">  FACT0RN  </div>

A blockchain replacing hashing as Proof of Work (PoW) by integer factorization. A fork from bitcoin V22.0.

The FACT0RN blockchain seeks to allow its user to pay FACT coins to place integers in a deadpool for factorization. 

The whitepaper will be published in May, 2022.


## Installation


Binaries are provided for Ubuntu 20.04 LTS. To build from source follow these steps:

#### Depends build
From the repository root folder run the following:
```
make -C depends NO_QT=1                                && \
./autogen.sh                                           && \
./configure --prefix=`pwd`/depends/x86_64-pc-linux-gnu && \ 
make
```
    
#### Binaries
You can grab the binaries from the releases page, untar them and run them as-is.


## Mining

We have created a python script to mine. Here's what you will need to mine:

1. Wallet
2. Python 3
3. Put it all togehter

First, we need a node running. See the installation section. Once you have your
node running here's how to create a wallet, generate an address and extract the 
scriptPubKey value that will allow you to earn mining rewards.


From the project's root folder:

```
src/factorn-wallet  -wallet=<wallet name>  -descriptors create
src/factorn-cli    loadwallet <wallet name>
src/factorn-cli    getnewaddress
src/factorn-cli    getaddressinfo <address from previous command>
```

For example, from the get ``getaddresinfo`` command above you should get something
similar to:

```
{
  "address": "fact1q4zjycg88kyk72szmhpjvm82mu0zm6p26g7zeh8",
  "scriptPubKey": "0014a8a44c20e7b12de5405bb864cd9d5be3c5bd055a",
  "ismine": true,
  "solvable": true,
  "desc": "wpkh([67f532c6/84'/0'/0'/0/0]02de55517a22dc5a66d4a193cb877a3658b1c456827c1404d18e79dec82d5d937a)#0hcn4tny",
  "parent_desc": "wpkh([67f532c6/84'/0'/0']xpub6D7gQbDdRjuiMN9EqbEzWY4owSUfNNcRdA4E5aaYCX4VoRPMzgeaF4C15D6hSCUpvUkZvjKJTLktDVvjZ3beL8sfW1ATsNQ6qCsAkV6STtr/0/*)#zylutadk",
  "iswatchonly": false,
  "isscript": false,
  "iswitness": true,
  "witness_version": 0,
  "witness_program": "a8a44c20e7b12de5405bb864cd9d5be3c5bd055a",
  "pubkey": "02de55517a22dc5a66d4a193cb877a3658b1c456827c1404d18e79dec82d5d937a",
  "ischange": false,
  "timestamp": 1650416082,
  "hdkeypath": "m/84'/0'/0'/0/0",
  "hdseedid": "0000000000000000000000000000000000000000",
  "hdmasterfingerprint": "67f532c6",
  "labels": [
    ""
  ]
}
```

You will need the value from "scriptPubKey" to mine. In this example, this person
would use the value "0014a8a44c20e7b12de5405bb864cd9d5be3c5bd055a" to pass to the 
python mining script. This would credit the rewards of the block to their address.

Once you have this scriptPubKey value from your wallet clone the miner code from ``https://github.com/FACT0RN/factoring``. 

You will need to install the following python packages:

```
cypari2
gmpy2
numpy
sympy
base58
```

If you are using Anaconda, here are the commands to install the needed packages.

```
conda install -c conda-forge cypari2 
conda install -c anaconda numpy 
conda install -c conda-forge gmpy2 
conda install -c conda-forge sympy 
conda install -c conda-forge base58 
```

You will only need the ``FACTOR.py`` script. 

```
python FACTOR.py <scriptPubKey>
```

