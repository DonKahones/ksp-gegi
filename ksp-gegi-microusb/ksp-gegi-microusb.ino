
#include <Arduino.h>
#include <Wire.h>
// https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads
#include <LiquidCrystal_I2C.h> // Using version 1.2.1
// https://github.com/MHeironimus/ArduinoJoystickLibrary
#include <Joystick.h>

// IDE: Arduino/Genuino Micro

// potencjometr: niebieski z czarną kreską do GND, biały do +5V, niebieski do pinu A0 / A1
// LCD: SDA->2, SCL->3

// Stage/Abort status (on=stage, off=abort)
uint8_t stageabort = 1;
// joy1 switch status - change meaning of joy2 axes (X:on=rotate, off=axis; Y:on=hatswitch#0, off=rudder)
uint8_t joy1switch = 0;
// joy2 switch status - change meaning of joy1 axes (X:on=rotate, off=axis; Y:on=rotate, off=axis)
uint8_t joy2switch = 0;

// almost copy from ksp-gegi-mini!

// used from within digitalPin class (not nice, I know!)
void handleSlaveButton(const uint8_t id, const uint8_t state);

class digitalPin {
  public:
    digitalPin(const uint8_t id, const uint8_t pinSwitch);
    void updateSwitch();
    uint8_t getId() { return(m_id); }
    uint8_t getSwitchState() const { return(m_lastPinState); }
  private:
    const uint8_t m_id, m_pinSwitch;
    uint8_t m_lastPinState { HIGH } ;
};

digitalPin digiPins[] = {
  digitalPin(10,5),    // joy1switch
  digitalPin(11,7)	   // joy2switch
};

digitalPin::digitalPin(const uint8_t id, const uint8_t pinSwitch) :
  m_id(id), m_pinSwitch(pinSwitch)
{
  if (m_pinSwitch>0) {
    pinMode(m_pinSwitch,INPUT_PULLUP);
  }
}

void digitalPin::updateSwitch() {
  if (m_pinSwitch>0) {
    // reverse logic because switches are connected to GND with internal pullup, so pushed state (ON) is 0V
    uint8_t state = !digitalRead(m_pinSwitch);
    if (state!=m_lastPinState) {
      Serial.write('D');
      Serial.print(m_id);
      Serial.write('=');
      Serial.println(state);
      m_lastPinState = state;
      // map to joystick events
      handleSlaveButton(m_id,m_lastPinState);
    }
  }
}

/////////////

class analogInPin {
  public:
    analogInPin(const uint8_t id, const uint8_t pin, const int threshold);
    void update();
  private:
    void updateJoystick() const;
    const uint8_t m_id, m_pin;
    const int m_threshold;
    int m_lastAValue { 0 };
};

analogInPin analogInPins[] = {
  analogInPin(0,A6,2),  // throttle
  analogInPin(1,A8,5),	// timewarp
  analogInPin(2,A0,2),	// joy1x
  analogInPin(3,A1,2),	// joy1y
  analogInPin(4,A2,2),	// joy2x
  analogInPin(5,A3,2),	// joy2y
};

analogInPin::analogInPin(const uint8_t id, const uint8_t pin, const int threshold) :
	m_id(id), m_pin(pin), m_threshold(threshold)
{
}

void analogInPin::update() {
	// map to the range of the analog out
	int aValue = map(analogRead(m_pin), 0, 1023, 0, 255);
	// is it different enough from the last reading?
	if (abs(aValue-m_lastAValue)>m_threshold) {
		if (aValue<m_threshold) {
			aValue = 0;
		}
		if (aValue>255-m_threshold) {
			aValue = 255;
		}
		// dump status
		Serial.print("P");
		Serial.print(m_id);
		Serial.print("=");
		Serial.println(aValue,HEX);
		m_lastAValue = aValue;
		// map to joystick events
		updateJoystick();
	}
}

void analogInPin::updateJoystick() const {
	switch (m_id) {
		case 0: // throttle pot
			Joystick.setThrottle(m_lastAValue);
			break;
		case 2:	// joy1x X on: rotation, off: axis
			if (joy2switch) {
				Joystick.setXAxisRotation(m_lastAValue-127);
			} else {
				Joystick.setXAxis(m_lastAValue-127);
			}
			break;
		case 3: // joy1y Y on: rotation, off: axis
			if (joy2switch) {
				Joystick.setYAxisRotation(m_lastAValue-127);
			} else {
				Joystick.setYAxis(m_lastAValue-127);
			}
			break;
		case 4: // joy2x Z on: rotation, off: axis
			if (joy1switch) {
				Joystick.setZAxisRotation(m_lastAValue-127);
			} else {
				Joystick.setZAxis(m_lastAValue-127);
			}
			break;
		case 5: // joy2y on: ignore, off: rudder
			if (joy1switch) {
			} else {
				Joystick.setRudder(m_lastAValue);
			}
			break;
	}
}

class analogOutPin {
  public:
    analogOutPin(const uint8_t id, const uint8_t pin);
    void updateState(const uint8_t val) const ;
    const uint8_t getId() { return(m_id); }
  private:
    const uint8_t m_id, m_pin;
};

