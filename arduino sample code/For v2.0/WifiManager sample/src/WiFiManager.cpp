/**************************************************************
   WiFiManager is a library for the ESP8266/Arduino platform
   (https://github.com/esp8266/Arduino) to enable easy
   configuration and reconfiguration of WiFi credentials using a Captive Portal
   inspired by:
   http://www.esp8266.com/viewtopic.php?f=29&t=2520
   https://github.com/chriscook8/esp-arduino-apboot
   https://github.com/esp8266/Arduino/tree/esp8266/hardware/esp8266com/esp8266/libraries/DNSServer/examples/CaptivePortalAdvanced
   Built by AlexT https://github.com/tzapu
   Licensed under MIT license
 **************************************************************/

#include "WiFiManager.h"

WiFiManagerParameter::WiFiManagerParameter(const char *custom) {
  _id = NULL;
  _placeholder = NULL;
  _length = 0;
  _value = NULL;

  _customHTML = custom;
}

WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *placeholder, const char *defaultValue, int length) {
  init(id, placeholder, defaultValue, length, "");
}

WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom) {
  init(id, placeholder, defaultValue, length, custom);
}

void WiFiManagerParameter::init(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom) {
  _id = id;
  _placeholder = placeholder;
  _length = length;
  _value = new char[length + 1];
  for (int i = 0; i < length; i++) {
    _value[i] = 0;
  }
  if (defaultValue != NULL) {
    strncpy(_value, defaultValue, length);
  }

  _customHTML = custom;
}

const char* WiFiManagerParameter::getValue() {
  return _value;
}
const char* WiFiManagerParameter::getID() {
  return _id;
}
const char* WiFiManagerParameter::getPlaceholder() {
  return _placeholder;
}
int WiFiManagerParameter::getValueLength() {
  return _length;
}
const char* WiFiManagerParameter::getCustomHTML() {
  return _customHTML;
}

WiFiManager::WiFiManager() {
}

void WiFiManager::addParameter(WiFiManagerParameter *p) {
  _params[_paramsCount] = p;
  _paramsCount++;
  DEBUG_WM("Adding parameter");
  DEBUG_WM(p->getID());
}

void WiFiManager::setupConfigPortal() {
  dnsServer.reset(new DNSServer());
  server.reset(new ESP8266WebServer(80));

  DEBUG_WM(F(""));
  _configPortalStart = millis();

  DEBUG_WM(F("Configuring access point... "));
  DEBUG_WM(_apName);
  if (_apPassword != NULL) {
    if (strlen(_apPassword) < 8 || strlen(_apPassword) > 63) {
      // fail passphrase to short or long!
      DEBUG_WM(F("Invalid AccessPoint password. Ignoring"));
      _apPassword = NULL;
    }
    DEBUG_WM(_apPassword);
  }

  //optional soft ip config
  if (_ap_static_ip) {
    DEBUG_WM(F("Custom AP IP/GW/Subnet"));
    WiFi.softAPConfig(_ap_static_ip, _ap_static_gw, _ap_static_sn);
  }

  if (_apPassword != NULL) {
    WiFi.softAP(_apName, _apPassword);//password option
  } else {
    WiFi.softAP(_apName);
  }

  delay(500); // Without delay I've seen the IP address blank
  DEBUG_WM(F("AP IP address: "));
  DEBUG_WM(WiFi.softAPIP());

  /* Setup the DNS server redirecting all the domains to the apIP */
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", WiFi.softAPIP());

  /* Setup web pages: root, wifi config pages, SO captive portal detectors and not found. */
  server->on("/", std::bind(&WiFiManager::handleRoot, this));
  server->on("/wifi", std::bind(&WiFiManager::handleWifi, this, true));
  server->on("/0wifi", std::bind(&WiFiManager::handleWifi, this, false));
  server->on("/wifisave", std::bind(&WiFiManager::handleWifiSave, this));
  server->on("/i", std::bind(&WiFiManager::handleInfo, this));
  server->on("/r", std::bind(&WiFiManager::handleReset, this));
  //server->on("/generate_204", std::bind(&WiFiManager::handle204, this));  //Android/Chrome OS captive portal check.
  server->on("/fwlink", std::bind(&WiFiManager::handleRoot, this));  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server->onNotFound (std::bind(&WiFiManager::handleNotFound, this));
  server->begin(); // Web server start
  DEBUG_WM(F("HTTP server started"));

}

