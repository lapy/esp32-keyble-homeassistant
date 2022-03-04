#include <Arduino.h>
#include <esp_log.h>
#include <sstream>
#include <queue>
#include <string>
#include "eQ3.h"
#include <PubSubClient.h>
#include <NimBLEDevice.h>
#include <WiFiClient.h>

#define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT
#define ETH_PHY_POWER 12

#include <ETH.h>

// -----------------------------------------------------------------------------
// CONFIGURATION
// -----------------------------------------------------------------------------
// MQTT configuration
#define MQTT_HOST ""
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""
// Keyble credentials
#define ADDRESS ""
#define USER_KEY ""
#define USER_ID 1
#define CARD_KEY ""
// Other settings - change only if you know what you are doing
#define MQTT_ROOT_TOPIC "smartlock"
#define MQTT_SUB_COMMAND "/KeyBLE/set"
#define MQTT_SUB_STATE "/KeyBLE/get"
#define MQTT_PUB_STATE "/KeyBLE"
#define MQTT_PUB_LOCK_STATE "/KeyBLE/lock_state"
#define MQTT_PUB_AVAILABILITY "/KeyBLE/availability"
#define MQTT_PUB_BATT "/KeyBLE/battery"
#define MQTT_PUB_RSSI "/KeyBLE/linkquality"
#define HOMEASSISTANT_MQTT_PREFIX "homeassistant"
#define REFRESH_INTERVAL 60000
#define CONFIG_BT_NIMBLE_PINNED_TO_CORE 0
#define PUBLISH_RETRIES 10
// -----------------------------------------------------------------------------
// END CONFIGURATION
// -----------------------------------------------------------------------------

eQ3 *keyble;
bool do_toggle = false;
bool do_open = false;
bool do_lock = false;
bool do_unlock = false;
bool do_status = false;
bool do_pair = false;
bool wifiActive = true;
bool cmdTriggered = false;
unsigned long timeout = 0;
bool waitForAnswer = false;
unsigned long starttime = 0;
LockStatus status;
BatteryStatus batteryLow;
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;

String mqtt_sub_command_value = "";
String mqtt_sub_state_value = "";
String mqtt_pub_state_value = "";
String mqtt_pub_lock_state_value = "";
String mqtt_pub_availability_value = "";
String mqtt_pub_battery_value = "";
String mqtt_pub_rssi_value = "";
const char *mqtt_sub_command_topic = MQTT_ROOT_TOPIC MQTT_SUB_COMMAND;
const char *mqtt_sub_state_topic = MQTT_ROOT_TOPIC MQTT_SUB_STATE;
const char *mqtt_pub_state_topic = MQTT_ROOT_TOPIC MQTT_PUB_STATE;
const char *mqtt_pub_lock_topic = MQTT_ROOT_TOPIC MQTT_PUB_LOCK_STATE;
const char *mqtt_pub_availability_topic = MQTT_ROOT_TOPIC MQTT_PUB_AVAILABILITY;
const char *mqtt_pub_battery_topic = MQTT_ROOT_TOPIC MQTT_PUB_BATT;
const char *mqtt_pub_rssi_topic = MQTT_ROOT_TOPIC MQTT_PUB_RSSI;
char charBufferStatus[10];
char charBufferLockStatus[9];
char charBufferAvailability[8];
char charBufferBatt[6];
char charBufferRssi[4];
bool statusChanged = false;

void MqttCallback(char *topic, byte *payload, unsigned int length);

WiFiClient wifiClient;
PubSubClient mqttClient(MQTT_HOST, MQTT_PORT, &MqttCallback, wifiClient);

static bool eth_connected = false;
uint64_t chipid;

