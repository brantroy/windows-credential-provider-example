/* 
 * QR Code generator implementation (C++)
 * 
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#include "qrcodegen.h"
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <bit>
#include <vector>
#include <string>
#include <stdexcept>

namespace qrcodegen {

/*---- Class QrSegment ----*/

QrSegment QrSegment::makeBytes(const std::vector<std::uint8_t> &data) {
	if (data.size() > static_cast<unsigned int>(INT_MAX))
		throw std::length_error("Data too long");
	std::vector<bool> bb;
	for (std::uint8_t b : data) {
		for (int i = 7; i >= 0; i--)
			bb.push_back(((b >> i) & 1) != 0);
	}
	return QrSegment(Mode::BYTE, static_cast<int>(data.size()), bb);
}


QrSegment QrSegment::makeNumeric(const char *digits) {
	if (!isNumeric(digits))
		throw std::domain_error("String contains non-numeric characters");
	std::vector<std::uint8_t> bb;
	int accumData = 0;
	int accumCount = 0;
	for (; *digits != '\0'; digits++) {
		char c = *digits;
		// Shift accumulator and add char
		accumData = accumData * 10 + (c - '0');
		accumCount++;
		// Process every 3 digits
		if (accumCount == 3) {
			for (int i = 9; i >= 0; i--)
				bb.push_back(((accumData >> i) & 1) != 0);
			accumData = 0;
			accumCount = 0;
		}
	}
	// Process the remaining 1 or 2 digits
	if (accumCount > 0) {  // 1 or 2 digits
		for (int i = accumCount * 3 - 1; i >= 0; i--)
			bb.push_back(((accumData >> i) & 1) != 0);
	}
	return QrSegment(Mode::NUMERIC, static_cast<int>(std::strlen(digits)), bb);
}


QrSegment QrSegment::makeAlphanumeric(const char *text) {
	if (!isAlphanumeric(text))
		throw std::domain_error("String contains unencodable characters in alphanumeric mode");
	std::vector<std::uint8_t> bb;
	int accumData = 0;
	int accumCount = 0;
	for (; *text != '\0'; text++) {
		char c = *text;
		// Shift accumulator and add char
		accumData = accumData * 45 + getAlphanumericCode(c);
		accumCount++;
		// Process every 2 characters
		if (accumCount == 2) {
			for (int i = 10; i >= 0; i--)
				bb.push_back(((accumData >> i) & 1) != 0);
			accumData = 0;
			accumCount = 0;
		}
	}
	// Process the remaining 1 character
	if (accumCount > 0) {  // 1 character
		for (int i = 5; i >= 0; i--)
			bb.push_back(((accumData >> i) & 1) != 0);
	}
	return QrSegment(Mode::ALPHANUMERIC, static_cast<int>(std::strlen(text)), bb);
}


QrSegment QrSegment::makeEci(long assignVal) {
	std::vector<std::uint8_t> bb;
	if (assignVal < 0)
		throw std::domain_error("ECI assignment value must be non-negative");
	for (int i = 20; i >= 0; i--) {
		if (assignVal >> i != 0) {
			bb.push_back(1);
			for (int j = i; j >= 0; j--)
				bb.push_back(((assignVal >> j) & 1) != 0);
			break;
		}
	}
	if (bb.empty())
		bb.push_back(0);  // Special case: ECI 0
	return QrSegment(Mode::ECI, 1, bb);
}


int QrSegment::getTotalBits(const std::vector<QrSegment> &segs, int version) {
	if (version < 1 || version > 40)
		throw std::domain_error("Version must be in the range 1 to 40");
	int result = 0;
	for (const QrSegment &seg : segs) {
		int ccbits = seg.mode.numCharCountBits(version);
		if (seg.numChars >= (1 << ccbits))
			return -1;  // The segment's length doesn't fit in the field's bit width
		result += 4 + ccbits + seg.data.size();
	}
	return result;
}


bool QrSegment::isNumeric(const char *chars) {
	for (; *chars != '\0'; chars++) {
		char c = *chars;
		if (c < '0' || c > '9')
			return false;
	}
	return true;
}


bool QrSegment::isAlphanumeric(const char *chars) {
	for (; *chars != '\0'; chars++) {
		char c = *chars;
		if (getAlphanumericCode(c) == -1)
			return false;
	}
	return true;
}


QrSegment::QrSegment(Mode md, int numCh, const std::vector<bool> &dt) :
		mode(md),
		numChars(numCh),
		data(dt) {
	if (numCh < 0)
		throw std::domain_error("Invalid value");
}


QrSegment::QrSegment(Mode md, int numCh, std::vector<bool> &&dt) :
		mode(md),
		numChars(numCh),
		data(std::move(dt)) {
	if (numCh < 0)
		throw std::domain_error("Invalid value");
}


const std::vector<bool> &QrSegment::getData() const {
	return data;
}


/*---- Class QrCode ----*/

QrCode QrCode::encodeText(const char *text, Ecc ecl) {
	std::vector<std::uint8_t> buffer;
	std::vector<std::uint8_t> eclBuffer;
	std::vector<std::uint8_t> dataBuffer;
	
	if (text == nullptr)
		throw std::domain_error("String is null");
	
	// Try to create the QR code with the smallest version that fits the data
	for (int version = 1; version <= 40; version++) {
		int dataLen = qrcodegen_BUFFER_LEN_FOR_VERSION(version);
		buffer.resize(dataLen);
		eclBuffer.resize(dataLen);
		dataBuffer.resize(dataLen);
		
		if (qrcodegen_encodeText(text, buffer.data(), eclBuffer.data(), dataBuffer.data(), version, static_cast<qrcodegen_Ecc>(ecl))) {
			return QrCode(version, ecl, dataBuffer, -1);  // -1 for automatic mask
		}
	}
	throw std::data_exception("Data too long for any version");
}


QrCode QrCode::encodeBinary(const std::vector<std::uint8_t> &data, Ecc ecl) {
	std::vector<std::uint8_t> buffer;
	std::vector<std::uint8_t> eclBuffer;
	std::vector<std::uint8_t> dataBuffer;
	
	// Try to create the QR code with the smallest version that fits the data
	for (int version = 1; version <= 40; version++) {
		int dataLen = qrcodegen_BUFFER_LEN_FOR_VERSION(version);
		buffer.resize(dataLen);
		eclBuffer.resize(dataLen);
		dataBuffer.resize(dataLen);
		
		if (qrcodegen_encodeBinary(const_cast<std::uint8_t*>(data.data()), data.size(), 
								   buffer.data(), eclBuffer.data(), dataBuffer.data(), version, 
								   static_cast<qrcodegen_Ecc>(ecl))) {
			return QrCode(version, ecl, dataBuffer, -1);  // -1 for automatic mask
		}
	}
	throw std::data_exception("Data too long for any version");
}


