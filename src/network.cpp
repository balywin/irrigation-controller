#include "i2c/hw_config.h"
#include "network.h"

#ifdef WIFI_NO_ETHERNET
  #include <WiFi.h>
  #include <WebServer.h>
#else
  #include <WebServer_WT32_ETH01.h>
#endif

// listen for incoming clients
WiFiServer server(80);
int flagReadDi = 0;

// Variable to store the HTTP request
String header;
uint32_t previousTime = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
long connectionTimeoutMs = 2000;

short relayStates[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // state of relays

  void networkInit() {
#ifdef WIFI_NO_ETHERNET
    Serial.print("\nStarting WiFi on " + String(ARDUINO_BOARD));
    WiFi.begin(AP_SSID, AP_PASSWORD);
    WiFi.mode(WIFI_STA);
#else
    Serial.print("\nStarting " + String(WEBSERVER_WT32_ETH01_VERSION) + " on " + String(ARDUINO_BOARD));
    Serial.println(" with " + String(SHIELD_TYPE));

    // To be called before ETH.begin()
    WT32_ETH01_onEvent();

    // bool begin(uint8_t phy_addr=ETH_PHY_ADDR, int power=ETH_PHY_POWER, int mdc=ETH_PHY_MDC, int mdio=ETH_PHY_MDIO,
    //            eth_phy_type_t type=ETH_PHY_TYPE, eth_clock_mode_t clk_mode=ETH_CLK_MODE);
    // ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);
    ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);
    // Static IP, leave without this line to get IP via DHCP
    // bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = 0, IPAddress dns2 = 0);
    //  ETH.config(myIP, myGW, mySN, myDNS);oled.setCursor(0, 0);oled.println(F("Manual IP config."));oled.display();
#endif
    Serial.println(" ... started.");
  }

  uint8_t getNetworkStatus() {
#ifdef WIFI_NO_ETHERNET
    return WiFi.status();
#else
    return ETH.linkUp() ? 1 : 0;
#endif
  }

  IPAddress getNetworkLocalIp() {
#ifdef WIFI_NO_ETHERNET
    return WiFi.localIP();
#else
    return ETH.localIP();
#endif
  }

  bool getNetworkIsConnected() {
#ifdef WIFI_NO_ETHERNET
    return WiFi.isConnected();
#else
    return WT32_ETH01_isConnected();
#endif
  }

void serverInit() {
  server.begin();
}

void httpHandler() {
  uint32_t currentTime;
  WiFiClient client = server.available();
  if (client) {                                // If a new client connects,
    Serial.println("New Client."); // print a message out in the serial port
    String currentLine = "";       // make a String to hold incoming data from the client
    currentTime = millis();
    while (client.connected() && ((currentTime - previousTime) <= connectionTimeoutMs)) { 
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
              flagReadDi = 1;
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

            if (flagReadDi == 1) {
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
    previousTime = currentTime;
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
    flagReadDi = 0;
  }
}