// -----------------------------------------------------------------------------
// ---[HomeAssistant-Setup]-----------------------------------------------------
// -----------------------------------------------------------------------------
void SetupHomeAssistant()
{
  // Temporarily increase buffer size to send bigger payloads
  mqttClient.setBufferSize(500);

  Serial.println("# Setting up Home Assistant autodiscovery");

  String lock_conf = String() + "{\"~\":\"" + MQTT_ROOT_TOPIC + "\"," +
                     +"\"name\":\"Eqiva Bluetooth Smart Lock\"," +
                     +"\"device\":{\"identifiers\":[\"keyble_" + ADDRESS + "\"]," +
                     +"\"manufacturer\":\"eQ-3\",\"model\":\"Key-BLE\",\"name\":\"Eqiva Bluetooth Smart Lock\" }," +
                     +"\"uniq_id\":\"keyble_" + ADDRESS + "\"," +
                     +"\"stat_t\":\"~" + MQTT_PUB_STATE + "\"," +
                     +"\"avty_t\":\"~" + MQTT_PUB_AVAILABILITY + "\"," +
                     +"\"opt\":false," + // optimistic false, wait for actual state update
                     +"\"cmd_t\":\"~" + MQTT_SUB_COMMAND + "\"}";

  Serial.println("# " + String(HOMEASSISTANT_MQTT_PREFIX) + "/lock/KeyBLE/config");
  mqttClient.publish((String(HOMEASSISTANT_MQTT_PREFIX) + "/lock/KeyBLE/config").c_str(), lock_conf.c_str(), true);

  String lock_state_conf = String() + "{\"~\":\"" + MQTT_ROOT_TOPIC + "\"," +
                           +"\"name\":\"Eqiva Bluetooth Smart Lock Lockstate\"," +
                           +"\"device\":{\"identifiers\":[\"keyble_" + ADDRESS + "\"]," +
                           +"\"manufacturer\":\"eQ-3\",\"model\":\"Key-BLE\",\"name\":\"Eqiva Bluetooth Smart Lock\"}," +
                           +"\"uniq_id\":\"keyble_" + ADDRESS + "_lockstate\"," +
                           +"\"stat_t\":\"~" + MQTT_PUB_LOCK_STATE + "\"," +
                           +"\"avty_t\":\"~" + MQTT_PUB_AVAILABILITY + "\"," +
                           +"\"icon\":\"mdi:lock-alert-outline\"}";

  Serial.println("# " + String(HOMEASSISTANT_MQTT_PREFIX) + "/sensor/KeyBLE/lockstate/config");
  mqttClient.publish((String(HOMEASSISTANT_MQTT_PREFIX) + "/sensor/KeyBLE/lockstate/config").c_str(), lock_state_conf.c_str(), true);

  String link_quality_conf = String() + "{\"~\":\"" + MQTT_ROOT_TOPIC + "\"," +
                             +"\"name\":\"Eqiva Bluetooth Smart Lock Linkquality\"," +
                             +"\"device\":{\"identifiers\":[\"keyble_" + ADDRESS + "\"]," +
                             +"\"manufacturer\":\"eQ-3\",\"model\":\"Key-BLE\",\"name\":\"Eqiva Bluetooth Smart Lock\"}," +
                             +"\"uniq_id\":\"keyble_" + ADDRESS + "_linkquality\"," +
                             +"\"stat_t\":\"~" + MQTT_PUB_RSSI + "\"," +
                             +"\"avty_t\":\"~" + MQTT_PUB_AVAILABILITY + "\"," +
                             +"\"unit_of_meas\":\"rssi\"," +
                             +"\"dev_cla\":\"signal_strength\"}";

  Serial.println("# " + String(HOMEASSISTANT_MQTT_PREFIX) + "/sensor/KeyBLE/linkquality/config");
  mqttClient.publish((String(HOMEASSISTANT_MQTT_PREFIX) + "/sensor/KeyBLE/linkquality/config").c_str(), link_quality_conf.c_str(), true);

  String battery_conf = String() + "{\"~\": \"" + MQTT_ROOT_TOPIC + "\"," +
                        +"\"name\":\"Eqiva Bluetooth Smart Lock Battery\"," +
                        +"\"device\":{\"identifiers\":[\"keyble_" + ADDRESS + "\"]," +
                        +"\"manufacturer\":\"eQ-3\",\"model\":\"Key-BLE\", \"name\":\"Eqiva Bluetooth Smart Lock\" }," +
                        +"\"uniq_id\":\"keyble_" + ADDRESS + "_battery\"," +
                        +"\"stat_t\":\"~" + MQTT_PUB_BATT + "\"," +
                        +"\"avty_t\":\"~" + MQTT_PUB_AVAILABILITY + "\"," +
                        +"\"dev_cla\":\"battery\"}";

  Serial.println("# " + String(HOMEASSISTANT_MQTT_PREFIX) + "/binary_sensor/KeyBLE/battery/config");
  mqttClient.publish((String(HOMEASSISTANT_MQTT_PREFIX) + "/binary_sensor/KeyBLE/battery/config").c_str(), battery_conf.c_str(), true);

  // Reset buffer size to default
  mqttClient.setBufferSize(256);

  Serial.println("# Home Assistant autodiscovery configured");
}

