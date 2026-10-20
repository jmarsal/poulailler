// ble_control.cpp
#include "ble_control.h"
#include "config.h"
#include "secrets.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Update.h>
#include <esp_gap_ble_api.h>
#include "datalog.h"

namespace {
  // Identifiants (UUID) propres à ce montage, choisis au hasard une fois pour toutes.
  const char* SERVICE_UUID = "6b1f0001-3c2d-4e5a-9b7c-5f0e2a9c4d10";
  const char* CMD_UUID     = "6b1f0002-3c2d-4e5a-9b7c-5f0e2a9c4d10";
  const char* STATE_UUID   = "6b1f0003-3c2d-4e5a-9b7c-5f0e2a9c4d10";
  const char* INFO_UUID    = "6b1f0004-3c2d-4e5a-9b7c-5f0e2a9c4d10";
  const char* CURVE_UUID   = "6b1f0005-3c2d-4e5a-9b7c-5f0e2a9c4d10";
  const char* OTA_UUID     = "6b1f0006-3c2d-4e5a-9b7c-5f0e2a9c4d10";
  const char* LOG_UUID     = "6b1f0007-3c2d-4e5a-9b7c-5f0e2a9c4d10";

  // Mise à jour en cours : les morceaux arrivent dans la tâche Bluetooth et sont
  // écrits aussitôt dans la zone libre de la mémoire flash (l'autre emplacement
  // de programme). Le programme actuel n'est jamais touché pendant l'envoi.
  volatile bool otaOn = false;
  volatile bool otaFailed = false;
  volatile size_t otaBytes = 0;
  size_t otaSize = 0;

  BLECharacteristic* stateChar = nullptr;
  BLECharacteristic* infoChar = nullptr;
  BLECharacteristic* curveChar = nullptr;
  volatile int connections = 0;      // jusqu'à 4 liaisons simultanées

  // Les commandes arrivent dans la tâche Bluetooth : on les range sous verrou
  // dans une petite file, et loop() les récupère dans l'ordre.
  constexpr int QUEUE = 6, CMD_LEN = 40;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  char queue[QUEUE][CMD_LEN];
  int qHead = 0, qCount = 0;

  class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
      ++connections;
      // Reste visible : si une ancienne liaison traîne (page rechargée, iPhone
      // qui n'a pas raccroché), le téléphone peut quand même se reconnecter.
      BLEDevice::startAdvertising();
    }
    void onDisconnect(BLEServer* server) override {
      if (connections > 0) --connections;
      // Plus aucun téléphone : un envoi de programme en cours est abandonné.
      if (connections == 0 && otaOn) { Update.abort(); otaOn = false; }
      server->startAdvertising();   // redevient visible pour la connexion suivante
    }
  };

  // Appairage : si un téléphone échoue (mauvais code, ou clé périmée après
  // « Oublier cet appareil » sur l'iPhone), la carte efface ses anciennes clés.
  // L'essai suivant repart d'un appairage propre ; le code reste exigé.
  void removeAllBonds() {
    int n = esp_ble_get_bond_device_num();
    if (n <= 0) return;
    esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * n);
    if (!list) return;
    if (esp_ble_get_bond_device_list(&n, list) == ESP_OK)
      for (int i = 0; i < n; ++i) esp_ble_remove_bond_device(list[i].bd_addr);
    free(list);
  }

  class SecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return BLE_PIN; }
    void onPassKeyNotify(uint32_t) override {}
    bool onSecurityRequest() override { return true; }
    bool onConfirmPIN(uint32_t) override { return false; }   // pas de comparaison de nombres
    void onAuthenticationComplete(esp_ble_auth_cmpl_t r) override {
      if (r.success) return;
      Serial.printf("Bluetooth : appairage refusé (raison 0x%02x), anciennes clés effacées\n", r.fail_reason);
      removeAllBonds();
    }
  };

  class CmdCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
      String v = String(c->getValue().c_str());   // marche avec les cœurs 2.x et 3.x
      portENTER_CRITICAL(&mux);
      if (qCount < QUEUE) {
        char* slot = queue[(qHead + qCount) % QUEUE];
        strncpy(slot, v.c_str(), CMD_LEN - 1);
        slot[CMD_LEN - 1] = 0;
        ++qCount;
      }
      portEXIT_CRITICAL(&mux);
    }
  };

  // Journal : le téléphone écrit un numéro de mesure (4 octets), puis lit la
  // réponse préparée ici : en-tête + jusqu'à 30 mesures à partir de ce numéro.
  constexpr size_t LOG_REPLY = 16 + 30 * sizeof(DataLog::Record);   // 496 octets
  uint8_t logReply[LOG_REPLY];

  class LogCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
      uint32_t from = 0;
      const uint8_t* d = c->getData();
      const size_t len = c->getLength();
      for (size_t i = 0; i < 4 && i < len; ++i) from |= (uint32_t)d[i] << (8 * i);
      const size_t n = DataLog::read(from, logReply, sizeof(logReply));
      c->setValue(logReply, n);
    }
  };

  class OtaCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
      if (!otaOn || otaFailed) return;
      uint8_t* data = c->getData();
      size_t len = c->getLength();
      if (otaBytes + len > otaSize || Update.write(data, len) != len) otaFailed = true;
      else otaBytes += len;
    }
  };

  BLECharacteristic* readOnly(BLEService* svc, const char* uuid, bool notify) {
    uint32_t props = BLECharacteristic::PROPERTY_READ | (notify ? BLECharacteristic::PROPERTY_NOTIFY : 0);
    BLECharacteristic* c = svc->createCharacteristic(uuid, props);
    c->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
    if (notify) {
      BLE2902* cccd = new BLE2902();   // permet au téléphone de s'abonner aux mises à jour
      cccd->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
      c->addDescriptor(cccd);
    }
    return c;
  }
}

