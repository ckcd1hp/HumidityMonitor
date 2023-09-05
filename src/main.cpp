#include <Arduino.h>
#include <DHT.h>
#include <Firebase_ESP_Client.h>

#include "pins.h" // for pin declarations
#include "util.h" // for utility functions
#include "wifiUtil.h"
#include "config.h"

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

extern ESP32Time rtc; // access esp32 internal real time clock

int previousMin = -1;
int previousHour = -1; // track the last hour to run functions once an hour
String get_wifi_status(int status)
{
  switch (status)
  {
  case WL_IDLE_STATUS:
    return "WL_IDLE_STATUS";
  case WL_SCAN_COMPLETED:
    return "WL_SCAN_COMPLETED";
  case WL_NO_SSID_AVAIL:
    return "WL_NO_SSID_AVAIL";
  case WL_CONNECT_FAILED:
    return "WL_CONNECT_FAILED";
  case WL_CONNECTION_LOST:
    return "WL_CONNECTION_LOST";
  case WL_CONNECTED:
    return "WL_CONNECTED";
  case WL_DISCONNECTED:
    return "WL_DISCONNECTED";
  }
}
DHT dht(DHT_PIN, DHT11);

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

void setup()
{
  Serial.begin(115200);
  // print reason for rebooting (debugging)
  // Serial.println("System Reset: " + printBootReason());
  Serial.println("Setup begin");

  setupWifi(); // setup wifi to nftv2.local, sync ntp, enable OTA
  int status = WL_IDLE_STATUS;
  while (status != WL_CONNECTED)
  {
    delay(500);
    status = WiFi.status();
    Serial.println(get_wifi_status(status));
  }
  // set pinout
  pinMode(LED_PIN, OUTPUT);
  dht.begin();

  // Assign the api key (required)
  config.api_key = API_KEY;
  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  // Assign the RTDB URL (required)
  config.database_url = DATABASE_URL;

  // // Sign up
  // if (Firebase.signUp(&config, &auth, "", ""))
  // {
  //   Serial.println("ok");
  //   signupOK = true;
  // }
  // else
  // {
  //   Serial.printf("%s\n", config.signer.signupError.message.c_str());
  // }

  fbdo.setResponseSize(4096);

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);

  String var = "$userId";
  String val = "($userId === auth.uid && auth.token.premium_account === true && auth.token.admin === true)";
  Firebase.RTDB.setReadWriteRules(&fbdo, "/users", var, val, val, DATABASE_SECRET);
}

void loop()
{
  // checkWifiStatus();
  int currentMin = rtc.getMinute();
  /* --------------- MINUTE CHANGE -------------------*/
  if (currentMin != previousMin)
  {
    // check every 15 minutes
    if (currentMin % 15 == 0)
    {
      // Reading temperature or humidity takes about 250 milliseconds!
      // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
      float h = dht.readHumidity();
      // Read temperature as Fahrenheit (isFahrenheit = true)
      float f = dht.readTemperature(true);

      // Check if any reads failed and exit early (to try again).
      if (isnan(h) || isnan(f))
      {
        Serial.println(F("Failed to read from DHT sensor!"));
        previousMin = currentMin;
        return;
      }

      // Compute heat index in Fahrenheit (the default)
      float hif = dht.computeHeatIndex(f, h);
      Serial.print(F("Humidity: "));
      Serial.print(h);
      Serial.print(F("%  Temperature: "));
      Serial.print(f);
      Serial.print(F("°F  Heat index: "));
      Serial.print(hif);
      Serial.println(F("°F"));

      if (Firebase.ready())
      {
        String path = "/users/";
        path += auth.token.uid.c_str(); //<- user uid of current user that sign in with Email/Password
        path += "/data";
        // Serial.printf("Set timestamp... %s\n", Firebase.RTDB.setTimestamp(&fbdo, "/test/timestamp") ? "ok" : fbdo.errorReason().c_str());

        if (fbdo.httpCode() == FIREBASE_ERROR_HTTP_CODE_OK)
        {

          // In setTimestampAsync, the following timestamp will be 0 because the response payload was ignored for all async functions.

          // Timestamp saved in millisecond, get its seconds from int value
          Serial.print("TIMESTAMP (Seconds): ");
          Serial.println(fbdo.to<int>());

          // Or print the total milliseconds from double value
          // Due to bugs in Serial.print in Arduino library, use printf to print double instead.
          printf("TIMESTAMP (milliSeconds): %lld\n", fbdo.to<uint64_t>());
        }

        // Serial.printf("Get timestamp... %s\n", Firebase.RTDB.getDouble(&fbdo, "/test/timestamp") ? "ok" : fbdo.errorReason().c_str());
        // if (fbdo.httpCode() == FIREBASE_ERROR_HTTP_CODE_OK)
        //   printf("TIMESTAMP: %lld\n", fbdo.to<uint64_t>());

        // To set and push data with timestamp, requires the JSON data with .sv placeholder
        FirebaseJson json;

        json.set("Humidity", h);
        json.set("Temp", f);
        // now we will set the timestamp value at Ts
        json.set("Ts/.sv", "timestamp"); // .sv is the required place holder for sever value which currently supports only string "timestamp" as a value

        // Set data with timestamp
        // Serial.printf("Set data with timestamp... %s\n", Firebase.RTDB.setJSON(&fbdo, "/test/set/data", &json) ? fbdo.to<FirebaseJson>().raw() : fbdo.errorReason().c_str());

        // Push data with timestamp
        Serial.printf("Push data with timestamp... %s\n", Firebase.RTDB.pushJSON(&fbdo, path, &json) ? "ok" : fbdo.errorReason().c_str());

        // Get previous pushed data
        // Serial.printf("Get previous pushed data... %s\n", Firebase.RTDB.getJSON(&fbdo, "/test/push/data/" + fbdo.pushName()) ? fbdo.to<FirebaseJson>().raw() : fbdo.errorReason().c_str());
      }
    }

    previousMin = currentMin;
  }
  // get current hour 0-23
  int currentHour = rtc.getHour(true);
  /* --------------- HOUR CHANGE -------------------*/
  if (currentHour != previousHour)
  {
    // Update time using NTP at same time everyday
    if (currentHour == NTP_SYNC_HOUR)
      updateAndSyncTime();
    previousHour = currentHour;
  }
}