analogOutPin::analogOutPin(const uint8_t id, const uint8_t pin) :
	m_id(id), m_pin(pin)
{
}

void analogOutPin::updateState(const uint8_t val) const {
  analogWrite(m_pin,val);
}

analogOutPin analogOutPins[] = {
  analogOutPin(0,9),	// g-force
  analogOutPin(1,10)    // electrical power
};

// Set the pins on the I2C chip used for LCD connections:
//                    addr, en,rw,rs,d4,d5,d6,d7,bl,blpol
LiquidCrystal_I2C lcd(0x3f, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

// serial port
#define SERIAL_SPEED 115200

void setup() {
  // initialize serial communications
  Serial.begin(SERIAL_SPEED);
  Serial1.begin(SERIAL_SPEED);
  // wait for USB device
  while (!Serial);
  // set timeout to 200ms
  Serial.setTimeout(200);
  // start blink counter
  lcd.begin(16,2);   // initialize the lcd for 16 chars 2 lines, turn on backlight
  lcd.setCursor(0,0);
  lcd.print("KSP-Gegi");
  lcd.setCursor(0,1);
  lcd.print("Ready for action");
  // joystick setup
  Joystick.begin(true);	// do auto send state
  // request status update
  Serial.println("I");
}

// commands like ^A{id}={val}$
void handleSerialInputAnalogOut() {
  const uint8_t id = Serial.parseInt();  // skip '='
  const uint8_t val = Serial.parseInt(); // skip newline
//  Serial.print(id); Serial.write(' '); Serial.println(val);
  for (auto &p : analogOutPins) {
    if (p.getId()==id) {
      p.updateState(val);
    }
  }
}

// commands like ^P{line:0,1}={text0..15}$ (consume all characters until end of the line)
void handleSerialInputLCD() {
  uint8_t i=0, c;
  const uint8_t id = Serial.parseInt();
  Serial.read(); // skip '='
//  Serial.print("LCDin"); Serial.println(id);
  if (id==0 || id==1) {
    lcd.setCursor(0,id);
    c = -1;
    while (c!='\r' && c!='\n') {
      if (Serial.available()>0) {
        c = Serial.read();
      } else {
        c = -1;
      }
      if (c>0 && c!='\r' && c!='\n' && i<16) {
        i++;
        lcd.write(c);
//        Serial.print(c,HEX);
      }
    }
    for (;i<16;i++) {
      lcd.write(' ');
    }
//    Serial.println("endLCD");
  }
}

// called after UART slave reports button press/release
// map switch IDs to joystick buttons
void handleSlaveButton(const uint8_t id, const uint8_t state) {
	// handle stage switch
	uint8_t buttonId = 0x80;
	switch (id) {
		case 4:
			buttonId = 5;
			break;
		case 5:
			buttonId = 4;
			break;
		case 6:
			buttonId = 2;
			break;
		case 7:
			buttonId = 3;
			break;
		case 8:
			buttonId = stageabort ? 1 : 0;
			break;
		case 9:
			stageabort = state;	// on=abort, off=stage
			break;
    case 10:
      buttonId = 6;
      break;
    case 11:
      buttonId = 7;
      break;
  }
//Serial.print("id,state,buttonid,stageabort="); Serial.print(id); Serial.write(' '); Serial.print(state); Serial.write(' '); Serial.print(buttonId); Serial.write(' '); Serial.println(stageabort);
	if (buttonId < 0x80) {
		Joystick.setButton(buttonId,state);
	}
}

// pass data from USB host (PC) to UART
// - handle own devices (consume this data)
// - pass everything else to mini slave
void checkSerialInputUSBtoUART() {
  uint8_t c;
  while (Serial.available()) {
    c = Serial.read();
    if (c == 'A') {
      handleSerialInputAnalogOut();
    }
    else if (c == 'P') {
      handleSerialInputLCD();
    }
	  else {
      Serial1.write(c);
	  }
  }
}

// pass data from UART slave (mini) to USB host (PC)
// - convert some button presses to joystick events
// - pass everyting else
void checkSerialInputUARTtoUSB() {
  uint8_t c;
  while (Serial1.available()) {
		c = Serial1.read();
		Serial.write(c);
		if (c == 'D') {
			const uint8_t id = Serial1.parseInt();  // skip '='
			const uint8_t state = Serial1.parseInt(); // skip newline
			Serial.print(id);
			Serial.write('=');
			Serial.println(state);
			handleSlaveButton(id,state);
		}
	}
}

void updateAnalogs() {
  for (auto &p : analogInPins) {
    p.update();
  }
}

void updatePins() {
  for (auto &p : digiPins) {
    p.updateSwitch();
	// remember state so analog axes are updated accordingly
	if (p.getId()==10) {
		joy1switch = p.getSwitchState();
	}
	if (p.getId()==11) {
		joy2switch = p.getSwitchState();
	}
  }
}

void loop() {
  checkSerialInputUSBtoUART();
  checkSerialInputUARTtoUSB();
  updatePins();
  updateAnalogs();
  delay(50);
}