QrCode QrCode::encodeSegments(const std::vector<QrSegment> &segs, Ecc ecl,
		int minVersion, int maxVersion, int mask, bool boostEcl) {
	if (minVersion < 1 || minVersion > 40 || maxVersion < 1 || maxVersion > 40 || minVersion > maxVersion)
		throw std::domain_error("Invalid value");
	
	std::vector<std::uint8_t> buffer;
	std::vector<std::uint8_t> eclBuffer;
	std::vector<std::uint8_t> dataBuffer;
	
	for (int version = minVersion; version <= maxVersion; version++) {
		int dataLen = qrcodegen_BUFFER_LEN_FOR_VERSION(version);
		buffer.resize(dataLen);
		eclBuffer.resize(dataLen);
		dataBuffer.resize(dataLen);
		
		// Convert C++ segments to C segments
		std::vector<qrcodegen_Segment> cSegs;
		for (const auto& seg : segs) {
			qrcodegen_Segment cseg;
			cseg.mode = static_cast<qrcodegen_Mode>(seg.mode);
			cseg.numChars = seg.numChars;
			cseg.data = reinterpret_cast<const uint8_t*>(seg.data.data());
			cseg.bitLength = seg.data.size();
			cSegs.push_back(cseg);
		}
		
		if (qrcodegen_encodeSegmentsAdvanced(cSegs.data(), cSegs.size(), 
											static_cast<qrcodegen_Ecc>(ecl), 
											version, version, mask, boostEcl, dataBuffer.data())) {
			return QrCode(version, ecl, dataBuffer, mask);
		}
	}
	throw std::data_exception("Data too long for any version");
}


QrCode::QrCode(int ver, Ecc ecl, const std::vector<std::uint8_t> &dataCodewords, int msk) :
		version(ver),
		size(ver * 4 + 17),
		errorCorrectionLevel(ecl),
		mask(msk) {
	if (ver < 1 || ver > 40 || msk < -1 || msk > 7)
		throw std::domain_error("Invalid value");
	
	// Initialize modules to all light
	std::size_t len = static_cast<std::size_t>(size) * size;
	modules.resize(len);
	
	// Fill in the data codewords
	for (std::size_t i = 0; i < dataCodewords.size(); i++) {
		for (int j = 0; j < 8; j++) {
			if ((dataCodewords[i] >> (7 - j) & 1) != 0) {
				modules[i * 8 + j] = 1;
			}
		}
	}
}


QrCode::QrCode(int ver, Ecc ecl, std::vector<std::uint8_t> &&dataCodewords, int msk) :
		version(ver),
		size(ver * 4 + 17),
		errorCorrectionLevel(ecl),
		mask(msk) {
	if (ver < 1 || ver > 40 || msk < -1 || msk > 7)
		throw std::domain_error("Invalid value");
	
	// Initialize modules to all light
	std::size_t len = static_cast<std::size_t>(size) * size;
	modules.resize(len);
	
	// Fill in the data codewords
	for (std::size_t i = 0; i < dataCodewords.size(); i++) {
		for (int j = 0; j < 8; j++) {
			if ((dataCodewords[i] >> (7 - j) & 1) != 0) {
				modules[i * 8 + j] = 1;
			}
		}
	}
}


bool QrCode::getModule(int x, int y) const {
	if (x < 0 || x >= size || y < 0 || y >= size)
		return false;
	return modules[static_cast<std::size_t>(y) * size + x];
}


std::string QrCode::toSvgString(int border) const {
	if (border < 0 || border > INT_MAX / 2 || border * 2 > INT_MAX - size)
		throw std::domain_error("Border value out of range");
	
	// Create SVG string
	std::string result;
	result += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	result += "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";
	result += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 ";
	result += std::to_string(size + border * 2);
	result += " ";
	result += std::to_string(size + border * 2);
	result += "\" stroke=\"none\">\n";
	result += "\t<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";
	result += "\t<path d=\"";
	
	bool head = true;
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			if (getModule(x, y)) {
				if (head)
					head = false;
				else
					result += " ";
				result += "M" + std::to_string(x + border) + "," + std::to_string(y + border) + "h1v1h-1z";
			}
		}
	}
	result += "\" fill=\"#000000\"/>\n";
	result += "</svg>\n";
	return result;
}


/*---- Private helper functions and constants ----*/

int QrCode::getAlphanumericCode(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	else if (c == ' ')
		return 36;
	else if (c == '$')
		return 37;
	else if (c == '%')
		return 38;
	else if (c == '*')
		return 39;
	else if (c == '+')
		return 40;
	else if (c == '-')
		return 41;
	else if (c == '.')
		return 42;
	else if (c == '/')
		return 43;
	else if (c == ':')
		return 44;
	else
		return -1;
}


int QrCode::getModeBits(Mode mode) {
	switch (mode) {
		case Mode::NUMERIC     : return 0x1;
		case Mode::ALPHANUMERIC: return 0x2;
		case Mode::BYTE        : return 0x4;
		case Mode::KANJI       : return 0x8;
		case Mode::ECI         : return 0x7;
		default:  throw std::domain_error("Invalid mode");
	}
}


int QrCode::getEccBits(Ecc ecl) {
	switch (ecl) {
		case Ecc::LOW     : return 1;
		case Ecc::MEDIUM  : return 0;
		case Ecc::QUARTILE: return 3;
		case Ecc::HIGH    : return 2;
		default:  throw std::domain_error("Invalid ECC");
	}
}


int QrCode::getVersion() const {
	return version;
}


int QrCode::getEccBits() const {
	switch (errorCorrectionLevel) {
		case Ecc::LOW     : return 1;
		case Ecc::MEDIUM  : return 0;
		case Ecc::QUARTILE: return 3;
		case Ecc::HIGH    : return 2;
		default:  throw std::domain_error("Invalid ECC");
	}
}


int QrCode::getMask() const {
	return mask;
}


/* Private static function and constants */
const std::vector<int> QrCode::VERSION_INFO_BITS = {
	0x07C94, 0x085BC, 0x09A99, 0x0A4D3, 0x0BBF6, 0x0C762, 0x0D847, 0x0E60D,
	0x0F928, 0x10B78, 0x1145D, 0x12A17, 0x13532, 0x149A6, 0x15683, 0x168C9,
	0x177EC, 0x18EC4, 0x191E1, 0x1AFAB, 0x1B08E, 0x1CC1A, 0x1D33F, 0x1ED75,
	0x1F250, 0x209D5, 0x216F0, 0x228BA, 0x2379F, 0x24B0B, 0x2542E, 0x26A64,
	0x27541, 0x28C69
};

const int QrCode::PENALTY_N1 = 3;
const int QrCode::PENALTY_N2 = 3;
const int QrCode::PENALTY_N3 = 40;
const int QrCode::PENALTY_N4 = 10;

} // namespace qrcodegen


