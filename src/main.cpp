#include <Arduino.h>
#include <esp_log.h>
#include <sstream>
#include <queue>
#include <string>
#include "eQ3.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>

#define WIFI_SSID "###"
#define WIFI_PASSWORD "###"
#define MQTT_HOST "###"
#define MQTT_PORT 1883
#define MQTT_USER "###"
#define MQTT_PASSWORD "###"
#define MQTT_ROOT_TOPIC "smartlock"
#define MQTT_SUB_COMMAND "/KeyBLE/set"
#define MQTT_SUB_STATE "/KeyBLE/get"
#define MQTT_PUB_STATE "/KeyBLE"
#define MQTT_PUB_LOCK_STATE "/KeyBLE/lock_state"
#define MQTT_PUB_AVAILABILITY "/KeyBLE/availability"
#define MQTT_PUB_BATT "/KeyBLE/battery"
#define MQTT_PUB_RSSI "/KeyBLE/linkquality"
#define ADDRESS "###"
#define USER_KEY "###"
#define USER_ID 2
#define CARD_KEY "###"
#define HOMEASSISTANT_MQTT_PREFIX "homeassistant"
#define REFRESH_INTERVAL 30000
#define CONFIG_BT_NIMBLE_PINNED_TO_CORE 0

// NETWORK PARAMETERS
IPAddress ip(0, 0, 0, 0); // Set fixed ip
IPAddress gateway(0, 0, 0, 0); // Gateway/router ip
IPAddress subnet(255, 255, 255, 0); // Subnet mask
IPAddress dns(0, 0, 0, 0); // Dns server

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
bool statusUpdated = false;
bool waitForAnswer = false;
unsigned long starttime = 0;
int status = 0;
bool batteryLow = false;
String keybleRssi = "";
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

void MqttCallback(char *topic, byte *payload, unsigned int length);

WiFiClient wifiClient;
PubSubClient mqttClient(MQTT_HOST, MQTT_PORT, &MqttCallback, wifiClient);

