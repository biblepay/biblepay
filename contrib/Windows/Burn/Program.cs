using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;

namespace Burn
{
    class Program
    {
        public static void Main(string[] args)
        {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 256; i++)
            {
                if (i == 25 || i == 140)
                {
                    sb.AppendFormat("Version: {0}, Address: {1}", i, GenerateZeroAddress((byte)i));
                    sb.AppendLine();
                }
            }
            string s = sb.ToString();
            Console.WriteLine(s);
            System.Threading.Thread.Sleep(35000);

        }
        public static string GenerateZeroAddress(byte version)
        {
            var hash = new byte[160 / 8]; //in a normal address, this would be a ripemd160(publickey)

            hash[0] = 0;
            // Version: 25, Address: B4T5ciTCkWauSqVAcVKy88ofjcSasUkSYU
            // Version: 140, Address: yLKSrCjLQFsfVgX8RjdctZ797d54atPjnV
            hash[0] = 1;
            // Version: 25, Address: B4YNHuaXAgpbyP5QgM3H6S41AuKcsRCEaB
            // Version: 140, Address: yLQjXPrepS7N2E7NVbLvrrMUYux6d8kkD8
            hash[0] = 2;
            // Version: 25, Address: B4dey6hqas4JVvfekCkb4jJLcCCevtXcfC
            //Version: 140, Address: yLW2CayyEcM4YmhcZT4Eq9bozCq8griKNa



            var bytes = new byte[160 / 8 + 1 + 4]; //include space for version byte and 4 checksum bytes
            hash.CopyTo(bytes, 1); //not necessary for our case, but just for illustration
            bytes[0] = version; //VERSION NUMBER. Get this from base58.h or chainparams.cpp "pubkeyhash"
            using (var sha256 = new SHA256Managed())
            {
                var tmp = sha256.ComputeHash(sha256.ComputeHash(bytes.Take(160 / 8 + 1).ToArray())); //include verison number, but do not include leading 4 bytes where checksum will go
                for (int i = 0; i < 4; i++)
                {
                    //take 4 bytes and add them to the end of the address bytes. this is our checksum
                    bytes[bytes.Length - 4 + i] = tmp[i];
                }
            }
            //base58 it up and it's all good
            return Base58Encode(bytes);
        }


        public static string Base58Encode(byte[] array)
        {
            const string ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
            string retString = string.Empty;
            BigInteger encodeSize = ALPHABET.Length;
            BigInteger arrayToInt = 0;
            for (int i = 0; i < array.Length; ++i)
            {
                arrayToInt = arrayToInt * 256 + array[i];
            }
            while (arrayToInt > 0)
            {
                int rem = (int)(arrayToInt % encodeSize);
                arrayToInt /= encodeSize;
                retString = ALPHABET[rem] + retString;
            }
            for (int i = 0; i < array.Length && array[i] == 0; ++i)
                retString = ALPHABET[0] + retString;

            return retString;
        }
    }
}