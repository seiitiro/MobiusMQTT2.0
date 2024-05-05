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

#include <esp_log.h>
#include <ESP32_MobiusBLE.h>
#include "ArduinoSerialDeviceEventListener.h"
#include "EspMQTTClient.h"
#include <string>
#include "secrets.h"
#include <ArduinoJson.h>
// Configuration for wifi and mqtt
EspMQTTClient client(
  mySSID,                // Your Wifi SSID
  myPassword,            // Your WiFi key
  mqttServer,            // MQTT Broker server ip
  mqttUser,              // mqtt username Can be omitted if not needed
  mqttPass,              // mqtt pass Can be omitted if not needed
  "Mobius",              // Client name that uniquely identify your device
  1883                   // MQTT Broker server port
);

// Lets try to create a 2d array to store scenes against a device.
// Need to make this persistent. Maybe EEPROM
// For now limit to 19 scenes
/*
const char* deviceArray[20][20] = \
{{"serialNumber1", "No Scene: 0", "Feed Mode: 1", "Battery Backup: 2", "All Off: 3", "Colour Cycle: 4", "Disco :5", "Thunderstorm: 6", "Cloud Cover: 7", "All On: 8", "All 50%: 9", "65534", "5435", "5434", "5455"},\
{"3N7K0014CDRWF8", "0", "1", "2", "3", "4", "5433", "5435", "5434", "5455"},\
{"7Y75008F44RBD6", "0", "1", "65534"},\
{"495C0042B6RBB7", "0", "1"}};
*/
//const char* deviceArray[20][20] = {};
const char* deviceArray[20] = {};
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

// Json mqtt template for home assistant auto discovery of select scene widget 
char *jsonDiscoveryDeviceSelect = "{\"name\": \"Set Scene\",\
    \"unique_id\": \"%s_select\",\
    \"command_topic\": \"homeassistant/select/mobius/scene/%s\",\
    \"force_update\": \"true\",\
    \"options\": [\"0\", \"1\", \"2\", \"3\", \"4\", \"5\", \"6\", \"7\", \"8\", \"9\"],\
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

char* jsonTextDiscovery =  "{\
    \"name\":\"Scenes JSON\",\
    \"command_topic\":\"homeassistant/text/mobiusBridge/set\",\
    \"state_topic\":\"homeassistant/text/mobiusBridge/state\",\
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

bool SceneDiscFlag = false;
bool prevSceneDiscFlag = false;
// Define a device buffer to hold found Mobius devices
MobiusDevice deviceBuffer[30];
//output variable to serialize the json
char jsonOutput[1024];
JsonDocument mainJsonDoc;

// wifi and mqtt connection established
void onConnectionEstablished()
{
  Serial.println("Connected to MQTT Broker :)");

  // Set keepalive (default is 15sec)
  client.setKeepAlive(120);

  // Increase default message limit
  //client.setMaxPacketSize(10000);

  // Set mqtt to be persistent
  client.enableMQTTPersistence();

  // Listen to wildcard topic for scene changes to HA
  client.subscribe("homeassistant/select/mobius/scene/#", onMessageReceived);

  client.subscribe("homeassistant/switch/mobiusBridge/set", [](const String& sceneDiscovery) {
    if (sceneDiscovery.length() > 0) {
      if (sceneDiscovery == "ON") {
        SceneDiscFlag = true;
        Serial.printf("INFO: Start Scene Discovery");
        unsigned long startMillis = millis();
        while (1000 > (millis() - startMillis)) {}

        client.publish("homeassistant/switch/mobiusBridge/state", "ON");
      } else {
        SceneDiscFlag = false;

        //checking the previous state
        prevSceneDiscFlag = true;
        Serial.printf("INFO: Disable Scene Discovery");
        unsigned long startMillis = millis();
        while (1000 > (millis() - startMillis)) {}

        client.publish("homeassistant/switch/mobiusBridge/state", "OFF");
      }
    }
  });

  //set the stored scenes JSON from HA
  client.subscribe("homeassistant/text/mobiusBridge/set", [](const String& scenesJSON) {
    if (scenesJSON.length() > 0) {
      Serial.printf("INFO: Set Scenes JSON");

      strcpy(jsonOutput, scenesJSON.c_str());

      DeserializationError error = deserializeJson(mainJsonDoc, jsonOutput);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
      }

      unsigned long startMillis = millis();
      while (1000 > (millis() - startMillis)) {}

    }
  });



}

// Store device scenes in an array, returns the available scenes for that device
/*const char* getDeviceScenes(const char* deviceSerial, const char* deviceScene){
  //char returnDeviceScenes[50]= {defaultScenes};
  //return returnDeviceScenes;
  return defaultScenes;
}
*/