// -----------------------------------------------------------------------------
// ---[MQTT-Setup]--------------------------------------------------------------
// -----------------------------------------------------------------------------
void SetupMqtt()
{
  Serial.println("# Setting up MQTT");
  while (!mqttClient.connected())
  { // Loop until we're reconnected to the MQTT server
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(&MqttCallback);
    //Serial.println("# Connect to MQTT-Broker... ");
    if (mqttClient.connect(MQTT_ROOT_TOPIC, MQTT_USER, MQTT_PASSWORD))
    {
      Serial.println("# MQTT connected!");
      mqttClient.subscribe(mqtt_sub_command_topic);
      Serial.print("# Subscribed to topic: ");
      Serial.println(mqtt_sub_command_topic);
      mqttClient.subscribe(mqtt_sub_state_topic);
      Serial.print("# Subscribed to topic: ");
      Serial.println(mqtt_sub_state_topic);
      Serial.println("# MQTT Setup done");
    }
  }
}

// -----------------------------------------------------------------------------
// ---[MqttCallback]------------------------------------------------------------
// -----------------------------------------------------------------------------
void MqttCallback(char *topic, byte *payload, unsigned int length)
{
  String topicString = String(topic);
  String payloadString = String((char *)payload).substring(0, length);
  // Serial.println("# topic: " + topicString + ", payload: " + payloadString);

  if (topicString.endsWith(MQTT_SUB_COMMAND) == 1)
  {
    Serial.println("# Command received");
    /*
    //pair
    if (payloadString == "PAIR")
    {
      do_pair = true;
      Serial.println("*** pair ***"");
      
    }
    */
    //toggle
    if (payloadString == "TOGGLE")
    {
      do_toggle = true;
      Serial.println("*** toggle ***");
    }
    //open
    if (payloadString == "OPEN")
    {
      do_open = true;
      Serial.println("*** open ***");
    }
    //lock
    if (payloadString == "LOCK")
    {
      do_lock = true;
      Serial.println("*** lock ***");
    }
    //unlock
    if (payloadString == "UNLOCK")
    {
      do_unlock = true;
      Serial.println("*** unlock ***");
    }
  }
  else if (topicString.endsWith(MQTT_SUB_STATE) == 1)
  {
    Serial.println("# Status request received");
    if (payloadString == "")
    {
      do_status = true;
      Serial.println("*** status ***");
    }
  }
}

