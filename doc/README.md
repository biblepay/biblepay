BiblePay Core
==========

This is the official reference wallet for BiblePay digital currency and comprises the backbone of the BiblePay peer-to-peer network. You can [download BiblePay Core](https://www.biblepay.org/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run BiblePay Core on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/biblepay-qt` (GUI) or
- `bin/biblepayd` (headless)

### Windows

Unpack the files into a directory, and then run biblepay-qt.exe.

### macOS

Drag BiblePay Core to your applications folder, and then run BiblePay Core.

### Need Help?

* See the [BiblePay documentation](https://docs.biblepay.org)
for help and more information.
* Ask for help on [BiblePay Discord](http://staybiblepayee.com)
* Ask for help on the [BiblePay Forum](https://biblepay.org/forum)

Building
---------------------
The following are developer notes on how to build BiblePay Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Android Build Notes](build-android.md)

Development
---------------------
The BiblePay Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- Source Code Documentation ***TODO***
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* See the [BiblePay Developer Documentation](https://biblepaycore.readme.io/)
  for technical specifications and implementation details.
* Discuss on the [BiblePay Forum](https://biblepay.org/forum), in the Development & Technical Discussion board.
* Discuss on [BiblePay Discord](http://staybiblepayee.com)
* Discuss on [BiblePay Developers Discord](http://chat.biblepaydevs.org/)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [biblepay.conf Configuration File](biblepay-conf.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [I2P Support](i2p.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [Managing Wallets](managing-wallets.md)
- [PSBT support](psbt.md)
- [Reduce Memory](reduce-memory.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