boolean WiFiManager::autoConnect() {
  String ssid = "ESP" + String(ESP.getChipId());
  return autoConnect(ssid.c_str(), NULL);
}

boolean WiFiManager::autoConnect(char const *apName, char const *apPassword) {
  DEBUG_WM(F(""));
  DEBUG_WM(F("AutoConnect"));

  // read eeprom for ssid and pass
  //String ssid = getSSID();
  //String pass = getPassword();

  // attempt to connect; should it fail, fall back to AP
  WiFi.mode(WIFI_STA);

  if (connectWifi("", "") == WL_CONNECTED)   {
    DEBUG_WM(F("IP Address:"));
    DEBUG_WM(WiFi.localIP());
    //connected
    return true;
  }

  return startConfigPortal(apName, apPassword);
}

boolean  WiFiManager::startConfigPortal(char const *apName, char const *apPassword) {
  //setup AP
  WiFi.mode(WIFI_AP_STA);
  DEBUG_WM("SET AP STA");

  _apName = apName;
  _apPassword = apPassword;

  //notify we entered AP mode
  if ( _apcallback != NULL) {
    _apcallback(this);
  }

  connect = false;
  setupConfigPortal();

  while (_configPortalTimeout == 0 || millis() < _configPortalStart + _configPortalTimeout) {
    //DNS
    dnsServer->processNextRequest();
    //HTTP
    server->handleClient();


    if (connect) {
      connect = false;
      delay(2000);
      DEBUG_WM(F("Connecting to new AP"));

      // using user-provided  _ssid, _pass in place of system-stored ssid and pass
      if (connectWifi(_ssid, _pass) != WL_CONNECTED) {
        DEBUG_WM(F("Failed to connect."));
      } else {
        //connected
        WiFi.mode(WIFI_STA);
        //notify that configuration has changed and any optional parameters should be saved
        if ( _savecallback != NULL) {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }

      if (_shouldBreakAfterConfig) {
        //flag set to exit after config after trying to connect
        //notify that configuration has changed and any optional parameters should be saved
        if ( _savecallback != NULL) {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }
    }
    yield();
  }

  server.reset();
  dnsServer.reset();

  return  WiFi.status() == WL_CONNECTED;
}


int WiFiManager::connectWifi(String ssid, String pass) {
  DEBUG_WM(F("Connecting as wifi client..."));

  // check if we've got static_ip settings, if we do, use those.
  if (_sta_static_ip) {
    DEBUG_WM(F("Custom STA IP/GW/Subnet"));
    WiFi.config(_sta_static_ip, _sta_static_gw, _sta_static_sn);
    DEBUG_WM(WiFi.localIP());
  }
  //fix for auto connect racing issue
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_WM("Already connected. Bailing out.");
    return WL_CONNECTED;
  }
  //check if we have ssid and pass and force those, if not, try with last saved values
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    if (WiFi.SSID()) {
      DEBUG_WM("Using last saved values, should be faster");
      //trying to fix connection in progress hanging
      ETS_UART_INTR_DISABLE();
      wifi_station_disconnect();
      ETS_UART_INTR_ENABLE();

      WiFi.begin();
    } else {
      DEBUG_WM("No saved credentials");
    }
  }

  int connRes = waitForConnectResult();
  DEBUG_WM ("Connection result: ");
  DEBUG_WM ( connRes );
  //not connected, WPS enabled, no pass - first attempt
  if (_tryWPS && connRes != WL_CONNECTED && pass == "") {
    startWPS();
    //should be connected at the end of WPS
    connRes = waitForConnectResult();
  }
  return connRes;
}

