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
- add rpc 'claimreward' - this allows biblepay easter eggs rewards to be claimed
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

- Add support for BTC, LTC, DOGE, BIBLEPAY and BBP utxostakes
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

1.5.4.5b - Mandatory Upgrade for Entire Network
- Ensure Pop3 class can be disabled (for sancs with shared hosting)

1.6.0.1-Transition to Dash 0.16.1.1
- Merge all Dash commits up to Nov 15, 2020 commit: 43d2973
- Rebase biblepay-core

Changes between 1.6.0.1b - 1.6.2.1

- Adjust toolbar for the mac, fix CSS to work with vertical menu
- Add png images for UTXO staking
- Add LLMQ params for chainlocks and LLMQ-instantsend
- Enable the Wallet on hot sanctuaries (allows sancs to work as UTXO oracles)
- Add ETHereum staking
- Implement new payment at HARVEST_HEIGHT2: // New payment %s for masternodes (20%), less for monthly budget (5%), more for UTXO (50%), RANDOMX (25%)
- Allow incoming connections to masternodes on TESTNET during syncing (allows us to bootstrap ourselves if all nodes are down)
- Provide a better UTXO error message when the foreign amount is empty
- Remove invalid getchaininfo responses
- Fix display width of masternodelist UI gride columns, Recaption Masternode to sanctuary
- Add NFT 1.0 (Non-Fungible Tokens)
- Improve coin-control display of locked stakes with commitments (added a chained lock)
- Add Referral Codes 1.0 (allow user to generatereferralcode, listattachedreferralcodes, checkreferralcode, claimreferralcode)
- Add biblepay mail delivery (Christmas Cards) and phrase protected virtual gift cards, Add Send Greeting Card UI
- Add lo/hi quality NFT URLs, add deleted field, and businessobject processing for the new fields.
- Modify listutxostakes to actually be JSON
- Raise BBP block fees from dust levels to small levels (they were at dust in testnet, should be about 1 bbp per 1K of size now - similar to prod) 
- Add tier2 payment for non POOS sancs.  This gives tier2 a 250bbp reward to run a sanc that does not sponsor an orphan, simply for network stability.
- Recaption Duffs to Pence (This gives a name to our microcoins)
- Coin Control: Display gift icon when it is a gift
- Auto unlock the gift if they 'acceptgift', lock gifts on wallet boot by default (this prevents gifts from being spent by the giver).  Add gift key to givers wallet so they can recall the gift if it is never redeemed.
- Add NFT Auctions (Buy It Now Amount and Reserve Price)
- Add orphan sponsorship duration to 'listnfts'
- Add Christmas, Easter and Kittens Greeting cards.  Make the greeting card fields customizable by the user.  
- Add Invoicing and Payments and Statements v1.0
- Add rpc getstatement
- Add DSQL to server side (this supports an insert and a read). 
- Make referralcode calculation easier to understand
- Add rest pushtx, getaddressutxo.   This allows a decentralized client to relay a tx, and allows a decentralized c# consumer to query utxos, and query DSQL data.
- Add Ripple and Stellar support for UTXO staking (v1.0), and modify getpin for this also
- Make referralrewards bonus decay over 2 years
- Fix listattachedreferralcodes to be nicer with a narrative
- Add preview to Greeting Card CSV process (from Greeting card UI)
- Add social media NFT type
- Add rpc getvalue command to allow you to convert from BBP to USD if you are an orphanage for example
- Remove Randomx calls on MAC making it an SPV client (due to incompatibility with /crypto/randomx on mac)
- Update icon for mac build
- Update build instructions for mac cross compiling
- Disable smtp and pop3 by default to prevent mac-testnet from crashing
- Merge in very upstream bitcoin commit that prevents crash on intel atom during startup of sha256
- Pre-merge changes coming for Stellar
- Add Build Biblepay Develop instructions
- Add Trust Wallet Integration (UTXO staking 2.0)
- Add BBP Crypto Index
- Add erasechain UI (wallet tools)
- Add bitcoincash + zcash to portfolio builder
- Optimize dsql query filter
- Fix cosmetic items on the 'mission critical todo' list
- Add Query button to utxodialog


Changes between 1.6.2.1 and 0.17.1.2 (Mandatory Upgrade @320,000):

- Rebase to dash 0.1703.17.3a / Aug. 2021 (https://github.com/biblepay/biblepay/commit/e366733116f162fb4f226c8b4ad5c67cd73465e9)
- 0.17.0.4c - Port in 'exec upgradesanc' command
- Allow width to be expanded, and height to be expanded of main window (CSS)
- Add ability to create a new Receive Address (it labels it with 6 random hex chars)
- Port in Portfolio Builder for sidechain
- Autolock sancs, and autolock portfolio builder collateral ending in .777
- Add toolbar buttons for prayer request, forum and unchained to left menu
- Port in Donate To Foundation in Send Money
- Add Portfolio builder txlist icon for rewards
- Add txdesc double click drill in advanced view
- Merge in POOS (proof of orphan sponsorship via sanctuary) 
- Add memorize, persist and depersist sidechain transactions routine
- Add exec listsc RPC function for sha256 sidechain hashes and referenced URLs


Changes between 0.17.1.3 and 0.17.2.5 (Mandatory upgrade @346,500):

- Add POVS (proof-of-video-streaming); requires sanctuaries to run the BMS software (biblepay video streaming server)

Changes between 0.17.2.6 and 0.17.3.7 (Mandatory upgrade @ 428,000):

- Added revivesanctuaries feature that automatically revives investor sancs once per 24 hours giving them a passive reward
- Added Sanctuary Mining meaning that Sanctuaries are the only nodes that can mine blocks.
- Added Unchained Desktop, which allows our user to view streaming videos, view gospel content
- Added the BiblePay Phone System, which allows our users to make long distance phone calls around the world, and provides a real US phone number
for them to receive phone calls on
- Removed Portfolio Builder and Turnkey Sanctuaries in favor of Active/Passive Sanctuaries
- Adjusted the Block Reward percentages to allocate the entire reward to the sanctuary for the block
- Removed RandomX heat mining (Kept RandomX hashes in the wallet for Sanctuary block checking)