/* 
 * QR Code generator implementation (C)
 * 
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "qrcodegen.h"

#define qrcodegen_FORMAT_INFO_MASK  0x5412


// Forward declarations for private functions
static int getModeBits(qrcodegen_Mode mode);
static int getNumDataCodewords(int version, qrcodegen_Ecc ecl);
static int getNumRawDataModules(int version);
static void addEccAndInterleave(uint8_t data[], int version, qrcodegen_Ecc ecl, uint8_t result[]);
static void drawCodewords(const uint8_t data[], int dataLen, uint8_t qrcode[]);
static void drawLightFunctionModules(uint8_t qrcode[], int version);
static void applyMask(const uint8_t functionModules[], uint8_t qrcode[], int mask);
static int getPenaltyScore(const uint8_t qrcode[]);
static int calcSegmentBitLength(qrcodegen_Mode mode, int numChars);
static int calcSegmentBitLengthAdvanced(qrcodegen_Mode mode, int numChars, int version);
static int getNumBitsForCharCount(int version, qrcodegen_Mode mode);
static void initializeFunctionModules(int version, uint8_t qrcode[]);
static bool getModule(const uint8_t qrcode[], int x, int y);
static void setModule(uint8_t qrcode[], int x, int y, bool isDark);
static void setModuleBounded(uint8_t qrcode[], int x, int y, bool isDark);
static int getMaskGen(int mask, int x, int y);
static uint8_t *reedSolomonComputeDivisor(int degree);
static uint8_t reedSolomonMultiply(uint8_t x, uint8_t y);
static uint8_t reedSolomonMultiplyFast(uint8_t x, uint8_t y, uint8_t logTable[256], uint8_t expTable[256]);
static void reedSolomonComputeRemainder(const uint8_t data[], int dataLen, const uint8_t generator[], int degree, uint8_t result[]);
static uint8_t *reedSolomonComputeRemainderAlloc(const uint8_t data[], int dataLen, const uint8_t generator[], int degree);
static void appendBitsToBuffer(unsigned int val, int numBits, uint8_t buffer[], int *bitLen);
static int getAlphanumericCode(char c);
static int getSegmentBitLength(const qrcodegen_Segment seg[], size_t len, int version);
static int getNumMaxBytes(int version, qrcodegen_Ecc ecl);
static void bitBufferFill(int start, int end, bool value, uint8_t buffer[]);

bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[], uint8_t eclBuffer[], uint8_t buffer[], int version, qrcodegen_Ecc ecl) {
	if (text == NULL || tempBuffer == NULL || eclBuffer == NULL || buffer == NULL)
		return false;
	
	// Choose automatic version if requested
	if (version == -1) {
		for (int v = 1; v <= 40; v++) {
			int dataUsedBits = calcSegmentBitLengthAdvanced(qrcodegen_Mode_BYTE, (int)strlen(text), v);
			int dataCapBits = getNumMaxBytes(v, ecl) * 8;  // Number of data bits
			if (dataUsedBits <= dataCapBits) {
				version = v;
				break;
			}
		}
		if (version == -1)
			return false;  // Data too long for any version
	}
	
	// Create segment and encode
	qrcodegen_Segment seg;
	seg.mode = qrcodegen_Mode_BYTE;
	seg.numChars = (int)strlen(text);
	seg.data = (uint8_t *)text;
	seg.bitLength = seg.numChars * 8;
	
	return qrcodegen_encodeSegments(&seg, 1, ecl, buffer, version, -1);
}


bool qrcodegen_encodeBinary(uint8_t data[], size_t dataLen, uint8_t tempBuffer[], uint8_t eclBuffer[], uint8_t buffer[], int version, qrcodegen_Ecc ecl) {
	if (data == NULL || tempBuffer == NULL || eclBuffer == NULL || buffer == NULL)
		return false;
	
	// Choose automatic version if requested
	if (version == -1) {
		for (int v = 1; v <= 40; v++) {
			int dataUsedBits = calcSegmentBitLengthAdvanced(qrcodegen_Mode_BYTE, (int)dataLen, v);
			int dataCapBits = getNumMaxBytes(v, ecl) * 8;  // Number of data bits
			if (dataUsedBits <= dataCapBits) {
				version = v;
				break;
			}
		}
		if (version == -1)
			return false;  // Data too long for any version
	}
	
	// Create segment and encode
	qrcodegen_Segment seg;
	seg.mode = qrcodegen_Mode_BYTE;
	seg.numChars = (int)dataLen;
	seg.data = data;
	seg.bitLength = (int)dataLen * 8;
	
	return qrcodegen_encodeSegments(&seg, 1, ecl, buffer, version, -1);
}


bool qrcodegen_encodeSegments(const qrcodegen_Segment segs[], size_t len, qrcodegen_Ecc ecl, uint8_t buffer[], int version, int mask) {
	return qrcodegen_encodeSegmentsAdvanced(segs, len, ecl, version, version, mask, false, buffer);
}


bool qrcodegen_encodeSegmentsAdvanced(const qrcodegen_Segment segs[], size_t len, qrcodegen_Ecc ecl, int minVersion, int maxVersion, int mask, bool boostEcl, uint8_t buffer[]) {
	if (segs == NULL || buffer == NULL || minVersion < 1 || minVersion > 40 || maxVersion < 1 || maxVersion > 40 || minVersion > maxVersion || mask < -1 || mask > 7)
		return false;
	
	// Find the minimal version that can accommodate the data
	int version = -1;
	for (int ver = minVersion; ver <= maxVersion; ver++) {
		int dataUsedBits = getSegmentBitLength(segs, len, ver);
		int dataCapBits = getNumMaxBytes(ver, ecl) * 8;  // Number of data bits
		if (dataUsedBits <= dataCapBits) {
			version = ver;
			break;
		}
	}
	if (version == -1)
		return false;  // Data too long for any version
	
	// Handle ECC level boost if requested
	if (boostEcl) {
		qrcodegen_Ecc newEcc = ecl;
		if (ecl == qrcodegen_Ecc_LOW && getSegmentBitLength(segs, len, version) <= getNumMaxBytes(version, qrcodegen_Ecc_MEDIUM) * 8)
			newEcc = qrcodegen_Ecc_MEDIUM;
		if (newEcc == qrcodegen_Ecc_MEDIUM && getSegmentBitLength(segs, len, version) <= getNumMaxBytes(version, qrcodegen_Ecc_QUARTILE) * 8)
			newEcc = qrcodegen_Ecc_QUARTILE;
		if (newEcc == qrcodegen_Ecc_QUARTILE && getSegmentBitLength(segs, len, version) <= getNumMaxBytes(version, qrcodegen_Ecc_HIGH) * 8)
			newEcc = qrcodegen_Ecc_HIGH;
		ecl = newEcl;
	}
	
	// Calculate data capacity in bits
	int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
	
	// Allocate and concatenate all segments' data
	uint8_t *dataBuffer = (uint8_t *)malloc(dataCapacityBits / 8);
	if (dataBuffer == NULL)
		return false;
	memset(dataBuffer, 0, dataCapacityBits / 8);
	
	// Add segments to buffer
	int bitLen = 0;
	for (size_t i = 0; i < len; i++) {
		const qrcodegen_Segment *seg = &segs[i];
		
		// Add mode indicator
		appendBitsToBuffer(getModeBits(seg->mode), 4, dataBuffer, &bitLen);
		
		// Add character count indicator
		int cciBits = getNumBitsForCharCount(version, seg->mode);
		appendBitsToBuffer(seg->numChars, cciBits, dataBuffer, &bitLen);
		
		// Add data
		for (int j = 0; j < seg->bitLength; j++) {
			int bitIndex = j % 8;
			if (seg->data[j / 8] & (1 << (7 - bitIndex))) {
				dataBuffer[bitLen / 8] |= 1 << (7 - (bitLen % 8));
			}
			bitLen++;
		}
	}
	
	// Add terminator and padding
	int terminatorBits = std::min(4, dataCapacityBits - bitLen);
	appendBitsToBuffer(0, terminatorBits, dataBuffer, &bitLen);
	
	// Pad up to 8 bits from the end
	int padUpTo8Bits = (8 - (bitLen % 8)) % 8;
	appendBitsToBuffer(0, padUpTo8Bits, dataBuffer, &bitLen);
	
	// Pad with alternating bytes until data capacity is reached
	for (uint8_t padByte = 0xEC; bitLen < dataCapacityBits; padByte ^= 0xEC ^ 0x11) {
		appendBitsToBuffer(padByte, 8, dataBuffer, &bitLen);
	}
	
	// Construct the QR code
	initializeFunctionModules(version, buffer);
	
	// Draw all segments' data
	int dataLen = bitLen / 8;
	drawCodewords(dataBuffer, dataLen, buffer);
	
	// Do masking
	if (mask == -1) {  // Automatically choose best mask
		uint8_t originalBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(maxVersion)];
		memcpy(originalBuffer, buffer, qrcodegen_BUFFER_LEN_FOR_VERSION(maxVersion));
		
		int32_t minPenalty = INT32_MAX;
		for (int i = 0; i < 8; i++) {
			// Try each mask
			drawLightFunctionModules(buffer, version);
			applyMask(originalBuffer, buffer, i);
			
			int penalty = getPenaltyScore(buffer);
			if (penalty < minPenalty) {
				mask = i;
				minPenalty = penalty;
			}
			
			if (i < 7)  // Last iteration doesn't need to restore
				memcpy(buffer, originalBuffer, qrcodegen_BUFFER_LEN_FOR_VERSION(maxVersion));
		}
	}
	
	// Overwrite old data with final result
	drawLightFunctionModules(buffer, version);
	applyMask(dataBuffer, buffer, mask);  // Use dataBuffer as temporary space
	
	free(dataBuffer);
	return true;
}


int qrcodegen_getSize(const qrcodegen_QrCode *qr) {
	return qr->size;
}


bool qrcodegen_getModule(const qrcodegen_QrCode *qr, int x, int y) {
	if (x < 0 || x >= qr->size || y < 0 || y >= qr->size)
		return false;
	int index = y * qr->size + x;
	return (qr->qr[index / 8] & (1 << (7 - (index % 8)))) != 0;
}


/*---- Private helper functions ----*/