uint8_t WiFiManager::waitForConnectResult() {
  if (_connectTimeout == 0) {
    return WiFi.waitForConnectResult();
  } else {
    DEBUG_WM (F("Waiting for connection result with time out"));
    unsigned long start = millis();
    boolean keepConnecting = true;
    uint8_t status;
    while (keepConnecting) {
      status = WiFi.status();
      if (millis() > start + _connectTimeout) {
        keepConnecting = false;
        DEBUG_WM (F("Connection timed out"));
      }
      if (status == WL_CONNECTED || status == WL_CONNECT_FAILED) {
        keepConnecting = false;
      }
      delay(100);
    }
    return status;
  }
}

void WiFiManager::startWPS() {
  DEBUG_WM("START WPS");
  WiFi.beginWPSConfig();
  DEBUG_WM("END WPS");
}
/*
  String WiFiManager::getSSID() {
  if (_ssid == "") {
    DEBUG_WM(F("Reading SSID"));
    _ssid = WiFi.SSID();
    DEBUG_WM(F("SSID: "));
    DEBUG_WM(_ssid);
  }
  return _ssid;
  }

  String WiFiManager::getPassword() {
  if (_pass == "") {
    DEBUG_WM(F("Reading Password"));
    _pass = WiFi.psk();
    DEBUG_WM("Password: " + _pass);
    //DEBUG_WM(_pass);
  }
  return _pass;
  }
*/
String WiFiManager::getConfigPortalSSID() {
  return _apName;
}

void WiFiManager::resetSettings() {
  DEBUG_WM(F("settings invalidated"));
  DEBUG_WM(F("THIS MAY CAUSE AP NOT TO START UP PROPERLY. YOU NEED TO COMMENT IT OUT AFTER ERASING THE DATA."));
  WiFi.disconnect(true);
  //delay(200);
}
void WiFiManager::setTimeout(unsigned long seconds) {
  setConfigPortalTimeout(seconds);
}

void WiFiManager::setConfigPortalTimeout(unsigned long seconds) {
  _configPortalTimeout = seconds * 1000;
}

void WiFiManager::setConnectTimeout(unsigned long seconds) {
  _connectTimeout = seconds * 1000;
}

void WiFiManager::setDebugOutput(boolean debug) {
  _debug = debug;
}

void WiFiManager::setAPStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn) {
  _ap_static_ip = ip;
  _ap_static_gw = gw;
  _ap_static_sn = sn;
}

void WiFiManager::setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn) {
  _sta_static_ip = ip;
  _sta_static_gw = gw;
  _sta_static_sn = sn;
}

void WiFiManager::setMinimumSignalQuality(int quality) {
  _minimumQuality = quality;
}

void WiFiManager::setBreakAfterConfig(boolean shouldBreak) {
  _shouldBreakAfterConfig = shouldBreak;
}

/** Handle root or redirect to captive portal */
void WiFiManager::handleRoot() {
  DEBUG_WM(F("Handle root"));
  if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
    return;
  }

  String page = FPSTR(HTTP_HEAD);
  page.replace("{v}", "Options");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += "<img style=\"display: block;  margin-left: auto; margin-right: auto;  width: 100%;\" src=\"data:image/jpeg;base64,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\"></img><br />";
  page += FPSTR(HTTP_PORTAL_OPTIONS);
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);



}

