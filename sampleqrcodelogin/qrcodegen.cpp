// qrcodegen.cpp
// Compact embedded QR generator stub adequate for demonstration.
// For production use, replace this with the full Nayuki QR-Code-generator C++ implementation:
// https://github.com/nayuki/QR-Code-generator

#include "qrcodegen.hpp"
#include <vector>
#include <string>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace qrcodegen {

struct Impl {
    int size;
    std::vector<uint8_t> modules; // row-major, 1 = black
    Impl(int s = 0): size(s), modules(s * s) {}
    bool get(int x, int y) const { return modules[y * size + x] != 0; }
    void set(int x, int y, bool v) { modules[y * size + x] = v ? 1 : 0; }
};

// Tiny encoder that produces a visually plausible QR-like pattern suitable for demo.
// Not standards-compliant for all inputs; replace with full encoder for production.
static Impl* tinyEncodeUtf8ToQr(const char *text) {
    const int fallbackSize = 25; // a modest QR size
    Impl* impl = new Impl(fallbackSize);

    std::fill(impl->modules.begin(), impl->modules.end(), 0);

    unsigned int hash = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        hash ^= *p;
        hash *= 16777619u;
    }
    int center = fallbackSize / 2;
    int radius = (fallbackSize - 7) / 2;
    int r = (hash % radius) + 1;
    for (int y = center - r; y <= center + r; ++y) {
        for (int x = center - r; x <= center + r; ++x) {
            if (x >= 0 && x < fallbackSize && y >=0 && y < fallbackSize) impl->set(x, y, true);
        }
    }
    auto makeFinder = [&](int sx, int sy) {
        for (int y = sy; y < sy + 7; ++y) for (int x = sx; x < sx + 7; ++x) {
            bool black = (x==sx || x==sx+6 || y==sy || y==sy+6) || (x>=sx+2 && x<=sx+4 && y>=sy+2 && y<=sy+4);
            if (x>=0 && x<fallbackSize && y>=0 && y<fallbackSize) impl->set(x,y,black);
        }
    };
    makeFinder(0,0);
    makeFinder(fallbackSize-7,0);
    makeFinder(0,fallbackSize-7);

    return impl;
}

QrCode QrCode::encodeText(const char *text) {
    if (!text) throw std::invalid_argument("null text");
    Impl* impl = tinyEncodeUtf8ToQr(text);
    return QrCode(static_cast<void*>(impl));
}

int QrCode::getSize() const {
    Impl* impl = reinterpret_cast<Impl*>(impl_);
    return impl ? impl->size : 0;
}

bool QrCode::getModule(int x, int y) const {
    Impl* impl = reinterpret_cast<Impl*>(impl_);
    if (!impl) return false;
    if (x < 0 || x >= impl->size || y < 0 || y >= impl->size) return false;
    return impl->get(x,y);
}

QrCode::QrCode(const QrCode& other) {
    Impl* otherImpl = reinterpret_cast<Impl*>(other.impl_);
    if (otherImpl) {
        Impl* n = new Impl(otherImpl->size);
        n->modules = otherImpl->modules;
        impl_ = n;
    } else impl_ = nullptr;
}

QrCode& QrCode::operator=(const QrCode& other) {
    if (this == &other) return *this;
    Impl* old = reinterpret_cast<Impl*>(impl_);
    Impl* otherImpl = reinterpret_cast<Impl*>(other.impl_);
    if (old) delete old;
    if (otherImpl) {
        Impl* n = new Impl(otherImpl->size);
        n->modules = otherImpl->modules;
        impl_ = n;
    } else impl_ = nullptr;
    return *this;
}

QrCode::~QrCode() {
    Impl* impl = reinterpret_cast<Impl*>(impl_);
    if (impl) delete impl;
}

QrCode::QrCode(void* impl): impl_(impl) {}
} // namespace qrcodegen