#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "esp_bt_main.h"

//Zmienne globalne
//Zmienne pamiętające stan
uint8_t StaryStanA = 0xFF;
uint8_t StaryStanB = 0xFF;

//Ustawienie świateł jako wyłączone (domyślnie)
bool swiatla[16] = {false};     //false = światło zgaszone, true = światło zapalone

//Rozdzielnia swiatel magistrala A 
uint8_t RozdzielniaA[8] = {2, 14, 8, 3, 4, 5, 13, 7};

//Rozdzielnia świateł magistrala B
uint8_t RozdzielniaB[8] = {1, 9, 10, 11, 0, 0, 0, 0};

//Czas potrzebny do odpalenia swiatel na schodach
const TickType_t KrokZapaleniaSchodow = pdMS_TO_TICKS(200); //Co ile ma się zapalić lampka
bool AnimacjaSchodow = false; //Zaczynamy z wyłączona animacją schodów
TickType_t StartSchodow = 0; //Czas kiedy kliknięto schody 
int AktualnyStopienSchodow = 0; //Licznik na jakim etapie animacji jesteśmy

//Sekcja Bramy 
typedef enum {                      //Słowna definicja stanu bramy
    brama_zamykanie,
    brama_otwieranie,
    brama_pauza_otwieranie,
    brama_pauza_zamykanie,
    brama_otwarta,
    brama_zamknieta
} stan_bramy;
volatile bool PilotOtwarcieBramy = false;
volatile bool PilotZamkniecieBramy = false;
const TickType_t okres_migania_ms = pdMS_TO_TICKS(500);
const TickType_t CzasAutoZamykaniaMS = pdMS_TO_TICKS(10000);
const TickType_t CzasPauzyBramyMS = pdMS_TO_TICKS(5000);


// ==========================================
// 1. KONFIGURACJA SPRZĘTOWA
// ==========================================
#define I2C_PORT I2C_NUM_0

// Lista adresów Twoich układów 
#define MCP_ADDR 0x20 // Ekspander wejść/wyjść 
#define PCA_ADDR 0x40 // Sterownik PWM/LED 

// ==========================================
// 2. FUNKCJE BAZOWE 
// ==========================================

// Inicjalizacja zasilania i pinów I2C w ESP32
void i2c_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 21,
        .scl_io_num = 22,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000 
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

// UNIWERSALNA FUNKCJA ZAPISU 
void i2c_write(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t data[] = {reg, val};
    i2c_master_write_to_device(I2C_PORT, addr, data, 2, 100 / portTICK_PERIOD_MS);
}

// UNIWERSALNA FUNKCJA ODCZYTU 
uint8_t i2c_read(uint8_t addr, uint8_t reg) {
    uint8_t data_rx = 0xFF;
    i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, &data_rx, 1, 100 / portTICK_PERIOD_MS);
    return data_rx;
}


//Funkcja do ustawiania wartości wyjścia na PCA
void pca_set_pwm(uint8_t channel, uint16_t on_time, uint16_t off_time) {
    uint8_t reg = 0x06 + 4 * channel;
    uint8_t data[] = {reg, on_time & 0xFF, on_time >> 8, off_time & 0xFF, off_time >> 8};
    i2c_master_write_to_device(I2C_PORT, PCA_ADDR, data, 5, 100 / portTICK_PERIOD_MS);
}



