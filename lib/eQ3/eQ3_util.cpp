#include <hwcrypto/aes.h>
#include <string>
#include <assert.h>
#include <string.h>
#include <sstream>
#include <byteswap.h>
#include <cmath>
#include <Arduino.h>

using std::string;

// -----------------------------------------------------------------------------
// --[string_to_hex]------------------------------------------------------------
// -----------------------------------------------------------------------------
std::string string_to_hex(const std::string &input) {
    static const char *const lut = "0123456789ABCDEF";
    size_t len = input.length();
    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

// -----------------------------------------------------------------------------
// --[hexstring_to_string]------------------------------------------------------
// -----------------------------------------------------------------------------
std::string hexstring_to_string(const std::string& hex_chars) {
    std::string bytes;
    // must be int for correct overload!
    unsigned int c;
    for (int i = 0; i < hex_chars.length(); i+=2) {
        std::istringstream hex_chars_stream(hex_chars.substr(i,2));
        hex_chars_stream >> std::hex >> c;
        bytes.append(1, (char) c);
    }
    return bytes;
}

// -----------------------------------------------------------------------------
// --[encrypt_aes_ecb]----------------------------------------------------------
// -----------------------------------------------------------------------------
string encrypt_aes_ecb(string &data, string &key) { // input should be padded array
    assert(key.length() == 16);
    assert((data.length() % 16) == 0);
    esp_aes_acquire_hardware();
    esp_aes_context context;
    esp_aes_init(&context);
    unsigned char *aeskey = (unsigned char *) key.c_str();
    esp_aes_setkey(&context, aeskey, 128);
    unsigned char input[16];
    unsigned char output[16];
    std::stringstream output_data;
    for (int i = 0; i < data.length(); i += 16) {
        string inp = data.substr(i, 16);
        memcpy(input, inp.c_str(), 16);
        esp_aes_crypt_ecb(&context, ESP_AES_ENCRYPT, input, output);
        output_data.write((char *) output, 16);
    }
    esp_aes_free(&context);
    esp_aes_release_hardware();
    return output_data.str();
}

// -----------------------------------------------------------------------------
// --[xor_array]----------------------------------------------------------------
// -----------------------------------------------------------------------------
string xor_array(string data, string xor_array, int offset = 0) {
    for (int i = 0; i < data.length(); i++)
        data[i] = data[i] ^ xor_array[(offset + i) % xor_array.length()];
    return data;
}

// -----------------------------------------------------------------------------
// --[compute_nonce]------------------------------------------------------------
// -----------------------------------------------------------------------------
string compute_nonce(char msg_type_id, string session_open_nonce, uint16_t security_counter) {
    std::stringstream ss;
    ss.put(msg_type_id);
    ss << session_open_nonce;
    ss.put(0);
    ss.put(0);
    ss.put((char) (security_counter >> 8));
    ss.put((char) security_counter);
    return ss.str();
}

// -----------------------------------------------------------------------------
// --[generic_ceil]-------------------------------------------------------------
// -----------------------------------------------------------------------------
int generic_ceil(int value, int step, int minimum) {
    return (int) ((std::ceil((value - minimum) / step) * step) + minimum);
}

// -----------------------------------------------------------------------------
// --[compute_auth_value]-------------------------------------------------------
// -----------------------------------------------------------------------------
string compute_auth_value(string data, char msg_type_id, string session_open_nonce, uint16_t security_counter, std::string key) {
    Serial.println("# Computing auth value...");
    assert(key.length() == 16);
    string nonce = compute_nonce(msg_type_id, session_open_nonce, security_counter);
    size_t data_length = data.length(); // original data length
    if (data.length() % 16 > 0)
        data.append(16 - (data.length() % 16), 0);
    string encrypted_xor_data = "";
    encrypted_xor_data.append(1, 9);
    encrypted_xor_data.append(nonce);
    encrypted_xor_data.append(1, (char) (data_length >> 8));
    encrypted_xor_data.append(1, (char) data_length);
    encrypted_xor_data = encrypt_aes_ecb(encrypted_xor_data, key);
    for (int offset = 0; offset < data.length(); offset += 0x10) {
        string xored = xor_array(encrypted_xor_data, data, offset);
        encrypted_xor_data = encrypt_aes_ecb(xored, key);
    }
    string extra = "";
    extra.append(1, 1);
    extra.append(nonce);
    extra.append(2, 0);
    string ret = xor_array(encrypted_xor_data.substr(0, 4), encrypt_aes_ecb(extra, key));
    Serial.println("# Auth value computing done...");
    return ret;
}

// -----------------------------------------------------------------------------
// --[crypt_data]---------------------------------------------------------------
// -----------------------------------------------------------------------------
string crypt_data(string data, char msg_type_id, string session_open_nonce, uint16_t security_counter, string key) {
    Serial.print("# Cryptdata length: ");
    Serial.println(data.length());
    assert(key.length() == 16);
    assert(session_open_nonce.length() == 8);
    // is this not basically AES-CTR?
    string nonce = compute_nonce(msg_type_id, session_open_nonce, security_counter);
    string xor_data;
    int len = data.length();
    if (len % 16 > 0)
        len += 16 - (len % 16);
    len = len / 16;
    for (int i = 0; i < len; i++) {
        string to_encrypt = "";
        to_encrypt.append(1, 1);
        to_encrypt.append(nonce);
        to_encrypt.append(1, (char) ((i + 1) >> 8));
        to_encrypt.append(1, (char) (i + 1));
        xor_data.append(encrypt_aes_ecb(to_encrypt, key));
    }
    string ret = xor_array(data, xor_data);
    return ret;
}