// Set scene from Home Assistant select scene 
// Comes from mqtt subscribe to /homeassistant/select/mobius/scene/<device>
// The use of String seems to be corrupting memory
void onMessageReceived(const String& topic, const String& message) {
  // Get serial number from the end of topic
  // The use of String seems to be corrupting memory
  //String serialNumberGiven = topic.substring(34);
  //*/

//void onMessageReceived(const char& topic, const char& message) {
    
  std::string topicGiven = topic.c_str();
  std::string serialNumberGiven = topicGiven.substr(34);

  Serial.printf("INFO***: Serial Number: %s\n", serialNumberGiven.c_str());
  Serial.printf("INFO***: Set Scene is: %s\n", message);

  // Loop through devices till we match the serial number
  MobiusDevice device = deviceBuffer[0];
  int discovered = 0;
  while (discovered == 0){
    int count = 0;
    int scanDuration = 2; // in seconds
    while (!count) {
      count = MobiusDevice::scanForMobiusDevices(scanDuration, deviceBuffer);
    }
    Serial.printf("INFO*** Device count: %i\n", count);
    for (int i = 0; i < count; i++) {
      device = deviceBuffer[i];
      Serial.printf("INFO****: Check device number: %i\n", i);
      // Get manufacturer info
      std::string manuData = device._device->getManufacturerData();

      // Don't connect unless we have a serial number
      if (manuData.length() > 1){
        // Connect, get serialNumber and current scene          
        Serial.printf("INFO****: Found manuData: %s\n", manuData.c_str());
        // serialNumber is from byte 11
        std::string serialNumberString = manuData.substr(11, manuData.length());
        char serialNumber[serialNumberString.length() + 1] = {};
        strcpy(serialNumber, serialNumberString.c_str());
        Serial.printf("INFO*****: Device serial number: %s\n", serialNumber);
        Serial.printf("INFO*****: Serial number given: %s\n", serialNumberGiven.c_str());
        if (strcmp(serialNumber, serialNumberGiven.c_str()) == 0){
          Serial.printf("INFO****: MATCH!!\n");

          // Connect device
          if (!device.connect()){
            Serial.println("ERROR****: Failed to connect to device");
          }
          else {
            Serial.println("INFO****: Connected to device");
            // Set scene
            if (!device.setScene(message.toInt())){
              Serial.println("ERROR****: Failed to set device scene");
            }
            else {
              Serial.println("INFO****: Successfully set device scene");
            }
          }

          // Disconnect
          device.disconnect();

          // Mark done
          discovered = 1;
        }
      }
    }
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
  while(!client.isConnected()){client.loop();};

  // Optional functionality for EspMQTTClient
  //client.setOnConnectionEstablishedCallback(onConnectionEstablished);
  client.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  //client.enableHTTPWebUpdater(); // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overridded with enableHTTPWebUpdater("user", "password").
  //client.enableOTA(); // Enable OTA (Over The Air) updates. Password defaults to MQTTPassword. Port is the default OTA port. Can be overridden with enableOTA("password", port).
  client.enableLastWillMessage("homeassistant/mobius/lastwill", "I am going offline");  // You can activate the retain flag by setting the third parameter to true
  
  // Increase default packet size for HA mqtt json messages
  client.setMaxPacketSize(10000);

  // Initialize the library with a useful event listener
  MobiusDevice::init(new ArduinoSerialDeviceEventListener());

}

bool runBasicConfig = true;
/*!
 * Main Loop method
 */
void loop() {


  // Wait for mqtt and wifi connection
  //while(!client.isConnected()){client.loop();};

  // Loop mqtt
  client.loop();

  // Get number of mobius devices
  MobiusDevice device = deviceBuffer[0];
  int count = 0;
  //int scanDuration = 10; // in seconds
  int scanDuration = 2; // in seconds
  while (!count) {
    count = MobiusDevice::scanForMobiusDevices(scanDuration, deviceBuffer);
  }

  // Loop through each device found, autoconfigure home assistant with the device, and update the current scene of the device
  for (int i = 0; i < count; i++) {
    device = deviceBuffer[i];

    // Get manufacturer info
    std::string manuData = device._device->getManufacturerData();

    // Don't connect unless we have a serial number
    if (manuData.length() > 1){
      // Connect, get serialNumber and current scene
      Serial.printf("\nINFO: Connect to device number: %i\n", i);
      if(device.connect()) {
        //Serial.printf("INFO: Connected to: %s\n", device._device->toString().c_str());
      
        // Get the devices mac address. Note that this changes every reboot so likely not useful
        std::string addressString = device._device->getAddress().toString();
        char deviceAddress[addressString.length() + 1] = {};
        strcpy(deviceAddress, addressString.c_str());
        Serial.printf("INFO: Device mac address is: %s\n", deviceAddress);


        const char* modelNumber = device.getModelNum().c_str();
        if (modelNumber == NULL){
          modelNumber = "Unknown";
        }
        const char* fwRev = device.getFWRev().c_str();
        if (fwRev == NULL){
          fwRev = "Unknown";
        }            
        const char* Manufa = device.getManufName().c_str();
        if (Manufa == NULL){
          Manufa = "Unknown";
        }
        const char* serialNumber = device.getSerialNumber().c_str();

        // Home Assistant autodiscovery
        // Substitute serialNumber into jsonDiscoveryDevice
        // Substitutions in order: name, unique_id, "icon", discovery_component (e.g. sensor ), sensor_topic (e.g. scene), serialNumber, "sensorType"
        char json[512];
        sprintf(json, jsonDiscoveryDevice, "Current Scene", serialNumber, "mdi:pump", "sensor", serialNumber, "scene", deviceAddress, serialNumber, modelNumber, Manufa, serialNumber, fwRev);
        Serial.println(json);
        //sprintf(json, jsonDiscoveryDevice, serialNumber, serialNumber, serialNumber, deviceAddress, serialNumber);
        Serial.printf("INFO: Device discovery message:%s\n", json);
        char deviceDiscoveryTopic[100];
        sprintf(deviceDiscoveryTopic, "homeassistant/sensor/mobius/%s/config", serialNumber);
        Serial.printf("INFO: Device Discovery Topic: %s\n", deviceDiscoveryTopic);
        //client.publish(deviceDiscoveryTopic, json);

        char deviceTopic[100];
        sprintf(deviceTopic, "homeassistant/sensor/mobius/%s/scene/state", serialNumber);

        // Create scene select input
        char jsonSelect[512];
        //sprintf(jsonSelect, jsonDiscoveryDeviceSelect, serialNumber, serialNumber, deviceAddress, serialNumber);
        sprintf(jsonSelect, jsonDiscoveryDeviceSelect, serialNumber, serialNumber, deviceAddress, serialNumber, modelNumber, Manufa, serialNumber, fwRev);
        Serial.printf("INFO: Device select discovery message:%s\n", jsonSelect);
        char deviceSelectDiscoveryTopic[100];
        
        sprintf(deviceSelectDiscoveryTopic, "homeassistant/select/mobius/%s/config", serialNumber);
        
        Serial.printf("INFO: Device Select Discovery Topic: %s\n", deviceSelectDiscoveryTopic);
        Serial.printf("INFO: SERIAL NUMBER BEFORE publish %s\n", serialNumber);
        //client.publish(deviceSelectDiscoveryTopic, jsonSelect);
        Serial.printf("INFO: SERIAL NUMBER AFTER %s\n", serialNumber);

        // Get current scene
        //char deviceTopic[400];
        //sprintf(deviceTopic, "homeassistant/sensor/mobius/%s/scene/state", serialNumber);
        Serial.printf("INFO: Device Topic: %s\n", deviceTopic);
        uint16_t sceneId = device.getCurrentScene();
        char sceneString[8];
        dtostrf(sceneId, 2, 0, sceneString);
        Serial.printf("INFO: Current scene string:%s\n", sceneString);
        //client.publish(deviceTopic, sceneString);

        if (SceneDiscFlag) {
          Serial.println("****************************************");
          Serial.println("Print Json Document before processing Serial#/Scene");
          mainJsonDoc.shrinkToFit();  // optional
          serializeJson(mainJsonDoc, jsonOutput);
          Serial.println(jsonOutput);

          //search for the serial in the jsondocument
          bool hasSerial = mainJsonDoc.containsKey(serialNumber);

          if (hasSerial) {
            //If serial exists, check for scene number
            Serial.println("Serial Exists, moving to check scene");

            Serial.println("Existing scenes for this Serial");

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
        } else {
          if (prevSceneDiscFlag == true){
            //If just finished discovery, send JSON to HA and save to flash          
            prevSceneDiscFlag = false;

            char jsonOutputHA[1024];
            serializeJsonPretty(mainJsonDoc, jsonOutputHA);

            client.publish("homeassistant/text/mobiusBridge/state", jsonOutputHA);
          }
        }

/*
        // Update device register with device info and available scenes
        // Store serialNumber, macAddress, and each discovered scene
        // We can then use the scenes to update the mqtt ha autodiscovery scene list
        // Or maybe we can do this from HA using a script / automation.
        // See if we have the device in the deviceArray
        int numRows = sizeof(deviceArray)/sizeof(deviceArray[0]);
        //int numCols = sizeof(deviceArray[0])/sizeof(deviceArray[0][0]);
        Serial.print("Number of rows = ");Serial.println(numRows);
        //Serial.print("Number of cols = ");Serial.println(numCols);
        int foundMatch = 0;
        int deviceArrayCount = 0;
        for (int i=0; i < numRows; i++){
          Serial.printf("Looking at row %i ", i);
          //Serial.printf("for serial number %s \n", serialNumber2.c_str());
          Serial.printf("for serial number %s \n", serialNumber);
          //if (deviceArray[i][0] != NULL){
          if (deviceArray[i] != NULL){
            //Serial.printf("Row value is %s ", deviceArray[i][0]);
            Serial.printf("Row value is %s ", deviceArray[i]);
            //if (strcmp(serialNumber2.c_str(), deviceArray[i][0]) == 0){
            //if (strcmp(serialNumber2.c_str(), deviceArray[i]) == 0){
            if (strcmp(serialNumber, deviceArray[i]) == 0){
              Serial.println("MATCHHHHH");
              foundMatch = 1;
              // Update scenes
              break;
            }
          }
          else {
            deviceArrayCount = i;
            break;
          }
          deviceArrayCount = i;
        }
        // Add device to deviceArray if we haven't seen it before
        if (foundMatch == 0){
          //memcpy(&deviceArray[deviceArrayCount+1][0], serialNumber2.c_str(), strlen(serialNumber2.c_str()) + 1);
          //memcpy(&deviceArray[deviceArrayCount+1][0], serialNumber, strlen(serialNumber) + 1);
          //deviceArray[deviceArrayCount][0] = serialNumber;
          //deviceArray[deviceArrayCount][0] = serialNumber2.c_str();
          //deviceArray[deviceArrayCount] = serialNumber2.c_str();
          deviceArray[deviceArrayCount] = serialNumber;
          //Serial.printf("Adding serial number %s ", serialNumber2.c_str());
          Serial.printf("Adding serial number %s ", serialNumber);
          Serial.printf("to row %i\n", deviceArrayCount);
          //Serial.printf("DEVICE ADDED: %s \n", deviceArray[deviceArrayCount][0]);
          Serial.printf("DEVICE ADDED: %s \n", deviceArray[deviceArrayCount]);
        }
*/      
        // Debug - print out the entire deviceArray
        //for (int i=0; i < numRows ;i++){
        //  if (deviceArray[i][0] != NULL){
        //    Serial.printf("Row %i:", i);
        //    Serial.printf(deviceArray[i][0]);
        //    Serial.println("");
        //}

        // Do the publish last and delay execution 2 seconds
        client.publish(deviceDiscoveryTopic, json);
        client.publish(deviceSelectDiscoveryTopic, jsonSelect);
        client.publish(deviceTopic, sceneString);

        //Publish basic MQTT controls once after boot
        if(runBasicConfig) {
          Serial.println(jsonSwitchDiscovery);
          client.publish("homeassistant/switch/mobiusBridge/config", jsonSwitchDiscovery);
          client.publish("homeassistant/text/mobiusBridge/config", jsonTextDiscovery);

          // delaying without sleeping
          unsigned long startMillis = millis();
          while (1000 > (millis() - startMillis)) {}

          client.publish("homeassistant/switch/mobiusBridge/state", "OFF");
          client.publish("homeassistant/text/mobiusBridge/state", "");

          runBasicConfig = false;
        }

        //client.executeDelayed(2 * 1000, []() {
        //client.executeDelayed(2 * 1000, client.publish(deviceSelectDiscoveryTopic, jsonSelect));
        //  client.publish(deviceSelectDiscoveryTopic, jsonSelect);
        //  });
        //client.executeDelayed(2 * 1000, client.publish(deviceTopic, sceneString));
        //client.executeDelayed(2 * 1000, []() {
        //  client.publish(deviceTopic, sceneString);
        //});
        
        // Disconnect
        device.disconnect();

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
  }
  Serial.println("================================================================================");
  Serial.println("Print full Json Document, after processing all devices");
  if (!mainJsonDoc.isNull()){
    mainJsonDoc.shrinkToFit();  // optional
    serializeJson(mainJsonDoc, jsonOutput);
  }
  Serial.println(jsonOutput);  
}
