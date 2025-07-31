#define DEBUG_ETHERNET_WEBSERVER_PORT       Serial 
#define NTP_DBG_PORT                      Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_ 3
#define _NTP_LOGLEVEL_                3
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT
#define ETH_PHY_POWER   -1
#define ETH_PHY_ADDR    0

#include <NTP.h>
#include <WiFiUdp.h>
#include <WebServer_WT32_ETH01.h>
#include <RTClib.h>
#include <HX710B.h>

#include "i2c/inout.h"
#include "i2c/oled.h"

#define HX_SCK_PIN 32
#define HX_DAT_PIN 33

RTC_DS3231 rtc;
WiFiServer server(80);

WiFiUDP ntpUDP;
NTP ntp(ntpUDP);

HX710B air_press(HX_SCK_PIN, HX_DAT_PIN);

#define TIME_UPDATE_PERIOD_MS 1000UL
#define INPUTS_UPDATE_PERIOD_MS 500UL

//  Select the IP address according to your local network
// IPAddress myIP(192, 168, 255, 201);
// IPAddress myGW(192, 168, 255, 65);
// IPAddress mySN(255, 255, 255, 0);

//  DNS Server IP
// IPAddress myDNS(8, 8, 8, 8);

void ScanKey();
void setPump1(bool value);
void setPump2(bool value);

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;
unsigned long lastTimeShowTime = 0;
unsigned long lastTimeShowLevel = 0;
unsigned long lastTimeShowInputs = 0;
bool previousConnected = false;
int8_t previousLinkUp = -1;

// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

int flag_read_di = 0;

uint32_t pressure_raw = 0;


// Variable to store the HTTP request
String header;

