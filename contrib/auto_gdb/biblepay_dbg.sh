#!/usr/bin/env bash
# Copyright (c) 2018-2023 The BiblePay Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
# use testnet settings,  if you need mainnet,  use ~/.biblepaycore/biblepayd.pid file instead
export LC_ALL=C

biblepay_pid=$(<~/.biblepaycore/testnet3/biblepayd.pid)
sudo gdb -batch -ex "source debug.gdb" biblepayd ${biblepay_pid}