// ==========================================
// 3. Bluetooth
// ==========================================
void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    switch (event) {
        case ESP_SPP_INIT_EVT:
            esp_bt_gap_set_device_name("SmartHome"); 
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE); 
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "SPP_SERVER"); 
            break;

    // 3. EVENT: Przyszły dane

    case ESP_SPP_DATA_IND_EVT: {
        
        // Statyczny bufor pamięta dane między kolejnymi, pofragmentowanymi odebraniami
        static uint8_t bufor_bt[2];
        static uint8_t bufor_idx = 0;

        // Pętla przechodząca przez to, co akurat przyszło w rurze Bluetooth
        for (int i = 0; i < param->data_ind.len; i++) {
            bufor_bt[bufor_idx] = param->data_ind.data[i];
            bufor_idx++;

            // Kiedy uzbieramy równe 2 bajty, dopiero wtedy procesujemy logikę!
            if (bufor_idx == 2) {
                uint8_t NumerSwiatla = bufor_bt[0];
                uint8_t Wartosc = bufor_bt[1];
                
                bufor_idx = 0; // Opróżniamy koszyk na przyszłość

                // ===================================================
                // SCENARIUSZ A: PRZYSZEDŁ KOD 255 (Przycisk ON/OFF)
                // ===================================================
                if (Wartosc == 255) {
                    if (NumerSwiatla == 6) {
                        AnimacjaSchodow = true;
                        StartSchodow = xTaskGetTickCount();
                        AktualnyStopienSchodow = 0;
                        swiatla[0] = !swiatla[0];
                        swiatla[12] = !swiatla[12];
                        swiatla[13] = !swiatla[13];
                    } 
                    else if (NumerSwiatla == 5) { 
                        swiatla[5] = !swiatla[5];
                        swiatla[6] = !swiatla[6];
                        if (swiatla[5] == true){
                            pca_set_pwm(5, 0, 4095);
                            pca_set_pwm(6, 0, 4095);
                        } else {
                            pca_set_pwm(5, 0 , 0);
                            pca_set_pwm(6, 0, 0);
                        }
                    }
                    else if (NumerSwiatla < 16) { 
                        swiatla[NumerSwiatla] = !swiatla[NumerSwiatla];
                        if (swiatla[NumerSwiatla] == true) {
                            pca_set_pwm(NumerSwiatla, 0, 4095);
                        } else { 
                            pca_set_pwm(NumerSwiatla, 0, 0);
                        }
                    }
                    else if (NumerSwiatla == 99) { 
                        for (int x = 0; x < 16; x++) {
                            swiatla[x] = false;
                            pca_set_pwm(x, 0, 0);
                        }
                        AnimacjaSchodow = false;
                    }
                    else if(NumerSwiatla == 101){
                        PilotOtwarcieBramy = true;
                    }
                    else if(NumerSwiatla == 102){
                        PilotZamkniecieBramy = true;
                    }

                }
                // ===================================================
                // SCENARIUSZ B: SUWAK (Wartość od 0 do 100)
                // ===================================================
                else {
                    bool CzySwieci = (Wartosc > 0);
                    uint32_t NatezenieSwiatla = (Wartosc * 4095) / 100;

                    if (NumerSwiatla == 5) {
                        swiatla[5] = CzySwieci;
                        swiatla[6] = CzySwieci;
                        pca_set_pwm(5, 0, NatezenieSwiatla);
                        pca_set_pwm(6, 0, NatezenieSwiatla);
                    }
                    else if (NumerSwiatla != 6 && NumerSwiatla != 99 && NumerSwiatla < 16) {
                        swiatla[NumerSwiatla] = CzySwieci;
                        pca_set_pwm(NumerSwiatla, 0, NatezenieSwiatla);
                    }
                }
            } 
        } 
        break;
    } 
    default:
        break;
    }
}


// Sekcja kopiuj-wklej 
void start_bluetooth() {
    // 1. Pamięć NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    // 2. Zwalniamy RAM z nieużywanego BLE (to naprawiło błąd restartów!)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    // 3. Kontroler sprzętowy
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    
    // TUTAJ ZMIANA: Skoro w Menuconfig mamy Dual Mode (BTDM), tu też musi być BTDM
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));

    // 4. Bluedroid (Oprogramowanie)
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // 5. Rejestracja zdarzeń SPP
    ESP_ERROR_CHECK(esp_spp_register_callback(esp_spp_cb));
    
    esp_spp_cfg_t bt_spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = false,
        .tx_buffer_size = 0,
    };
    ESP_ERROR_CHECK(esp_spp_enhanced_init(&bt_spp_cfg));
}

// ==========================================

/* ==========================================
    Opis wejść i wyjść:
    PCA:
    0 Schody góra korytarz
    1 Spiżarnia
    2 Kuchnia
    3 Garaż
    4 Gabinet
    5 Salon (światło 2)
    6 Salon (światło 1)
    7 Korytarz
    8 Łazienka
    9 Strych
    10 Strych schody
    11 Siłownia
    12 Schody środek korytarz
    13 Schody dół korytarz
    14 Sypialnia
    15 Serwo - Brama

    MCP:
    A0 Kuchnia
    A1 Sypialnia
    A2 Łazienka
    A3 Garaż
    A4 Gabinet
    A5 Salon
    A6 Schody korytarz
    A7 korytarz

    B0 Spiżarnia
    B1 Strych
    B2 Schody strych
    B3 Siłownia
    B4 Kontaktron Otwarcie
    B5 Kontaktron Zamknięcie
    B6 Bariera IR
    B7 Kogut na bramie
========================================== */

