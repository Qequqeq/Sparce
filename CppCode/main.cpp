#include <iostream>
#include <limits>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sys/select.h>
#include <chrono>


#include "CSRMatrix.h"

i32 modulo = 0;

bool sendAndReceive(const std::string& portName, unsigned int baudRate,
                    const std::vector<uint8_t>& header,
                    const std::vector<uint8_t>& body,
                    int timeoutMs = 3000) {
    int fd = open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::cerr << "open failed: " << strerror(errno) << std::endl;
        return false;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "tcgetattr: " << strerror(errno) << std::endl;
        close(fd);
        return false;
    }

    speed_t speed;
    switch (baudRate) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        default:
            std::cerr << "Unsupported baudrate\n";
            close(fd);
            return false;
    }
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(INLCR | ICRNL | IGNCR);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 10; // 1 sec inter-byte timeout

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "tcsetattr: " << strerror(errno) << std::endl;
        close(fd);
        return false;
    }

    // Очищаем буферы
    tcflush(fd, TCIOFLUSH);

    // Отправляем данные
    ssize_t written = write(fd, header.data(), header.size());
    if (written != (ssize_t)header.size()) {
        std::cerr << "Ошибка записи заголовка" << std::endl;
        close(fd);
        return false;
    }
    written = write(fd, body.data(), body.size());
    if (written != (ssize_t)body.size()) {
        std::cerr << "Ошибка записи тела" << std::endl;
        close(fd);
        return false;
    }
    std::cout << "Данные отправлены (" << header.size() + body.size() << " байт)" << std::endl;

    // ----- Читаем ответ, пока не получим все байты или не истечёт таймаут -----
    std::vector<uint8_t> reply;
    auto start = std::chrono::steady_clock::now();
    size_t expectedBytes = header.size() + body.size();

    while (reply.size() < expectedBytes) {
        char byte;
        ssize_t n = read(fd, &byte, 1);
        if (n == 1) {
            reply.push_back(static_cast<uint8_t>(byte));
            std::cout << "Получен байт: 0x" << std::hex << (int)byte << std::dec << std::endl;
        } else if (n == 0) {
            // Таймаут между байтами
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutMs) {
                std::cout << "Таймаут ожидания данных (" << timeoutMs << " мс)" << std::endl;
                break;
            }
            usleep(10000); // 10 мс
        } else {
            std::cerr << "Ошибка чтения: " << strerror(errno) << std::endl;
            break;
        }
    }

    close(fd);

    // Вывод результата
    std::cout << "Получено " << reply.size() << " байт из " << expectedBytes << std::endl;
    if (!reply.empty()) {
        std::cout << "Дамп ответа: ";
        for (uint8_t b : reply) printf("%02X ", b);
        std::cout << std::endl;
    }

    return reply.size() == expectedBytes;
}

void printHexDump(const std::vector<uint8_t>& data, const std::string& label) {
    std::cout << "--- " << label << " (" << data.size() << " bytes) ---" << std::endl;
    for (size_t i = 0; i < data.size(); ++i) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) std::cout << std::endl;
    }
    std::cout << std::endl;
}

void formatMessage() {
    std::cout << "При вводе матрицы строки разделяются символом ;" << std::endl;
    std::cout << "Столбцы (соседние элементы) разделяются пробелом" << std::endl;
    std::cout << "Например, запись вида" << std::endl;
    std::cout << "1 0 0;0 1 0;0 0 1" << std::endl;
    std::cout << "Соответствует матрице" << std::endl;
    std::cout << "1\t0\t0" << std::endl;
    std::cout << "0\t1\t0" << std::endl;
    std::cout << "0\t0\t1" << std::endl;
    std::cout << std::endl;
}

void userInput() {
    ui32 matrixSide = 0;
    std::cout << "Введите сторону матрицы: ";
    std::cin >> matrixSide;

    if (std::cin.fail()) {
        std::cout << "Некорректный ввод" << std::endl;
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return;
    }

    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::cout << "Введите матрицу:" << std::endl;

    std::string inputString;

    std::getline(std::cin, inputString);

    if (std::cin.fail()) {
        std::cout << "Ошибка чтения строки" << std::endl;
        return;
    }

    try {
        CSRMatrix matrix = CSRMatrix::fromString(inputString, modulo);
        std::cout << matrix.toString();

    } catch (const std::bad_alloc& _) {
        std::cout << "Слишком много памяти, ошибка выделения" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "Ошибка при разборе матрицы: " << e.what() << std::endl;
    }
}

int main() {
    formatMessage();
    std::cout << "Максимальный модуль: " << MAXVAL << std::endl;
    std::cout << "Введите модуль для кольца чисел (0 для всего диапазона): ";
    std::cin >> modulo;
    if (std::cin.fail() || modulo < 0 || modulo >  MAXVAL) {
        std::cout << "Некорректный модуль" << std::endl;
        return 0;
    }
    if (!modulo) modulo = MAXVAL;

    std::string testMatrix = "1 0 0;0 2 0;0 0 9999";
    CSRMatrix matrix = CSRMatrix::fromString(testMatrix, modulo);
    std::cout << matrix.toString();

    ui32 determinant = 0;

    if (matrix.sendAndReceiveDeterminant("/dev/ttyUSB0", 115200, determinant)) {
        std::cout << "Определитель: " << determinant << std::endl;
    } else {
        std::cout << "Обмен не удался" << std::endl;
    }

    return 0;
}