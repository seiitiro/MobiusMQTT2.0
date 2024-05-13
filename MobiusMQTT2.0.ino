/*!
 * Discover and Control Mobius Devices
 *
 * Establishes a wifi and mqtt connection. 
 * Scans for Mobius devices and updates mqtt with a json for Home Assistant auto discovery.
 * The current scene for each device is found and an update sent to mqtt. 
 * Subscribes to a set scene mqtt topic that is set by Home Assistant.
 * Triggers a Mobius scene change on change in Home Assistant select scene control.
 * 
 * TODO:
 * + Doesn't always trigger loop() and have to press EN button on esp32 to start
 * + Seeme to disconnect from wifi after a fwe hours - memory leak?
 * 
 * This example code is released into the public domain.
 */

// Uncoment below to set scene auto discover to ON on boot
//#define DISCOVER_ON             

#include <esp_log.h>
#include <ESP32_MobiusBLE.h>
#include "ArduinoSerialDeviceEventListener.h"
#include "EspMQTTClient.h"
#include <string>
#include "secrets.h"
#include <ArduinoJson.h>
#include <AntiDelay.h>
#include "MobiusSerialDecoder.h"

AntiDelay scanMobius(0);          // Create an AntiDelay object with initial 0 millis (run on first execution)
MobiusDevice deviceBuffer[30];    // Define a device buffer to hold found Mobius devices
JsonDocument mainJsonDoc;         // JSON Document for the main Serial#, Scenes
JsonDocument deviceSelectDoc;     // JSON Document to build the select

float minutes = 0;                // set minutes to 0 for continuous scan
char jsonOutputFlash[1024];
//output variable to serialize the json
char jsonOutput[1024];


#if defined(DISCOVER_ON)
  char discoverSwitch[4] = "ON";   
  bool SceneDiscFlag = true;
#else
  char discoverSwitch[4] = "OFF";   // Set default state for the scene discovery switch
  bool SceneDiscFlag = false;
#endif



// Configuration for wifi and mqtt
EspMQTTClient mqttClient(
  mySSID,                // Your Wifi SSID
  myPassword,            // Your WiFi key
  mqttServer,            // MQTT Broker server ip
  mqttUser,              // mqtt username Can be omitted if not needed
  mqttPass,              // mqtt pass Can be omitted if not needed
  "Mobius",              // Client name that uniquely identify your device
  1883                   // MQTT Broker server port
);

const char* defaultScenes[] = { "No Scene: 0", "Feed Mode: 1", "Battery Backup: 2", "All Off: 3", "Colour Cycle: 4", "Disco :5", "Thunderstorm: 6", "Cloud Cover: 7", "All On: 8", "All 50%: 9"};

// Json mqtt template for home assistant auto discovery of mobius devices
char *jsonDiscoveryDevice = "{\"name\": \"%s\",\
  \"unique_id\": \"%s\",\
  \"icon\": \"%s\",\
  \"state_topic\": \"homeassistant/%s/mobius/%s/%s/state\",\
  \"force_update\": \"true\",\
  \"device\" : {\
  \"identifiers\" : [ \"%s\" ],\
  \"name\": \"%s\",\
  \"model\": \"%s\",\
  \"manufacturer\": \"%s\",\
  \"serial_number\": \"%s\"}\
}";

char* jsonSwitchDiscovery =  "{\
    \"name\":\"Discover Scenes\",\
    \"command_topic\":\"homeassistant/switch/mobiusBridge/set\",\
    \"state_topic\":\"homeassistant/switch/mobiusBridge/state\",\
    \"unique_id\":\"mobius01BLEBdge\",\
    \"device\":{\
      \"identifiers\":[\
        \"mobridge01ad\"\
      ],\
      \"name\":\"Mobius\",\
      \"manufacturer\": \"Team Down Under\",\
      \"model\": \"Mobius BLE Bridge\",\
      \"sw_version\": \"2024.05.03\"\
         }}";

char* jsonTextDiscovery =  "{\"name\":\"Scenes JSON\",\"command_topic\":\"homeassistant/text/mobiusBridge/set\",\"state_topic\":\"homeassistant/text/mobiusBridge/state\",\"unique_id\":\"mobius01BLEBdge\",\
\"device\":{\"identifiers\":[\"mobridge01ad\"],\"name\":\"Mobius\",\"manufacturer\": \"Team Down Under\",\"model\": \"Mobius BLE Bridge\",\"sw_version\": \"2024.05.03\"}}";

