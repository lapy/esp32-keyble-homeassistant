#include <Arduino.h>
#include <string>
#include <NimBLEDevice.h>
#include <ctime>
#include <sstream>
#include "eQ3.h"
#include "eQ3_util.h"
using namespace std;

int _LockStatus = 0;
int _RSSI = 0;


// -----------------------------------------------------------------------------
// --[tickTask]-----------------------------------------------------------------
// -----------------------------------------------------------------------------
void tickTask(void *params) {
    auto * inst = (eQ3*) params;
    while(inst->onTick()) {
        yield();
    }
}

eQ3* cb_instance;

#define SEMAPHORE_WAIT_TIME  (10000 / portTICK_PERIOD_MS)

String LockStatusToString (LockStatus ls) {
    switch(ls) {
        case LockStatus::UNKNOWN:
            return "UNKNOWN";
        case LockStatus::MOVING:
            return "MOVING";
        case LockStatus::UNLOCKED:
            return "UNLOCKED";
        case LockStatus::LOCKED:
            return "LOCKED";
        case LockStatus::OPENED:
            return "OPENED";
        default:
            return "UNKNOWN";
    };    
}

// -----------------------------------------------------------------------------
// --[notify_func callback]--------------------------------------------------------------
// -----------------------------------------------------------------------------
void notify_func(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    cb_instance->onNotify(pBLERemoteCharacteristic,pData,length,isNotify);
}

// -----------------------------------------------------------------------------
// --[status_func callback]-----------------------------------------------------
// -----------------------------------------------------------------------------
void status_func(LockStatus _LockStatus, BatteryStatus _BatteryStatus, int _RSSI) {
    Serial.println("# Status changed: " + LockStatusToString(_LockStatus));
}

// -----------------------------------------------------------------------------
// --[ctor]---------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Important: Initialize NimBLEDevice::init(""); yourself
eQ3::eQ3(std::string ble_address, std::string user_key, unsigned char user_id) {
    state.user_key = hexstring_to_string(user_key);
    //Serial.println(state.user_key.length());
    state.user_id = user_id;

    // init BLE scan
    bleScan = NimBLEDevice::getScan();
    // init BLE client
    bleClient = NimBLEDevice::createClient();
    bleClient->setClientCallbacks((NimBLEClientCallbacks *)this);


    cb_instance = this;
    mutex = xSemaphoreCreateMutex();
    this->address = ble_address;

    setOnStatusChange(status_func);

    // pin to core 1 (where the Arduino main loop resides), priority 1
    xTaskCreatePinnedToCore(&tickTask, "worker", 10240, this, 1, nullptr, 1);
}

// -----------------------------------------------------------------------------
// --[onConnect]----------------------------------------------------------------
// -----------------------------------------------------------------------------
// NimBLEClientCallbacks virtual override
void eQ3::onConnect(NimBLEClient *pClient) {
    state.connectionState = CONNECTING;
}

// -----------------------------------------------------------------------------
// --[onDisconnect]-------------------------------------------------------------
// -----------------------------------------------------------------------------
// NimBLEClientCallbacks virtual override
void eQ3::onDisconnect(NimBLEClient *pClient) {
    Serial.println("# Disconnected...");
    state.connectionState = DISCONNECTED;
    recvFragments.clear();
    std::queue<eQ3Message::MessageFragment>().swap(sendQueue); // clear queue
    queue.clear();
    sendCharacteristic->deleteDescriptors();
    recvCharacteristic->deleteDescriptors();
}

// NimBLEClientCallbacks virtual override
bool eQ3::onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) {
    Serial.println("# onConnParamsUpdateRequest!!!");
    pClient->setConnectionParams(params->itvl_min, params->itvl_max, params->latency, params->supervision_timeout);
    return true;
}

