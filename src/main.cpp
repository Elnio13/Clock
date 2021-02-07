#include <MD_Parola.h> //https://github.com/MajicDesigns/MD_Parola
#include <MD_MAX72xx.h>
#include <SPI.h>
#include "Font_Data.h"
#include "secret.h"

#include <RTClib.h> // https://github.com/adafruit/RTClib?utm_source=platformio&utm_medium=piohome
#include <Wire.h>
#include <TimeLib.h>
//#include "sntp.h" // NTP connect   Very bad. TZ dont work
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#include <NtpClientLib.h> //Include NtpClient library header https://github.com/gmag11/NtpClient

#define DS3231_SCL 4
#define DS3231_SDA 5

#define TIME_NOW_UNIX 1609240000L

RTC_DS3231 RTC;

NTPSyncEvent_t ntpEvent; // Last triggered event

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

static uint8_t currentMinute = 0xff;
static uint8_t currentDay = 0xff;

bool ntpStatus = false;

//-==========================================  Time ===========================================================-
#define TIME_SYNC_PERIOD 66 // [s] If RTC

#define DEBUG_SERIAL Serial
#define DEBUG_BAUDRATE 115200
#define DEBUG(...) Serial.printf(__VA_ARGS__)
#define DEBUGLN(...)            \
	Serial.printf(__VA_ARGS__); \
	Serial.println()