// wifi and mqtt connection established
void onConnectionEstablished()
{
  // Set keepalive (default is 15sec)
  mqttClient.setKeepAlive(120);

  // Set mqtt to be persistent
  mqttClient.enableMQTTPersistence();

  // Listen to wildcard topic for scene changes to HA
  mqttClient.subscribe("homeassistant/select/mobius/scene/#", mobiusSetScene);

  mqttClient.subscribe("homeassistant/switch/mobiusBridge/set", mobiusSceneSwitch);

  //set the stored scenes JSON from HA
  mqttClient.subscribe("homeassistant/text/mobiusBridge/set", onJsonStoreSet);

}

void mobiusSetScene(const String& topic, const String& message) {
  //Implement the set scene here
  Serial.println("************************************************************");
  Serial.print("Implement the set scene here for topic:");
  Serial.println(topic);
  Serial.println("------------------------------------------------------------");
  Serial.print("Scene: ");
  Serial.println(message);

  //for some reason topic.substring(34, 48).c_str() do not work in a single step but works if splitted
  String sub_S = topic.substring(34, 48);
  const char* serialNumber1 = sub_S.c_str();
  
  Serial.print("Serial: ");
  Serial.println(serialNumber1);

  Serial.println("************************************************************");
}

void mobiusSceneSwitch(const String& sceneDiscovery) {
  if (sceneDiscovery.length() > 0) {
    if (sceneDiscovery == "ON") {
      SceneDiscFlag = true;

/*
      unsigned long startMillis = millis();
      while (1000 > (millis() - startMillis)) {}
*/
      waitFunction(1000);

      mqttClient.publish("homeassistant/switch/mobiusBridge/state", "ON", true);
    } else {
      SceneDiscFlag = false;

      waitFunction(1000);
/*
      unsigned long startMillis = millis();
      while (1000 > (millis() - startMillis)) {}
*/
      mqttClient.publish("homeassistant/switch/mobiusBridge/state", "OFF", true);
    }
    //reset timer
    scanMobius.setInterval(0);
    scanMobius.reset();
  }
};

//To update the Scenes JSON with the one sent from HA
void onJsonStoreSet(const String& scenesJSON){
  if (scenesJSON.length() > 0) {
    strcpy(jsonOutput, scenesJSON.c_str());

    DeserializationError error = deserializeJson(mainJsonDoc, jsonOutput);

    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
    }

    waitFunction(1000);
/*
    unsigned long startMillis = millis();
    while (1000 > (millis() - startMillis)) {}
*/
    //Publish it back with retain
    char jsonOutputHA[1024];
    serializeJson(mainJsonDoc, jsonOutputHA);

    mqttClient.publish("homeassistant/text/mobiusBridge/state", jsonOutputHA, true);    
  }
}

/*!
 * Main Setup method
 */
void setup() {
  // Connect the serial port for logs
  Serial.begin(115200);
  while (!Serial);

  // Connect to wifi and mqtt server
  while(!mqttClient.isConnected()){mqttClient.loop();};

  mqttClient.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  mqttClient.enableLastWillMessage("homeassistant/mobius/lastwill", "I am going offline");  // You can activate the retain flag by setting the third parameter to true
  
  // Increase default packet size for HA mqtt json messages
  mqttClient.setMaxPacketSize(10000);

  // Initialize the library with a useful event listener
  MobiusDevice::init(new ArduinoSerialDeviceEventListener());

//Publish basic MQTT controls once after boot
  mqttClient.publish("homeassistant/switch/mobiusBridge/config", jsonSwitchDiscovery);

  // delaying without sleeping
  waitFunction(1000);
/*
  unsigned long startMillis = millis();
  while (1000 > (millis() - startMillis)) {}
*/
  mqttClient.publish("homeassistant/text/mobiusBridge/config", jsonTextDiscovery);

  // delaying without sleeping
  waitFunction(1000);
  /*
  startMillis = millis();
  while (1000 > (millis() - startMillis)) {}
*/
  mqttClient.publish("homeassistant/switch/mobiusBridge/state", discoverSwitch);

  char jsonOutputHA[1024];
  if (!mainJsonDoc.isNull())
    serializeJson(mainJsonDoc, jsonOutputHA);

  // delaying without sleeping
  waitFunction(1000);
/*
  startMillis = millis();
  while (1000 > (millis() - startMillis)) {}
*/  
  if (sizeof(jsonOutputHA) > 0)
    mqttClient.publish("homeassistant/text/mobiusBridge/state", jsonOutputHA, true);
  else
    mqttClient.publish("homeassistant/text/mobiusBridge/state", "");

}

/*!
 * Main Loop method
 */