static int getModeBits(qrcodegen_Mode mode) {
	switch (mode) {
		case qrcodegen_Mode_NUMERIC     : return 0x1;
		case qrcodegen_Mode_ALPHANUMERIC: return 0x2;
		case qrcodegen_Mode_BYTE        : return 0x4;
		case qrcodegen_Mode_KANJI       : return 0x8;
		case qrcodegen_Mode_ECI         : return 0x7;
		default:  return -1;  // Should not reach
	}
}


static int getNumDataCodewords(int version, qrcodegen_Ecc ecl) {
	int total = getNumRawDataModules(version) / 8;
	switch (ecl) {
		case qrcodegen_Ecc_LOW     : return total - (7 * (version <= 9 ? 1 : (version <= 26 ? 2 : 4)));
		case qrcodegen_Ecc_MEDIUM  : return total - (14 * (version <= 9 ? 1 : (version <= 26 ? 2 : 4)));
		case qrcodegen_Ecc_QUARTILE: return total - (21 * (version <= 9 ? 1 : (version <= 26 ? 2 : 4)));
		case qrcodegen_Ecc_HIGH    : return total - (28 * (version <= 9 ? 1 : (version <= 26 ? 2 : 4)));
		default:  return -1;  // Should not reach
	}
}


static int getNumRawDataModules(int ver) {
	if (ver < 1 || ver > 40)
		return -1;
	int result = (16 * ver + 128) * ver + 64;
	if (ver >= 2) {
		int numAlign = ver / 7 + 2;
		result -= (25 * numAlign - 10) * numAlign - 55;
		if (ver >= 7)
			result -= 36;
	}
	return result;
}


