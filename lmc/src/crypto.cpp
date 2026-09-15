/****************************************************************************
**
** This file is part of LAN Messenger.
**
** Copyright (c) 2010 - 2012 Qualia Digital Solutions.
**
** Contact:  qualiatech@gmail.com
**
** LAN Messenger is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** LAN Messenger is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with LAN Messenger.  If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/


#include "trace.h"
#include "crypto.h"

lmcCrypto::lmcCrypto(void) {
	pRsa = NULL;
	encryptMap.clear();
	decryptMap.clear();
	bits = 1024;
	exponent = 65537;
}

lmcCrypto::~lmcCrypto(void) {
	RSA_free(pRsa);
}

//	creates an RSA key pair and returns the string representation of the public key
QByteArray lmcCrypto::generateRSA(void) {
	RSA_free(pRsa);
	pRsa = RSA_generate_key(bits, exponent, NULL, NULL);
	if(!pRsa) {
		publicKey.clear();
		lmcTrace::write("Error: RSA key generation failed");
		return publicKey;
	}

	BIO* bio = BIO_new(BIO_s_mem());
	if(!bio || !PEM_write_bio_RSAPublicKey(bio, pRsa)) {
		if(bio)
			BIO_free_all(bio);
		publicKey.clear();
		lmcTrace::write("Error: RSA public key creation failed");
		return publicKey;
	}
	int keylen = BIO_pending(bio);
	char* pem_key = (char*)calloc(keylen + 1, 1);
	if(!pem_key || BIO_read(bio, pem_key, keylen) != keylen) {
		BIO_free_all(bio);
		free(pem_key);
		publicKey.clear();
		lmcTrace::write("Error: RSA public key export failed");
		return publicKey;
	}
	publicKey = QByteArray(pem_key, keylen);
	BIO_free_all(bio);
	free(pem_key);

	return publicKey;
}

//	generates a random aes key and iv, and encrypts it with the public key
QByteArray lmcCrypto::generateAES(QString* lpszUserId, QByteArray& pubKey) {
	char* pemKey = pubKey.data();
	RSA* rsa = NULL;
	BIO* bio = BIO_new_mem_buf(pemKey, pubKey.length());
	if(!bio || !PEM_read_bio_RSAPublicKey(bio, &rsa, NULL, NULL) || !rsa) {
		if(bio)
			BIO_free_all(bio);
		RSA_free(rsa);
		lmcTrace::write("Error: Invalid RSA public key");
		return QByteArray();
	}

	int keyDataLen = 32;
	unsigned char* keyData = (unsigned char*)malloc(keyDataLen);
	int keyLen = 32;
	int ivLen = EVP_CIPHER_iv_length(EVP_aes_256_cbc());
	int keyIvLen = keyLen + ivLen;
	unsigned char* keyIv = (unsigned char*)malloc(keyIvLen);
	if(!keyData || !keyIv || RAND_bytes(keyData, keyDataLen) != 1) {
		BIO_free_all(bio);
		RSA_free(rsa);
		free(keyIv);
		free(keyData);
		lmcTrace::write("Error: AES key generation failed");
		return QByteArray();
	}
	int rounds = 5;
	keyLen = EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha1(), NULL, keyData, keyDataLen, rounds, keyIv, keyIv + keyLen);

	EVP_CIPHER_CTX ectx, dctx;
	unsigned char* eKeyIv = (unsigned char*)malloc(RSA_size(rsa));
	int eKeyIvLen = eKeyIv ? RSA_public_encrypt(keyIvLen, keyIv, eKeyIv, rsa, RSA_PKCS1_OAEP_PADDING) : -1;
	QByteArray baKeyIv;
	if(keyLen == 32 && ectx.ptr() && dctx.ptr() &&
		EVP_EncryptInit_ex(ectx.ptr(), EVP_aes_256_cbc(), NULL, keyIv, keyIv + keyLen) == 1 &&
		EVP_DecryptInit_ex(dctx.ptr(), EVP_aes_256_cbc(), NULL, keyIv, keyIv + keyLen) == 1 && eKeyIvLen > 0) {
		encryptMap.insert(*lpszUserId, ectx);
		decryptMap.insert(*lpszUserId, dctx);
		baKeyIv = QByteArray((char*)eKeyIv, eKeyIvLen);
	} else {
		lmcTrace::write("Error: AES session initialization failed");
	}

	BIO_free_all(bio);
	RSA_free(rsa);
	free(keyIv);
	free(eKeyIv);
	free(keyData);

	return baKeyIv;
}

//	decrypts the aes key and iv with the private key
bool lmcCrypto::retreiveAES(QString* lpszUserId, QByteArray& aesKeyIv) {
	if(!pRsa)
		return false;
	unsigned char* keyIv = (unsigned char*)malloc(RSA_size(pRsa));
	if(!keyIv)
		return false;
	int decryptedLength = RSA_private_decrypt(aesKeyIv.length(), (unsigned char*)aesKeyIv.data(), keyIv,
		pRsa, RSA_PKCS1_OAEP_PADDING);

	int keyLen = 32;
	int keyIvLen = keyLen + EVP_CIPHER_iv_length(EVP_aes_256_cbc());
	if(decryptedLength != keyIvLen) {
		free(keyIv);
		lmcTrace::write("Error: Invalid encrypted AES session key");
		return false;
	}
	EVP_CIPHER_CTX ectx, dctx;
	bool initialized = ectx.ptr() && dctx.ptr() &&
		EVP_EncryptInit_ex(ectx.ptr(), EVP_aes_256_cbc(), NULL, keyIv, keyIv + keyLen) == 1 &&
		EVP_DecryptInit_ex(dctx.ptr(), EVP_aes_256_cbc(), NULL, keyIv, keyIv + keyLen) == 1;
	if(initialized) {
		encryptMap.insert(*lpszUserId, ectx);
		decryptMap.insert(*lpszUserId, dctx);
	}

	free(keyIv);
	return initialized;
}

QByteArray lmcCrypto::encrypt(QString* lpszUserId, QByteArray& clearData) {
	if(!encryptMap.contains(*lpszUserId)) {
		lmcTrace::write("Error: Encryption session not initialized");
		return QByteArray();
	}
	int outLen = clearData.length() + AES_BLOCK_SIZE;
	unsigned char* outBuffer = (unsigned char*)malloc(outLen);
	if(outBuffer == NULL) {
		lmcTrace::write("Error: Buffer not allocated");
		return QByteArray();
	}
	int foutLen = 0;

	EVP_CIPHER_CTX ctx = encryptMap.value(*lpszUserId);
	if(ctx.ptr() && EVP_EncryptInit_ex(ctx.ptr(), NULL, NULL, NULL, NULL)) {
		if(EVP_EncryptUpdate(ctx.ptr(), outBuffer, &outLen, (unsigned char*)clearData.data(), clearData.length())) {
			if(EVP_EncryptFinal_ex(ctx.ptr(), outBuffer + outLen, &foutLen)) {
				outLen += foutLen;
				QByteArray byteArray((char*)outBuffer, outLen);
				free(outBuffer);
				return byteArray;
			}
		}
	}
	lmcTrace::write("Error: Message encryption failed");
	free(outBuffer);
	return QByteArray();
}

QByteArray lmcCrypto::decrypt(QString* lpszUserId, QByteArray& cipherData) {
	if(!decryptMap.contains(*lpszUserId)) {
		lmcTrace::write("Error: Decryption session not initialized");
		return QByteArray();
	}
	int outLen = cipherData.length();
	unsigned char* outBuffer = (unsigned char*)malloc(outLen);
	if(outBuffer == NULL) {
		lmcTrace::write("Error: Buffer not allocated");
		return QByteArray();
	}
	int foutLen = 0;

	EVP_CIPHER_CTX ctx = decryptMap.value(*lpszUserId);
	if(ctx.ptr() && EVP_DecryptInit_ex(ctx.ptr(), NULL, NULL, NULL, NULL)) {
		if(EVP_DecryptUpdate(ctx.ptr(), outBuffer, &outLen, (unsigned char*)cipherData.data(), cipherData.length())) {
			if(EVP_DecryptFinal_ex(ctx.ptr(), outBuffer + outLen, &foutLen)) {
				outLen += foutLen;
				QByteArray byteArray((char*)outBuffer, outLen);
				free(outBuffer);
				return byteArray;
			}
		}
	}
	lmcTrace::write("Error: Message decryption failed");
	free(outBuffer);
	return QByteArray();
}