void loop() {
  // Loop mqtt
  mqttClient.loop();

	if (scanMobius) {
    //Run Mobius routine every x minutes defined below in [float minutes = ]
    if (!scanMobius.isRunning()){
      scanMobius.setInterval(minutes*60000);
    }

    // Get number of mobius devices
    MobiusDevice mobiusBLEdevice = deviceBuffer[0];
    int count = 0;

    int scanDuration = 2; // in seconds
    while (!count) {
      count = MobiusDevice::scanForMobiusDevices(scanDuration, deviceBuffer);
    }

    // Loop through each device found, autoconfigure home assistant with the device, and update the current scene of the device
    for (int i = 0; i < count; i++) {
      mobiusBLEdevice = deviceBuffer[i];

      // Get manufacturer info
      std::string manuData = mobiusBLEdevice._device->getManufacturerData();

      // Connect, get serialNumber and current scene
//      Serial.printf("\nINFO: Connect to device number: %i\n", i);
      if(mobiusBLEdevice.connect()) {
        //Serial.printf("INFO: Connected to: %s\n", mobiusBLEdevice._device->toString().c_str());
      
        // Get the devices mac address. Note that this changes every reboot so likely not useful
        std::string addressString = mobiusBLEdevice._device->getAddress().toString();
        char deviceAddress[addressString.length() + 1] = {};
        strcpy(deviceAddress, addressString.c_str());
        Serial.printf("INFO: Device mac address is: %s\n", deviceAddress);

        const char* fwRev = mobiusBLEdevice.getFWRev().c_str();
        const char* Manufa = mobiusBLEdevice.getManufName().c_str();
        const char* serialNumber = mobiusBLEdevice.getSerialNumber().c_str();
        const char* modelName = getModelName(mobiusBLEdevice.getSerialNumber());
        
        // Home Assistant autodiscovery
        // Substitute serialNumber into jsonDiscoveryDevice
        // Substitutions in order: name, unique_id, "icon", discovery_component (e.g. sensor ), sensor_topic (e.g. scene), serialNumber, "sensorType"
        char json[512];
        sprintf(json, jsonDiscoveryDevice, "Current Scene", serialNumber, "mdi:pump", "sensor", serialNumber, "scene", serialNumber, serialNumber, modelName, Manufa, serialNumber, fwRev);
//        Serial.println(json);
        //sprintf(json, jsonDiscoveryDevice, serialNumber, serialNumber, serialNumber, deviceAddress, serialNumber);
//        Serial.printf("INFO: Device discovery message:%s\n", json);
        char deviceDiscoveryTopic[100];
        sprintf(deviceDiscoveryTopic, "homeassistant/sensor/mobius/%s/config", serialNumber);
//        Serial.printf("INFO: Device Discovery Topic: %s\n", deviceDiscoveryTopic);
        //mqttClient.publish(deviceDiscoveryTopic, json);

        char deviceTopic[100];
        sprintf(deviceTopic, "homeassistant/sensor/mobius/%s/scene/state", serialNumber);

        // Create scene select input
        deviceSelectDoc["name"] = "Set Scene";

        char uniqueID[25];
        strcpy(uniqueID, serialNumber);
        strcat(uniqueID, "_select");

        deviceSelectDoc["unique_id"] = uniqueID;

        char cmdTopic[50];
        strcpy(cmdTopic, "homeassistant/select/mobius/scene/");
        strcat(cmdTopic, serialNumber);

        deviceSelectDoc["command_topic"] = cmdTopic;

        deviceSelectDoc["force_update"] = "true";

        JsonArray devOptions = deviceSelectDoc["options"].to<JsonArray>();
        if (!mainJsonDoc.isNull()){
          //if Scenes JSON is not empty, check if serial exists
          if (mainJsonDoc.containsKey(serialNumber)) {
            //If serial exists, get the scenes array
            Serial.println("Serial Exists, moving to check scene");

            Serial.println("Existing scenes for this Serial");

            //create an array with all scenes for the Serial
            JsonArray scenesJson = mainJsonDoc[serialNumber].as<JsonArray>();

            for (JsonVariant value : scenesJson) {
              //loop all scenes already in json and look for current scene
              char intToChar[6];
              itoa(value.as<int>(), intToChar, 10);   // value.as<const char*>() wasn't working properly so changed to itoa
              devOptions.add(intToChar);
            }
          }
        }

        if (devOptions.size() == 0) {
          //if Scenes JSON has no scenes for this serial, add sample scenes
          for (int i = 1; i <= 9; i++) {
            char intToChar[6];
            itoa(i, intToChar, 10);
            devOptions.add(intToChar);
          }          
        }

        JsonObject deviceJson = deviceSelectDoc["device"].to<JsonObject>();
        deviceJson["identifiers"][0] = serialNumber;
        deviceJson["name"] = serialNumber;

        char jsonSelect[512];

        deviceSelectDoc.shrinkToFit();  // optional

        serializeJson(deviceSelectDoc, jsonSelect);

//        Serial.printf("INFO: Device select discovery message:%s\n", jsonSelect);
        char deviceSelectDiscoveryTopic[100];
        
        sprintf(deviceSelectDiscoveryTopic, "homeassistant/select/mobius/%s/config", serialNumber);
        
        // Get current scene
//        Serial.printf("INFO: Device Topic: %s\n", deviceTopic);
        uint16_t sceneId = mobiusBLEdevice.getCurrentScene();
        char sceneString[8];
        dtostrf(sceneId, 2, 0, sceneString);
//        Serial.printf("INFO: Current scene string:%s\n", sceneString);

        if (SceneDiscFlag) {
//          Serial.println("****************************************");
//          Serial.println("Print Json Document before processing Serial#/Scene");
          mainJsonDoc.shrinkToFit();  // optional
          serializeJson(mainJsonDoc, jsonOutput);
//          Serial.println(jsonOutput);

          //search for the serial in the jsondocument
          bool hasSerial = mainJsonDoc.containsKey(serialNumber);

          if (hasSerial) {
            //If serial exists, check for scene number
//            Serial.println("Serial Exists, moving to check scene");

//            Serial.println("Existing scenes for this Serial");

            //create an array with all scenes for the Serial
            JsonArray scenesJson = mainJsonDoc[serialNumber].as<JsonArray>();
            bool hasScene = false;
            for (JsonVariant value : scenesJson) {
              //loop all scenes already in json and look for current scene
              Serial.println(value.as<int>());
              if (value.as<int>() == sceneId) {
                Serial.println("Scene Exists, exiting the loop");
                //Scene exists, set hasScene to true and break from loop
                hasScene = true;
                break;
              }
            }
            if (!hasScene) {
              Serial.println("Scene does NOT Exist, adding scene");
              //If current scene not in json document for the serial, add scene 
              scenesJson.add(sceneId);
            }
          }
          else {
            Serial.println("Serial Does NOT Exist, adding Serial and scene");

            //if serial does not exist, add serial and an empty scene array
            JsonArray newSceneJson = mainJsonDoc[serialNumber].to<JsonArray>();
            //Then add scene to the array
            newSceneJson.add(sceneId);
            //newSceneJson.add(1974);
            Serial.println("Serial and scene added..");
          }            

          //Process below to serialize and deserialize every each Mobius device to ensure the jsonDoc has all the items (Serial and its scenes)
          mainJsonDoc.shrinkToFit();  // optional
          serializeJson(mainJsonDoc, jsonOutput);
          DeserializationError error = deserializeJson(mainJsonDoc, jsonOutput);

          if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
          }

          /****************************************************************************************************
           *  TO DO: 
           *
           *      Validate the json output with flash and only publish to MQTT Broker if different
           *
           *      jsonOutputFlash is representing the data stored inESP32 flash memory
           *
           *
           ****************************************************************************************************/
          if (jsonOutputFlash != jsonOutput) {
            Serial.print("Updated JSON from: ");
            Serial.println(jsonOutputFlash);
            Serial.print(" to : ");
            Serial.println(jsonOutput);

            strcpy(jsonOutputFlash, jsonOutput);
            mqttClient.publish("homeassistant/text/mobiusBridge/state", jsonOutputFlash, true);
          }
        }

        // Do the publish last and delay execution 2 seconds
        mqttClient.publish(deviceDiscoveryTopic, json);
        mqttClient.publish(deviceSelectDiscoveryTopic, jsonSelect);
        mqttClient.publish(deviceTopic, sceneString);

        // Disconnect
        mobiusBLEdevice.disconnect();

        if (SceneDiscFlag) {
          Serial.println("****************************************");
          Serial.println("Print Json Document again, after processing Serial#/Scene");
          Serial.println(jsonOutput);
        }
      }
      else {
        Serial.println("ERROR: Failed to connect to device");
      }

    }
    Serial.println("================================================================================");
    Serial.println("Print full Json Document, after processing all devices");
    if (!mainJsonDoc.isNull()){
      mainJsonDoc.shrinkToFit();  // optional
      serializeJson(mainJsonDoc, jsonOutput);
    }
    Serial.println(jsonOutput);  
  }
}

void waitFunction(int intTimer){
  unsigned long startMillis = millis();
  while (intTimer > (millis() - startMillis)) {}  
}