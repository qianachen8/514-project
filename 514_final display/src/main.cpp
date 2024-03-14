#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AccelStepper.h>
#include <queue>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Define the stepper motor connections
#define motorPin1  GPIO_NUM_1 // Replace with the ESP32 pin connected to motor
#define motorPin2  GPIO_NUM_2 // Replace with the ESP32 pin connected to motor
#define motorPin3  GPIO_NUM_3 // Replace with the ESP32 pin connected to motor
#define motorPin4  GPIO_NUM_4 // Replace with the ESP32 pin connected to motor

// Initialize the stepper library on pins motorPin1 through motorPin4
AccelStepper stepper(AccelStepper::FULL4WIRE, motorPin1, motorPin3, motorPin2, motorPin4);

const int position9oclock = 0; // 9
const int position6oclock = -300; // 6

static BLEUUID serviceUUID("9421d390-7612-4ea9-ab96-c28e14e38fdf");
static BLEUUID charUUID("1b3a6e07-fcdc-416a-961b-7c2ed7482778");

std::queue<float> pHBuffer; // Queue to store recent pH readings
const int movingAverageWindow = 10; // Window size for moving average
float movingAverageSum = 0.0; // Sum for calculating the moving average

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

// Notify callback function to handle data received from the server
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)pData[i];
    }

    Serial.println(message);

 // Display the message on OLED
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    // Split the message by newline to separate turbidity from pH value
int newlineIndex = message.indexOf('\n');
if (newlineIndex != -1) {
    String turbidity = message.substring(0, newlineIndex);
    String phString = message.substring(newlineIndex + 1);
    display.setCursor(0,0);
    display.println(turbidity);

    // Stepper Motor Control based on pH value
    if (phString.indexOf("Acid") != -1 && stepper.currentPosition() != position6oclock) {
        stepper.moveTo(position6oclock);
    } else if (phString.indexOf("Natural") != -1 && stepper.currentPosition() != position9oclock) {
        stepper.moveTo(position9oclock);
    } else if (phString.indexOf("Basic") != -1 && stepper.currentPosition() != position12oclock) {
    stepper.moveTo(position12oclock);


    // Extracting and converting pH value and turbidity value
    float phValue = phString.substring(phString.indexOf(" ")).toFloat(); // Assuming format "pH X.XX"
    float turbidityValue = turbidity.substring(turbidity.indexOf(" ")).toFloat(); // Assuming format "Turbidity X.XX NTU"
    // Update the moving average buffer
    if (pHBuffer.size() >= movingAverageWindow) {
    movingAverageSum -= pHBuffer.front();
    pHBuffer.pop();   
    }
    pHBuffer.push(phValue);
    movingAverageSum += phValue;
    float movingAveragepH = movingAverageSum / pHBuffer.size();

    // Displaying pH value and turbidity value
    display.print("pH: ");
    display.println(phValue);

    display.print("Turbidity: ");
    display.print(turbidityValue);
    display.println(" NTU");
} else {
    // Fallback if the format is not as expected
    display.setCursor(0,0);
    display.println(message);
}

display.display();
}


class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnect");
  }
};

bool connectToServer() {
    Serial.println("Forming a connection to ");
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");
    pClient->setClientCallbacks(new MyClientCallback());
    pClient->connect(myDevice); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      return false;
    }
    Serial.println(" - Found our service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canRead()) {
      std::string value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
    }

    if(pRemoteCharacteristic->canNotify())
      pRemoteCharacteristic->registerForNotify(notifyCallback);

    connected = true;
    return true;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("BLE Advertised Device found: ");
        Serial.println(advertisedDevice.toString().c_str());

        // We have found a device, let us now see if it contains the service we are looking for.
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
            BLEDevice::getScan()->stop();
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
            doScan = false;
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("Starting Arduino BLE Client application...");
    BLEDevice::init("");

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Check your OLED I2C address
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Don't proceed, loop forever
    }
    display.display();
    delay(2000); // Pause for 2 seconds

    // Display initial text
    display.clearDisplay();
    display.setTextSize(1);      // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    display.setCursor(0,0);     // Start at top-left corner
    display.print(F("Scanning..."));
    display.display();

    stepper.setMaxSpeed(1000);
    stepper.setAcceleration(500);
    stepper.setCurrentPosition(0); // suppose current location is 9 clock
    stepper.moveTo(position9oclock); // if not 9 clock, move to 9clock

    // Start scanning for BLE servers and specify the callback function that will handle the results
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(5, false);
}

void loop() {
    if (doConnect == true) {
        if (connectToServer()) {
            Serial.println("We are now connected to the BLE Server.");
        } else {
            Serial.println("We have failed to connect to the server. Restarting scan...");
            BLEDevice::getScan()->start(0); // 0 = do not stop scanning after a period
        }
        doConnect = false;
    }
    stepper.run();
}