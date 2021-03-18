// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_CRYPTER_H
#define BITCOIN_WALLET_CRYPTER_H

#include <keystore.h>
#include <serialize.h>
#include <support/allocators/secure.h>

#include <atomic>

const unsigned int WALLET_CRYPTO_KEY_SIZE = 32;
const unsigned int WALLET_CRYPTO_SALT_SIZE = 8;
const unsigned int WALLET_CRYPTO_IV_SIZE = 16;

/**
 * Private key encryption is done based on a CMasterKey,
 * which holds a salt and random encryption key.
 *
 * CMasterKeys are encrypted using AES-256-CBC using a key
 * derived using derivation method nDerivationMethod
 * (0 == EVP_sha512()) and derivation iterations nDeriveIterations.
 * vchOtherDerivationParameters is provided for alternative algorithms
 * which may require more parameters (such as scrypt).
 *
 * Wallet Private Keys are then encrypted using AES-256-CBC
 * with the double-sha256 of the public key as the IV, and the
 * master key's key as the encryption key (see keystore.[ch]).
 */

/** Master key for wallet encryption */
class CMasterKey
{
public:
    std::vector<unsigned char> vchCryptedKey;
    std::vector<unsigned char> vchSalt;
    //! 0 = EVP_sha512()
    //! 1 = scrypt()
    unsigned int nDerivationMethod;
    unsigned int nDeriveIterations;
    //! Use this for more parameters to key derivation,
    //! such as the various parameters to scrypt
    std::vector<unsigned char> vchOtherDerivationParameters;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vchCryptedKey);
        READWRITE(vchSalt);
        READWRITE(nDerivationMethod);
        READWRITE(nDeriveIterations);
        READWRITE(vchOtherDerivationParameters);
    }

    CMasterKey()
    {
        // 25000 rounds is just under 0.1 seconds on a 1.86 GHz Pentium M
        // ie slightly lower than the lowest hardware we need bother supporting
        nDeriveIterations = 25000;
        nDerivationMethod = 0;
        vchOtherDerivationParameters = std::vector<unsigned char>(0);
    }
};

typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;

namespace wallet_crypto
{
    class TestCrypter;
}

/** Encryption/decryption context with key information */
class CCrypter
{
friend class wallet_crypto::TestCrypter; // for test access to chKey/chIV
private:
    std::vector<unsigned char, secure_allocator<unsigned char>> vchKey;
    std::vector<unsigned char, secure_allocator<unsigned char>> vchIV;
    bool fKeySet;

    int BytesToKeySHA512AES(const std::vector<unsigned char>& chSalt, const SecureString& strKeyData, int count, unsigned char *key,unsigned char *iv) const;

public:
    bool SetKeyFromPassphrase(const SecureString &strKeyData, const std::vector<unsigned char>& chSalt, const unsigned int nRounds, const unsigned int nDerivationMethod);
    bool Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext) const;
    bool Decrypt(const std::vector<unsigned char>& vchCiphertext, CKeyingMaterial& vchPlaintext) const;
    bool SetKey(const CKeyingMaterial& chNewKey, const std::vector<unsigned char>& chNewIV);

    void CleanKey()
    {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        fKeySet = false;
    }

    CCrypter()
    {
        fKeySet = false;
        vchKey.resize(WALLET_CRYPTO_KEY_SIZE);
        vchIV.resize(WALLET_CRYPTO_IV_SIZE);
    }

    ~CCrypter()
    {
        CleanKey();
    }
};

bool EncryptAES256(const SecureString& sKey, const SecureString& sPlaintext, const std::string& sIV, std::string& sCiphertext);
bool DecryptAES256(const SecureString& sKey, const std::string& sCiphertext, const std::string& sIV, SecureString& sPlaintext);

std::string EncryptAES256(std::string sPlaintext, std::string sKey);
std::string EncryptAES256WithIV(std::string sPlaintext, std::string sKey, std::string sIV);

