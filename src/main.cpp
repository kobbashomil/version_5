#include <Arduino.h>
//--------- lib---------------
#include <WiFi.h>
#include <WebServer.h>
#include <RtcDS1302.h>
#include <ThreeWire.h>

#include <EEPROM.h>
#define EEPROM_SIZE 32 // حجم الذاكرة المطلوبة (يمكن تعديله حسب الحاجة)
//----------------- lib---------------

// ----------------------- Configuration -----------------------
bool autoMode = true;
int morningStartHour = 7;
int nightReturnHour = 18;
int stepInterval = 30;
int motorStepTime = 1000; // متغير يمكن تحديثه أثناء التشغيل

// ----------------------- EEPROM Functions -----------------------
void saveSettingsToEEPROM()
{
  EEPROM.writeBool(0, autoMode);
  EEPROM.write(1, morningStartHour);
  EEPROM.write(2, nightReturnHour);
  EEPROM.write(3, stepInterval);
  EEPROM.write(4, motorStepTime & 0xFF);        // حفظ الجزء الأدنى
  EEPROM.write(5, (motorStepTime >> 8) & 0xFF); // حفظ الجزء الأعلى
  EEPROM.commit();
  Serial.println("✅ Settings saved to EEPROM");
}

void loadSettingsFromEEPROM()
{
  autoMode = EEPROM.readBool(0);
  morningStartHour = EEPROM.read(1);
  nightReturnHour = EEPROM.read(2);
  stepInterval = EEPROM.read(3);
  motorStepTime = EEPROM.read(4) | (EEPROM.read(5) << 8);
  Serial.println("✅ Settings loaded from EEPROM");
}
// ----------------------- EEPROM Functions -----------------------
void validateOrResetSettings()
{
  if (morningStartHour > 23)
    morningStartHour = 6;
  if (nightReturnHour > 23)
    nightReturnHour = 18;
  if (stepInterval < 1 || stepInterval > 60)
    stepInterval = 10;
  if (motorStepTime < 100 || motorStepTime > 10000)
    motorStepTime = 1000;
}

// ----------------------- Configuration -----------------------
const char *ssid = "solar_track";
const char *password = "admin70503";

const int RELAY_EAST = 12;
const int RELAY_WEST = 14;
const int SENSOR_EAST = 22;
const int SENSOR_WEST = 23;

// bool autoMode = true;
// int morningStartHour = 7;
// int nightReturnHour = 18;
// int stepInterval = 30;
// int motorStepTime = 1000; // متغير يمكن تحديثه أثناء التشغيل

bool isMovingEast = false;
bool isMovingWest = false;

unsigned long lastMoveTime = 0; // Stores last movement time
bool returningToEast = false;   // Track if we returned to East

ThreeWire myWire(15, 2, 4); //  DAT = GPIO15, CLK= GPIO2, RST = GPIO4
RtcDS1302<ThreeWire> Rtc(myWire);
WebServer server(80);

const char *adminPassword = "1234"; // Change this to your desired password
bool isAuthenticated = false;       // Tracks if the user is authenticated

// ----------------------- WiFi Access Point Setup -----------------------
void setupWiFi()
{
  WiFi.softAP(ssid, password);
  Serial.print("Access Point IP Address: ");
  Serial.println(WiFi.softAPIP());
}

// ----------------------- Motor Control Functions -----------------------
void moveEast()
{
  if (digitalRead(SENSOR_EAST) == HIGH)
  {
    Serial.println("East limit reached");
    return;
  }
  if (isMovingWest)
  {
    Serial.println("Cannot move East while West is active!");
    return;
  }

  Serial.println("Moving East");
  digitalWrite(RELAY_EAST, HIGH);
  digitalWrite(RELAY_WEST, LOW); // تأكد من إيقاف الاتجاه الآخر
  isMovingEast = true;
  isMovingWest = false;
}

void moveWest()
{
  if (digitalRead(SENSOR_WEST) == HIGH)
  {
    Serial.println("West limit reached");
    return;
  }
  if (isMovingEast)
  {
    Serial.println("Cannot move West while East is active!");
    return;
  }

  Serial.println("Moving West");
  digitalWrite(RELAY_WEST, HIGH);
  digitalWrite(RELAY_EAST, LOW); // تأكد من إيقاف الاتجاه الآخر
  isMovingWest = true;
  isMovingEast = false;
}