static void addEccAndInterleave(uint8_t data[], int version, qrcodegen_Ecc ecl, uint8_t result[]) {
	// Calculate parameter numbers
	if (version < 1 || version > 40 || ecl < 0 || ecl > 3)
		return;
	
	// Get ECC parameters for this version and error correction level
	int numBlocks = 0, blockEccLen = 0, numShortBlocks = 0, shortBlockDataLen = 0;
	
	// These are simplified parameters based on QR code specification
	if (version >= 1 && version <= 9) {
		static const int eccParams[][4] = {
			{1, 7, 1, 19},   // LOW
			{1, 10, 1, 16},  // MEDIUM
			{1, 13, 1, 13},  // QUARTILE
			{1, 17, 1, 9}    // HIGH
		};
		int i = (int)ecl;
		numBlocks = eccParams[i][0];
		blockEccLen = eccParams[i][1];
		shortBlockDataLen = eccParams[i][3];
		numShortBlocks = 0;
	} else if (version >= 10 && version <= 26) {
		static const int eccParams[][4] = {
			{1, 15, 1, 34},
			{2, 16, 1, 32},
			{2, 22, 1, 26},
			{4, 28, 1, 16}
		};
		int i = (int)ecl;
		numBlocks = eccParams[i][0];
		blockEccLen = eccParams[i][1];
		shortBlockDataLen = eccParams[i][3];
		numShortBlocks = 0;
	} else { // version 27-40
		static const int eccParams[][4] = {
			{1, 26, 1, 55},
			{4, 18, 2, 38},
			{4, 26, 2, 28},
			{4, 36, 4, 16}
		};
		int i = (int)ecl;
		numBlocks = eccParams[i][0];
		blockEccLen = eccParams[i][1];
		shortBlockDataLen = eccParams[i][3];
		numShortBlocks = eccParams[i][2];
	}
	
	int shortBlockFullLen = shortBlockDataLen + blockEccLen;
	int longBlockFullLen = shortBlockFullLen + 1;
	int numLongBlocks = numBlocks - numShortBlocks;
	int dataLen = getNumDataCodewords(version, ecl);
	
	// Allocate data blocks
	uint8_t **blocks = (uint8_t**)malloc(numBlocks * sizeof(uint8_t*));
	for (int i = 0; i < numBlocks; i++) {
		blocks[i] = (uint8_t*)malloc(longBlockFullLen);
	}
	
	// Split data into blocks
	for (int i = 0; i < numShortBlocks; i++) {
		memcpy(blocks[i], &data[i * shortBlockDataLen], shortBlockDataLen);
	}
	for (int i = numShortBlocks; i < numBlocks; i++) {
		int offset = numShortBlocks * shortBlockDataLen + (i - numShortBlocks) * (shortBlockDataLen + 1);
		memcpy(blocks[i], &data[offset], shortBlockDataLen + 1);
	}
	
	// Add ECC to each block
	uint8_t *generator = reedSolomonComputeDivisor(blockEccLen);
	for (int i = 0; i < numBlocks; i++) {
		// Calculate ECC for this block
		uint8_t *ecc = reedSolomonComputeRemainderAlloc(blocks[i], 
													   (i < numLongBlocks) ? shortBlockDataLen + 1 : shortBlockDataLen, 
													   generator, blockEccLen);
		memcpy(&blocks[i][(i < numLongBlocks) ? shortBlockDataLen + 1 : shortBlockDataLen], ecc, blockEccLen);
		free(ecc);
	}
	free(generator);
	
	// Interleave blocks
	int index = 0;
	
	// Interleave data
	for (int i = 0; i < shortBlockDataLen + 1; i++) {
		for (int j = 0; j < numBlocks; j++) {
			if (i != shortBlockDataLen || j >= numLongBlocks) {
				result[index] = blocks[j][i];
				index++;
			}
		}
	}
	
	// Interleave ECC
	for (int i = 0; i < blockEccLen; i++) {
		for (int j = 0; j < numBlocks; j++) {
			result[index] = blocks[j][shortBlockDataLen + (j >= numLongBlocks ? 1 : 0) + i];
			index++;
		}
	}
	
	// Clean up
	for (int i = 0; i < numBlocks; i++) {
		free(blocks[i]);
	}
	free(blocks);
}


static void drawCodewords(const uint8_t data[], int dataLen, uint8_t qrcode[]) {
	int bitIndex = 0;
	for (int i = 0; i < dataLen; i++) {
		for (int j = 7; j >= 0; j--, bitIndex++) {
			int y = bitIndex / qrcodegen_VERSION_MAX * 2 + (bitIndex % 2);
			int x = bitIndex % qrcodegen_VERSION_MAX;
			if (x >= qrcode[0]) continue; // Skip if out of bounds
			if ((data[i] >> j) & 1)
				qrcode[bitIndex / 8] |= 1 << (7 - (bitIndex % 8));
		}
	}
}


static void drawLightFunctionModules(uint8_t qrcode[], int version) {
	int size = version * 4 + 17;
	
	// Draw horizontal and vertical timing patterns
	bitBufferFill(0, size * 6 + 6, true, qrcode); // Top timing pattern
	for (int y = 0; y < 6; y++) {  // Left timing pattern
		for (int x = 6; x < 7; x++) {
			setModule(qrcode, x, y, true);
		}
	}
	
	// Draw 3 finder patterns (all corners except bottom-right)
	for (int y = 0; y < 7; y++) {
		for (int x = 0; x < 7; x++) {
			bool dark = (x == 0 || x == 6 || y == 0 || y == 6 || (x >= 2 && x <= 4 && y >= 2 && y <= 4));
			setModule(qrcode, x, y, dark);
			setModule(qrcode, size - 7 + x, y, dark);
			setModule(qrcode, x, size - 7 + y, dark);
		}
	}
	
	// Draw numerous alignment patterns
	static const int alignPatPos[40][6] = {
		{-1}, {-1}, {6,18}, {6,22}, {6,26}, {6,30}, {6,34}, {6,22,38}, {6,24,42}, {6,26,46},  // 1-10
		{6,28,50}, {6,30,54}, {6,32,58}, {6,34,62}, {6,26,46,66}, {6,26,48,70}, {6,26,50,74}, {6,30,54,78}, {6,30,56,82}, {6,30,58,86},  // 11-20
		{6,34,62,90}, {6,28,50,72,94}, {6,26,50,74,98}, {6,30,54,78,102}, {6,28,54,80,106}, {6,32,58,84,110}, {6,30,58,86,114}, {6,34,62,90,118}, {6,26,50,74,98,122},  // 21-29
		{6,30,54,78,102,126}, {6,26,52,78,104,130}, {6,30,56,82,108,134}, {6,34,60,86,112,138}, {6,30,58,86,114,142}, {6,34,62,90,118,146}, {6,30,54,78,102,126,150},  // 30-36
		{6,24,50,76,102,128,154}, {6,28,54,80,106,132,158}, {6,32,58,84,110,136,162}, {6,26,54,82,110,138,166}, {6,30,58,86,114,142,170}  // 37-40
	};
	const int numAlign = (version == 1) ? 0 : (version / 7 + 2);
	for (int i = 0; i < numAlign; i++) {
		for (int j = 0; j < numAlign; j++) {
			if ((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0))
				continue;  // Skip the three finder corners
			for (int dy = -2; dy <= 2; dy++) {
				for (int dx = -2; dx <= 2; dx++) {
					int x = alignPatPos[version - 1][i];
					int y = alignPatPos[version - 1][j];
					bool dark = (dx == 0 && dy == 0) || (abs(dx) == 2 || abs(dy) == 2);
					setModule(qrcode, x + dx, y + dy, dark);
				}
			}
		}
	}
	
	// Draw configuration data
	for (int i = 0; i < 15; i++) {
		int bit = 0;
		setModule(qrcode, 8, i + (i < 6 ? 0 : 1), (bit & 1) != 0);
		setModule(qrcode, i + (i < 8 ? 0 : 1), 8, (bit & 1) != 0);
	}
	setModule(qrcode, 8, size - 8, true);
}


