import serial
import time
from datetime import datetime

# Настройте COM-порт (может быть COM3, COM4, /dev/ttyACM0 и т.д.)
# Узнайте номер вашего COM-порта в Диспетчере устройств.
PORT = 'COM3'
BAUD_RATE = 9600


def run_time_server():
    """Слушает порт и отправляет текущее время при получении команды."""
    try:
        # Открываем последовательный порт
        ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
        print(f"Сервер времени запущен на {PORT} @ {BAUD_RATE} бод. Ожидание запроса...")

        while True:
            # Читаем данные из порта (до 100 байт)
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()

                # Если Arduino отправила команду "GET_TIME"
                if line == "GET_TIME":
                    # Получаем текущее время и форматируем его: ГГГГММДДччммсс
                    now = datetime.now()
                    time_data = "S" + now.strftime("%Y%m%d%H%M%S") # S20250101120000

                    # Отправляем время обратно Arduino
                    ser.write(time_data.encode('utf-8'))
                    print(f"Отправлено: {time_data}")

            time.sleep(0.1)

    except serial.SerialException as e:
        print(f"Ошибка COM-порта: {e}")
        # Попробуйте снова через 5 секунд, если порт занят/недоступен
        time.sleep(5)
        run_time_server()
    except KeyboardInterrupt:
        print("\nСервер остановлен пользователем.")
    except Exception as e:
        print(f"Произошла непредвиденная ошибка: {e}")


if __name__ == "__main__":
    run_time_server()