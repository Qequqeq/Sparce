#ifndef SPARCEMATRIXFPGA_CSRMATRIX_H
#define SPARCEMATRIXFPGA_CSRMATRIX_H
#include <string>
#include <vector>


using i32 = int32_t;
using ui32 = uint32_t;
using ui8 = uint8_t;
using ui16 = uint16_t;

constexpr ui32 MAXVAL = 46340;

ui32 fastPow(ui32 base, ui32 exp);

ui32 antiHammingWeight(std::string& inputString, ui32 modulo);

class CSRMatrix {
private:
    std::vector<i32> values;
    std::vector<ui32> colIdx;
    std::vector<ui32> rowPtr;

    ui32 nnz; // Number of Non Zero values
    ui32 nor; // Number Of Rows
    ui32 noc; // Number Of Cols
    ui32 modulo;

    //for serializing
    ui32 maxRow;
    ui32 maxVal;

    void clean();

    [[nodiscard]] std::vector<ui8> serializeHeader() const;
    [[nodiscard]] std::vector<ui8> serializeBody() const;
public:
    CSRMatrix();
    ~CSRMatrix();

    static CSRMatrix fromString(std::string& inputString, ui32 mod);
    [[nodiscard]] std::string toString() const;

    [[nodiscard]] bool sendAndReceiveDeterminant(const std::string& portName, unsigned int baudRate, ui32& determinant) const;
};


#endif //SPARCEMATRIXFPGA_CSRMATRIX_H