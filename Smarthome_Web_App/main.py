import eel
import socket
import sys

# --- KONFIGURACJA BLUETOOTH ---
MAC_MIKROKONTROLERA = "E0:8C:FE:5D:6C:FE" 
PORT_BLUETOOTH = 1 
bt_socket = None 

eel.init('web')

def polacz_z_bluetooth():
    global bt_socket
    if not MAC_MIKROKONTROLERA:
        print("Brak adresu MAC. Działam w trybie SYMULACJI (bez prawdziwego sprzętu).")
        return False
        
    try:
        print(f"Próba połączenia z {MAC_MIKROKONTROLERA}...")
        
        if not hasattr(socket, 'AF_BLUETOOTH'):
            print("BLAD: Twój Python na tym komputerze nie wspiera natywnie AF_BLUETOOTH.")
            return False
            
        bt_socket = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
        bt_socket.connect((MAC_MIKROKONTROLERA, PORT_BLUETOOTH))
        print("Sukces! Polaczono z mikrokontrolerem.")
        return True
    except Exception as e:
        print(f"Blad polaczenia: {e}")
        bt_socket = None
        return False

# --- WYSYŁANIE 1 BAJTU (WŁĄCZ/WYŁĄCZ) ---
@eel.expose
def wyslij_przez_bluetooth(komenda):
    global bt_socket
    
    if isinstance(komenda, int):
        # Wysyłanie 2-bajtowej paczki danych
        dane_do_wyslania = bytes([komenda, 255])
        print(f"[Terminal] Przycisk 0/1 - wysłano ID: {komenda}")
    elif isinstance(komenda, str):
        dane_do_wyslania = komenda.encode('utf-8')
        print(f"[Terminal] Interfejs wysłał tekst: {komenda}")
    else:
        print(f"[Terminal] Błąd: Nierozpoznany typ komendy: {type(komenda)}")
        return

    if bt_socket:
        try:
            bt_socket.send(dane_do_wyslania)
        except Exception as e:
            print(f" -> Blad wysylania: {e}")
    else:
        print(" -> (Symulacja BT) Dane nie opusciły komputera.")

# --- WYSYŁANIE 2 BAJTÓW (JASNOŚĆ) ---
@eel.expose
def ustaw_jasnosc_bt(id_swiatla, procent):
    global bt_socket
    
    # Tworzymy paczkę dwóch bajtów: np. [5, 70] (Salon na 70%)
    dane_do_wyslania = bytes([id_swiatla, int(procent)])
    
    print(f"[Terminal] Suwak - Światło ID: {id_swiatla} -> Jasność: {procent}%")

    if bt_socket:
        try:
            bt_socket.send(dane_do_wyslania)
        except Exception as e:
            print(f" -> Blad wysylania jasności: {e}")
    else:
        print(" -> (Symulacja BT) Jasność nie wysłana.")

if __name__ == '__main__':
    print("="*50)
    print("URUCHAMIANIE SYSTEMU SMARTHOME (Wersja z Jasnością)")
    print("="*50)
    
    polacz_z_bluetooth()
    
    if sys.platform.startswith('linux'):
        opcje_gpu = ['--ignore-gpu-blocklist', '--enable-gpu-rasterization', '--enable-zero-copy']
        eel.start('index.html', size=(1200, 800), cmdline_args=opcje_gpu)
    else:
        eel.start('index.html', size=(1200, 800))