static void applyMask(const uint8_t functionModules[], uint8_t qrcode[], int mask) {
	int size = qrcode[0];  // Get size from first byte
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			if (getModule(functionModules, x, y))
				continue;
			bool invert;
			switch (mask) {
				case 0:  invert = (x + y) % 2 == 0;                    break;
				case 1:  invert = y % 2 == 0;                          break;
				case 2:  invert = x % 3 == 0;                          break;
				case 3:  invert = (x + y) % 3 == 0;                    break;
				case 4:  invert = (x / 3 + y / 2) % 2 == 0;            break;
				case 5:  invert = x * y % 2 + x * y % 3 == 0;          break;
				case 6:  invert = (x * y % 2 + x * y % 3) % 2 == 0;    break;
				case 7:  invert = ((x + y) % 2 + x * y % 3) % 2 == 0;  break;
				default:  return;
			}
			if (invert) {
				int index = y * size + x;
				qrcode[index / 8] ^= 1 << (7 - (index % 8));
			}
		}
	}
}


static int getPenaltyScore(const uint8_t qrcode[]) {
	int size = qrcode[0];
	int result = 0;
	
	// Evaluate single modules in succession
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			bool color = getModule(qrcode, x, y);
			
			// Check for horizontal runs
			int runX = 0;
			for (int i = x; i < size && getModule(qrcode, i, y) == color; i++)
				runX++;
			if (runX >= 5)
				result += runX - 2;
			
			// Check for vertical runs
			int runY = 0;
			for (int i = y; i < size && getModule(qrcode, x, i) == color; i++)
				runY++;
			if (runY >= 5)
				result += runY - 2;
		}
	}
	
	// Evaluate 2x2 blocks
	for (int y = 0; y < size - 1; y++) {
		for (int x = 0; x < size - 1; x++) {
			bool color = getModule(qrcode, x, y);
			if (color == getModule(qrcode, x + 1, y) &&
				color == getModule(qrcode, x, y + 1) &&
				color == getModule(qrcode, x + 1, y + 1))
				result += 3;
		}
	}
	
	// Evaluate patterns in rows
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size - 6; x++) {
			if (getModule(qrcode, x, y) && !getModule(qrcode, x + 1, y) && getModule(qrcode, x + 2, y) &&
				getModule(qrcode, x + 3, y) && getModule(qrcode, x + 4, y) && !getModule(qrcode, x + 5, y) &&
				getModule(qrcode, x + 6, y)) {
				// Check for white border
				bool borderLeft  = (x > 0) && !getModule(qrcode, x - 1, y);
				bool borderRight = (x + 7 < size) && !getModule(qrcode, x + 7, y);
				if (borderLeft && borderRight)
					result += 40;
			}
		}
	}
	
	// Evaluate patterns in columns
	for (int x = 0; x < size; x++) {
		for (int y = 0; y < size - 6; y++) {
			if (getModule(qrcode, x, y) && !getModule(qrcode, x, y + 1) && getModule(qrcode, x, y + 2) &&
				getModule(qrcode, x, y + 3) && getModule(qrcode, x, y + 4) && !getModule(qrcode, x, y + 5) &&
				getModule(qrcode, x, y + 6)) {
				// Check for white border
				bool borderUp  = (y > 0) && !getModule(qrcode, x, y - 1);
				bool borderDown = (y + 7 < size) && !getModule(qrcode, x, y + 7);
				if (borderUp && borderDown)
					result += 40;
			}
		}
	}
	
	return result;
}


static int calcSegmentBitLength(qrcodegen_Mode mode, int numChars) {
	int bitLength = 4;  // Mode indicator
	switch (mode) {
		case qrcodegen_Mode_NUMERIC     : bitLength += 10; break;
		case qrcodegen_Mode_ALPHANUMERIC: bitLength += 9;  break;
		case qrcodegen_Mode_BYTE        : bitLength += 8;  break;
		case qrcodegen_Mode_KANJI       : bitLength += 8;  break;
		default:  return -1;  // Should not reach
	}
	
	if (numChars >= (1 << 16))  // Should not occur in most applications
		bitLength += 16;
	else if (numChars >= (1 << 8))
		bitLength += 12;
	else
		bitLength += 8;
	
	return bitLength + numChars * (mode == qrcodegen_Mode_NUMERIC ? 10 : 
								  mode == qrcodegen_Mode_ALPHANUMERIC ? 11 : 
								  mode == qrcodegen_Mode_BYTE ? 8 : 
								  mode == qrcodegen_Mode_KANJI ? 13 : 0);
}


static int calcSegmentBitLengthAdvanced(qrcodegen_Mode mode, int numChars, int version) {
	if (numChars < 0)
		return -1;
	int numCharBits = 0;
	if (version <= 9) {
		if (mode == qrcodegen_Mode_NUMERIC) numCharBits = 10;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) numCharBits = 9;
		else if (mode == qrcodegen_Mode_BYTE) numCharBits = 8;
		else if (mode == qrcodegen_Mode_KANJI) numCharBits = 8;
	} else if (version <= 26) {
		if (mode == qrcodegen_Mode_NUMERIC) numCharBits = 12;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) numCharBits = 11;
		else if (mode == qrcodegen_Mode_BYTE) numCharBits = 16;
		else if (mode == qrcodegen_Mode_KANJI) numCharBits = 10;
	} else {
		if (mode == qrcodegen_Mode_NUMERIC) numCharBits = 14;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) numCharBits = 13;
		else if (mode == qrcodegen_Mode_BYTE) numCharBits = 16;
		else if (mode == qrcodegen_Mode_KANJI) numCharBits = 12;
	}
	
	int charCountBits = 0;
	if (version <= 9) {
		if (mode == qrcodegen_Mode_NUMERIC) charCountBits = 10;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) charCountBits = 9;
		else if (mode == qrcodegen_Mode_BYTE) charCountBits = 8;
		else if (mode == qrcodegen_Mode_KANJI) charCountBits = 8;
	} else if (version <= 26) {
		if (mode == qrcodegen_Mode_NUMERIC) charCountBits = 12;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) charCountBits = 11;
		else if (mode == qrcodegen_Mode_BYTE) charCountBits = 16;
		else if (mode == qrcodegen_Mode_KANJI) charCountBits = 10;
	} else {
		if (mode == qrcodegen_Mode_NUMERIC) charCountBits = 14;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) charCountBits = 13;
		else if (mode == qrcodegen_Mode_BYTE) charCountBits = 16;
		else if (mode == qrcodegen_Mode_KANJI) charCountBits = 12;
	}
	
	return 4 + charCountBits + numChars * numCharBits;
}


static int getNumBitsForCharCount(int version, qrcodegen_Mode mode) {
	if (version <= 9) {
		if (mode == qrcodegen_Mode_NUMERIC) return 10;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) return 9;
		else if (mode == qrcodegen_Mode_BYTE) return 8;
		else if (mode == qrcodegen_Mode_KANJI) return 8;
	} else if (version <= 26) {
		if (mode == qrcodegen_Mode_NUMERIC) return 12;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) return 11;
		else if (mode == qrcodegen_Mode_BYTE) return 16;
		else if (mode == qrcodegen_Mode_KANJI) return 10;
	} else {
		if (mode == qrcodegen_Mode_NUMERIC) return 14;
		else if (mode == qrcodegen_Mode_ALPHANUMERIC) return 13;
		else if (mode == qrcodegen_Mode_BYTE) return 16;
		else if (mode == qrcodegen_Mode_KANJI) return 12;
	}
	return 0;
}