std::string DecryptAES256(std::string s64, std::string sKey);
std::string DecryptAES256WithIV(std::string s64, std::string sKey, std::string sIV);

bool BibleDecrypt(const std::vector<unsigned char>& vchCiphertext,std::vector<unsigned char>& vchPlaintext);
bool BibleEncrypt(std::vector<unsigned char> vchPlaintext, std::vector<unsigned char> &vchCiphertext);
std::vector<unsigned char> StringToVector(std::string sData);
std::string VectorToString(std::vector<unsigned char> v);
std::string RPAD(std::string sUnpadded, int nLength);


int RSA_GENERATE_KEYPAIR(std::string sPublicKeyPath, std::string sPrivateKeyPath);
unsigned char *RSA_ENCRYPT_CHAR(std::string sPubKeyPath, unsigned char *plaintext, int plaintext_length, int& cipher_len, int& rsa_len, std::string& sError);
void RSA_Encrypt_File(std::string sPubKeyPath, std::string sSourcePath, std::string sEncryptPath, std::string& sError);
unsigned char *RSA_DECRYPT_CHAR(std::string sPriKeyPath, unsigned char *ciphertext, int ciphrtext_size, int& plaintxt_len, std::string& sError);
void RSA_Decrypt_File(std::string sPriKeyPath, std::string sSourcePath, std::string sDecryptPath, std::string sError);
std::string RSA_Encrypt_String(std::string sPubKeyPath, std::string sData, std::string& sError);
std::string RSA_Decrypt_String(std::string sPrivKeyPath, std::string sData, std::string& sError);
std::string RSA_Encrypt_String_With_Key(std::string sPubKey, std::string sData, std::string& sError);
std::string RSA_Decrypt_String_With_Key(std::string sPrivKey, std::string sData, std::string& sError);

/** Keystore which keeps the private keys encrypted.
 * It derives from the basic key store, which is used if no encryption is active.
 */
class CCryptoKeyStore : public CBasicKeyStore
{
private:
    CHDChain cryptedHDChain;

    CKeyingMaterial vMasterKey;

    //! if fUseCrypto is true, mapKeys must be empty
    //! if fUseCrypto is false, vMasterKey must be empty
    std::atomic<bool> fUseCrypto;

    //! keeps track of whether Unlock has run a thorough check before
    bool fDecryptionThoroughlyChecked;

    //! if fOnlyMixingAllowed is true, only mixing should be allowed in unlocked wallet
    bool fOnlyMixingAllowed;

protected:
    bool SetCrypted();

    //! will encrypt previously unencrypted keys
    bool EncryptKeys(CKeyingMaterial& vMasterKeyIn);

    bool EncryptHDChain(const CKeyingMaterial& vMasterKeyIn);
    bool DecryptHDChain(CHDChain& hdChainRet) const;
    bool SetHDChain(const CHDChain& chain);
    bool SetCryptedHDChain(const CHDChain& chain);

    bool Unlock(const CKeyingMaterial& vMasterKeyIn, bool fForMixingOnly = false);
    CryptedKeyMap mapCryptedKeys;

public:
    CCryptoKeyStore() : fUseCrypto(false), fDecryptionThoroughlyChecked(false), fOnlyMixingAllowed(false)
    {
    }
	std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
	std::vector<char> ReadAllBytes(char const* filename);

    bool IsCrypted() const { return fUseCrypto; }
    bool IsLocked(bool fForMixing = false) const;
    bool Lock(bool fForMixing = false);

    virtual bool AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey) override;
    bool HaveKey(const CKeyID &address) const override;
    bool GetKey(const CKeyID &address, CKey& keyOut) const override;
    bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const override;
    std::set<CKeyID> GetKeys() const override;

    virtual bool GetHDChain(CHDChain& hdChainRet) const override;

    /**
     * Wallet status (encrypted, locked) changed.
     * Note: Called without locks held.
     */
    boost::signals2::signal<void (CCryptoKeyStore* wallet)> NotifyStatusChanged;
};

#endif // BITCOIN_WALLET_CRYPTER_H