namespace BleControl {
  void begin() {
    BLEDevice::init(BLE_NAME);
    BLEDevice::setMTU(517);   // gros paquets : l'iPhone en accepte environ 180 octets

    // Sécurité : appairage par code à 6 chiffres (secrets.h), connexion chiffrée.
    // L'iPhone demande le code une seule fois, puis se souvient de l'appareil (« bond »).
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLESecurity* security = new BLESecurity();
    security->setStaticPIN(BLE_PIN);
    security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    // 6 caractéristiques, dont une avec descripteur : il faut plus de place que les 15 handles par défaut.
    BLEService* service = server->createService(BLEUUID(SERVICE_UUID), 24);

    // L'iPhone garde en mémoire la position de chaque caractéristique. Toute nouvelle
    // caractéristique doit être créée APRÈS les autres (à la fin de cette liste) :
    // sinon les anciennes changent de place et l'iPhone lit au mauvais endroit
    // jusqu'à « Oublier cet appareil ».
    BLECharacteristic* cmd = service->createCharacteristic(
        CMD_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    cmd->setCallbacks(new CmdCallbacks());
    // Écriture refusée tant que le téléphone n'est pas appairé avec le bon code.
    cmd->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);

    BLECharacteristic* ota = service->createCharacteristic(
        OTA_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    ota->setCallbacks(new OtaCallbacks());
    ota->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);

    BLECharacteristic* logChar = service->createCharacteristic(
        LOG_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    logChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
    logChar->setCallbacks(new LogCallbacks());
    logChar->setValue(logReply, DataLog::read(0, logReply, 16));   // en-tête seul au départ

    stateChar = readOnly(service, STATE_UUID, true);
    infoChar = readOnly(service, INFO_UUID, false);
    curveChar = readOnly(service, CURVE_UUID, false);
    stateChar->setValue("{}");
    infoChar->setValue("{}");

    service->start();
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
  }

  bool pollCommand(String& out) {
    char copy[CMD_LEN];
    bool got = false;
    portENTER_CRITICAL(&mux);
    if (qCount > 0) {
      memcpy(copy, queue[qHead], CMD_LEN);
      qHead = (qHead + 1) % QUEUE;
      --qCount;
      got = true;
    }
    portEXIT_CRITICAL(&mux);
    if (got) out = String(copy);
    return got;
  }

  void publishState(const String& json) {
    if (!stateChar) return;
    stateChar->setValue(json.c_str());
    if (connections > 0) stateChar->notify();
  }

  void setInfo(const String& json) {
    if (infoChar) infoChar->setValue(json.c_str());
  }

  void setCurve(const uint8_t* data, size_t len) {
    if (curveChar) curveChar->setValue((uint8_t*)data, len);
  }

  bool connected() { return connections > 0; }

  void clearBonds() { removeAllBonds(); }

  String otaBegin(size_t size) {
    if (otaOn) { Update.abort(); otaOn = false; }
    if (size < 1024) return "mise à jour : taille invalide";
    if (!Update.begin(size, U_FLASH)) return "mise à jour : refusée (" + String(Update.errorString()) + ")";
    otaSize = size;
    otaBytes = 0;
    otaFailed = false;
    otaOn = true;
    return "mise à jour : prête";
  }

  bool otaEnd(String& msg) {
    if (!otaOn) { msg = "mise à jour : rien en cours"; return false; }
    otaOn = false;
    if (otaFailed || otaBytes != otaSize) {
      Update.abort();
      msg = "mise à jour : échec, " + String(otaBytes) + " octets sur " + String(otaSize);
      return false;
    }
    // Update.end vérifie l'image reçue (en-tête et empreinte) avant de l'adopter.
    if (!Update.end(true)) {
      msg = "mise à jour : programme refusé (" + String(Update.errorString()) + ")";
      return false;
    }
    msg = "mise à jour réussie, redémarrage";
    return true;
  }

  void otaAbort() { if (otaOn) { Update.abort(); otaOn = false; } }
  bool otaActive() { return otaOn; }
  size_t otaReceived() { return otaBytes; }
}
