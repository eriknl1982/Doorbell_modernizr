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
  page += "<img style=\"display: block;  margin-left: auto; margin-right: auto;  width: 50%;\" src=\"data:image/jpeg;base64,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\"></img>";
  page += "<h1>";
  page += _apName;
  page += "</h1>";
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