// -----------------------------------------------------------------------------
// --[onTick]-------------------------------------------------------------------
// -----------------------------------------------------------------------------
bool eQ3::onTick() {
    if (xSemaphoreTake(mutex, 0)) {
        //Serial.println("# Taking semaphore in tick");
        switch (state.connectionState)
        {

        case DISCONNECTED:
            break;

        case SCANNING:
            // Handled by callback
            break;

        case FOUND:
        {
            Serial.println("# Found, connecting...");
            bleClient->connect(NimBLEAddress(address));
            break;
        }
        break;

        case CONNECTING:
        {
            Serial.println("# Retrieving characteristics and establishing connection...");
            NimBLERemoteService *comm;
            comm = bleClient->getService(NimBLEUUID(BLE_UUID_SERVICE));
            sendCharacteristic = comm->getCharacteristic(NimBLEUUID(BLE_UUID_WRITE)); // write buffer characteristic
            recvCharacteristic = comm->getCharacteristic(NimBLEUUID(BLE_UUID_READ));  // read buffer characteristic
            recvCharacteristic->subscribe(true, notify_func);
            lastActivity = time(NULL);
            Serial.println("# Connected");
            state.connectionState = CONNECTED;
        }
        break;

        case CONNECTED:
        {
            //Serial.println("# Executing CONNECTED queue in tick");
            auto queueFunc = queue.find(CONNECTED);
            if (queueFunc != queue.end())
            {
                queue.erase(CONNECTED);
                queueFunc->second();
            }
        }
        break;

        case NONCES_EXCHANGING:
        {
            Serial.println("# Verifying remote session nonce...");
            state.user_id = _ConnectionInfoMessage.getUserId();
            state.remote_session_nonce = _ConnectionInfoMessage.getRemoteSessionNonce();
            assert(state.remote_session_nonce.length() == 8);
            state.local_security_counter = 1;
            state.remote_security_counter = 0;
            state.connectionState = NONCES_EXCHANGED;
        }
        break;
        case NONCES_EXCHANGED:
        {
            //Serial.println("# Executing NONCES_EXCHANGED queue in tick");
            auto queueFunc = queue.find(NONCES_EXCHANGED);
            if (queueFunc != queue.end())
            {
                queue.erase(queueFunc);
                queueFunc->second();
            }
        }
        break;
        default:
            Serial.println("# Default tick");
        }
        if (state.connectionState > CONNECTING) {
            sendNextFragment();
            lastActivity = time(NULL);
        }
        //Serial.println("# Releasing semaphore in tick");
        xSemaphoreGive(mutex);
    }
    return true;
}

// -----------------------------------------------------------------------------
// --[onResult]-----------------------------------------------------------------
// -----------------------------------------------------------------------------
// NimBLEAdvertisedDeviceCallbacks virtual override
void eQ3::onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    if (advertisedDevice->getAddress().toString() == address) { // TODO: Make name and address variable
        Serial.print("# Found device: ");
        Serial.println(advertisedDevice->getAddress().toString().c_str());
        Serial.print("# RSSI: ");
        Serial.println(advertisedDevice->getRSSI());
        _RSSI = advertisedDevice->getRSSI();
        bleScan->stop();
        state.connectionState = FOUND;
    }
 
}

// -----------------------------------------------------------------------------
// --[setOnStatusChange]--------------------------------------------------------
// -----------------------------------------------------------------------------
void eQ3::setOnStatusChange(std::function<void(LockStatus, BatteryStatus, int)> cb) {
    xSemaphoreTake(mutex, SEMAPHORE_WAIT_TIME);
    onStatusChange = cb;
    xSemaphoreGive(mutex);
}

// -----------------------------------------------------------------------------
// --[exchangeNonces]-----------------------------------------------------------
// -----------------------------------------------------------------------------
void eQ3::exchangeNonces() {
    state.local_session_nonce.clear();
    for (int i = 0; i < 8; i++)
        state.local_session_nonce.append(1,esp_random());
    auto *msg = new eQ3Message::Connection_Request_Message;
    Serial.println("# Nonce exchange");
    sendMessage(msg);
}

// -----------------------------------------------------------------------------
// --[connect]------------------------------------------------------------------
// -----------------------------------------------------------------------------
void eQ3::connect() {
    state.connectionState = SCANNING;
    bleScan->setAdvertisedDeviceCallbacks(this);
    bleScan->setActiveScan(true);
    bleScan->setInterval(50);
    bleScan->setWindow(50);
    bleScan->start(25, false);
    Serial.println("# Searching ...");
}

// -----------------------------------------------------------------------------
// --[sendMessage]--------------------------------------------------------------
// -----------------------------------------------------------------------------
bool eQ3::sendMessage(eQ3Message::Message *msg) {
    std::string data;
    if (msg->isSecure()) {
        if (state.connectionState < NONCES_EXCHANGED) {
            // TODO check if slot for nonces_exchanged is already set?
            queue.insert(make_pair(NONCES_EXCHANGED,[this,msg]{
                sendMessage(msg);
            }));
            exchangeNonces();
            return true;
        }

        string padded_data;
        padded_data.append(msg->encode(&state));
        int pad_to = generic_ceil(padded_data.length(), 15, 8);
        if (pad_to > padded_data.length())
            padded_data.append(pad_to - padded_data.length(), 0);
        //crypt_data(padded_data, msg->id, state.remote_session_nonce, state.local_security_counter, state.user_key);
        data.append(1, msg->id);
        data.append(crypt_data(padded_data, msg->id, state.remote_session_nonce, state.local_security_counter, state.user_key));
        data.append(1, (char) (state.local_security_counter >> 8));
        data.append(1, (char) state.local_security_counter);
        data.append(compute_auth_value(padded_data, msg->id, state.remote_session_nonce, state.local_security_counter, state.user_key));
        state.local_security_counter++;
    } else {
        if (state.connectionState < CONNECTED) {
            // TODO check if slot for connected is already set?
            queue.insert(make_pair(CONNECTED,[this,msg]{
                sendMessage(msg);
            }));
            connect();
            return true;
        }
        data.append(1, msg->id);
        data.append(msg->encode(&state));
    }
    // fragment
    int chunks = data.length() / 15;
    if (data.length() % 15 > 0)
        chunks += 1;
    for (int i = 0; i < chunks; i++) {
        eQ3Message::MessageFragment frag;
        frag.data.append(1, (i ? 0 : 0x80) + (chunks - 1 - i)); // fragment status byte
        frag.data.append(data.substr(i * 15, 15));
        if (frag.data.length() < 16)
            frag.data.append(16 - (frag.data.length() % 16), 0);  // padding
        sendQueue.push(frag);
    }
    Serial.println("# sendMessage end.");;
    free(msg);
    return true;
}