void stopMotor()
{
  digitalWrite(RELAY_EAST, LOW);
  digitalWrite(RELAY_WEST, LOW);
  isMovingEast = false;
  isMovingWest = false;
  Serial.println("Motor stopped.");
}
//------------------handleUnlock()-------------
void handleUnlock()
{
  String password = server.arg("password");
  if (password == adminPassword)
  {
    isAuthenticated = true;
    Serial.println("✅ Password correct. Settings unlocked.");
  }
  else
  {
    isAuthenticated = false;
    Serial.println("❌ Incorrect password.");
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Redirecting...");
}

// ----------------------- Web Server Handlers -----------------------
void handleRoot()
{
  RtcDateTime now = Rtc.GetDateTime();
  char timeBuffer[20];
  sprintf(timeBuffer, "%02d:%02d:%02d", now.Hour(), now.Minute(), now.Second());

  String html = "<!DOCTYPE html><html><head><title>Solar Controller</title>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>"
                "body {text-align:center; font-family:Arial, sans-serif; background:#f4f4f4; color:#333; padding:20px;}"
                "h1 {color:#007bff; margin-bottom: 10px;}"
                ".container {max-width: 600px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0px 0px 10px rgba(0,0,0,0.1);}"
                ".btn {display: inline-block; padding: 10px 20px; margin: 10px; border: none; cursor: pointer; color: white; border-radius: 5px; font-size: 16px; text-decoration: none;}"
                ".btn.east {background: #28a745;}"
                ".btn.west {background: #dc3545;}"
                ".btn.stop {background: #ffc107; color: #000;}"
                ".btn.save {background: #007bff;}"
                ".btn.time {background: #17a2b8;}"
                ".settings-box {padding: 15px; background: #f8f9fa; border-radius: 10px; margin-top: 20px; text-align: left;}"
                "form {margin-top: 20px;}"
                "label {display: block; margin: 8px 0; font-weight: bold;}"
                "input {width: 100%; padding: 8px; margin: 5px 0; border: 1px solid #ccc; border-radius: 5px;}"
                "input[type='submit'] {width: auto; padding: 10px 15px; cursor: pointer;}"
                ".error {color: red; font-weight: bold;}"
                ".success {color: green; font-weight: bold;}"
                "</style></head><body>"
                "<div class='container'>"
                "<h1>Solar Panel Controller</h1>";

  // Display error or success messages
  if (server.hasArg("error"))
  {
    html += "<p class='error'> Incorrect password. Access denied.</p>";
  }
  else if (server.hasArg("success"))
  {
    html += "<p class='success'> Operation successful!</p>";
  }

  html += "<p><strong>Current Time:</strong> " +
          String(timeBuffer) + "</p>"
                               "<a href='/move?dir=east' class='btn east'>Move East</a>"
                               "<a href='/move?dir=west' class='btn west'>Move West</a>"
                               "<a href='/move?dir=stop' class='btn stop'>Stop</a>";

  html += "<div class='settings-box'>"
          "<h2>Set Time</h2>"
          "<form action='/settime' method='POST'>"
          "<label>Hour: <input type='number' name='hour' min='0' max='23'></label>"
          "<label>Minute: <input type='number' name='minute' min='0' max='59'></label>"
          "<label>Second: <input type='number' name='second' min='0' max='59'></label>"
          "<label>Day: <input type='number' name='day' min='1' max='31'></label>"
          "<label>Month: <input type='number' name='month' min='1' max='12'></label>"
          "<label>Year: <input type='number' name='year' min='2020' max='2099'></label>"
          "<input type='submit' value='Set Time' class='btn time'></form>"
          "</div>"
          "<div class='settings-box'>"
          "<h2>Settings</h2>"
          "<form action='/settings' method='POST'>"
          "<label>Password: <input type='password' name='password' required></label>"
          "<label>Auto Mode: <input type='checkbox' name='autoMode' " +
          String(autoMode ? "checked" : "") + "></label>"
                                              "<label>Morning Start Hour: <input type='number' name='morningStart' value='" +
          String(morningStartHour) + "'></label>"
                                     "<label>Night Return Hour: <input type='number' name='nightReturn' value='" +
          String(nightReturnHour) + "'></label>"
                                    "<label>Step Interval (min): <input type='number' name='stepInterval' value='" +
          String(stepInterval) + "'></label>"
                                 "<label>Motor Step Time (ms): <input type='number' name='motorStepTime' value='" +
          String(motorStepTime) + "'></label><br>"
                                  "<input type='submit' value='Save Settings' class='btn save'></form>"
                                  "</div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}
//---------------- handleMove()--------------
void handleMove()
{
  String direction = server.arg("dir");
  if (direction == "east")
    moveEast();
  else if (direction == "west")
    moveWest();
  else if (direction == "stop")
    stopMotor();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Redirecting...");
}
//--------------------handleSettings()------------
void handleSettings()
{
  String password = server.arg("password");
  if (password != adminPassword)
  {
    // Redirect back to the main page with an error message
    server.sendHeader("Location", "/?error=1", true);
    server.send(302, "text/plain", "Redirecting...");
    return;
  }

  autoMode = server.arg("autoMode") == "on";
  morningStartHour = server.arg("morningStart").toInt();
  nightReturnHour = server.arg("nightReturn").toInt();
  stepInterval = server.arg("stepInterval").toInt();

  if (server.hasArg("motorStepTime"))
  {
    motorStepTime = server.arg("motorStepTime").toInt();
    Serial.print("New Motor Step Time: ");
    Serial.println(motorStepTime);
  }

  // Redirect back to the main page after successful update
  server.sendHeader("Location", "/?success=1", true);
  server.send(302, "text/plain", "Redirecting...");
  saveSettingsToEEPROM();
  Serial.println("Settings updated and saved to EEPROM.");
}

//-------------------handleSetTime()---------------
void handleSetTime()
{
  String password = server.arg("password");

  RtcDateTime newTime(server.arg("year").toInt(), server.arg("month").toInt(), server.arg("day").toInt(),
                      server.arg("hour").toInt(), server.arg("minute").toInt(), server.arg("second").toInt());
  Rtc.SetDateTime(newTime);

  // Redirect back to the main page after successful update
  server.sendHeader("Location", "/?success=1", true);
  server.send(302, "text/plain", "Redirecting...");
}
// ----------------------- Setup and Loop -----------------------
void setup()
{
  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);
  loadSettingsFromEEPROM();
  validateOrResetSettings();

  pinMode(RELAY_EAST, OUTPUT);
  pinMode(RELAY_WEST, OUTPUT);
  pinMode(SENSOR_EAST, INPUT_PULLDOWN); // NO: استخدم PULLDOWN
  pinMode(SENSOR_WEST, INPUT_PULLDOWN); // NO: استخدم PULLDOWN
  digitalWrite(RELAY_EAST, LOW);
  digitalWrite(RELAY_WEST, LOW);

  setupWiFi();
  Rtc.Begin();

  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/settime", HTTP_POST, handleSetTime);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.begin();
}
//-----------------------------loop ----------
void loop()
{
  server.handleClient(); // معالجة الطلبات دائمًا

  // تحديث الوقت من RTC
  RtcDateTime now = Rtc.GetDateTime();
  int currentHour = now.Hour();
  unsigned long currentMillis = millis();

  // منع تشغيل الريليهات معًا
  if (isMovingEast && isMovingWest)
  {
    stopMotor();
    Serial.println("⚠️ Error: Both relays active! Stopping motor.");
  }

  // **الوضع التلقائي**
  if (autoMode)
  {
    // 🌞 **الصباح: التحرك شرقًا بفواصل زمنية**
    // الصباح: التحرك غربًا بفواصل زمنية
    if (currentHour >= morningStartHour && currentHour < nightReturnHour)
    {
      returningToEast = false; // إعادة ضبط حالة العودة الليلية

      if (!isMovingEast && !isMovingWest && (currentMillis - lastMoveTime >= (stepInterval * 60000)))
      {
        Serial.println("🌞 Auto Mode: Moving West Step");
        moveWest();              // تشغيل المحرك غربًا
        delay(motorStepTime);    // الاستمرار بالحركة لمدة محددة
        stopMotor();             // التوقف بعد الفترة المحددة
        lastMoveTime = millis(); // تحديث وقت الحركة الأخيرة
      }
    }

    // 🌙 **الليل: العودة إلى الشرق حتى الوصول إلى المستشعر**
    else if (currentHour >= nightReturnHour && !returningToEast)
    {
      Serial.println("🌙 Auto Mode: Returning to East");

      // الاستمرار في التحرك شرقًا حتى يلمس الحساس الشرقي
      if (digitalRead(SENSOR_EAST) == LOW)
      {
        Serial.println("🌙 Moving East to return to start position...");
        moveEast(); // تأكد أننا نتحرك شرقًا وليس غربًا
      }
      else
      {
        Serial.println("✅ Reached East Position - Stopping motor");
        stopMotor();
        returningToEast = true; // تأكيد العودة وعدم تكرار العملية
      }
    }
  }
}