// -----------------------------------------------------------------------------
// ---[StatusUpdateCallback]----------------------------------------------------
// -----------------------------------------------------------------------------
void StatusUpdateCallback(LockStatus newlockstatus, BatteryStatus newbatterystatus, int RSSI)
{
  Serial.println("# Status changed: " + LockStatusToString(newlockstatus));

  //MQTT_PUB_STATE status
  status = newlockstatus;
  batteryLow = newbatterystatus;
  String str_status = "";
  if (status == LockStatus::UNLOCKED || status == LockStatus::OPENED)
    str_status = "UNLOCKED";
  else if (status == LockStatus::LOCKED)
    str_status = "LOCKED";
  str_status.toCharArray(charBufferStatus, 10);

  //MQTT_PUB_LOCK_STATE lock status (more detailed)
  String strBufferLockStatus = LockStatusToString(newlockstatus);
  strBufferLockStatus.toCharArray(charBufferLockStatus, 9);

  //MQTT_PUB_AVAILABILITY availability
  String str_availability = (status > LockStatus::UNKNOWN) ? "online" : "offline";
  str_availability.toCharArray(charBufferAvailability, 8);

  //MQTT_PUB_BATT battery
  String str_batt = newbatterystatus ? "ON" : "OFF";
  str_batt.toCharArray(charBufferBatt, 6);

  //MQTT_PUB_RSSI rssi
  String keybleRssi = String(RSSI);
  keybleRssi.toCharArray(charBufferRssi, 4);

  statusChanged = true;
}

// -----------------------------------------------------------------------------
// ---[WiFiEventHandler]---------------------------------------------------------------
// -----------------------------------------------------------------------------
void WiFiEventHandler(WiFiEvent_t event)
{
  switch (event)
  {
  case SYSTEM_EVENT_ETH_START:
    Serial.println("# ETH Started");
    //set eth hostname here
    ETH.setHostname("esp32-ethernet");
    break;
  case SYSTEM_EVENT_ETH_CONNECTED:
    Serial.println("# ETH Connected");
    break;
  case SYSTEM_EVENT_ETH_GOT_IP:
    Serial.print("# ETH MAC: ");
    Serial.print(ETH.macAddress());
    Serial.print(", IPv4: ");
    Serial.print(ETH.localIP());
    if (ETH.fullDuplex())
    {
      Serial.print(", FULL_DUPLEX");
    }
    Serial.print(", ");
    Serial.print(ETH.linkSpeed());
    Serial.println("Mbps");
    eth_connected = true;
    break;
  case SYSTEM_EVENT_ETH_DISCONNECTED:
    Serial.println("# ETH Disconnected");
    eth_connected = false;
    break;
  case SYSTEM_EVENT_ETH_STOP:
    Serial.println("# ETH Stopped");
    eth_connected = false;
    break;
  default:
    break;
  }
}

// -----------------------------------------------------------------------------
// ---[Setup]-------------------------------------------------------------------
// -----------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  Serial.println("Start...");
  //Ethernet
  chipid = ESP.getEfuseMac();                                      //The chip ID is essentially its MAC address(length: 6 bytes).
  Serial.printf("ESP32 Chip ID = %04X", (uint16_t)(chipid >> 32)); //print High 2 bytes
  Serial.printf("%08X\n", (uint32_t)chipid);                       //print Low 4bytes.
  WiFi.onEvent(WiFiEventHandler);
  ETH.begin();
  //Bluetooth
  NimBLEDevice::init("esp32ble");
  keyble = new eQ3(ADDRESS, USER_KEY, USER_ID);
  keyble->setOnStatusChange(StatusUpdateCallback);
  do_status = true;
}