// -----------------------------------------------------------------------------
// --[onNotify]-----------------------------------------------------------------
// -----------------------------------------------------------------------------
void eQ3::onNotify(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    xSemaphoreTake(mutex, SEMAPHORE_WAIT_TIME);
    Serial.println("# Taking semaphore onNotify");
    eQ3Message::MessageFragment frag;
    lastActivity = time(NULL);
    frag.data = std::string((char *) pData, length);
    recvFragments.push_back(frag);
    Serial.print("# Fragment Data: ");
    Serial.println(string_to_hex(frag.data).c_str());
    if (frag.isLast()) {
        if (!sendQueue.empty())
            sendQueue.pop();
        // concat message
        std::stringstream ss;
        auto msgtype = recvFragments.front().getType();
        for (auto &received_fragment : recvFragments) {
            ss << received_fragment.getData();
        }
        std::string msgdata = ss.str();
        recvFragments.clear();
        if (eQ3Message::Message::isTypeSecure(msgtype)) {
            auto msg_security_counter = static_cast<uint16_t>(msgdata[msgdata.length() - 6]);
            msg_security_counter <<= 8;
            msg_security_counter += msgdata[msgdata.length() - 5];
            //Serial.println((int)msg_security_counter);
            if (msg_security_counter <= state.remote_security_counter) {
                Serial.print("# Msg. security counter: ");
                Serial.println(msg_security_counter);
                Serial.print("# Security counter: ");
                Serial.println(state.remote_security_counter);
                Serial.println("# Counterfeit remote counter, releasing semaphore onNotify");
                xSemaphoreGive(mutex);
                return;
            }
            state.remote_security_counter = msg_security_counter;
            string msg_auth_value = msgdata.substr(msgdata.length() - 4, 4);
            Serial.print("# Auth value: ");
            Serial.println(string_to_hex(msg_auth_value).c_str());
            //std::string decrypted = crypt_data(msgdata.substr(0, msgdata.length() - 6), msgtype,
            std::string decrypted = crypt_data(msgdata.substr(1, msgdata.length() - 7), msgtype, state.local_session_nonce, state.remote_security_counter, state.user_key);
            Serial.print("# Crypted data: ");
            Serial.println(string_to_hex(msgdata.substr(1, msgdata.length() - 7)).c_str());
            std::string computed_auth_value = compute_auth_value(decrypted, msgtype, state.local_session_nonce, state.remote_security_counter, state.user_key);
            if (msg_auth_value != computed_auth_value) {
                Serial.println("# Auth value mismatch, releasing semaphore onNotify");
                xSemaphoreGive(mutex);
                return;
            }
            msgdata = decrypted;
            Serial.print("# Decrypted data: ");
            Serial.println(string_to_hex(msgdata).c_str());
        }

        switch (msgtype) {
            case 0: {
                // fragment ack, remove first
                if (!sendQueue.empty())
                    sendQueue.pop();
                Serial.println("# Ack message, releasing semaphore onNotify");
                xSemaphoreGive(mutex);
                return;
            }

            case 0x81: // answer with security
                // TODO call callback to user that pairing succeeded
                Serial.println("# Answer with security...");
                break;

            case 0x01: // answer without security
                // TODO report error
                Serial.println("# Answer without security...");
                break;

            case 0x05: {
                // status changed notification
                Serial.println("# Status changed notification");
                auto * message = new eQ3Message::StatusRequestMessage;
                sendMessage(message);
                break;
            }

            case 0x03: {
                // connection info message
                _ConnectionInfoMessage.data = msgdata;
                state.connectionState = NONCES_EXCHANGING;
                break;
            }

            case 0x83: {
                // status info
                eQ3Message::Status_Info_Message message;
                message.data = msgdata;
                _LockStatus = message.getLockStatus();
                _BatteryStatus = message.getBatteryStatus();
                _RSSI = bleClient->getRssi();
                onStatusChange(_LockStatus, _BatteryStatus, _RSSI); // BUG: löst einen Reset aus!!
                break;
            }

            default:
            /*case 0x8f: */{ // user info
                Serial.println("# User info, releasing semaphore onNotify");
                xSemaphoreGive(mutex);
                return;
            }
        }

    } else {
        // create ack
        Serial.println("# send Ack ");
        eQ3Message::FragmentAckMessage ack(frag.getStatusByte());
        sendQueue.push(ack);
    }
    Serial.println("# Releasing semaphore onNotify (end)");
    xSemaphoreGive(mutex);
}

