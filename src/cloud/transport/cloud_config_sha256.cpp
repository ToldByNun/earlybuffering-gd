#include "cloud_config_sha256.hpp"
#include <array>
#include <cstring>

std::string EbCloudConfigSha256::hashHex(void const* data, size_t length) {
    EbCloudConfigSha256Core core;
    if (data && length > 0) {
        core.update(static_cast<uint8_t const*>(data), length);
    }
    uint8_t dig[32];
    core.finalize(dig);
    static char const* hex = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (int index = 0; index < 32; ++index) {
        out[index * 2] = hex[(dig[index] >> 4) & 0xf];
        out[index * 2 + 1] = hex[dig[index] & 0xf];
    }
    return out;
}

std::string EbCloudConfigSha256::hashHex(std::string_view bytes) {
    return hashHex(bytes.data(), bytes.size());
}

bool EbCloudConfigSha256::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    // FIPS 180-2 / NIST examples.
    expect(hashHex(std::string_view("abc")) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "abc");
    expect(hashHex(std::string_view("")) ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "empty");
    expect(hashHex(std::string_view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
           "long56");
    expect(hashHex(std::string_view("x")).size() == 64, "len64");
    expect(hashHex(nullptr, 0).size() == 64, "null-empty");

    if (fails == 0) {
        report = "CloudConfigSha256 selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigSha256 selfCheck fails: ") + report;
    return false;
}

EbCloudConfigSha256Core::EbCloudConfigSha256Core() {
    this->reset();
}

auto EbCloudConfigSha256Core::reset() -> void {
    this->m_hashState = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    this->m_totalBytes = 0;
    this->m_bufferSize = 0;
}

auto EbCloudConfigSha256Core::update(uint8_t const* data, size_t length) -> void {
    this->m_totalBytes += length;
    while (length > 0) {
        size_t const count =
            (length < (64 - this->m_bufferSize)) ? length : (64 - this->m_bufferSize);
        std::memcpy(this->m_buffer.data() + this->m_bufferSize, data, count);
        this->m_bufferSize += count;
        data += count;
        length -= count;
        if (this->m_bufferSize == 64) {
            this->transform(this->m_buffer.data());
            this->m_bufferSize = 0;
        }
    }
}

auto EbCloudConfigSha256Core::finalize(uint8_t output[32]) -> void {
    uint64_t const bitLen = this->m_totalBytes * 8ull;
    this->m_buffer[this->m_bufferSize++] = 0x80;
    if (this->m_bufferSize > 56) {
        while (this->m_bufferSize < 64)
            this->m_buffer[this->m_bufferSize++] = 0;
        this->transform(this->m_buffer.data());
        this->m_bufferSize = 0;
    }
    while (this->m_bufferSize < 56)
        this->m_buffer[this->m_bufferSize++] = 0;
    for (int index = 7; index >= 0; --index) {
        this->m_buffer[this->m_bufferSize++] =
            static_cast<uint8_t>((bitLen >> (index * 8)) & 0xffu);
    }
    this->transform(this->m_buffer.data());
    for (int index = 0; index < 8; ++index) {
        output[index * 4 + 0] = static_cast<uint8_t>((this->m_hashState[index] >> 24) & 0xffu);
        output[index * 4 + 1] = static_cast<uint8_t>((this->m_hashState[index] >> 16) & 0xffu);
        output[index * 4 + 2] = static_cast<uint8_t>((this->m_hashState[index] >> 8) & 0xffu);
        output[index * 4 + 3] = static_cast<uint8_t>(this->m_hashState[index] & 0xffu);
    }
}

auto EbCloudConfigSha256Core::rotr(uint32_t x, uint32_t count) -> uint32_t {
    return (x >> count) | (x << (32u - count));
}

auto EbCloudConfigSha256Core::transform(uint8_t const* chunk) -> void {
    static uint32_t const roundConstants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
        0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
        0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
        0xc67178f2u};

    uint32_t messageSchedule[64];
    for (int index = 0; index < 16; ++index) {
        messageSchedule[index] =
            (uint32_t(chunk[index * 4]) << 24) | (uint32_t(chunk[index * 4 + 1]) << 16) |
            (uint32_t(chunk[index * 4 + 2]) << 8) | uint32_t(chunk[index * 4 + 3]);
    }
    for (int index = 16; index < 64; ++index) {
        uint32_t const smallSigmaZero = rotr(messageSchedule[index - 15], 7) ^
                                        rotr(messageSchedule[index - 15], 18) ^
                                        (messageSchedule[index - 15] >> 3);
        uint32_t const smallSigmaOne = rotr(messageSchedule[index - 2], 17) ^
                                       rotr(messageSchedule[index - 2], 19) ^
                                       (messageSchedule[index - 2] >> 10);
        messageSchedule[index] = messageSchedule[index - 16] + smallSigmaZero +
                                 messageSchedule[index - 7] + smallSigmaOne;
    }

    uint32_t workingA = this->m_hashState[0], workingB = this->m_hashState[1],
             workingC = this->m_hashState[2], workingD = this->m_hashState[3];
    uint32_t workingE = this->m_hashState[4], workingF = this->m_hashState[5],
             workingG = this->m_hashState[6], workingH = this->m_hashState[7];
    for (int index = 0; index < 64; ++index) {
        uint32_t const bigSigmaOne = rotr(workingE, 6) ^ rotr(workingE, 11) ^ rotr(workingE, 25);
        uint32_t const chooseBits = (workingE & workingF) ^ ((~workingE) & workingG);
        uint32_t const firstTemporary =
            workingH + bigSigmaOne + chooseBits + roundConstants[index] + messageSchedule[index];
        uint32_t const bigSigmaZero = rotr(workingA, 2) ^ rotr(workingA, 13) ^ rotr(workingA, 22);
        uint32_t const majorityBits =
            (workingA & workingB) ^ (workingA & workingC) ^ (workingB & workingC);
        uint32_t const secondTemporary = bigSigmaZero + majorityBits;
        workingH = workingG;
        workingG = workingF;
        workingF = workingE;
        workingE = workingD + firstTemporary;
        workingD = workingC;
        workingC = workingB;
        workingB = workingA;
        workingA = firstTemporary + secondTemporary;
    }
    this->m_hashState[0] += workingA;
    this->m_hashState[1] += workingB;
    this->m_hashState[2] += workingC;
    this->m_hashState[3] += workingD;
    this->m_hashState[4] += workingE;
    this->m_hashState[5] += workingF;
    this->m_hashState[6] += workingG;
    this->m_hashState[7] += workingH;
}