// -----------------------------------------------------------------------------
// ---[SetWifi]-----------------------------------------------------------------
// -----------------------------------------------------------------------------
void SetWifi(bool active)
{
  wifiActive = active;

  if (active)
  {
    WiFi.mode(WIFI_STA);
    Serial.println("# WiFi enabled");
  }
  else
  {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("# WiFi disabled");
  }
  // delay(100);
  // yield();
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
// ---[Wifi signal quality]-----------------------------------------------------
// -----------------------------------------------------------------------------
int GetWifiSignalQuality()
{
  float signal = 2 * (WiFi.RSSI() + 100);
  if (signal > 100)
    return 100;
  else
    return signal;
}

// -----------------------------------------------------------------------------
// ---[Start WiFi]--------------------------------------------------------------
// -----------------------------------------------------------------------------
void SetupWifi()
{
  Serial.println("# WIFI: configuring...");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.config(ip, dns, gateway, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("# WIFI: connection restored to SSiD: " + WiFi.SSID());

  int maxWait = 5000000;
  while (WiFi.status() != WL_CONNECTED)
  {
    //Serial.println("# WIFI: check SSiD: " + String(WIFI_SSID));
    //WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(1);

    if (maxWait <= 0)
      ESP.restart();
    maxWait--;
  }
  Serial.println("# WIFI: connected!");
  Serial.println("# WIFI: signal quality: " + String(GetWifiSignalQuality()) + "%");
  Serial.print("# WIFI: IP Address: ");
  Serial.println(WiFi.localIP());
}
// ---[HomeAssistant-Setup]--------------------------------------------------------------
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

  String link_quality_conf = String() + "{\"~\":\"" + MQTT_ROOT_TOPIC + "\"," +
                             +"\"name\":\"Eqiva Bluetooth Smart Lock Linkquality\"," +
                             +"\"device\":{\"identifiers\":[\"keyble_" + ADDRESS + "\"]," +
                             +"\"manufacturer\":\"eQ-3\",\"model\":\"Key-BLE\",\"name\":\"Eqiva Bluetooth Smart Lock\"}," +
                             +"\"uniq_id\":\"keyble_" + ADDRESS + "_linkquality\"," +
                             +"\"stat_t\":\"~" + MQTT_PUB_RSSI + "\"," +
                             +"\"avty_t\":\"~" + MQTT_PUB_AVAILABILITY + "\"," +
                             +"\"icon\":\"mdi:signal\"," +
                             +"\"unit_of_meas\":\"rssi\"}";

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
// ---[Setup]-------------------------------------------------------------------
// -----------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  Serial.println("Start...");
  //Serial.setDebugOutput(true);

  SetupWifi();

  //Bluetooth
  NimBLEDevice::init("esp32ble");
  keyble = new eQ3(ADDRESS, USER_KEY, USER_ID);

  //MQTT
  SetupMqtt();
  SetupHomeAssistant();
  //delay(500);
  do_status = true;
}

// -----------------------------------------------------------------------------
// ---[loop]--------------------------------------------------------------------
// -----------------------------------------------------------------------------
void loop()
{
  // Wifi reconnect
  if (wifiActive)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("# WiFi disconnected, reconnect...");
      SetupWifi();
    }
    else
    {
      // MQTT connected?
      if (!mqttClient.connected())
      {
        if (WiFi.status() == WL_CONNECTED)
        {
          Serial.println("# MQTT disconnected, reconnect...");
          SetupMqtt();
        }
      }
      else if (mqttClient.connected())
        mqttClient.loop();
    }

    if (statusUpdated && mqttClient.connected())
    {
      // delay(100);
      //MQTT_PUB_STATE status
      batteryLow = (keyble->_BatteryStatus);
      status = keyble->_LockStatus;
      String str_status = "unknown";
      char charBufferStatus[10];

      if (status == LockStatus::UNLOCKED || status == LockStatus::OPENED)
        str_status = "UNLOCKED";
      else if (status == LockStatus::LOCKED)
        str_status = "LOCKED";

      String strBuffer = str_status;
      strBuffer.toCharArray(charBufferStatus, 10);
      mqttClient.publish(mqtt_pub_state_topic, charBufferStatus, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_state_topic);
      Serial.print("/");
      Serial.println(charBufferStatus);
      mqtt_pub_state_value = charBufferStatus;
      // delay(100);

      //MQTT_PUB_LOCK_STATE lock status
      String str_lock_status = "";
      char charBufferLockStatus[9];

      if (status == LockStatus::MOVING)
        str_lock_status = "moving";
      else if (status == LockStatus::UNLOCKED)
        str_lock_status = "unlocked";
      else if (status == LockStatus::LOCKED)
        str_lock_status = "locked";
      else if (status == LockStatus::OPENED)
        str_lock_status = "opened";
      else if (status == LockStatus::UNKNOWN)
        str_lock_status = "unknown";

      String strBufferLockStatus = String(str_status);
      strBufferLockStatus.toCharArray(charBufferLockStatus, 9);
      mqttClient.publish(mqtt_pub_lock_topic, charBufferLockStatus, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_lock_topic);
      Serial.print("/");
      Serial.println(charBufferLockStatus);
      mqtt_pub_lock_state_value = charBufferLockStatus;
      // delay(100);

      //MQTT_PUB_AVAILABILITY availability
      String str_availability = (status > LockStatus::UNKNOWN) ? "online" : "offline";
      char charBufferAvailability[8];
      str_availability.toCharArray(charBufferAvailability, 8);
      mqttClient.publish(mqtt_pub_availability_topic, charBufferAvailability, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_availability_topic);
      Serial.print("/");
      Serial.println(charBufferAvailability);
      mqtt_pub_availability_value = charBufferAvailability;
      // delay(100);

      //MQTT_PUB_BATT battery
      String str_batt = batteryLow ? "true" : "false";
      char charBufferBatt[6];
      str_batt.toCharArray(charBufferBatt, 6);
      mqttClient.publish(mqtt_pub_battery_topic, charBufferBatt, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_battery_topic);
      Serial.print("/");
      Serial.println(charBufferBatt);
      mqtt_pub_battery_value = charBufferBatt;
      // delay(100);

      //MQTT_PUB_RSSI rssi
      keybleRssi = keyble->_RSSI;
      char charBufferRssi[4];
      keybleRssi.toCharArray(charBufferRssi, 4);
      mqttClient.publish(mqtt_pub_rssi_topic, charBufferRssi, false);
      Serial.print("# published ");
      Serial.print(mqtt_pub_rssi_topic);
      Serial.print("/");
      Serial.println(charBufferRssi);
      mqtt_pub_rssi_value = charBufferRssi;
      // delay(100);

      statusUpdated = false;
    }
  }
  if (do_open || do_lock || do_unlock || do_status || do_toggle || do_pair)
  {
    // delay(200);
    SetWifi(false);
    // yield();
    waitForAnswer = true;
    keyble->_LockStatus = -1;
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
      keyble->bleClient->disconnect();
      while (keyble->state.connectionState != DISCONNECTED && !timeout)
      {
        delay(500);
      }
    }
    else if (timeout)
    {
      Serial.println("# Lock timed out!");
    }

    if (finished || timeout)
    {
      // delay(200);
      // yield();
      SetWifi(true);

      statusUpdated = true;
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
  //keyble->onTick();
}
