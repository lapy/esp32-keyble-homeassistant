//
// Created by marius on 16.12.18.
//

#ifndef DOOR_OPENER_UTIL_H
#define DOOR_OPENER_UTIL_H

std::string string_to_hex(const std::string &input);
std::string hexstring_to_string(const std::string &hex_chars);

// input should be padded array
string encrypt_aes_ecb(string &data, string &key);
string xor_array(string data, string xor_array, int offset = 0);
string compute_nonce(char msg_type_id, string session_open_nonce, uint16_t security_counter);
int generic_ceil(int value, int step, int minimum);
string compute_auth_value(string data, char msg_type_id, string session_open_nonce, uint16_t security_counter, std::string key);
string crypt_data(string data, char msg_type_id, string session_open_nonce, uint16_t security_counter, string key);
#endif //DOOR_OPENER_UTIL_H
