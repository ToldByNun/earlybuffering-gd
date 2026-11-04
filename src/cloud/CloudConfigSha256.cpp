#include "CloudConfigSha256.hpp"

#include <array>
#include <cstring>

class CloudConfigSha256Core {
public:
    CloudConfigSha256Core() {
        this->reset();
    }

    void reset() {
        this->m_h = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        };
        this->m_len = 0;
        this->m_bufLen = 0;
    }

    void update(uint8_t const* data, size_t len) {
        this->m_len += len;
        while (len > 0) {
            size_t const n = (len < (64 - this->m_bufLen)) ? len : (64 - this->m_bufLen);
            std::memcpy(this->m_buf.data() + this->m_bufLen, data, n);
            this->m_bufLen += n;
            data += n;
            len -= n;
            if (this->m_bufLen == 64) {
                this->transform(this->m_buf.data());
                this->m_bufLen = 0;
            }
        }
    }

    void finalize(uint8_t out[32]) {
        uint64_t const bitLen = this->m_len * 8ull;
        this->m_buf[this->m_bufLen++] = 0x80;
        if (this->m_bufLen > 56) {
            while (this->m_bufLen < 64) this->m_buf[this->m_bufLen++] = 0;
            this->transform(this->m_buf.data());
            this->m_bufLen = 0;
        }
        while (this->m_bufLen < 56) this->m_buf[this->m_bufLen++] = 0;
        for (int i = 7; i >= 0; --i) {
            this->m_buf[this->m_bufLen++] = static_cast<uint8_t>((bitLen >> (i * 8)) & 0xffu);
        }
        this->transform(this->m_buf.data());
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = static_cast<uint8_t>((this->m_h[i] >> 24) & 0xffu);
            out[i * 4 + 1] = static_cast<uint8_t>((this->m_h[i] >> 16) & 0xffu);
            out[i * 4 + 2] = static_cast<uint8_t>((this->m_h[i] >> 8) & 0xffu);
            out[i * 4 + 3] = static_cast<uint8_t>(this->m_h[i] & 0xffu);
        }
    }

private:
    static uint32_t rotr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32u - n));
    }

    void transform(uint8_t const* chunk) {
        static uint32_t const k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(chunk[i * 4]) << 24)
                | (uint32_t(chunk[i * 4 + 1]) << 16)
                | (uint32_t(chunk[i * 4 + 2]) << 8)
                | uint32_t(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = this->m_h[0], b = this->m_h[1], c = this->m_h[2], d = this->m_h[3];
        uint32_t e = this->m_h[4], f = this->m_h[5], g = this->m_h[6], h = this->m_h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t const ch = (e & f) ^ ((~e) & g);
            uint32_t const t1 = h + S1 + ch + k[i] + w[i];
            uint32_t const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t const t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        this->m_h[0] += a; this->m_h[1] += b; this->m_h[2] += c; this->m_h[3] += d;
        this->m_h[4] += e; this->m_h[5] += f; this->m_h[6] += g; this->m_h[7] += h;
    }

    std::array<uint32_t, 8> m_h{};
    uint64_t m_len = 0;
    size_t m_bufLen = 0;
    std::array<uint8_t, 64> m_buf{};
};

std::string CloudConfigSha256::hashHex(void const* data, size_t len) {
    CloudConfigSha256Core core;
    if (data && len > 0) {
        core.update(static_cast<uint8_t const*>(data), len);
    }
    uint8_t dig[32];
    core.finalize(dig);
    static char const* hex = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = hex[(dig[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[dig[i] & 0xf];
    }
    return out;
}

std::string CloudConfigSha256::hashHex(std::string_view bytes) {
    return hashHex(bytes.data(), bytes.size());
}

bool CloudConfigSha256::selfCheck(std::string& report) {
    report.clear();
    int fails = 0;
    auto expect = [&](bool ok, char const* name) {
        if (!ok) {
            ++fails;
            report += std::string("FAIL ") + name + "; ";
        }
    };

    // FIPS 180-2 / NIST examples.
    expect(
        hashHex(std::string_view("abc"))
            == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "abc"
    );
    expect(
        hashHex(std::string_view(""))
            == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "empty"
    );
    expect(
        hashHex(std::string_view(
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
        ))
            == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "long56"
    );
    expect(hashHex(std::string_view("x")).size() == 64, "len64");
    expect(hashHex(nullptr, 0).size() == 64, "null-empty");

    if (fails == 0) {
        report = "CloudConfigSha256 selfCheck OK";
        return true;
    }
    report = std::string("CloudConfigSha256 selfCheck fails: ") + report;
    return false;
}
