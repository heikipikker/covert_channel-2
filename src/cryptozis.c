#include <zlib.h>
#include <stdio.h>
#ifdef STDC
#  include <string.h>
#  include <stdlib.h>
#endif
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/hmac.h>

/**
 * Create an 256 bit key and IV using the supplied key_data. salt can be added for taste.
 * Fills in the encryption and decryption ctx objects and returns 0 on success
 **/
int aes_init(unsigned char *key_data, int key_data_len, unsigned char *salt, EVP_CIPHER_CTX *e_ctx, 
             EVP_CIPHER_CTX *d_ctx)
{
  int i, nrounds = 5;
  unsigned char key[32], iv[32];
  
  /*
   * Gen key & IV for AES 256 CBC mode. A SHA1 digest is used to hash the supplied key material.
   * nrounds is the number of times the we hash the material. More rounds are more secure but
   * slower.
   */
  i = EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha1(), salt, key_data, key_data_len, nrounds, key, iv);
  if (i != 32) {
    printf("Key size is %d bits - should be 256 bits\n", i);
    return -1;
  }

  EVP_CIPHER_CTX_init(e_ctx);
  EVP_EncryptInit_ex(e_ctx, EVP_aes_256_cbc(), NULL, key, iv);
  EVP_CIPHER_CTX_init(d_ctx);
  EVP_DecryptInit_ex(d_ctx, EVP_aes_256_cbc(), NULL, key, iv);

  return 0;
}

/*
 * Encrypt *len bytes of data
 * All data going in & out is considered binary (unsigned char[])
 */
static u_char *aes_encrypt(EVP_CIPHER_CTX *e, unsigned char *plaintext, int *len)
{
  /* max ciphertext len for a n bytes of plaintext is n + AES_BLOCK_SIZE -1 bytes */
  int c_len = *len + AES_BLOCK_SIZE, f_len = 0;
  unsigned char *ciphertext = malloc(c_len);

  /* allows reusing of 'e' for multiple encryption cycles */
  EVP_EncryptInit_ex(e, NULL, NULL, NULL, NULL);

  /* update ciphertext, c_len is filled with the length of ciphertext generated,
    *len is the size of plaintext in bytes */
  EVP_EncryptUpdate(e, ciphertext, &c_len, plaintext, *len);

  /* update ciphertext with the final remaining bytes */
  EVP_EncryptFinal_ex(e, ciphertext+c_len, &f_len);

  *len = c_len + f_len;
  return ciphertext;
}

/*
 * Decrypt *len bytes of ciphertext
 */
static u_char *aes_decrypt(EVP_CIPHER_CTX *e, unsigned char *ciphertext, int *len)
{
  /* because we have padding ON, we must allocate an extra cipher block size of memory */
  int p_len = *len, f_len = 0;
  unsigned char *plaintext = malloc(p_len + AES_BLOCK_SIZE);
  
  EVP_DecryptInit_ex(e, NULL, NULL, NULL, NULL);
  EVP_DecryptUpdate(e, plaintext, &p_len, ciphertext, *len);
  EVP_DecryptFinal_ex(e, plaintext+p_len, &f_len);

  *len = p_len + f_len;
  return plaintext;
}

int enrypt_digest(EVP_CIPHER_CTX *en,
		  u_char *frame,
		  u_int32_t frame_len,
		  u_char** sha_frame,
		  u_char **encr_frame,
		  int*encr_frame_len,
		  u_char key[])
{
  
  *encr_frame = aes_encrypt(en,frame, encr_frame_len);
  if (*encr_frame ==NULL)
    return -1;
  *sha_frame = HMAC(EVP_sha256(), key, sizeof(key)-1, frame, (const int) (*encr_frame_len), NULL, NULL);
  return 0;
}

