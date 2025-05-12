#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

#define RST_PIN 9
#define SS_PIN  10

MFRC522 mfrc522(SS_PIN, RST_PIN);

// Order tracking 
int orders[5] = {0, 0, 0, 0, 0};
char *order_names[5]={"Resistor","Capacitor","Inductor","IC","Sensor"};
// --- Admin credentials ---
const String ADMIN_USER = "admin";
const String ADMIN_PASS = "1234"; 
bool adminMode = false;

// --- EEPROM layout: 5 entries × (4-byte UID + 1-byte itemNo) = 25 bytes ---
const int MAX_ENTRIES = 5;
const int ENTRY_SIZE  = 5;

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();

  Serial.println(F("System Ready."));
  printMenu();
}

// ---------------------------------------------------------------------------
void loop() {
  if (adminMode) {
    handleAdmin();
  } else {
    handleNormal();
  }
}


// NORMAL MODE: orders + dispatch

void handleNormal() {
  // 1) Order input
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '5') {
      int idx = c - '1';
      orders[idx]++;
      Serial.print(F(" Order placed for item "));
      Serial.print(order_names[idx]);
      Serial.print(F(". Total now: "));
      Serial.println(orders[idx]);
      printMenu();
    }
    else if (c == 'A') {
      enterAdminLogin();
    }
    else {
      Serial.println(F(" Invalid. 1–5 to order, A for admin."));
    }
  }

  // 2) RFID dispatch
  checkRFIDScan();
}

// ---------------------------------------------------------------------------
// ADMIN MODE: registration of up to 5 cards
// ---------------------------------------------------------------------------
void handleAdmin() {
  Serial.println(F("\n--- ADMIN MODE ---"));
  Serial.println(F("R = Register new card"));
  Serial.println(F("V = View Current orders"));
  Serial.println(F("X = Exit admin"));
  while (!Serial.available()) { }
  char cmd = Serial.read();
  
  if (cmd == 'R') {
    registerNewCard();
  }
  else if(cmd=='V'){
    int flag=0;
    for(int i=0;i<5;i++){
      if(orders[i]>0){
        Serial.print("Orders pending for \"");
        Serial.print(order_names[i]);
        Serial.print("\" is ");
        Serial.println((orders[i]));
        flag=1;
      }
    }
    if(flag==0){
      Serial.println("No orders are pending");
    }
  }
  else if (cmd == 'X') {
    adminMode = false;
    Serial.println(F("Exiting admin mode."));
    printMenu();
  }
  else {
    Serial.println(F("Unknown admin command."));
  }
}

// ---------------------------------------------------------------------------
// LOGIN FLOW
// ---------------------------------------------------------------------------
void enterAdminLogin() {
  Serial.println(F("Enter User ID:"));
  String uid = readStringFromSerial();
  Serial.println(F("Enter Password:"));
  String pwd = readStringFromSerial();

  if (uid == ADMIN_USER && pwd == ADMIN_PASS) {
    adminMode = true;
    Serial.println(F(" Admin authenticated."));
  } else {
    Serial.println(F(" Invalid credentials."));
    printMenu();
  }
}

// ---------------------------------------------------------------------------
// REGISTER FLOW
// ---------------------------------------------------------------------------
void registerNewCard() {
  // Find next free slot
  int slot = -1;
  for (int i = 0; i < MAX_ENTRIES; i++) {
    if (EEPROM.read(i * ENTRY_SIZE + 4) == 0xFF) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    Serial.println(F(" EEPROM full. Cannot register more."));
    return;
  }

  Serial.println(F("Scan new RFID card to register..."));
  while (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {}

  byte newUID[4];
  for (byte i = 0; i < 4; i++) {
    newUID[i] = mfrc522.uid.uidByte[i];
  }

  Serial.print(F("Scanned UID: "));
  for (byte i = 0; i < 4; i++) {
    if (newUID[i] < 0x10) Serial.print('0');
    Serial.print(newUID[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  // Check for duplicate UID
  if (findItemNumber(newUID) != -1) {
    Serial.println(F(" This RFID card is already registered."));
    return;
  }

  // Ask for item number
  Serial.println(F("Enter item number to associate (1–5):"));
  char c;
  do {
    while (!Serial.available()) {}
    c = Serial.read();
  } while (c < '1' || c > '5');
  byte itemNo = c - '0';

  // Check if item number is already used
  for (int i = 0; i < MAX_ENTRIES; i++) {
    int addr = i * ENTRY_SIZE;
    byte storedItem = EEPROM.read(addr + 4);
    if (storedItem == itemNo) {
      Serial.println(F(" This item number is already associated with another RFID card."));
      return;
    }
  }

  storeItemToEEPROM(newUID, itemNo, slot);

  Serial.print(F(" Registered slot "));
  Serial.print(slot + 1);
  Serial.print(F(" → item "));
  Serial.println(itemNo);

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}


// ---------------------------------------------------------------------------
// RFID DISPATCH FLOW (normal mode)
// ---------------------------------------------------------------------------
void checkRFIDScan() {
  if (!mfrc522.PICC_IsNewCardPresent() ||
      !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  byte scanUID[4];
  for (byte i = 0; i < 4; i++) {
    scanUID[i] = mfrc522.uid.uidByte[i];
  }

  Serial.print(F("Scanned UID: "));
  for (byte i = 0; i < 4; i++) {
    if (scanUID[i] < 0x10) Serial.print('0');
    Serial.print(scanUID[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  int itemNo = findItemNumber(scanUID);
  if (itemNo < 1) {
    Serial.println(F(" Unknown RFID tag."));
  } else {
    Serial.print(F(" Tag for item "));
    Serial.println(itemNo);
    int idx = itemNo - 1;
    if (orders[idx] > 0) {
      orders[idx]--;
      Serial.print(F("📦 Dispatched one. Remaining orders for item \""));
      Serial.print(order_names[idx]);
      Serial.print(F("\": "));
      Serial.println(orders[idx]);
    } else {
      Serial.println(F("  No pending orders for this item."));
    }
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

// ---------------------------------------------------------------------------
// EEPROM HELPERS
// ---------------------------------------------------------------------------
void storeItemToEEPROM(byte uid[4], byte itemNumber, byte index) {
  int addr = index * ENTRY_SIZE;
  for (int i = 0; i < 4; i++) {
    EEPROM.update(addr + i, uid[i]);
  }
  EEPROM.update(addr + 4, itemNumber);
}

// Return item number (1–5) or -1 if not found
int findItemNumber(byte scannedUID[4]) {
  for (int i = 0; i < MAX_ENTRIES; i++) {
    int addr = i * ENTRY_SIZE;
    bool match = true;
    for (int j = 0; j < 4; j++) {
      if (EEPROM.read(addr + j) != scannedUID[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      return EEPROM.read(addr + 4);
    }
  }
  return -1;
}

// UTILITIES

String readStringFromSerial() {
  String s = "";
  while (s.length() == 0) {
    while (Serial.available() == 0) { }
    s = Serial.readStringUntil('\n');
    s.trim();
  }
  return s;
}

void printMenu() {
  Serial.println(F("\n*************** MENU ***************"));
  Serial.println(F("A         : Admin login"));
  Serial.println(F("To order  "));
  Serial.println(F("Resistor  : Enter 1"));
  Serial.println(F("Capacitor : Enter 2"));
  Serial.println(F("Inductor  : Enter 3"));
  Serial.println(F("IC        : Enter 4"));
  Serial.println(F("Sensor    : Enter 5"));
  
  Serial.println(F("************************************"));
}
