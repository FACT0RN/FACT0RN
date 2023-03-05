# Apple Silicon Build Guide

**Updated for MacOS Ventura**

This guide describes how to build factornd and command-line utilities on Apple M1

## Dependencies

The following dependencies are **required**:

Library                                                    | Purpose    | Description
-----------------------------------------------------------|------------|----------------------
[automake](https://formulae.brew.sh/formula/automake)      | Build      | Generate makefile
[libtool](https://formulae.brew.sh/formula/libtool)        | Build      | Shared library support
[pkg-config](https://formulae.brew.sh/formula/pkg-config)  | Build      | Configure compiler and linker flags

## Preparation

The commands in this guide should be executed in a Terminal application.
macOS comes with a built-in Terminal located in:

```
/Applications/Utilities/Terminal.app
```

### 1. Xcode Command Line Tools

The Xcode Command Line Tools are a collection of build tools for macOS.
These tools must be installed in order to build FACT0RN from source.

To install, run the following command from your terminal:

``` bash
xcode-select --install
```

Upon running the command, you should see a popup appear.
Click on `Install` to continue the installation process.

### 2. Homebrew Package Manager

Homebrew is a package manager for macOS that allows one to install packages from the command line easily.
While several package managers are available for macOS, this guide will focus on Homebrew as it is the most popular.
Since the examples in this guide which walk through the installation of a package will use Homebrew, it is recommended that you install it to follow along.
Otherwise, you can adapt the commands to your package manager of choice.

To install the Homebrew package manager, see: https://brew.sh

Note: If you run into issues while installing Homebrew or pulling packages, refer to [Homebrew's troubleshooting page](https://docs.brew.sh/Troubleshooting).

### 3. Install Required Dependencies

The first step is to download the required dependencies.
These dependencies represent the packages required to get a barebones installation up and running.
To install, run the following from your terminal:

``` bash
brew install automake libtool pkg-config
```

### 4. Clone FACT0RN repository

`git` should already be installed by default on your system.

Now that all the required dependencies are installed, let's clone the FACT0RN repository to a directory.
All build scripts and commands will run from this directory.

``` bash
git clone https://github.com/FACT0RN/FACT0RN.git
```

## Building FACT0RN

### 1. Build dependencies

```bash
make -C depends NO_QT=1
```

Take note of the directory that the dependencies are installed in at the end of
this command, you will need this later. It looks something like

```console
...
copying packages: native_b2 native_ds_store native_mac_alias boost libevent
cryptopp gmp bdb sqlite miniupnpc libnatpmp zeromq
to: /Users/you/FACT0RN/depends/aarch64-apple-darwin22.3.0
```

### 2. Configuration

``` bash
./autogen.sh
./configure --prefix=/Users/you/FACT0RN/depends/aarch64-apple-darwin22.3.0 --with-gui=no
```

##### Further Configuration

You may want to dig deeper into the configuration options to achieve your desired behavior.
Examine the output of the following command for a full list of configuration options:

```bash
./configure -help
```

### 3. Compile

After configuration, you are ready to compile.
Run the following in your terminal to compile FACT0RN:

```bash
make        # use "-j N" here for N parallel jobs
```

## Running FACT0RN

factornd should now be available at `./src/factornd`.

The first time you run `factornd`, it will start downloading the blockchain.
This process could take up to a couple of hours.

By default, blockchain and wallet data files will be stored in:

``` bash
/Users/${USER}/Library/Application Support/Factorn/
```

Before running, you may create an empty configuration file:

```shell
mkdir -p "$HOME/Library/Application Support/Factorn"

touch "$HOME/Library/Application Support/Factorn/factorn.conf"

chmod 600 "$HOME/Library/Application Support/Factorn/factorn.conf"
```

You can monitor the download process by looking at the debug.log file:

```shell
tail -f $HOME/Library/Application\ Support/Factorn/debug.log
```

## Other commands:

```shell
./src/factornd -daemon      # Starts the daemon.
./src/factorn-cli --help    # Outputs a list of command-line options.
./src/factorn-cli help      # Outputs a list of RPC commands when the daemon is running.
```
