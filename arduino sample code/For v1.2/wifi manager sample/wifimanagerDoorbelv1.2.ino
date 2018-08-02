/***************************************************************************
 Code to use with MQTT wifi doorbell, see https://www.tindie.com/products/ErikLemcke/mqtt--wifi-doorbell-with-esp8266/
 Created by Erik Lemcke 22-06-2018


 This code can be used from the arduino library, you will need the following libraries:
 https://github.com/tzapu/WiFiManager     Included in this repository (src folder)
 https://github.com/bblanchon/ArduinoJson
 https://github.com/knolleary/pubsubclient
 https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS

 credits:

 https://tzapu.com/                   for the awesome WiFiManger library
 https://github.com/esp8266/Arduino   fopr the ESP8266 arduino core
 https://arduinojson.org/             for the ArduinoJson library
 https://pubsubclient.knolleary.net/  for the mqtt library
  
 ***************************************************************************/
#include <fs.h>                   //this needs to be first, or it all crashes and burns...
#import "index.h"

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "src/WiFiManager.h"          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <PubSubClient.h>

#include <ESP8266mDNS.h>        // Include the mDNS library

int doorbellState = 0;
int resetState = 0;
const int doorbellPin = 16;

WiFiClient espClient;
PubSubClient client(espClient);

WiFiManager wifiManager;

ESP8266WebServer server(80);


//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] ;
char mqtt_username[40];
char mqtt_password[40];
char mqtt_topic[40];
char mqtt_status[60] = "unknown";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Connection to previous set wifi failed. Erasing settings before becoming AP");
  SPIFFS.format();
}

//Handle webserver root request
void handleRoot() {
  Serial.println("Handling webserver request");
  
  String configPage = config_page;

  configPage.replace("{v}", "Wifi doorbell configuration");
  configPage.replace("{1}", mqtt_server);
  configPage.replace("{2}", mqtt_port);
  configPage.replace("{3}", mqtt_username);
  configPage.replace("{4}", mqtt_password);
  configPage.replace("{5}", mqtt_topic);
  configPage.replace("{6}", mqtt_status);
  
  server.send(200, "text/html", configPage);
  
}

void saveSettings() {
  Serial.println("Handling webserver request savesettings");

  //store the updates values in the json config file
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = server.arg("mqtt_server");
    json["mqtt_port"] = server.arg("mqtt_port");
    json["mqtt_username"] = server.arg("mqtt_username");
    json["mqtt_password"] = server.arg("mqtt_password");
    json["mqtt_topic"] = server.arg("mqtt_topic");
   
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();

    //put updated parameters into memory so they become effective immediately
    server.arg("mqtt_server").toCharArray(mqtt_server,40);
    server.arg("mqtt_port").toCharArray(mqtt_port,40);
    server.arg("mqtt_username").toCharArray(mqtt_username,40);
    server.arg("mqtt_password").toCharArray(mqtt_password,40);
    server.arg("mqtt_topic").toCharArray(mqtt_topic,40);
   
    server.send(200, "text/html", "Settings have been saved. You will be redirected to the configuration page in 5 seconds <meta http-equiv=\"refresh\" content=\"5; url=/\" />");
    
    //mqtt settings might have changed, let's reconnect
    reconnect();
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  pinMode(doorbellPin, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, HIGH);

  Serial.println("before format");

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read


  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_username("username", "mqtt username", mqtt_username, 40);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 40);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 40);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  //WiFiManager wifiManager;

  WiFiManagerParameter custom_text("<p>Fill the folowing values with your home assistant infromation. Username and password are optional</p>");
  wifiManager.addParameter(&custom_text);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.setAPCallback(configModeCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_topic);

    //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("WiFi doorbell")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...!");

  //Define url's 
  server.on("/", handleRoot);
  server.on("/saveSettings", saveSettings);
  server.begin();


  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_topic"] = mqtt_topic;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  client.setServer(mqtt_server, atoi(mqtt_port));

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  //Expose as mdns
  if (!MDNS.begin("doorbell")) {  // Start the mDNS responder for doorbell.local
    Serial.println("Error setting up MDNS responder!");
  } else {
      Serial.println("mDNS responder started");
      MDNS.addService("http", "tcp", 80);
  }

}


