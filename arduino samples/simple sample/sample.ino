#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define wifi_ssid "YOUR WIFI NAME"
#define wifi_password "YOUR WIFI PASSWORD"

#define mqtt_server "YOUR HOME ASSISTANT MQTT IP"
#define mqtt_user "YOUR HOME ASSISTANT USERNAME"
#define mqtt_password "YOUR HOME ASSISTANT PASSWORD"

#define doorbell_topic "hal/doorbell" //change to whatever you like to use a s topic

int doorbellState = 0;
const int doorbellPin = 2;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  pinMode(doorbellPin, INPUT_PULLUP);

  WiFi.mode(WIFI_STA); //don't be a AP
 }

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (client.connect("ESP8266Client")) {
    if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
 
doorbellState = digitalRead(doorbellPin);

 if ( doorbellState == LOW ) {
    // Put your code here.  e.g. connect, send, disconnect.
    client.publish(doorbell_topic, "on" , true);
    Serial.println("Doorbell is pressed!");

    //wait 5 seconds, then publish the off message
    delay( 5000 );
    client.publish(doorbell_topic, "off" , true);
  }
}