static void initializeFunctionModules(int version, uint8_t qrcode[]) {
	int size = version * 4 + 17;
	qrcode[0] = size;  // Store size in first byte for later use
	
	// Initialize all modules as light
	for (int i = 1; i < qrcodegen_BUFFER_LEN_FOR_VERSION(version); i++) {
		qrcode[i] = 0;
	}
	
	// Light function modules are drawn in drawLightFunctionModules
}


static bool getModule(const uint8_t qrcode[], int x, int y) {
	int size = qrcode[0];
	if (x < 0 || x >= size || y < 0 || y >= size)
		return false;
	int index = y * size + x;
	return (qrcode[1 + index / 8] & (1 << (7 - (index % 8)))) != 0;
}


static void setModule(uint8_t qrcode[], int x, int y, bool isDark) {
	int size = qrcode[0];
	if (x < 0 || x >= size || y < 0 || y >= size)
		return;
	int index = y * size + x;
	if (isDark)
		qrcode[1 + index / 8] |= 1 << (7 - (index % 8));
	else
		qrcode[1 + index / 8] &= (1 << (7 - (index % 8))) ^ 0xFF;
}


static void setModuleBounded(uint8_t qrcode[], int x, int y, bool isDark) {
	int size = qrcode[0];
	if (x >= 0 && x < size && y >= 0 && y < size)
		setModule(qrcode, x, y, isDark);
}


static int getMaskGen(int mask, int x, int y) {
	switch (mask) {
		case 0:  return (x + y) % 2;
		case 1:  return y % 2;
		case 2:  return x % 3;
		case 3:  return (x + y) % 3;
		case 4:  return (x / 3 + y / 2) % 2;
		case 5:  return x * y % 2 + x * y % 3;
		case 6:  return (x * y % 2 + x * y % 3) % 2;
		case 7:  return ((x + y) % 2 + x * y % 3) % 2;
		default:  return -1;  // Invalid mask
	}
}


static uint8_t *reedSolomonComputeDivisor(int degree) {
	if (degree < 1 || degree > 255)
		return NULL;
	uint8_t *result = (uint8_t*)malloc(degree);
	if (result == NULL)
		return NULL;
	
	// Generate log and exp tables for GF(256)
	uint8_t logTable[256];
	uint8_t expTable[256];
	logTable[0] = 255;  // Invalid value
	for (int i = 0, val = 1; i < 255; i++) {
		logTable[val] = (uint8_t)i;
		expTable[i] = (uint8_t)val;
		val = (val << 1) ^ ((val >> 7) * 0x11D);  // Multiply by polynomial x+1
	}
	
	// Compute the divisor
	result[degree - 1] = 1;  // Start with monic polynomial
	for (int i = 0; i < degree; i++) {
		// Multiply result by (x - expTable[i])
		uint8_t factor = expTable[i];
		for (int j = 0; j < degree; j++) {
			uint8_t coef = result[j];
			if (coef != 0) {
				int logCoef = logTable[coef];
				result[j] = coef ^ reedSolomonMultiplyFast(factor, coef, logTable, expTable);
			} else {
				result[j] = reedSolomonMultiplyFast(factor, 1, logTable, expTable);
			}
		}
	}
	
	return result;
}


static uint8_t reedSolomonMultiply(uint8_t x, uint8_t y) {
	// Russian peasant multiplication in GF(256) with modulo x^8 + x^4 + x^3 + x^2 + 1
	uint8_t z = 0;
	for (int i = 7; i >= 0; i--) {
		z = (z << 1) ^ ((z >> 7) * 0x11D);
		if ((y >> i) & 1)
			z ^= x;
	}
	return z;
}


static uint8_t reedSolomonMultiplyFast(uint8_t x, uint8_t y, uint8_t logTable[256], uint8_t expTable[256]) {
	if (x == 0 || y == 0)
		return 0;
	else
		return expTable[(logTable[x] + logTable[y]) % 255];
}


static void reedSolomonComputeRemainder(const uint8_t data[], int dataLen, const uint8_t generator[], int degree, uint8_t result[]) {
	for (int i = 0; i < degree; i++)
		result[i] = 0;
	
	for (int i = 0; i < dataLen; i++) {
		uint8_t factor = data[i] ^ result[0];
		for (int j = 0; j < degree; j++) {
			result[j] = j + 1 < degree ? result[j + 1] : 0;
			if (factor != 0)
				result[j] ^= reedSolomonMultiply(generator[j], factor);
		}
	}
}


static uint8_t *reedSolomonComputeRemainderAlloc(const uint8_t data[], int dataLen, const uint8_t generator[], int degree) {
	uint8_t *result = (uint8_t*)malloc(degree);
	if (result != NULL) {
		reedSolomonComputeRemainder(data, dataLen, generator, degree, result);
	}
	return result;
}


static void appendBitsToBuffer(unsigned int val, int numBits, uint8_t buffer[], int *bitLen) {
	for (int i = numBits - 1; i >= 0; i--, (*bitLen)++)
		if ((val >> i) & 1)
			buffer[*bitLen / 8] |= 1 << (7 - (*bitLen % 8));
}


static int getAlphanumericCode(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	else if (c == ' ')
		return 36;
	else if (c == '$')
		return 37;
	else if (c == '%')
		return 38;
	else if (c == '*')
		return 39;
	else if (c == '+')
		return 40;
	else if (c == '-')
		return 41;
	else if (c == '.')
		return 42;
	else if (c == '/')
		return 43;
	else if (c == ':')
		return 44;
	else
		return -1;
}


static int getSegmentBitLength(const qrcodegen_Segment segs[], size_t len, int version) {
	int result = 0;
	for (size_t i = 0; i < len; i++) {
		int ccbits = getNumBitsForCharCount(version, segs[i].mode);
		if (segs[i].numChars >= (1 << ccbits))
			return -1;  // The segment's length doesn't fit in the field's bit width
		result += 4 + ccbits + segs[i].bitLength;
	}
	return result;
}


static int getNumMaxBytes(int version, qrcodegen_Ecc ecl) {
	if (version < 1 || version > 40)
		return -1;
	
	// Simplified calculation for max data bytes
	int totalModules = getNumRawDataModules(version);
	int eccBytes = 0;
	switch (ecl) {
		case qrcodegen_Ecc_LOW:     eccBytes = totalModules / 8 * 7 / 8; break;
		case qrcodegen_Ecc_MEDIUM:  eccBytes = totalModules / 8 * 6 / 8; break;
		case qrcodegen_Ecc_QUARTILE: eccBytes = totalModules / 8 * 5 / 8; break;
		case qrcodegen_Ecc_HIGH:    eccBytes = totalModules / 8 * 4 / 8; break;
		default: return -1;
	}
	return (totalModules / 8) - eccBytes;
}


