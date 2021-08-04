#include "cryptUtils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "account/account.h"
#include "crypt.h"
#include "defines/settings.h"
#include "defines/version.h"
#include "hexCrypt.h"
#include "utils/listUtils.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/oidc_error.h"
#include "utils/stringUtils.h"
#include "utils/versionUtils.h"

/**
 * @brief decrypts the content of a file with the given password.
 * the file content must be content generated by @c encryptWithVersionLine
 * @param filecontent the filecontent to be decrypted
 * @param password the password used for encryption
 * @return a pointer to the decrypted filecontent. It has to be freed after
 * usage.
 */
char* decryptFileContent(const char* fileContent, const char* password) {
  list_t* lines = delimitedStringToList(fileContent, '\n');
  char*   ret   = decryptLinesList(lines, password);
  secFreeList(lines);
  return ret;
}

/**
 * @brief decrypts the content of a hex encoded file with the given password.
 * the file must have been generated before version 2.1.0
 * @param cipher the filecontent to be decrypted
 * @param password the password used for encryption
 * @return a pointer to the decrypted filecontent. It has to be freed after
 * usage.
 */
char* decryptHexFileContent(const char* cipher, const char* password) {
  char*         fileText       = oidc_strcopy(cipher);
  unsigned long cipher_len     = strToInt(strtok(fileText, ":"));
  char*         salt_encoded   = strtok(NULL, ":");
  char*         nonce_encoded  = strtok(NULL, ":");
  char*         cipher_encoded = strtok(NULL, ":");
  if (cipher_len == 0 || salt_encoded == NULL || nonce_encoded == NULL ||
      cipher_encoded == NULL) {
    oidc_errno = OIDC_ECRYPM;
    secFree(fileText);
    return NULL;
  }
  unsigned char* decrypted = crypt_decrypt_hex(
      cipher_encoded, cipher_len, password, nonce_encoded, salt_encoded);
  secFree(fileText);
  return (char*)decrypted;
}

/**
 * @brief decrypts a list of lines with the given password.
 * The list has to contain sepcific information in the correct order; the last
 * line has to be the version line (if there is one, files encrypted before
 * 2.1.0 will only have on line).
 * @param lines the list of lines
 * @param password the password used for encryption
 * @return a pointer to the decrypted cipher. It has to be freed after
 * usage.
 */
char* decryptLinesList(list_t* lines, const char* password) {
  if (lines == NULL) {
    oidc_setArgNullFuncError(__func__);
    return NULL;
  }
  list_node_t* node   = list_at(lines, 0);
  char*        cipher = node ? node->val : NULL;
  node                = list_at(lines, -1);
  char* version_line  = lines->len > 1 ? node ? node->val : NULL : NULL;
  char* version       = versionLineToSimpleVersion(version_line);
  if (versionAtLeast(version, MIN_BASE64_VERSION)) {
    secFree(version);
    return crypt_decryptFromList(lines, password);
  } else {  // old config file format; using hex encoding
    secFree(version);
    return decryptHexFileContent(cipher, password);
  }
}

/**
 * @brief decrypts a cipher that was generated with a specific version with the
 * given password
 * @param cipher the cipher to be decrypted - this is a formatted string
 * containing all relevant encryption information; the format differ with the
 * used version
 * @param password the password used for encryption
 * @param version the oidc-agent version that was used when cipher was encrypted
 * @return a pointer to the decrypted cipher. It has to be freed after
 * usage.
 */
char* decryptText(const char* cipher, const char* password,
                  const char* version) {
  if (cipher == NULL || password == NULL) {  // allow NULL for version
    oidc_setArgNullFuncError(__func__);
    return NULL;
  }
  if (versionAtLeast(version, MIN_BASE64_VERSION)) {
    return crypt_decrypt(cipher, password);
  } else {  // old config file format; using hex encoding
    return decryptHexFileContent(cipher, password);
  }
}

/**
 * @brief encrypts a given text with the given password
 * @return the encrypted text in a formatted string that holds all relevant
 * encryption information and that can be passed to @c decryptText
 * @note when saving the encrypted text you also have to save the oidc-agent
 * version. But there is a specific function for this. See
 * @c encryptWithVersionLine
 * @note before version 2.1.0 this function used hex encoding
 */
char* encryptText(const char* text, const char* password) {
  return crypt_encrypt(text, password);
}

/**
 * @brief encrypts a given text with the given password and adds the current
 * oidc-agent version
 * @return the encrypted text in a formatted string that holds all relevant
 * encryption information as well as the oidc-agent version. Can be passed to
 * @c decryptFileContent
 */
char* encryptWithVersionLine(const char* text, const char* password) {
  char* crypt        = encryptText(text, password);
  char* version_line = simpleVersionToVersionLine(VERSION);
  char* ret          = oidc_sprintf("%s\n%s", crypt, version_line);
  secFree(crypt);
  secFree(version_line);
  return ret;
}

char* randomString(size_t len) {
  char* str = secAlloc(len + 1);
  randomFillBase64UrlSafe(str, len);
  size_t shifts;
  for (shifts = 0; shifts < len && isalnum(str[0]) == 0;
       shifts++) {  // assert first char is alphanumeric
    oidc_memshiftr(str, len);
  }
  if (shifts >= len) {
    secFree(str);
    return randomString(len);
  }
  return str;
}
