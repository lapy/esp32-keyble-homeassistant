#ifndef DOOR_OPENER_EQ3_H
#define DOOR_OPENER_EQ3_H

#include <NimBLEScan.h>
#include <NimBLEClient.h>
#include <queue>
#include <functional>
#include "eQ3_constants.h"
#include "eQ3_message.h"

#define BLE_UUID_SERVICE "58e06900-15d8-11e6-b737-0002a5d5c51b"
#define BLE_UUID_WRITE "3141dd40-15db-11e6-a24b-0002a5d5c51b"
#define BLE_UUID_READ "359d4820-15db-11e6-82bd-0002a5d5c51b"
#define LOCK_TIMEOUT 35
class eQ3;

typedef std::function<void(void*, eQ3*)> KeyBleStatusHandler;

void tickTask(void *params);
void notify_func(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
void scan_func(NimBLEScanResults pBLEScanResults);
class eQ3 : public NimBLEAdvertisedDeviceCallbacks, public NimBLEClientCallbacks {
    friend void tickTask(void *params);
    std::map<ConnectionState,std::function<void(void)>> queue;

    void onConnect(NimBLEClient *pClient);
    void connectHandler();
    void onDisconnect(NimBLEClient *pClient);
    void onResult(NimBLEAdvertisedDevice* advertisedDevice);
    void sendNextFragment();
    void exchangeNonces();
    bool sendMessage(eQ3Message::Message *msg);
    void sendCommand(CommandType command);

    friend NimBLEClient;
    friend NimBLEScan;

    std::queue<eQ3Message::MessageFragment> sendQueue;
    std::vector<eQ3Message::MessageFragment> recvFragments;

    string address;

    NimBLEScan *bleScan;
    
    NimBLERemoteCharacteristic *sendChar;
    NimBLERemoteCharacteristic *recvChar;
    NimBLEAdvertisedDevice *device;

    time_t lastActivity = 0;

    SemaphoreHandle_t mutex;
    std::function<void(LockStatus)> onStatusChange;
public:
    ClientState state;
    NimBLEClient *bleClient;
    int _LockStatus;
    int _BatteryStatus;
    int _RSSI;
    bool onTick();
    void lock();
    void unlock();
    void open();
    void pairingRequest(std::string cardkey);
    void connect();
    void updateInfo();
    void toggle();
    void setOnStatusChange(std::function<void(LockStatus)> cb);
    void onNotify(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
    eQ3(std::string ble_address, std::string user_key, unsigned char user_id = 0xFF);
};

#endif //DOOR_OPENER_EQ3_H
