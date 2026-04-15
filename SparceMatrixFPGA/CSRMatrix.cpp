#include "CSRMatrix.h"
#include <sstream>
#include <algorithm>
#include <charconv>
#include <bit>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <thread>

ui32 antiHammingWeight(const std::string& inputString, ui32 modulo) {
    ui32 weight = 0;
    for (char c : inputString) {
        if (c % modulo == 0 || c == ';' || c == ' ') ++weight;
    }
    return weight;
}

ui32 fastPow(ui32 base, ui32 exp) {
    ui32 result = 1;
    while (exp) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

void CSRMatrix::clean() {
    values.clear();
    values.shrink_to_fit();
    colIdx.clear();
    colIdx.shrink_to_fit();
    rowPtr.clear();
    rowPtr.shrink_to_fit();
    nnz = 0;
    nor = 0;
    noc = 0;
}

CSRMatrix::CSRMatrix():
    nnz(0),
    nor(0),
    noc(0),
    modulo(MAXVAL),
    maxRow(0),
    maxVal(0){}

CSRMatrix::~CSRMatrix() {
    clean();
}

CSRMatrix CSRMatrix::fromString(std::string &input, ui32 mod) {
    CSRMatrix result;
    result.modulo = mod;
    result.nor = std::ranges::count(input, ';') + 1;
    result.rowPtr.resize(result.nor + 1, 0);
    result.maxVal = 0;
    result.maxRow = 0;

    std::vector<i32> tempValues;
    std::vector<ui32> tempColIdx;
    ui32 currentCol = 0;
    ui32 row = 0;
    ui32 currentRowNNZ = 0;

    auto parseNumber = [&](std::string_view token) -> i32 {
        if (token.empty()) return 0;
        i32 val = 0;
        auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
        if (ec != std::errc()) return 0;
        val %= static_cast<i32>(mod);
        if (val < 0) val += static_cast<i32>(mod);
        return val;
    };

    for (size_t i = 0; i < input.size(); ) {
        if (input[i] == ';') {
            result.rowPtr[++row] = tempValues.size();
            if (currentRowNNZ > result.maxRow) result.maxRow = currentRowNNZ;
            currentRowNNZ = 0;
            currentCol = 0;
            ++i;
        } else if (input[i] == ' ') {
            ++currentCol;
            ++i;
        } else if (std::isdigit(input[i]) || input[i] == '-') {
            size_t start = i;
            while (i < input.size() && (std::isdigit(input[i]) || input[i] == '-')) ++i;
            i32 val = parseNumber(input.substr(start, i - start));
            if (val != 0) {
                tempValues.push_back(val);
                tempColIdx.push_back(currentCol);
                if (result.maxVal < static_cast<ui32>(val)) result.maxVal = val;
                ++currentRowNNZ;
            }
        } else {
            ++i;
        }
    }
    result.rowPtr[result.nor] = tempValues.size();
    if (currentRowNNZ > result.maxRow) result.maxRow = currentRowNNZ;
    result.nnz = tempValues.size();
    result.values.assign(tempValues.begin(), tempValues.end());
    result.colIdx.assign(tempColIdx.begin(), tempColIdx.end());
    result.noc = tempColIdx.empty() ? 0 : *std::ranges::max_element(tempColIdx) + 1;
    return result;
}

std::string CSRMatrix::toString() const {
    std::ostringstream oss;

    for (ui32 i = 0; i < nor; ++i) {
        ui32 start = rowPtr[i];
        ui32 end   = rowPtr[i + 1];
        ui32 k = start;

        for (ui32 j = 0; j < noc; ++j) {
            if (k < end && colIdx[k] == j) {
                oss << values[k];
                ++k;
            } else {
                oss << '0';
            }

            if (j != noc - 1) {
                oss << ' ';
            }
        }
        oss << '\n';
    }

    return oss.str();
}

static ui8 bits_for_max(ui32 max_val) {
    if (max_val == 0) return 1;
    return std::bit_width(max_val);
}

std::vector<ui8> CSRMatrix::serializeHeader() const {
    /*
    Заголовок
    nor = noc для определителя
    битность -- колво бит для представления значения
    4 байта на размерность, 4 байта на ненулевые, 2 байта на модуль,
    1 байт на битность значений
    1 байт на битность строк
    1 байт на битность столбцов
    */
    std::vector<ui8> header;
    header.reserve(4 + 4 + 2 + 1 + 1 + 1);

    auto write_u32 = [&](ui32 x) {
        for (int i = 0; i < 4; ++i)
            header.push_back(static_cast<ui8>((x >> (i * 8)) & 0xFF));
    };
    auto write_u16 = [&](ui16 x) {
        header.push_back(static_cast<ui8>(x & 0xFF));
        header.push_back(static_cast<ui8>((x >> 8) & 0xFF));
    };
    auto write_u8 = [&](ui8 x) { header.push_back(x); };

    write_u32(nor);
    write_u32(nnz);
    write_u16(static_cast<ui16>(modulo));

    ui8 bits_val = bits_for_max(maxVal);
    write_u8(bits_val);

    ui8 bits_col = bits_for_max(nor ? nor - 1 : 0);
    write_u8(bits_col);

    ui8 bits_row = bits_for_max(maxRow);
    write_u8(bits_row);

    return header;
}

std::vector<ui8> CSRMatrix::serializeBody() const {
    ui8 bits_val = bits_for_max(maxVal);
    ui8 bits_col = bits_for_max(nor ? nor - 1 : 0);
    ui8 bits_row = bits_for_max(maxRow);

    std::vector<ui32> vals_uint;
    vals_uint.reserve(nnz);
    for (ui32 v : values) {
        vals_uint.push_back(static_cast<ui32>(v));
    }

    std::vector<ui32> colIdx_uint;
    colIdx_uint.reserve(nnz);
    for (ui32 c : colIdx) {
        colIdx_uint.push_back(c);
    }

    std::vector<ui32> row_diffs;
    row_diffs.reserve(nor);
    for (ui32 i = 0; i < nor; ++i) {
        row_diffs.push_back(rowPtr[i + 1] - rowPtr[i]);
    }

    size_t total_bits = nnz * bits_val + nnz * bits_col + nor * bits_row;
    size_t total_bytes = (total_bits + 7) / 8;
    std::vector<ui8> body(total_bytes, 0);


    ui32 bit_pos = 0;
    auto pack = [&](const std::vector<ui32>& data, ui8 bits) {
        for (ui32 val : data) {
            for (ui8 b = 0; b < bits; ++b) {
                if ((val >> b) & 1) {
                    body[bit_pos >> 3] |= (1 << (bit_pos & 7));
                }
                ++bit_pos;
            }
        }
    };

    pack(vals_uint, bits_val);
    pack(colIdx_uint, bits_col);
    pack(row_diffs, bits_row);

    return body;
}

bool CSRMatrix::sendAndReceiveDeterminant(const std::string& portName, unsigned int baudRate, ui32& determinant) const {
    int fd = open(portName.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::cerr << "open failed: " << strerror(errno) << std::endl;
        return false;
    }

    termios tty{};
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

    auto header = serializeHeader();
    auto body = serializeBody();

    ui8 bits_val = bits_for_max(modulo - 1);
    size_t expectedBytes = (bits_val + 7) / 8;

    tty.c_cc[VMIN] = expectedBytes;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "tcsetattr: " << strerror(errno) << std::endl;
        close(fd);
        return false;
    }

    tcflush(fd, TCIOFLUSH);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    ssize_t written = write(fd, header.data(), header.size());
    if (written != static_cast<ssize_t>(header.size())) {
        std::cerr << "Error writing header" << std::endl;
        close(fd);
        return false;
    }
    written = write(fd, body.data(), body.size());
    if (written != static_cast<ssize_t>(body.size())) {
        std::cerr << "Error writing body" << std::endl;
        close(fd);
        return false;
    }

    std::vector<ui8> reply(expectedBytes);
    size_t received = 0;
    while (received < expectedBytes) {
        ssize_t n = read(fd, reply.data() + received, expectedBytes - received);
        if (n > 0) {
            received += n;
        } else if (n == 0) {
            break;
        } else {
            std::cerr << "Read error: " << strerror(errno) << std::endl;
            close(fd);
            return false;
        }
    }
    close(fd);

    if (received == expectedBytes) {
        determinant = 0;
        for (size_t i = 0; i < expectedBytes; ++i) {
            determinant |= (static_cast<int32_t>(reply[i]) << (i * 8));
        }
    }
    return received == expectedBytes;
}