// -----------------------------------------------------------------------------
// --[pairingRequest]-----------------------------------------------------------
// -----------------------------------------------------------------------------
void eQ3::pairingRequest(std::string cardkey) {
    xSemaphoreTake(mutex, SEMAPHORE_WAIT_TIME);
    Serial.println("# Taking semaphore pairingRequest");
    if (state.connectionState < NONCES_EXCHANGED) {
        // TODO check if slot for nonces_exchanged is already set?

        // TODO callback when pairing finished, or make blocking?
        queue.insert(make_pair(NONCES_EXCHANGED,[this,cardkey]{
            pairingRequest(cardkey);
        }));
        Serial.println("# Pairing request");
        exchangeNonces();
        Serial.println("# Releasing semaphore pairingRequest");
        xSemaphoreGive(mutex);
        return;
    }
    auto *message = new eQ3Message::PairingRequestMessage();
    //return concatenated_array([data.user_id], padded_array(data.encrypted_pair_key, 22, 0), integer_to_byte_array(data.security_counter, 2), data.authentication_value);
    message->data.append(1, state.user_id);
    Serial.print("#Message id: ");
    Serial.println((int) message->id);

    // enc pair key
    assert(state.remote_session_nonce.length() == 8);
    assert(state.user_key.length() == 16);

    std::string card_key = hexstring_to_string(cardkey);

    string encrypted_pair_key = crypt_data(state.user_key, 0x04, state.remote_session_nonce, state.local_security_counter, card_key);
    if (encrypted_pair_key.length() < 22)
        encrypted_pair_key.append(22 - encrypted_pair_key.length(), 0);
    assert(encrypted_pair_key.length() == 22);
    message->data.append(encrypted_pair_key);

    // counter
    message->data.append(1, (char) (state.local_security_counter >> 8));
    message->data.append(1, (char) (state.local_security_counter));

    // auth value
    string extra;
    extra.append(1, state.user_id);
    extra.append(state.user_key);
    if (extra.length() < 23)
        extra.append(23 - extra.length(), 0);
    assert(extra.length() == 23);
    string auth_value = compute_auth_value(extra, 0x04, state.remote_session_nonce, state.local_security_counter, card_key);
    message->data.append(auth_value);
    assert(message->data.length() == 29);
    sendMessage(message);
    Serial.println("# Releasing semaphore pairingRequest");
    xSemaphoreGive(mutex);
}

// -----------------------------------------------------------------------------
// --[sendNextFragment]---------------------------------------------------------
// -----------------------------------------------------------------------------
// Used on Tick
void eQ3::sendNextFragment() {
    if (sendQueue.empty())
        return;
    if (sendQueue.front().sent && std::difftime(sendQueue.front().timeSent, std::time(NULL)) < 5)
        return;
    sendQueue.front().sent = true;
    string data = sendQueue.front().data;
    sendQueue.front().timeSent = std::time(NULL);
    Serial.print("# Sending fragment: ");
    Serial.println(string_to_hex(data).c_str());
    assert(data.length() == 16);
    sendCharacteristic->writeValue((uint8_t *) (data.c_str()), 16, true);
}

void eQ3::sendCommand(CommandType command) {
    xSemaphoreTake(mutex, SEMAPHORE_WAIT_TIME);
    Serial.println("# Taking semaphore sendCommand");
    auto msg = new eQ3Message::CommandMessage(command);
    sendMessage(msg);
    Serial.println("# Releasing semaphore sendCommand");
    xSemaphoreGive(mutex);
}

void eQ3::unlock() {
    sendCommand(UNLOCK);
}

void eQ3::lock() {
    sendCommand(LOCK);
}

void eQ3::open() {
    sendCommand(OPEN);
}

void eQ3::updateInfo() {
    xSemaphoreTake(mutex, SEMAPHORE_WAIT_TIME);
    Serial.println("# Taking semaphore updateInfo");
    auto * message = new eQ3Message::StatusRequestMessage;
    sendMessage(message);
    Serial.println("# Releasing semaphore updateInfo");
    xSemaphoreGive(mutex);
}