int decrypt_digest(EVP_CIPHER_CTX *de,
		   u_char * pUncomp_cipher_frame, 
		   u_char** sha_frame,
		   u_char **decr_frame,
		   int* decr_frame_len,
		   u_char key[])
{
  *decr_frame = aes_decrypt(de, pUncomp_cipher_frame, decr_frame_len);
  if (*decr_frame ==NULL)
    return -1;
  *sha_frame = HMAC(EVP_sha256(), key, sizeof(key)-1, *decr_frame, (const int)*decr_frame_len, NULL, NULL);
  return 0;
}
int compress_cipher_frame(u_char **pCmp_cipher_frame,
		      ulong *compressed_frame_len,	  
		      u_char * cipher_frame,
		      int cipher_frame_len)
{
  int cmp_status;
  *pCmp_cipher_frame = (u_int8_t *)malloc((size_t)*compressed_frame_len);
  if (!pCmp_cipher_frame)
    {
      printf("Out of memory!\n");
      return EXIT_FAILURE;
    }  
  cmp_status = compress(*pCmp_cipher_frame, compressed_frame_len, (const u_char *)cipher_frame, cipher_frame_len);
  if (cmp_status != Z_OK)
    {
      printf("compress() failed!\n");
      free(pCmp_cipher_frame);
      return EXIT_FAILURE;
    }

  return 0;
}
int  uncompress_cipher_frame(u_char** pUncomp_cipher_frame,
			     u_char* pCmp_cipher_frame,
			     ulong *uncompressed_frame_len,
			     ulong compressed_frame_len,
			     int cipher_frame_len)
{
  int cmp_status;
  *pUncomp_cipher_frame = (u_int8_t *)malloc((size_t)cipher_frame_len);
  if (!pUncomp_cipher_frame)
    {
      printf("Out of memory!\n");
      return EXIT_FAILURE;
    } 
  cmp_status = uncompress(*pUncomp_cipher_frame, uncompressed_frame_len, pCmp_cipher_frame, compressed_frame_len);
  if (cmp_status != Z_OK)
    {
      printf("uncompress failed!\n");
      free(pUncomp_cipher_frame);
      return EXIT_FAILURE;
    }
  return 0;
}

#if 0
int main(int argc, char **argv)
{
  /* "opaque" encryption, decryption ctx structures that libcrypto uses to record
     status of enc/dec operations 
  */
  EVP_CIPHER_CTX en, de;

  /* 8 bytes to salt the key_data during key generation. This is an example of
     compiled in salt. We just read the bit pattern created by these two 4 byte 
     integers on the stack as 64 bits of contigous salt material - 
     ofcourse this only works if sizeof(int) >= 4 
  */
  u_int32_t salt[] = {12345, 54321};
  u_char key_data []= "This is the key";
  int key_data_len;
  ulong  compressed_frame_len, uncompressed_frame_len;
  u_int8_t *pCmp_cipher_frame, *pUncomp_cipher_frame;
  u_char *decrypted_frame;
  int decrypted_frame_len, cipher_frame_len, orig_frame_len,sha_len;
  u_char * sha_orig_frame,*sha_decr_frame;
  u_char* cipher_frame;
  key_data_len = strlen(key_data);
  u_char frame [] = "I want to see the relative diffence in the length of the ciphertext. It seems the gap between the two keeps decreasing ever so slowly and might turn out that the size is dereasing with the increaseing text size. It seems to have stopped to a point when they are four bytes apart all the time no matter how long the plaintext is";
  /* gen key and iv. init the cipher ctx object */
  if (aes_init(key_data, key_data_len, (unsigned char *)&salt, &en, &de)) {
    printf("Couldn't initialize AES cipher\n");
    return -1;
  }
  /* encrypt and decrypt each frame string and compare with the original */

  /* The enc/dec functions deal with binary data and not C strings. strlen() will 
     return length of the string without counting the '\0' string marker. We always
     pass in the marker byte to the encrypt/decrypt functions so that after decryption 
       we end up with a legal C string 
  */
  cipher_frame_len = orig_frame_len = sizeof(frame);
  enrypt_digest(&en,frame,orig_frame_len,&sha_orig_frame, &cipher_frame,&cipher_frame_len,key_data);
  compressed_frame_len = compressBound(cipher_frame_len);
  compress_cipher_frame(&pCmp_cipher_frame, &compressed_frame_len, cipher_frame, cipher_frame_len);
  uncompress_cipher_frame(&pUncomp_cipher_frame, pCmp_cipher_frame, &uncompressed_frame_len, compressed_frame_len, cipher_frame_len );
  decrypted_frame_len=cipher_frame_len;
  decrypt_digest(&de,pUncomp_cipher_frame, &sha_decr_frame, &decrypted_frame,&decrypted_frame_len,key_data);
  if ((uncompressed_frame_len != cipher_frame_len) || (memcmp(pUncomp_cipher_frame, cipher_frame, (size_t)cipher_frame_len)))
    {
      printf("Decompression failed!\n");
      free(pCmp_cipher_frame);
      free(pUncomp_cipher_frame);
      return EXIT_FAILURE;
    }
  printf("the decrypted_frame len=%d\n", decrypted_frame_len);
  if (strncmp((const char*)decrypted_frame,frame, (const char*)cipher_frame_len)) 
    printf("FAIL: enc/dec failed for \n");
  else 
    printf("OK: enc/dec ok for \"%s\"\n", decrypted_frame);

  free(cipher_frame);
  free(decrypted_frame);
 
  free(pCmp_cipher_frame);
  free(pUncomp_cipher_frame);

  printf("end of compression compression part\n"); 
  EVP_CIPHER_CTX_cleanup(&en);
  EVP_CIPHER_CTX_cleanup(&de);
  return 0;
}
#endif