void app_main() {

    start_bluetooth();

    i2c_init();

    //Ustawienie MCP jako wejścia i ustawienie rezystorów podciągających
    i2c_write(MCP_ADDR, 0x00, 0xFF);
    i2c_write(MCP_ADDR, 0x01, 0x7F);
    i2c_write(MCP_ADDR, 0x0C, 0xFF);
    i2c_write(MCP_ADDR, 0x0D, 0x7F);

    //Ustawienie PCA jako wyjścia
    i2c_write(PCA_ADDR, 0x00, 0x20);

    //Ustawienie wszystkich świateł jako zgaszone
    for (int i = 0; i < 16; i++) {
        pca_set_pwm(i, 0, 0);
    }

    //Inicjalizacja bramy
    TickType_t AktualnyCzasDiody = xTaskGetTickCount();
    __attribute__((unused)) TickType_t OstatniCzasMignieciaDiody = AktualnyCzasDiody;
    TickType_t CzasAutoZamykania = 0;
    TickType_t CzasPauzyBramy = 0;
    TickType_t CzasPauzyOtwierania = 0;
    stan_bramy AktualnyStanBramy = brama_zamknieta;




    while (1) {
        AktualnyCzasDiody = xTaskGetTickCount();

        uint8_t AktualnyStanA = i2c_read(MCP_ADDR, 0x12); //Czytamy port A
        uint8_t AktualnyStanB = i2c_read(MCP_ADDR, 0x13); //Czytamy port B

        // --- DIAGNOSTYKA: Daje nam informacje, jakie porty są aktywne
        static TickType_t OstatniCzasSkanowania = 0;
        if (AktualnyCzasDiody - OstatniCzasSkanowania >= pdMS_TO_TICKS(1000)) {
            printf("\n==== SKAN PORTOW MCP23017 ====\n");
            printf(" - B4 (Kontaktron Otwarcie): %d\n", (AktualnyStanB & (1<<4)) ? 1 : 0);
            printf(" - B5 (Kontaktron Zamkniecie): %d\n", (AktualnyStanB & (1<<5)) ? 1 : 0);
            printf(" - B6 (Bariera IR): %d\n", (AktualnyStanB & (1<<6)) ? 1 : 0);
            printf("==============================\n");
            OstatniCzasSkanowania = AktualnyCzasDiody;
        }
        // ---------------------------------------------------

        //==========================
        // SEKCJA PORTÓW A
            for (int i = 0; i < 8; i++){
                if (StaryStanA & (1 << i) && !(AktualnyStanA & (1 << i))){
                    //Schody Korytarz
                    if (i == 6) {
                        AnimacjaSchodow = true;
                        StartSchodow = xTaskGetTickCount();
                        AktualnyStopienSchodow = 0;

                        //Przełączanie całej grupy świateł na raz
                        swiatla[0] = !swiatla[0];
                        swiatla[12] = !swiatla[12];
                        swiatla[13] = !swiatla[13];

                    //Salon
                    }
                    else if (i == 5) {
                        //Przełączanie całej grupy świateł na raz
                        swiatla[5] = !swiatla[5];
                        swiatla[6] = !swiatla[6];

                        if (swiatla[5] == true){
                            pca_set_pwm(5, 0, 4095);
                            pca_set_pwm(6, 0, 4095);
                        }
                        else {
                            pca_set_pwm(5, 0 , 0);
                            pca_set_pwm(6, 0, 0);
                        }
                    }
                    //Standardowe pokoje z jednym światłem
                    else {
                        uint8_t NumerSwiatla = RozdzielniaA[i];
                        swiatla[NumerSwiatla] = !swiatla[NumerSwiatla];

                    if (swiatla[NumerSwiatla] == true){
                        pca_set_pwm(NumerSwiatla, 0, 4095);
                    }
                    else {
                        pca_set_pwm(NumerSwiatla, 0, 0);
                    }
                }
                }
                
            }

            //SEKCJA OPDPOWIADAJĄCA ZA ANIMACJE ŚWIATEŁ PRZY SCHODACH
            
            if (AnimacjaSchodow == true){
                            TickType_t Teraz = xTaskGetTickCount();
                            TickType_t Roznica = Teraz - StartSchodow;

                        if (AktualnyStopienSchodow == 0){
                            if(swiatla[13] == true){        //Sprawdzamy, czy światła są ustawione na zapalone
                                pca_set_pwm(13, 0, 4095);   //Jeśli są to włączamy światło
                            }
                            else {                          //Jeżeli światła są ustawione na zgaszone
                                pca_set_pwm(13, 0, 0);      //To gasimy światło
                            }
                            AktualnyStopienSchodow = 1;     //Przechodzimy do kolejnego etapu animacji
                        }

                        if (AktualnyStopienSchodow == 1 && Roznica >= KrokZapaleniaSchodow){    //Sprawdzamy czy mamy odpowiedni stopień animacji oraz czy minął odpowiedni czas
                            if(swiatla[12] == true){
                                pca_set_pwm(12, 0, 4095);
                            }
                            else {
                                pca_set_pwm(12, 0, 0);
                            }
                            AktualnyStopienSchodow = 2;
                        }

                        if (AktualnyStopienSchodow == 2 && Roznica >= (KrokZapaleniaSchodow *2)){
                            if (swiatla[0] == true){
                                pca_set_pwm(0, 0, 4095);
                            }
                            else {
                                pca_set_pwm(0, 0, 0);
                            }
                            AnimacjaSchodow = false;
                        }
                        }
            //==============================

            //==============================
            //SEKCJA PORTÓW B
            for (int i = 0; i < 4; i++){ //Zmienione na 4, żeby nie spamić pływającymi portami
                if (StaryStanB & (1 << i) && !(AktualnyStanB & (1 << i))){
                    
                        uint8_t NumerSwiatla = RozdzielniaB[i];             
                        swiatla[NumerSwiatla] = !swiatla[NumerSwiatla];

                    if (swiatla[NumerSwiatla] == true){
                        pca_set_pwm(NumerSwiatla, 0, 4095);
                    }
                    else {
                        pca_set_pwm(NumerSwiatla, 0, 0);
                    }
                }
            }
            //==============================

            StaryStanA = AktualnyStanA;
            StaryStanB = AktualnyStanB;

    //Sekcja Zarządzania Bramą
    switch (AktualnyStanBramy) {
        
        //==========================
        // Brama OTWARTA
        //==========================
        case brama_otwarta:
            if(PilotZamkniecieBramy == true){           
                AktualnyStanBramy = brama_zamykanie;
                PilotZamkniecieBramy = false; // Reset flagi po zużyciu
            }
            // Zamknij bramę jeśli minie ustalony czas auto zamykania
            else if ((AktualnyCzasDiody - CzasAutoZamykania) >= CzasAutoZamykaniaMS){    
                AktualnyStanBramy = brama_zamykanie;
            }
            break;

        //==========================
        // Brama ZAMKNIĘTA
        //==========================
        case brama_zamknieta:
            if(PilotOtwarcieBramy == true){                     
                AktualnyStanBramy = brama_otwieranie;
                PilotOtwarcieBramy = false; // Reset flagi
            }
            // Zabezpieczenie: czy brama jest zamknięta?
            else if (!(AktualnyStanB & (1<<5))){
                AktualnyStanBramy = brama_zamknieta; // Podtrzymanie
            }
            break;

        //==========================
        //Brama W TRAKCIE OTWIERANIA
        //==========================
        case brama_otwieranie: { 
            pca_set_pwm(15, 0, 400); 

            // Zadziałanie Bariery IR 
            if (AktualnyStanB & (1<<6)) {                   
                AktualnyStanBramy = brama_pauza_otwieranie;     
                CzasPauzyOtwierania = AktualnyCzasDiody;        
                pca_set_pwm(15, 0, 307);                
                break;
            }

            // Sygnał od kontaktronu otwarcia 
            else if (!(AktualnyStanB & (1<<4))){
                AktualnyStanBramy = brama_otwarta;
                pca_set_pwm(15, 0, 307);
                CzasAutoZamykania = AktualnyCzasDiody;
            }
            break;
        }

        //========================
        case brama_pauza_otwieranie:
            // Jeśli bariera IR wciąż przerwana 
            if (AktualnyStanB & (1<<6)){
                CzasPauzyOtwierania = AktualnyCzasDiody; // Resetuj timer, niech czeka
            }
            else if (AktualnyCzasDiody - CzasPauzyOtwierania >= CzasPauzyBramyMS){
                AktualnyStanBramy = brama_otwieranie;
            }
            break;
        
        //========================
        case brama_pauza_zamykanie:
            if (AktualnyStanB & (1<<6)){
                CzasPauzyBramy = AktualnyCzasDiody;
            }
            // Po przerwie na zamykaniu, brama wraca do otwierania
            else if (AktualnyCzasDiody - CzasPauzyBramy >= CzasPauzyBramyMS){
                AktualnyStanBramy = brama_otwieranie;
            }
            break;
        
        //========================
        case brama_zamykanie: {
            pca_set_pwm(15, 0, 200);

            // Czujnik zbliżenia IR 
            if (AktualnyStanB & (1<<6)){
                AktualnyStanBramy = brama_pauza_zamykanie;
                CzasPauzyBramy = AktualnyCzasDiody;
                pca_set_pwm(15, 0, 307);
                break;
            }

            // Kontaktron zamknięcia 
            else if (!(AktualnyStanB & (1<<5))){
                AktualnyStanBramy = brama_zamknieta;
                pca_set_pwm(15, 0, 307);
            }
            break;
        }
        
        default:
            break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}
}