/** Wifi config page handler */
void WiFiManager::handleWifi(boolean scan) {

  String page = FPSTR(HTTP_HEAD);
  page.replace("{v}", "Config ESP");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);

  if (scan) {
    int n = WiFi.scanNetworks();
    DEBUG_WM(F("Scan done"));
    if (n == 0) {
      DEBUG_WM(F("No networks found"));
      page += F("No networks found. Refresh to scan again.");
    } else {

      //sort networks
      int indices[n];
      for (int i = 0; i < n; i++) {
        indices[i] = i;
      }

      // RSSI SORT

      // old sort
      for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
          if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
            std::swap(indices[i], indices[j]);
          }
        }
      }

      /*std::sort(indices, indices + n, [](const int & a, const int & b) -> bool
        {
        return WiFi.RSSI(a) > WiFi.RSSI(b);
        });*/

      // remove duplicates ( must be RSSI sorted )
      if (_removeDuplicateAPs) {
        String cssid;
        for (int i = 0; i < n; i++) {
          if (indices[i] == -1) continue;
          cssid = WiFi.SSID(indices[i]);
          for (int j = i + 1; j < n; j++) {
            if (cssid == WiFi.SSID(indices[j])) {
              DEBUG_WM("DUP AP: " + WiFi.SSID(indices[j]));
              indices[j] = -1; // set dup aps to index -1
            }
          }
        }
      }

      //display networks in page
      for (int i = 0; i < n; i++) {
        if (indices[i] == -1) continue; // skip dups
        DEBUG_WM(WiFi.SSID(indices[i]));
        DEBUG_WM(WiFi.RSSI(indices[i]));
        int quality = getRSSIasQuality(WiFi.RSSI(indices[i]));

        if (_minimumQuality == -1 || _minimumQuality < quality) {
          String item = FPSTR(HTTP_ITEM);
          String rssiQ;
          rssiQ += quality;
          item.replace("{v}", WiFi.SSID(indices[i]));
          item.replace("{r}", rssiQ);
          if (WiFi.encryptionType(indices[i]) != ENC_TYPE_NONE) {
            item.replace("{i}", "l");
          } else {
            item.replace("{i}", "");
          }
          //DEBUG_WM(item);
          page += item;
          delay(0);
        } else {
          DEBUG_WM(F("Skipping due to quality"));
        }

      }
      page += "<br/>";
    }
  }

  page += FPSTR(HTTP_FORM_START);
  char parLength[2];
  // add the extra parameters to the form
  for (int i = 0; i < _paramsCount; i++) {
    if (_params[i] == NULL) {
      break;
    }

    String pitem = FPSTR(HTTP_FORM_PARAM);
    if (_params[i]->getID() != NULL) {
      pitem.replace("{i}", _params[i]->getID());
      pitem.replace("{n}", _params[i]->getID());
      pitem.replace("{p}", _params[i]->getPlaceholder());
      snprintf(parLength, 2, "%d", _params[i]->getValueLength());
      pitem.replace("{l}", parLength);
      pitem.replace("{v}", _params[i]->getValue());
      pitem.replace("{c}", _params[i]->getCustomHTML());
    } else {
      pitem = _params[i]->getCustomHTML();
    }

    page += pitem;
  }
  if (_params[0] != NULL) {
    page += "<br/>";
  }

  if (_sta_static_ip) {

    String item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "ip");
    item.replace("{n}", "ip");
    item.replace("{p}", "Static IP");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_ip.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "gw");
    item.replace("{n}", "gw");
    item.replace("{p}", "Static Gateway");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_gw.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "sn");
    item.replace("{n}", "sn");
    item.replace("{p}", "Subnet");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_sn.toString());

    page += item;

    page += "<br/>";
  }

  page += FPSTR(HTTP_FORM_END);
  page += FPSTR(HTTP_SCAN_LINK);

  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);


  DEBUG_WM(F("Sent config page"));
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void WiFiManager::handleWifiSave() {
  DEBUG_WM(F("WiFi save"));

  //SAVE/connect here
  _ssid = server->arg("s").c_str();
  _pass = server->arg("p").c_str();

  //parameters
  for (int i = 0; i < _paramsCount; i++) {
    if (_params[i] == NULL) {
      break;
    }
    //read parameter
    String value = server->arg(_params[i]->getID()).c_str();
    //store it in array
    value.toCharArray(_params[i]->_value, _params[i]->_length);
    DEBUG_WM(F("Parameter"));
    DEBUG_WM(_params[i]->getID());
    DEBUG_WM(value);
  }

  if (server->arg("ip") != "") {
    DEBUG_WM(F("static ip"));
    DEBUG_WM(server->arg("ip"));
    //_sta_static_ip.fromString(server->arg("ip"));
    String ip = server->arg("ip");
    optionalIPFromString(&_sta_static_ip, ip.c_str());
  }
  if (server->arg("gw") != "") {
    DEBUG_WM(F("static gateway"));
    DEBUG_WM(server->arg("gw"));
    String gw = server->arg("gw");
    optionalIPFromString(&_sta_static_gw, gw.c_str());
  }
  if (server->arg("sn") != "") {
    DEBUG_WM(F("static netmask"));
    DEBUG_WM(server->arg("sn"));
    String sn = server->arg("sn");
    optionalIPFromString(&_sta_static_sn, sn.c_str());
  }

  String page = FPSTR(HTTP_HEAD);
  page.replace("{v}", "Credentials Saved");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += FPSTR(HTTP_SAVED);
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent wifi save page"));

  connect = true; //signal ready to connect/reset
}

