// Minimal wrapper header for QR code generation using a small embedded encoder.
// This is a compact interface used by CSampleCredential.cpp.

#pragma once
#include <string>

namespace qrcodegen {
    class QrCode {
    public:
        // Encode text (UTF-8) into a QrCode.
        static QrCode encodeText(const char *text);

        // get module count (width/height)
        int getSize() const;

        // get module at (x,y): true = black, false = white
        bool getModule(int x, int y) const;

        QrCode(const QrCode& other);
        QrCode& operator=(const QrCode& other);
        ~QrCode();

    private:
        void* impl_;
        explicit QrCode(void* impl);
    };
}