#define DPRINT(...) Serial.print(__VA_ARGS__)
#define DPRINTLN(...) Serial.println(__VA_ARGS__)
#define DPRINTF(...) Serial.print(F(__VA_ARGS__))
#define DPRINTLNF(...) Serial.println(F(__VA_ARGS__))
#define DEBUG_PRINT(...)                 \
	Serial.print(F(#__VA_ARGS__ " = ")); \
	Serial.print(__VA_ARGS__);           \
	Serial.print(F(" "))
#define DEBUG_PRINTLN(...)    \
	DEBUG_PRINT(__VA_ARGS__); \
	Serial.println()
// -=================================================== for Set RTC as time provider =================================================-
time_t syncProvider() // this does the same thing as RTC_DS1307::get()
{
	return RTC.now().unixtime();
}
// -=================================================== Time to string ================================================================-
char *dateTimeToChar(time_t dt)
{
	static char buffer[64];
	sprintf(buffer, "%d/%d/%d %02d:%02d:%02d", day(dt), month(dt), year(dt), hour(dt), minute(dt), second(dt));
	return buffer;
}
// -======================================================== Live time ================================================================-
char *charNow()
{
	return dateTimeToChar(now());
}
// -=============================================== Sync RTC with NTP ====================================================-
void syncRTCWithNTP()
{
	//	ntpStatus = false;
	//	if (WiFi.status() != WL_CONNECTED)
	//		return;
	char buffer[1];

	//	for (uint8_t i = 1; i < 10; i++)
	//	{ // 10 connection attempt
	//		time_t tmNtp = sntp_get_current_timestamp();

	time_t tmNtp = NTP.getLastNTPSync();
	DEBUG("NTP time: %s\n", dateTimeToChar(tmNtp));
	//	DEBUG("NTP time: %d\n", tmNtp);

	if (tmNtp < TIME_NOW_UNIX)
	{	// verify time correction
		//		sprintf(buffer, "No response from NTP: %d\n", i);

		ntpStatus = false;
		sprintf(buffer, "No response from NTP");
		DPRINT(buffer);

		//	delay(500);
		//yield();
		//	continue;
	}
	else
	{
		RTC.adjust(tmNtp);
		//		DEBUG("NTP time: %s\n", dateTimeToChar(tmNtp));
		DEBUG("RTC time: %s\n", dateTimeToChar(RTC.now().unixtime()));
		ntpStatus = true;
		//	break;
	}
	//	}
}

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_CLK_ZONES 2
#define ZONE_SIZE 8
#define MAX_DEVICES (MAX_CLK_ZONES * ZONE_SIZE)
#define ZONE_UPPER 1
#define ZONE_LOWER 0
#define CLK_PIN D5
#define DATA_PIN D7
#define CS_PIN D8

MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

#define SPEED_TIME 75
#define PAUSE_TIME 0
#define MAX_MESG 9
char szTimeL[MAX_MESG]; // mm:ss\0
char szTimeH[MAX_MESG];

void getTime(char *psz, bool f = true, bool w = false, bool n = false)
{
	time_t timestamp = now();
	sprintf(psz, "%02d%c%02d%c%c%c", hour(timestamp), ':', minute(timestamp), (f ? '.' : ' '), (w ? ' ' : '.'), (n ? ' ' : '.'));
}

void createHString(char *pH, char *pL)
{
	for (; *pL != '\0'; pL++)
		*pH++ = *pL | 0x80; // offset character
	*pH = '\0';				// terminate the string
}

void processSyncEvent(NTPSyncEvent_t ntpEvent)
{
	if (ntpEvent < 0)
	{
		Serial.printf("Time Sync error: %d\n", ntpEvent);
		if (ntpEvent == noResponse)
			Serial.println("NTP server not reachable");
		else if (ntpEvent == invalidAddress)
			Serial.println("Invalid NTP server address");
		else if (ntpEvent == errorSending)
			Serial.println("Error sending request");
		else if (ntpEvent == responseError)
			Serial.println("NTP response error");
	}
	else
	{
		if (ntpEvent == timeSyncd)
		{
			Serial.print("Got NTP time: ");
			Serial.println(NTP.getTimeDateString(NTP.getLastNTPSync()));
		}
	}
}

void setup(void)
{
#ifdef DEBUG_SERIAL
	DEBUG_SERIAL.begin(DEBUG_BAUDRATE); // Start Serial
	DEBUG_SERIAL.setDebugOutput(true);	// Debug ON
	DEBUG_SERIAL.setDebugOutput(false); // Debug OFF
#endif
	P.begin(MAX_CLK_ZONES);
	P.setZone(ZONE_LOWER, 0, ZONE_SIZE - 1);
	P.setZone(ZONE_UPPER, ZONE_SIZE, MAX_DEVICES - 1);
	P.setFont(numeric7SegDouble);
	P.setIntensity(0);						  // brightness
	P.setCharSpacing(P.getCharSpacing() * 2); // double height --> double spacing
	P.displayZoneText(ZONE_LOWER, szTimeL, PA_LEFT, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);
	P.displayZoneText(ZONE_UPPER, szTimeH, PA_LEFT, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);

	//-========================================================   RTC  =========================================================-
	Wire.begin(DS3231_SDA, DS3231_SCL); // Remap default pins. SDA/SCL default to pins 4 & 5 but any two pins can be assigned as SDA/SCL using Wire.begin(SDA,SCL)
	if (!RTC.begin())
	{
		DPRINTLNF("Couldn't find RTC");
		while (1)
			;
	}
	if (RTC.lostPower())
	{
		DPRINTLNF("\n\nRTC lost power, lets set the time!");
	}
	else if (RTC.now().unixtime() < TIME_NOW_UNIX) // protect from missing time
	{
		DPRINTLNF("\n\nRTC time not set!");
	}
	else
	{
		DPRINTLNF("\n\nBattery RTC is OK");
	}
	DEBUG("RTC time: %s\n", dateTimeToChar(RTC.now().unixtime()));
	DEBUG("NOW time: %s\n", dateTimeToChar(now()));
	// -======================================================== Time provider ==================================================-
	//    setSyncProvider(getNtpTime);                                              // Set up the internal clock synchronizing function with a Network Time Protocol (NTP) server
	setSyncProvider(syncProvider); // Set up the internal clock synchronizing function with a RTC
	setSyncInterval(TIME_SYNC_PERIOD);
	DEBUG("After setSyncProvider now(): %s\n", dateTimeToChar(now()));

	//DEBUG("Connect to SSID: %s with pass: %s\n", ssid, password);
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);

	if (WiFi.getAutoConnect() != true) //configuration will be saved into SDK flash area
	{
		WiFi.setAutoConnect(true);	 //on power-on automatically connects to last used hwAP
		WiFi.setAutoReconnect(true); //automatically reconnects to hwAP in case it's disconnected
	}

	//	sntp_setservername(0, (char *)"ntp2.vniiftri.ru");
	//	sntp_setservername(1, (char *)"ntp4.vniiftri.ru");
	//	sntp_set_timezone(TZ);
	//	sntp_init();

	NTP.setInterval(21633);
	NTP.setNTPTimeout(1500);
	NTP.begin("ntp2.vniiftri.ru", 5, false, 0);

	//	NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
	//        ntpEvent = event;
	//        ntpStatus = true;
	//    });

	currentMinute = minute();
	currentDay = day();
	//	DEBUG_PRINTLN(currentMinute);
	//	DEBUG_PRINTLN(currentDay);
	DEBUG("RTC time: %s\n", dateTimeToChar(RTC.now().unixtime()));
	DEBUG("NOW time: %s\n", dateTimeToChar(now()));

	httpUpdater.setup(&httpServer, update_path, update_username, update_password);
	httpServer.begin();
}

void loop(void)
{
	static uint32_t lastTime = 0;	// millis() memory
	static bool flasher = false;	// seconds passing flasher
	static bool wifiStatus = false; // seconds passing flasher

	httpServer.handleClient();
	P.displayAnimate();
	if (P.getZoneStatus(ZONE_LOWER) && P.getZoneStatus(ZONE_UPPER))
	{
		if (millis() - lastTime >= 1000)
		{
			lastTime = millis();
			getTime(szTimeL, flasher, wifiStatus, ntpStatus);
			createHString(szTimeH, szTimeL);
			flasher = !flasher;
			P.displayReset();
			P.synchZoneStart();
		}
	}

	// -====================================== A new minute has happened! Log, save and reset ====================================-
	if (currentMinute != minute())
	{
		currentMinute = minute();

		P.displayClear(); // test
		P.displayClear(); // test
		P.displayClear(); // test
		P.displayClear(); // test
		P.displayClear(); // test
		P.displayClear(); // test
		P.displayClear(); // test

		if (WiFi.status() != WL_CONNECTED)
		{
			wifiStatus = false;
		}
		else
		{
			wifiStatus = true;
			DEBUGLN("WiFi connect with IP: %s", WiFi.localIP().toString().c_str());
		}

		if (ntpEvent < 0)
		{
			ntpStatus = false;
		}
		else
		{
			ntpStatus = true;
		}
		processSyncEvent(ntpEvent);

		// -===================================================== the day changes ====================================================-
		if (currentDay != day())
		{
			currentDay = day();

			if (ntpEvent < 0)
			{
				ntpStatus = false;
			}
			else
			{
				syncRTCWithNTP(); // If Time need correction
			}
		}
	}
}