/** Handle the info page */
void WiFiManager::handleInfo() {
  DEBUG_WM(F("Info"));

  String page = FPSTR(HTTP_HEAD);
  page.replace("{v}", "Info");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += F("<dl>");
  page += F("<dt>Chip ID</dt><dd>");
  page += ESP.getChipId();
  page += F("</dd>");
  page += F("<dt>Flash Chip ID</dt><dd>");
  page += ESP.getFlashChipId();
  page += F("</dd>");
  page += F("<dt>IDE Flash Size</dt><dd>");
  page += ESP.getFlashChipSize();
  page += F(" bytes</dd>");
  page += F("<dt>Real Flash Size</dt><dd>");
  page += ESP.getFlashChipRealSize();
  page += F(" bytes</dd>");
  page += F("<dt>Soft AP IP</dt><dd>");
  page += WiFi.softAPIP().toString();
  page += F("</dd>");
  page += F("<dt>Soft AP MAC</dt><dd>");
  page += WiFi.softAPmacAddress();
  page += F("</dd>");
  page += F("<dt>Station MAC</dt><dd>");
  page += WiFi.macAddress();
  page += F("</dd>");
  page += F("</dl>");
  page += FPSTR(HTTP_END);

  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent info page"));
}

/** Handle the reset page */
void WiFiManager::handleReset() {
  DEBUG_WM(F("Reset"));

  String page = FPSTR(HTTP_HEAD);
  page.replace("{v}", "Info");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEAD_END);
  page += F("Module will reset in a few seconds.");
  page += FPSTR(HTTP_END);
  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent reset page"));
  delay(5000);
  ESP.reset();
  delay(2000);
}



//removed as mentioned here https://github.com/tzapu/WiFiManager/issues/114
/*void WiFiManager::handle204() {
  DEBUG_WM(F("204 No Response"));
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send ( 204, "text/plain", "");
}*/

void WiFiManager::handleNotFound() {
  if (captivePortal()) { // If captive portal redirect instead of displaying the error page.
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += ( server->method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";

  for ( uint8_t i = 0; i < server->args(); i++ ) {
    message += " " + server->argName ( i ) + ": " + server->arg ( i ) + "\n";
  }
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send ( 404, "text/plain", message );
}


/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
boolean WiFiManager::captivePortal() {
  if (!isIp(server->hostHeader()) ) {
    DEBUG_WM(F("Request redirected to captive portal"));
    server->sendHeader("Location", String("http://") + toStringIp(server->client().localIP()), true);
    server->send ( 302, "text/plain", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server->client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

//start up config portal callback
void WiFiManager::setAPCallback( void (*func)(WiFiManager* myWiFiManager) ) {
  _apcallback = func;
}

//start up save config callback
void WiFiManager::setSaveConfigCallback( void (*func)(void) ) {
  _savecallback = func;
}

//sets a custom element to add to head, like a new style tag
void WiFiManager::setCustomHeadElement(const char* element) {
  _customHeadElement = element;
}

//if this is true, remove duplicated Access Points - defaut true
void WiFiManager::setRemoveDuplicateAPs(boolean removeDuplicates) {
  _removeDuplicateAPs = removeDuplicates;
}



template <typename Generic>
void WiFiManager::DEBUG_WM(Generic text) {
  if (_debug) {
    Serial.print("*WM: ");
    Serial.println(text);
  }
}

int WiFiManager::getRSSIasQuality(int RSSI) {
  int quality = 0;

  if (RSSI <= -100) {
    quality = 0;
  } else if (RSSI >= -50) {
    quality = 100;
  } else {
    quality = 2 * (RSSI + 100);
  }
  return quality;
}

/** Is this an IP? */
boolean WiFiManager::isIp(String str) {
  for (int i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

/** IP to String? */
String WiFiManager::toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}