static void bitBufferFill(int start, int end, bool value, uint8_t buffer[]) {
	for (int i = start; i < end; i++) {
		int byteIndex = i / 8;
		int bitIndex = 7 - (i % 8);
		if (value) {
			buffer[byteIndex] |= (1 << bitIndex);
		} else {
			buffer[byteIndex] &= ~(1 << bitIndex);
		}
	}
}


bool qrcodegen_isNumeric(const char *str) {
	if (str == NULL)
		return false;
	for (int i = 0; str[i] != '\0'; i++) {
		char c = str[i];
		if (c < '0' || c > '9')
			return false;
	}
	return true;
}


bool qrcodegen_isAlphanumeric(const char *str) {
	if (str == NULL)
		return false;
	for (int i = 0; str[i] != '\0'; i++) {
		if (getAlphanumericCode(str[i]) == -1)
			return false;
	}
	return true;
}


int qrcodegen_calcSegmentBits(const char *str, qrcodegen_Mode mode) {
	if (str == NULL)
		return 0;
	int len = (int)strlen(str);
	return calcSegmentBitLength(mode, len);
}


int qrcodegen_makeNumeric(const char *str, uint8_t buffer[]) {
	if (str == NULL || buffer == NULL || !qrcodegen_isNumeric(str))
		return 0;
	
	int len = (int)strlen(str);
	std::vector<bool> bits;
	
	// Add mode indicator (0001)
	bits.push_back(0); bits.push_back(0); bits.push_back(0); bits.push_back(1);
	
	// Add character count (10 bits for version 1-9)
	int numChars = len;
	for (int i = 9; i >= 0; i--) {
		bits.push_back((numChars >> i) & 1);
	}
	
	// Add data bits
	int i = 0;
	while (i < len) {
		// Take up to 3 digits
		int num = 0;
		for (int j = 0; j < 3 && i + j < len; j++) {
			num = num * 10 + (str[i + j] - '0');
		}
		int digits = (len - i < 3) ? (len - i) : 3;
		int bitsNeeded = (digits == 1) ? 4 : (digits == 2) ? 7 : 10;
		
		for (int j = bitsNeeded - 1; j >= 0; j--) {
			bits.push_back((num >> j) & 1);
		}
		i += digits;
	}
	
	// Convert to bytes
	int byteCount = (bits.size() + 7) / 8;
	for (int i = 0; i < byteCount; i++) {
		buffer[i] = 0;
		for (int j = 0; j < 8 && i * 8 + j < bits.size(); j++) {
			buffer[i] |= (bits[i * 8 + j] ? 1 : 0) << (7 - j);
		}
	}
	
	return byteCount;
}


int qrcodegen_makeAlphanumeric(const char *str, uint8_t buffer[]) {
	if (str == NULL || buffer == NULL || !qrcodegen_isAlphanumeric(str))
		return 0;
	
	int len = (int)strlen(str);
	std::vector<bool> bits;
	
	// Add mode indicator (0010)
	bits.push_back(0); bits.push_back(0); bits.push_back(1); bits.push_back(0);
	
	// Add character count (9 bits for version 1-9)
	int numChars = len;
	for (int i = 8; i >= 0; i--) {
		bits.push_back((numChars >> i) & 1);
	}
	
	// Add data bits
	int i = 0;
	while (i < len) {
		// Take 2 characters
		int code1 = getAlphanumericCode(str[i]);
		int code2 = (i + 1 < len) ? getAlphanumericCode(str[i + 1]) : -1;
		
		int value = code1 * 45;
		if (code2 != -1) {
			value += code2;
			// 11 bits for 2 characters
			for (int j = 10; j >= 0; j--) {
				bits.push_back((value >> j) & 1);
			}
			i += 2;
		} else {
			// 6 bits for 1 character
			for (int j = 5; j >= 0; j--) {
				bits.push_back((code1 >> j) & 1);
			}
			i += 1;
		}
	}
	
	// Convert to bytes
	int byteCount = (bits.size() + 7) / 8;
	for (int i = 0; i < byteCount; i++) {
		buffer[i] = 0;
		for (int j = 0; j < 8 && i * 8 + j < bits.size(); j++) {
			buffer[i] |= (bits[i * 8 + j] ? 1 : 0) << (7 - j);
		}
	}
	
	return byteCount;
}


int qrcodegen_makeBytes(const uint8_t *data, size_t len, uint8_t buffer[]) {
	if (data == NULL || buffer == NULL)
		return 0;
	
	std::vector<bool> bits;
	
	// Add mode indicator (0100)
	bits.push_back(0); bits.push_back(1); bits.push_back(0); bits.push_back(0);
	
	// Add character count (8 bits for version 1-9)
	for (int i = 7; i >= 0; i--) {
		bits.push_back((len >> i) & 1);
	}
	
	// Add data bits
	for (size_t i = 0; i < len; i++) {
		for (int j = 7; j >= 0; j--) {
			bits.push_back((data[i] >> j) & 1);
		}
	}
	
	// Convert to bytes
	int byteCount = (bits.size() + 7) / 8;
	for (int i = 0; i < byteCount; i++) {
		buffer[i] = 0;
		for (int j = 0; j < 8 && i * 8 + j < bits.size(); j++) {
			buffer[i] |= (bits[i * 8 + j] ? 1 : 0) << (7 - j);
		}
	}
	
	return byteCount;
}


int qrcodegen_calcSegmentBitLength(qrcodegen_Mode mode, int numChars) {
	return calcSegmentBitLength(mode, numChars);
}


qrcodegen_Segment qrcodegen_makeSegment(const char *str, qrcodegen_Mode mode) {
	qrcodegen_Segment seg;
	seg.mode = mode;
	seg.numChars = (int)strlen(str);
	seg.bitLength = calcSegmentBitLength(mode, seg.numChars);
	
	// For simplicity, we'll just point to the input string
	// In a real implementation, you'd want to copy the data
	seg.data = (const uint8_t *)str;
	
	return seg;
}


qrcodegen_Segment qrcodegen_makeEci(long assignVal) {
	qrcodegen_Segment seg;
	seg.mode = qrcodegen_Mode_ECI;
	seg.numChars = 1;
	
	// Calculate bit length for ECI
	seg.bitLength = 0;
	if (assignVal < 0) {
		seg.bitLength = 0;  // Invalid
	} else if (assignVal < (1 << 7)) {
		seg.bitLength = 8;  // 0 + 7 bits
	} else if (assignVal < (1 << 14)) {
		seg.bitLength = 16; // 10 + 14 bits
	} else {
		seg.bitLength = 24; // 110 + 21 bits (rounded to 24)
	}
	
	seg.data = NULL;  // ECI data is computed on the fly
	
	return seg;
}