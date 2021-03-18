Changelog
2-19-2021
R Andrews
BiblePay

Changes between 1.5.2.9-1.5.4.4:
1.5.3.1:
- Add User Edit Page UI Page (QT -> User Record)
- Add Chat Dialog for encrypted, non encrypted, 1:1 & 1:many chat
- Add SMTP and POP3 decentralized server
- Add RSA keys for User Encryption (for emails and chats)
- Add burn addresses for DAC and DAC matches
- Add audit params to dwsquote (this allows us to see if anyone is shorted in the future)
- add rpc 'claimreward' - this allows dash easter eggs rewards to be claimed
- Added txoutsetinfo advanced metrics that track exact coin money supply
- Added MultiSigSignRawTransaction and MultiSigCreateRawTransaction - Lets user create cash that is recallable if not spent (or N-of-X multisig transactions)
- Added xspork-orphan, xspork-charity record types (this allows sanctuaries to add or edit these types of records in a decentralized way), and enables the DAC to allocate funds from the DacEngine
- Increased our Deflation Rate to 36% temporarily until Emission matches schedule (see Validation ln 1348)
- Modified APM to only enact if price goes down (if enabled)
- Increased DWS (Dynamic Whale Staking) max per day to 10MM (from 5MM), increased individual burn amount from 1MM to 2MM per block, and added a low threshhold of .05DWU so that no stakers are turned away
- Added DAC Engine:  Daily donations to DAC burn address are relayed and allocated automatically by smart percentage owed.  For now, Matches are relayed in the same way (IE they are not behaving as matches, they are behaving as donations for now until we build out matches).
- Made user record NickName drive the internal email (pop3 address), and the encrypted and non-encrypted chat dialogs
- Added rest_pushtx (This allows a light node to push a transaction through a sanc using REST)
- Added POP3 e-mail receive for e-mail clients, and SMTP e-mail send for e-mail clients, and encrypted RSA e-mail
1.5.3.1b:
- Add ability to disable SMTP server
1.5.3.1c:
- Change default SMTP/POP3 ports for linux compatibility
1.5.3.1d-Leisure Upgrade

- Work on e-mail remote propagation for missing emails
1.5.3.1e-Leisure Upgrade

- Increase string size for node propagation
1.5.3.1f-Leisure Upgrade

- Add UTXO Stake Campaign to GSC, add UTXO Stake UI list in leaderboard, Add portfolio drill-in double click to leaderboard
1.5.3.2

- Add expense and revenue
1.5.3.3 - Mandatory Upgrade for TestNet

- Force upgrade of Prayers database
- Ensure certain message types are parsed
1.5.3.3b

- Fix bug in check in
1.5.3.3c-Mandatory Upgrade

- Ensure memory pool rejects are not processed in response to DAC Donations
1.5.3.3e - Leisure Upgrade

- Fix bug in encrypted chat decipher routine
1.5.3.3f - Leisure Upgrade

- Fix bug in Decryption routine
1.5.3.3g-Leisure

- Fix final error in decryption routine
1.5.3.4 - Mandatory Upgrade for TestNet

- Fix incomplete SMTP protocol code, and incomplete POP3 code.  Ensure email with invalid Recipients bounces back.  Delete emails older than 1 day on the hard drive in ~/.biblepaycore/SAN/email*.
- Disable sidechain sync and number of blocks.
1.5.3.5 - Mandatory Upgrade for TestNet

- Rename Foundation to DAC in Send Coins
- Add multiple recipient support to SMTP (and fix SMTP bug)
1.5.3.5b-Mandatory Upgrade for TestNet

- Fix large pop3 buffer receive bug
1.5.3.6 - Mandatory Upgrade for TestNet

- Fix SMTP for Windows
1.5.3.7 - Mandatory Upgrade for testnet

- Add support for BTC, LTC, DOGE, DASH and BBP utxostakes
- Add pin staking (rpc: getpin)
- Ensure SMTP supports CC & BCC (we did this, please test)
- Add Memorize Scriptures page with Learn Mode and Test Mode
- Add easystake, easybbpstake, getpin.  Add auto currency detection by address.
1.5.3.7b-Mandatory Upgrade

- Fix bug in email serializer
1.5.3.8 - Mandatory Upgrade for TestNet

- Add rpc: listexpenses, listdacdonations, listutxostakes
- Add two more fields to gettxoutsetinfo (synthesized emission as of future date for budget planning + percent_emitted)
- Track donations to DAC for listdacdonations
- Add BBP University 1.0, Module 1 (First 4 courses) and course material, and first 4 final exams, and Exam Testing Center
- Ensure UTXO stakes from command line can be sent without coin age (try with coin age first, then fall back to no coin age).  Ensure UTXO mining reward is only valid with coin age.
1.5.3.9 - Mandatory Upgrade for TestNet

- Increment e-mail protocol version to 2 (to prevent duplicate e-mails and empty From and To lines)
- Add rpc command for new user to cash in a newbie reward
- Add multiple verse support to memorize scriptures
1.5.3.9b-Mandatory Upgrade for TestNet

- Ensure new emails use version 2
1.5.4.1-Mandatory Upgrade

- Eliminate duplicate pop3 downloads, Add encrypted emails, Add ability to send mail to "all"
1.5.4.1b-Leisure Upgrade

- Ensure BBP-Univ is visible on the main menu for mac
1.5.4.1d - Leisure Upgrade

- Fix menu on mac for BBPU
1.5.4.1e-Mandatory Upgrade for TestNet

- Ensure smtp outbound does not result in duplicates
- Ensure pop3 inbound does not receive empty emails
- Add timestamp to chat UI
1.5.4.2-Mandatory Upgrade

- Bump version to 1542 for ID
1.5.4.4 - Mandatory Upgrade for Entire Network

- Add scripture reference to Final Exam - Learn Mode (In answer textbox)
- Enforce 3meg email limit
- Bump version and cutover height for prod
- Decrease quorum size to be more reflective of our Paid-sponsorship-orphan sanctuary model
