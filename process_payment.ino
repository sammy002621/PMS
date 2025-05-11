#include <SPI.h>
    #include <MFRC522.h>
    
    #define RST_PIN 9
    #define SS_PIN 10
    
    MFRC522 rfid(SS_PIN, RST_PIN); // Create MFRC522 instance
    MFRC522::MIFARE_Key key;
    
    void setup() {
      Serial.begin(9600);
      SPI.begin();
      rfid.PCD_Init();
      Serial.println("RFID Ready. Scan a card...");
    
      // Prepare the default key (0xFF 0xFF 0xFF 0xFF 0xFF 0xFF)
      for (byte i = 0; i < 6; i++) {
        key.keyByte[i] = 0xFF;
      }
    }
    
    void loop() {
      // Check for a new card
      if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        return;
      }
    
      // Verify the UID (assuming 5B ED 5F 3B from previous context)
      byte expectedUID[] = {0x5B, 0xED, 0x5F, 0x3B};
      bool uidMatch = true;
      for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] != expectedUID[i]) {
          uidMatch = false;
          break;
        }
      }
    
      if (!uidMatch) {
        Serial.println("ERROR: Invalid UID");
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        return;
      }
    
      // Print card type for debugging
      MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
      Serial.print("Card Type: ");
      Serial.println(rfid.PICC_GetTypeName(piccType));
    
      // Authenticate for sector 1 (blocks 4-7) using Key A
      byte blockAddr = 4;
      Serial.println("Trying Key A...");
      MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(rfid.uid));
      if (status != MFRC522::STATUS_OK) {
        Serial.print("Key A Authentication failed: ");
        Serial.println(rfid.GetStatusCodeName(status));
    
        // Try Key B
        Serial.println("Trying Key B...");
        status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_B, blockAddr, &key, &(rfid.uid));
        if (status != MFRC522::STATUS_OK) {
          Serial.print("Key B Authentication failed: ");
          Serial.println(rfid.GetStatusCodeName(status));
          rfid.PICC_HaltA();
          rfid.PCD_StopCrypto1();
          return;
        } else {
          Serial.println("Key B Authentication successful!");
        }
      } else {
        Serial.println("Key A Authentication successful!");
      }
    
      // Read current license plate and cash amount
      byte buffer[18];
      byte size = 18;
      status = rfid.MIFARE_Read(blockAddr, buffer, &size);
      if (status != MFRC522::STATUS_OK) {
        Serial.println("ERROR: Read failed for block 4");
        return;
      }
      String licensePlate = "";
      for (byte i = 0; i < 16; i++) {
        if (buffer[i] != ' ') licensePlate += (char)buffer[i];
      }
      blockAddr = 5;
      status = rfid.MIFARE_Read(blockAddr, buffer, &size);
      if (status != MFRC522::STATUS_OK) {
        Serial.println("ERROR: Read failed for block 5");
        return;
      }
      String cashStr = "";
      for (byte i = 0; i < 16; i++) {
        if (buffer[i] != ' ') cashStr += (char)buffer[i];
      }
      long currentCash = cashStr.toInt();
    
      // Send plate and amount to Python
      Serial.print("DATA:");
      Serial.print(licensePlate);
      Serial.print(",");
      Serial.println(currentCash);
    
      // Wait for response from Python
      String response = "";
      while (Serial.available() == 0) {} // Wait for Python response
      while (Serial.available() > 0) {
        response += (char)Serial.read();
        delay(10); // Small delay to allow buffer to fill
      }
    
      if (response.startsWith("CHARGE:")) {
        long charge = response.substring(7).toInt();
        long newCash = currentCash - charge;
        if (newCash < 0) {
          Serial.println("ERROR: Insufficient funds");
          rfid.PICC_HaltA();
          rfid.PCD_StopCrypto1();
          return;
        }
    
        // Prepare and write new cash amount with retry
        String newCashStr = String(newCash);
        for (byte i = 0; i < 16; i++) {
          buffer[i] = (i < newCashStr.length()) ? newCashStr[i] : ' ';
        }
        blockAddr = 5;
        byte retryCount = 0;
        const byte maxRetries = 3;
        while (retryCount < maxRetries) {
          status = rfid.MIFARE_Write(blockAddr, buffer, 16);
          if (status == MFRC522::STATUS_OK) {
            break;
          }
          retryCount++;
          delay(100); // Wait before retry
        }
        if (status != MFRC522::STATUS_OK) {
          Serial.println("ERROR: Write failed");
          rfid.PICC_HaltA();
          rfid.PCD_StopCrypto1();
          return;
        }
    
        // Send DONE to Python
        Serial.println("DONE");
      } else {
        Serial.println("ERROR: Invalid response from Python");
      }
    
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      delay(1000);
    }