short relayStates[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // state of relays
bool timeSet = false;
bool rtcReady = false;
bool timeBlink = false;

char* ip2CharArray(IPAddress ip) {
  static char a[16];
  sprintf(a, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return a;
}

void setup_NTP() {
  ntp.ruleDST("EEST", Last, Sun, Mar, 2, 180); // last sunday in march 2:00, timetone +180min (+2 GMT + 1h summertime offset)
  ntp.ruleSTD("EET", Last, Sun, Oct, 3, 120);  // last sunday in october 3:00, timezone +120min (+2 GMT)
  ntp.begin();
}

void setup_ETH() {
  Serial.print("\nStarting " + String(WEBSERVER_WT32_ETH01_VERSION) + " on " + String(ARDUINO_BOARD));
  Serial.println(" with " + String(SHIELD_TYPE));

  // To be called before ETH.begin()
  WT32_ETH01_onEvent();

  // bool begin(uint8_t phy_addr=ETH_PHY_ADDR, int power=ETH_PHY_POWER, int mdc=ETH_PHY_MDC, int mdio=ETH_PHY_MDIO,
  //            eth_phy_type_t type=ETH_PHY_TYPE, eth_clock_mode_t clk_mode=ETH_CLK_MODE);
  // ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);
  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);
  oled_show(1, "ETH started.");

  // Static IP, leave without this line to get IP via DHCP
  // bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = 0, IPAddress dns2 = 0);
//  ETH.config(myIP, myGW, mySN, myDNS);oled.setCursor(0, 0);oled.println(F("Manual IP config."));oled.display();
}

void setRtc(NTP *ntp) {
    rtc.adjust(DateTime(ntp->year(), ntp->month(), ntp->day(), ntp->hours(), ntp->minutes(), ntp->seconds()));
//    rtc.adjust(ntp->epoch());
    timeSet = true;
}

void checkConnection() {
  bool now_connected = WT32_ETH01_isConnected();
  int8_t linkUp = ETH.linkUp() ? 1 : 0;
  String s;
  if (linkUp != previousLinkUp) {
    if (linkUp == 1) {
      s = "Cable connected.";
    } else {
      s = "Cable disconnected.";
    }
    Serial.println(s);oled_show(1, s);
    previousLinkUp = linkUp;
  }
  if (now_connected != previousConnected) {
    if (!now_connected) { 
      s = linkUp ? "Waiting for DHCP..." : "";
      Serial.println(s);oled_show(2, s);
      s = "";
    } else {
      s = String(ip2CharArray(ETH.localIP()));
      Serial.println(s);oled_show(2, s);

      setup_NTP();  // This also updates the time
      if (ntp.epoch() > (24 * 60 * 60)) {
        setRtc(&ntp);
      }

      // start the web server on port 80
      server.begin();
      s = "Server started.";
    }
    if (s != "") Serial.println(s);
    oled_show(3, s);
    previousConnected = now_connected;
  }
}

void showLevel(uint8_t line, uint8_t size) {
  char pressure[10];
  currentTime = millis();
  if ((currentTime - lastTimeShowLevel) >= INPUTS_UPDATE_PERIOD_MS) {
    uint8_t code = air_press.read(&pressure_raw, 500UL);
    if ( code != HX710B_OK ) {
      sprintf(pressure, "err: %d", code);
      oled_show(line, pressure, size);
      Serial.println("Error reading pressure");
    } else {
      sprintf(pressure, "%d ", ((long) pressure_raw) / 1024);
      oled_show(line, pressure, size);
      Serial.print("Preessure raw value: ");
      Serial.println((long) pressure_raw);
    }
    lastTimeShowLevel = currentTime;
  }
}

void showTime() {
  currentTime = millis();
  if ((currentTime - lastTimeShowTime) >= (TIME_UPDATE_PERIOD_MS/(2-uint8_t(timeSet)))) {
    char tm[10];
    char line[20];
    if (rtcReady) {
      sprintf(tm, "%02u:%02u:%02u", rtc.now().hour(), rtc.now().minute(), rtc.now().second());
      sprintf(line, "%.2f C    %s", rtc.getTemperature(), (timeSet || timeBlink) ? tm : "        ");

    } else {
      sprintf(tm, "%02u:%02u:%02u", ntp.hours(), ntp.minutes(), ntp.seconds());
      sprintf(line, "        %s", (timeSet || timeBlink) ? tm : "        ");
    }
    oled_show(0, line, 1);
    timeBlink = !timeBlink;
    lastTimeShowTime = currentTime;
  }
}

void showInputs() {
  currentTime = millis();
  if ((currentTime - lastTimeShowInputs) >= (INPUTS_UPDATE_PERIOD_MS)) {
    char inputs[20];
    uint8_t ia = pcf8574_I1.digitalReadAll();
    uint8_t ib = pcf8574_I2.digitalReadAll();
    sprintf(inputs, "A= %02X B= %02X ", ia, ib);
    Serial.print(inputs);
    oled_show(7, inputs, 1);
    lastTimeShowInputs = currentTime;
    uint8_t ia7 = pcf8574_I1.digitalRead(7, true);
    uint8_t ib7 = pcf8574_I2.digitalRead(7, true);
    Serial.print(ia7);Serial.print(" ");
    Serial.print(ib7);Serial.print(" ");

    setPump2((ia & 0x80) ? 1 : 0);
  }
}

void setup() {
  Serial.begin(115200);
  //-------------------------------------
  while (!Serial)
    delay(100);

  // Set I2C pins
  Wire.setPins(I2C_SDA, I2C_SCL);

  if (rtc.begin()) {
    rtcReady = true;
    timeSet = !rtc.lostPower();
  } else {
    Serial.println(" *** Error intializing RTC ***");
    Serial.flush();
  }
  // Init OLED
  Serial.println("Init OLED...");
  init_oled(); 
  // Init PCFs
  uint8_t code = init_pcfs();
  String s = "Init PCFs... " + (code == 0 ? "OK" : "Error " + String(code, HEX));
  Serial.println(s);oled_show(0, s);

  code = air_press.init();
//  pinMode(HX_DAT_PIN, INPUT);
  s = "Init H710B... " + (code == HX710B_OK ? "OK" : "Error " + String(code, HEX));
  Serial.println(s);oled_show(0, s);



//  test_oled();
  test_pcf();

  setup_ETH();
}

void loop() {
  // listen for incoming clients
  WiFiClient client = server.available();

  checkConnection();
  if (WT32_ETH01_isConnected() && ntp.update()) {
    Serial.println("Time synced: " + String(ntp.formattedTime("%T")));
    setRtc(&ntp);
  }

  showTime();
  showInputs();
  showLevel(4, 2);

  if ((long)pressure_raw > 50000) {
    setPump1(1);
  }
  if ((long)pressure_raw < -20000) {
    setPump1(0);
  }

  if (client) {                                // If a new client connects,
    Serial.println("New Client."); // print a message out in the serial port
    String currentLine = "";       // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime) { 
      // loop while the client's connected
      currentTime = millis();
      if (client.available()) {
        // if there's bytes to read from the client,
        char c = client.read(); // read a byte, then
        Serial.write(c);        // print it out the serial monitor
        header += c;
        if (c == '\n')
        { // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // // turns 32 relays on and off
            // if (header.indexOf("GET /1/on") >= 0) {
            //   relayStates[0] = "on";
            //   pcf8574_R1.digitalWrite(P0, LOW);
            // }
            
            if (header.indexOf("GET /status") >= 0) {
              flag_read_di = 1;
            }
            
            client.print("<!DOCTYPE HTML>\r\n");
            client.print("<html>\r\n");

            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");

            if (flag_read_di == 1) {
              for (int i = 0; i < 16; i++ ) {
                client.print(" relay");client.print(i);
                client.print(":" + relayStates[i]);
              }
            } else {
              // Web Page Heading
              client.println("<body><h1>KC868-A16 Ethernet Web Server</h1>");

              // Display current state, and ON/OFF buttons for relay
              // client.println("<p>relay1 - State " + relayStates[0] + "</p>");
              // If the relay is off, it displays the ON button
              for (int i = 0; i < 16; i++) {
                client.print("<a href=\"/");
                client.print(i);
                client.print("/");
                client.print(relayStates[i]);
                client.print("\"><button class=\"button\">");
                client.print(i);
                client.print("-");
                client.print(relayStates[i]);
                client.println("</button></a>");
              }
              client.println("<a href=\"/all/on\"><button class=\"button\">all ON</button></a>");
              client.println("<a href=\"/all/off\"><button class=\"button\">all OFF</button></a>");
            }
            client.println("</body></html>");

            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          }
          else
          { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {
          // if you got anything else but a carriage return character,
          currentLine += c; // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
    flag_read_di = 0;
  }
  previousTime = currentTime;
  ScanKey(); // scan key
}

void setPump1(bool value) {
  pcf8574_R1.digitalWrite(1, value);
}

void setPump2(bool value) {
  pcf8574_R1.digitalWrite(2, value);
}

void ScanKey()
{

  // if (pcf8574_I1.digitalRead(P0) == LOW) // key1 pressed
  // {
  //   delay(20);                             // anti-interference
  //   if (pcf8574_I1.digitalRead(P0) == LOW) // key1 pressed again
  //   {
  //     pcf8574_R1.digitalWrite(P0, !pcf8574_R1.digitalRead(P0)); // toggle relay1 state
  //     while (pcf8574_I1.digitalRead(P0) == LOW)
  //       ; // wait remove hand
  //     if (pcf8574_R1.digitalRead(P0) == LOW)
  //       relayStates[0] = "on";
  //     else
  //       relayStates[0] = "off";
  //   }
  // }
}