//MQTT reconnect function
void reconnect() {
  client.disconnect();
  client.setServer(mqtt_server, atoi(mqtt_port));
  Serial.print("Attempting MQTT connection to ");
  Serial.print(mqtt_server);
  Serial.print(" on port ");
  Serial.print(mqtt_port);
  Serial.print("...");
  
  if (client.connect("ESP8266Client", mqtt_username, mqtt_password)) {
     Serial.println("connected");
     String("<div style=\"color:green;float:left\">connected</div>").toCharArray(mqtt_status,60);
     Serial.print("sending 'off' message to ");
     Serial.print(mqtt_server);
     Serial.print(" on port ");
     Serial.print(mqtt_port);
     Serial.print(" with topic ");
     Serial.println(mqtt_topic);
     client.publish(mqtt_topic, "off" , true);
   } else {
     Serial.print("failed, rc=");
     String("<div style=\"color:red;float:left\">connection failed</div>").toCharArray(mqtt_status,60);
     Serial.print(client.state());
     Serial.println(" try again in 5 seconds");

    unsigned long previousMillis1 = 0;

     //connection failed, Go into a (non-blocking) loop until we are connected 
     while (!client.connected()) {
        resetstate();
         server.handleClient();  //call to handle webserver connection needed, because the while loop will block the processor
         unsigned long currentMillis1 = millis();
         if(currentMillis1 - previousMillis1 >= 5000) {
         previousMillis1 = currentMillis1;
         Serial.print("Attempting MQTT connection...");
          if (client.connect("ESP8266Client", mqtt_username, mqtt_password)) {
                Serial.println("connected");
                String("<div style=\"color:green;float:left\">connected</div>").toCharArray(mqtt_status,60);
             } else {
                     String("<div style=\"color:red;float:left\">connection failed</div>").toCharArray(mqtt_status,60);
                Serial.print("failed, rc=");
                Serial.print(client.state());
                Serial.println(" try again in 5 seconds");
             }
         }
      }
   }
}

long lastMsg = 0;

//Initialize a reset is pin 12 is low
void resetstate (){
   resetState = digitalRead(12);
   if (resetState == LOW){
    Serial.println("It seems someone wants to go for a reset...");
    int count = 0;

    //Flash the led for 5 seconds
    while (count < 25 and resetState == LOW) {
      digitalWrite(BUILTIN_LED, LOW);   // turn the LED on (HIGH is the voltage level)
      delay(100);                       // wait for a second
      digitalWrite(BUILTIN_LED, HIGH);    // turn the LED off by making the voltage LOW
      delay(100);                       // wait for a second
      count ++;
      resetState = digitalRead(12);
    }
  
    
    //See if they still want to go for it
    if (resetState == LOW){
       Serial.println("Let's do it");
       SPIFFS.format();
       wifiManager.resetSettings();
       delay(500);
       ESP.restart();  
    } else {
       Serial.println("They chickened out...");
    }
    
      
    
  }
}

void loop() {
  //for the webserver
  server.handleClient();
  
  resetstate();
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  doorbellState = digitalRead(doorbellPin);
  resetState = digitalRead(12);

 if ( doorbellState == LOW ) {
    // Put your code here.  e.g. connect, send, disconnect.
    client.publish(mqtt_topic, "on" , true);
    Serial.print("Doorbell is pressed!, sending 'on' message to ");
    Serial.print(mqtt_server);
    Serial.print(" on port ");
    Serial.print(mqtt_port);
    Serial.print(" with topic ");
    Serial.println(mqtt_topic);

    //wait 5 seconds, then publish the off message
    delay( 5000 );
    Serial.print("sending 'off' message to ");
    Serial.print(mqtt_server);
    Serial.print(" on port ");
    Serial.print(mqtt_port);
    Serial.print(" with topic ");
    Serial.println(mqtt_topic);
    client.publish(mqtt_topic, "off" , true);
    
  }
}