// -----------------------------------------------------------------------------
// ---[loop]--------------------------------------------------------------------
// -----------------------------------------------------------------------------
void loop()
{
  if (mqttClient.connected())
  {
    mqttClient.loop();
    if (statusChanged)
    {
      mqttClient.publish(mqtt_pub_state_topic, charBufferStatus, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_state_topic);
      Serial.print("/");
      Serial.println(charBufferStatus);
      mqtt_pub_state_value = charBufferStatus;

      mqttClient.publish(mqtt_pub_lock_topic, charBufferLockStatus, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_lock_topic);
      Serial.print("/");
      Serial.println(charBufferLockStatus);
      mqtt_pub_lock_state_value = charBufferLockStatus;

      mqttClient.publish(mqtt_pub_availability_topic, charBufferAvailability, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_availability_topic);
      Serial.print("/");
      Serial.println(charBufferAvailability);
      mqtt_pub_availability_value = charBufferAvailability;

      mqttClient.publish(mqtt_pub_battery_topic, charBufferBatt, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_battery_topic);
      Serial.print("/");
      Serial.println(charBufferBatt);
      mqtt_pub_battery_value = charBufferBatt;

      mqttClient.publish(mqtt_pub_rssi_topic, charBufferRssi, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_rssi_topic);
      Serial.print("/");
      Serial.println(charBufferRssi);
      mqtt_pub_rssi_value = charBufferRssi;

      statusChanged = false;
    }
  }
  else if (eth_connected) {
    //MQTT
    SetupMqtt();
    //Homeassistant
    SetupHomeAssistant();
  }

  if (do_open || do_lock || do_unlock || do_status || do_toggle || do_pair)
  {
    waitForAnswer = true;
    keyble->_LockStatus = LockStatus::UNKNOWN;
    starttime = millis();

    if (do_open)
    {
      Serial.println("*** open ***");
      keyble->open();
      do_open = false;
    }

    if (do_lock)
    {
      Serial.println("*** lock ***");
      keyble->lock();
      do_lock = false;
    }

    if (do_unlock)
    {
      Serial.println("*** unlock ***");
      keyble->unlock();
      do_unlock = false;
    }

    if (do_status)
    {
      Serial.println("*** get state ***");
      keyble->updateInfo();
      do_status = false;
    }

    if (do_toggle)
    {
      Serial.println("*** toggle ***");
      if ((status == LockStatus::UNLOCKED) || (status == LockStatus::OPENED))
      {
        keyble->lock();
        do_lock = false;
      }
      if (status == LockStatus::LOCKED)
      {
        keyble->unlock();
        do_unlock = false;
      }
      do_toggle = false;
    }

    if (do_pair)
    {
      Serial.println("*** pair ***");
      //Parse key card data
      std::string cardKey = CARD_KEY;
      if (cardKey.length() == 56)
      {
        Serial.println(cardKey.c_str());
        std::string pairMac = cardKey.substr(1, 12);

        pairMac = pairMac.substr(0, 2) + ":" + pairMac.substr(2, 2) + ":" + pairMac.substr(4, 2) + ":" + pairMac.substr(6, 2) + ":" + pairMac.substr(8, 2) + ":" + pairMac.substr(10, 2);
        Serial.println(pairMac.c_str());
        std::string pairKey = cardKey.substr(14, 32);
        Serial.println(pairKey.c_str());
        std::string pairSerial = cardKey.substr(46, 10);
        Serial.println(pairSerial.c_str());
      }
      else
      {
        Serial.println("# invalid CardKey! Pattern example:");
        Serial.println("  M followed by KeyBLE MAC length 12");
        Serial.println("  K followed by KeyBLE CardKey length 32");
        Serial.println("  Serialnumber");
        Serial.println("  MxxxxxxxxxxxxKxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxSSSSSSSSSS");
      }
      do_pair = false;
    }
  }
  if (waitForAnswer)
  {
    bool timeout = (millis() - starttime > LOCK_TIMEOUT * 1000 + 1000);
    bool finished = (keyble->_LockStatus > LockStatus::MOVING);

    if (finished)
    {
      Serial.println("# Lock operation terminated!");
    }
    else if (timeout)
    {
      Serial.println("# Lock timed out!");
    }

    if (finished || timeout)
    {
      waitForAnswer = false;

      if (REFRESH_INTERVAL)
      {
        // reset refresh counter
        previousMillis = millis();
      }
    }
    //delay(100);
  }
  //Periodic status refresh logic, executed only if no commands are waiting
  else if (REFRESH_INTERVAL)
  {
    currentMillis = millis();
    if (currentMillis - previousMillis > REFRESH_INTERVAL)
    {
      do_status = true;               //request status update
      previousMillis = currentMillis; //reset refresh counter
    }
  }
}
