#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <PubSubClient.h>

int doorbellState = 0;
int resetState = 0;
const int doorbellPin = 2;

WiFiClient espClient;
PubSubClient client(espClient);

WiFiManager wifiManager;


//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] ;
char mqtt_username[40];
char mqtt_password[40];
char mqtt_topic[40];

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


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  pinMode(doorbellPin, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  //pinMode(2, OUTPUT);

  Serial.println("before format");

  //clean FS, for testing
 // SPIFFS.format();

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

 

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_topic);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

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
  Serial.println("connected...yeey :)");

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

}


void reconnect() {
  int connectCount = 0;
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (client.connect("ESP8266Client")) {
    if (client.connect("ESP8266Client", mqtt_username, mqtt_password)) {
      Serial.println("connected");
      //after a reconnect, make sure we send a "off" message so that Home assistant will prperly initialize value after reconnect 
      Serial.println("sending 'off' message");
      client.publish(mqtt_topic, "off" , true);
    } else {
      //Reset esp after 5 failed connect attempts
      //maybe not the best ide, because it will reset ESP when homeassistant is offline for a bit...
      if (connectCount == 5){
        Serial.print("Too many failures, going for a hard reboot");
        SPIFFS.format();
        wifiManager.resetSettings();
        delay(500);
        ESP.restart();     
      }
      
      connectCount++;
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

long lastMsg = 0;

void loop() {
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  doorbellState = digitalRead(doorbellPin);
  resetState = digitalRead(12);

 if ( doorbellState == LOW ) {
    // Put your code here.  e.g. connect, send, disconnect.
    client.publish(mqtt_topic, "on" , true);
    Serial.println("Doorbell is pressed!, sending 'on' message");

    //wait 5 seconds, then publish the off message
    delay( 5000 );
    Serial.println("sending 'off' message");
    client.publish(mqtt_topic, "off" , true);
  }

  if (resetState == LOW){
    Serial.println("It seems someone wants to go for a reset...");
   //stuff below doesn't work yet, since the ESP built in led is connected to pin 2, but the octocoupler is too...
    
    /*int count = 0;

    //Flash the led for 5 seconds
    while (count < 25) {
     // digitalWrite(2, HIGH);   // turn the LED on (HIGH is the voltage level)
      delay(100);                       // wait for a second
     // digitalWrite(2, LOW);    // turn the LED off by making the voltage LOW
      delay(100);                       // wait for a second
      count ++;
      
    }
  
    resetState = digitalRead(12);
    //See if they still want to go for it
    if (resetState == LOW){
       Serial.println("Let's do it");
    } else {
       Serial.println("They chickened out...");
    }
    */
    SPIFFS.format();
    wifiManager.resetSettings();
    delay(500);
    ESP.restart();     
    
  }




  
  
}
