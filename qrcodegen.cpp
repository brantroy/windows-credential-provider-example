/* 
 * QR Code generator library (C++)
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

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>


namespace qrcodegen {

/*---- Class QrSegment ----*/

int QrSegment::getModeBits(Mode mode) {
	switch (mode) {
		case Mode::NUMERIC     :  return 0x1;
		case Mode::ALPHANUMERIC:  return 0x2;
		case Mode::BYTE        :  return 0x4;
		case Mode::KANJI       :  return 0x8;
		default:  throw std::logic_error("Assertion error");
	}
}


QrSegment QrSegment::makeBytes(const std::vector<std::uint8_t> &data) {
	if (data.size() > static_cast<std::uint32_t>(INT_MAX))
		throw std::length_error("Data too long");
	BitBuffer bb;
	for (std::uint8_t b : data)
		bb.appendBits(b, 8);
	return QrSegment(Mode::BYTE, static_cast<int>(data.size()), std::move(bb));
}


QrSegment QrSegment::makeNumeric(const char *digits) {
	if (digits == nullptr)
		throw std::domain_error("String is null");
	BitBuffer bb;
	int accumData = 0;
	int accumCount = 0;
	int charCount = 0;
	for (; *digits != '\0'; digits++, charCount++) {
		char c = *digits;
		if (c < '0' || c > '9')
			throw std::domain_error("String contains non-numeric characters");
		accumData = accumData * 10 + (c - '0');
		accumCount++;
		if (accumCount == 3) {
			bb.appendBits(static_cast<std::uint32_t>(accumData), 10);
			accumData = 0;
			accumCount = 0;
		}
	}
	if (accumCount > 0)  // 1 or 2 digits remaining
		bb.appendBits(static_cast<std::uint32_t>(accumData), accumCount * 3 + 1);
	return QrSegment(Mode::NUMERIC, charCount, std::move(bb));
}


QrSegment QrSegment::makeAlphanumeric(const char *text) {
	if (text == nullptr)
		throw std::domain_error("String is null");
	BitBuffer bb;
	int accumData = 0;
	int accumCount = 0;
	int charCount = 0;
	for (; *text != '\0'; text++, charCount++) {
		char c = *text;
		if (c < 0 || c > 127)
			throw std::domain_error("String contains unencodable characters in alphanumeric mode");
		int temp = getAlphanumericCode(c);
		if (temp == -1)
			throw std::domain_error("String contains unencodable characters in alphanumeric mode");
		accumData = accumData * 45 + temp;
		accumCount++;
		if (accumCount == 2) {
			bb.appendBits(static_cast<std::uint32_t>(accumData), 11);
			accumData = 0;
			accumCount = 0;
		}
	}
	if (accumCount > 0)  // 1 character remaining
		bb.appendBits(static_cast<std::uint32_t>(accumData), 6);
	return QrSegment(Mode::ALPHANUMERIC, charCount, std::move(bb));
}


std::vector<QrSegment> QrSegment::makeSegments(const char *text) {
	if (text == nullptr)
		throw std::domain_error("String is null");
	if (*text == '\0')
		return std::vector<QrSegment>();
	
	std::vector<QrSegment> result;
	
	// Choose numeric mode if all input characters are digits
	if (isNumeric(text)) {
		result.push_back(makeNumeric(text));
		return result;
	}
	
	// Choose alphanumeric mode if all input characters are alphanumeric
	if (isAlphanumeric(text)) {
		result.push_back(makeAlphanumeric(text));
		return result;
	}
	
	// Otherwise choose byte mode
	result.push_back(makeBytes(std::vector<std::uint8_t>(
		reinterpret_cast<const std::uint8_t*>(text),
		reinterpret_cast<const std::uint8_t*>(text) + strlen(text))));
	return result;
}


QrSegment QrSegment::makeEci(std::int32_t assignVal) {
	BitBuffer bb;
	if (assignVal < 0)
		throw std::domain_error("ECI assignment value must be non-negative");
	if (assignVal < (1 << 7))
		bb.appendBits(static_cast<std::uint32_t>(assignVal), 8);
	else if (assignVal < (1 << 14)) {
		bb.appendBits(2, 2);
		bb.appendBits(static_cast<std::uint32_t>(assignVal), 14);
	} else if (assignVal < 1000000L) {
		bb.appendBits(6, 3);
		bb.appendBits(static_cast<std::uint32_t>(assignVal), 21);
	} else
		throw std::domain_error("ECI assignment value must be in the range 0 to 999999");
	return QrSegment(Mode::BYTE, 0, std::move(bb));  // Use byte mode because it has no character set
}


bool QrSegment::isNumeric(const char *text) {
	if (text == nullptr)
		throw std::domain_error("String is null");
	for (; *text != '\0'; text++) {
		char c = *text;
		if (c < '0' || c > '9')
			return false;
	}
	return true;
}


bool QrSegment::isAlphanumeric(const char *text) {
	if (text == nullptr)
		throw std::domain_error("String is null");
	for (; *text != '\0'; text++) {
		if (getAlphanumericCode(*text) == -1)
			return false;
	}
	return true;
}


int QrSegment::getAlphanumericCode(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	else {
		static const char *const chars = " $%*+-./:";
		const char *const found = strchr(chars, c);
		return found != nullptr ? static_cast<int>(found - chars) + 36 : -1;
	}
}


QrSegment::QrSegment(Mode md, int numCh, const std::vector<bool> &dt) :
		mode(md),
		dataLen(numCh),
		data(dt) {
	if (numCh < 0)
		throw std::domain_error("Invalid value");
}


QrSegment::QrSegment(Mode md, int numCh, std::vector<bool> &&dt) :
		mode(md),
		dataLen(numCh),
		data(std::move(dt)) {
	if (numCh < 0)
		throw std::domain_error("Invalid value");
}


int QrSegment::getDataLength() const {
	return dataLen;
}


const std::vector<bool> &QrSegment::getData() const {
	return data;
}


QrSegment::Mode QrSegment::getMode() const {
	return mode;
}


/*---- Class QrCode ----*/

int QrCode::getVersion() const {
	return version;
}


int QrCode::getSize() const {
	return size;
}


QrCode::Ecc QrCode::getErrorCorrectionLevel() const {
	return errorCorrectionLevel;
}


int QrCode::getMask() const {
	return mask;
}


bool QrCode::getModule(int x, int y) const {
	return 0 <= x && x < size && 0 <= y && y < size && modules.at(y).at(x);
}


QrCode QrCode::encodeText(const char *text, Ecc ecl) {
	if (text == nullptr)
		throw std::domain_error("String is null");
	std::vector<QrSegment> segs = QrSegment::makeSegments(text);
	return encodeSegments(segs, ecl);
}


QrCode QrCode::encodeBinary(const std::vector<std::uint8_t> &data, Ecc ecl) {
	return encodeSegments(std::vector<QrSegment>(1, QrSegment::makeBytes(data)), ecl);
}


QrCode QrCode::encodeSegments(const std::vector<QrSegment> &segs, Ecc ecl,
		int minVersion, int maxVersion, bool boostEcl) {
	if (minVersion < 1 || minVersion > 40 || maxVersion < 1 || maxVersion > 40 || minVersion > maxVersion)
		throw std::domain_error("Invalid value");
	if (segs.empty())
		throw std::domain_error("No segments provided");
	for (const QrSegment &seg : segs) {
		if (seg.dataLen < 0)
			throw std::domain_error("Invalid segment length");
		if (seg.mode == QrSegment::Mode::KANJI)
			throw std::domain_error("Kanji mode is not supported in this implementation");
	}
	
	// Find the minimal version number to use
	int version, dataUsedBits;
	for (version = minVersion; ; version++) {
		int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;  // Number of data bits available
		dataUsedBits = QrSegment::getTotalBits(segs, version);
		if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
			break;  // This version number is found to be suitable
		if (version >= maxVersion)  // All versions in the range could not fit the given data
			throw std::length_error("Data too long");
	}
	if (dataUsedBits == -1)
		throw std::logic_error("Assertion error");
	
	// Increase the error correction level while the data still fits in the current version number
	for (Ecc newEcl : {Ecc::MEDIUM, Ecc::QUARTILE, Ecc::HIGH}) {  // From low to high
		if (boostEcl && dataUsedBits <= getNumDataCodewords(version, newEcl) * 8)
			ecl = newEcl;
	}
	
	// Create the data bit string by concatenating all segments
	BitBuffer bb;
	for (const QrSegment &seg : segs) {
		bb.appendBits(QrSegment::getModeBits(seg.mode), 4);
		bb.appendBits(static_cast<std::uint32_t>(seg.dataLen), QrSegment::getNumCharacterCountBits(seg.mode, version));
		bb.insert(bb.end(), seg.data.begin(), seg.data.end());
	}
	
	// Add terminator and pad up to a byte if applicable
	size_t dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
	if (bb.size() > static_cast<std::size_t>(dataCapacityBits))
		throw std::logic_error("Assertion error");
	bb.appendBits(0, std::min(4, static_cast<int>(dataCapacityBits - bb.size())));
	bb.appendBits(0, (8 - static_cast<int>(bb.size() % 8)) % 8);
	
	// Pad with alternating bytes until data capacity is reached
	for (std::uint8_t padByte = 0xEC; bb.size() < static_cast<std::size_t>(dataCapacityBits); padByte ^= 0xEC ^ 0x11)
		bb.appendBits(padByte, 8);
	
	// Pack bits into bytes in a byte vector
	std::vector<std::uint8_t> dataCodewords;
	dataCodewords.reserve(bb.size() / 8);
	for (std::size_t i = 0; i < bb.size(); i += 8) {
		std::uint8_t b = 0;
		for (int j = 0; j < 8; j++)
			b = (b << 1) | (bb.at(i + j) ? 1 : 0);
		dataCodewords.push_back(b);
	}
	
	// Create the QR Code object
	return QrCode(version, ecl, dataCodewords, -1);
}


QrCode::QrCode(int ver, Ecc ecl, const std::vector<std::uint8_t> &dataCodewords, int mask) :
		version(ver),
		size(ver * 4 + 17),
		errorCorrectionLevel(ecl) {
	if (ver < 1 || ver > 40)
		throw std::domain_error("Version value out of range");
	if (mask < -1 || mask > 7)
		throw std::domain_error("Mask value out of range");
	
	modules    = std::vector<std::vector<bool>>(size, std::vector<bool>(size));
	isFunction = std::vector<std::vector<bool>>(size, std::vector<bool>(size));
	
	// Draw function patterns, draw all codewords, do masking
	drawFunctionPatterns();
	const std::vector<std::uint8_t> allCodewords = appendErrorCorrection(dataCodewords);
	drawCodewords(allCodewords);
	
	// Find the best mask
	if (mask == -1) {  // Do a brute-force search for the mask that yields lowest penalty
		long minPenalty = LONG_MAX;
		for (int i = 0; i < 8; i++) {
			drawFormatBits(i);
			applyMask(i);
			long penalty = getPenaltyScore();
			if (penalty < minPenalty) {
				mask = i;
				minPenalty = penalty;
			}
			applyMask(i);  // Undoes the mask due to XOR
		}
	}
	if (mask < 0 || mask > 7)
		throw std::logic_error("Assertion error");
	drawFormatBits(mask);  // Overwrite old format bits
	applyMask(mask);  // Apply the final choice of mask
}


std::vector<std::uint8_t> QrCode::appendErrorCorrection(const std::vector<std::uint8_t> &data) const {
	if (data.size() != static_cast<std::size_t>(getNumDataCodewords(version, errorCorrectionLevel)))
		throw std::domain_error("Invalid argument");
	
	// Calculate parameter numbers
	int numBlocks = QrCode::getNumErrorCorrectionBlocks(errorCorrectionLevel, version);
	int blockEccLen = getNumEccCodewords(version, errorCorrectionLevel) / numBlocks;
	int numShortBlocks = numBlocks - getNumRawDataModules(version) % numBlocks;
	int shortBlockLen = getNumRawDataModules(version) / numBlocks / 8;
	
	// Split data into blocks and append ECC to each block
	std::vector<std::vector<std::uint8_t>> blocks;
	const std::vector<std::uint8_t> rsDiv = reedSolomonComputeDivisor(blockEccLen);
	for (int i = 0, k = 0; i < numBlocks; i++) {
		std::vector<std::uint8_t> dat(data.cbegin() + k, data.cbegin() + k + shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1));
		const std::vector<std::uint8_t> ecc = reedSolomonComputeRemainder(dat, rsDiv);
		dat.insert(dat.end(), ecc.cbegin(), ecc.cend());
		blocks.push_back(std::move(dat));
		k += dat.size() - blockEccLen;
	}
	
	// Interleave (not concatenate) the bytes from every block into a single sequence
	std::vector<std::uint8_t> result;
	for (int i = 0; static_cast<std::size_t>(i) < blocks.at(0).size(); i++) {
		for (int j = 0; static_cast<std::size_t>(j) < blocks.size(); j++) {
			// Skip the padding byte in short blocks
			if (i != shortBlockLen - blockEccLen || j >= numShortBlocks) {
				result.push_back(blocks.at(j).at(i));
			}
		}
	}
	return result;
}


void QrCode::drawFunctionPatterns() {
	// Draw horizontal and vertical timing patterns
	for (int i = 0; i < size; i++) {
		setFunctionModule(6, i, i % 2 == 0);
		setFunctionModule(i, 6, i % 2 == 0);
	}
	
	// Draw 3 finder patterns (all corners except bottom right; overwrites some timing modules)
	drawFinderPattern(3, 3);
	drawFinderPattern(size - 4, 3);
	drawFinderPattern(3, size - 4);
	
	// Draw numerous alignment patterns
	const std::vector<int> alignPatPos = getAlignmentPatternPositions(version);
	int numAlign = static_cast<int>(alignPatPos.size());
	for (int i = 0; i < numAlign; i++) {
		for (int j = 0; j < numAlign; j++) {
			// Don't draw on the three finder corners
			if (!((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)))
				drawAlignmentPattern(alignPatPos.at(i), alignPatPos.at(j));
		}
	}
	
	// Draw configuration data
	drawFormatBits(0);  // Dummy mask value; overwritten later in the constructor
	drawVersion();
}


void QrCode::drawFormatBits(int mask) {
	// Calculate error correction code and pack bits
	int data = getFormatBits(errorCorrectionLevel) << 3 | mask;  // errCorrLvl is uint2, mask is uint3
	int rem = data;
	for (int i = 0; i < 10; i++)
		rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	const int bits = (data << 10 | rem) ^ 0x5412;  // uint15
	if (bits >> 15 != 0)
		throw std::logic_error("Assertion error");
	
	// Draw first copy
	for (int i = 0; i <= 5; i++)
		setFunctionModule(8, i, getBit(bits, i));
	setFunctionModule(8, 7, getBit(bits, 6));
	setFunctionModule(8, 8, getBit(bits, 7));
	setFunctionModule(7, 8, getBit(bits, 8));
	for (int i = 9; i < 15; i++)
		setFunctionModule(14 - i, 8, getBit(bits, i));
	
	// Draw second copy
	for (int i = 0; i < 8; i++)
		setFunctionModule(size - 1 - i, 8, getBit(bits, i));
	for (int i = 8; i < 15; i++)
		setFunctionModule(8, size - 15 + i, getBit(bits, i));
	setFunctionModule(8, size - 8, true);  // Always dark
}


void QrCode::drawVersion() {
	if (version < 7)
		return;
	
	// Calculate error correction code and pack bits
	int rem = version;  // version is uint6, in the range [7, 40]
	for (int i = 0; i < 12; i++)
		rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
	const int bits = version << 12 | rem;  // uint18
	if (bits >> 18 != 0)
		throw std::logic_error("Assertion error");
	
	// Draw two copies
	for (int i = 0; i < 18; i++) {
		bool bit = getBit(bits, i);
		int a = size - 11 + i % 3;
		int b = i / 3;
		setFunctionModule(a, b, bit);
		setFunctionModule(b, a, bit);
	}
}


void QrCode::drawFinderPattern(int x, int y) {
	for (int dy = -4; dy <= 4; dy++) {
		for (int dx = -4; dx <= 4; dx++) {
			int dist = std::max(std::abs(dx), std::abs(dy));  // Chebyshev/infinity norm
			int xx = x + dx, yy = y + dy;
			if (0 <= xx && xx < size && 0 <= yy && yy < size)
				setFunctionModule(xx, yy, dist != 2 && dist != 4);
		}
	}
}


void QrCode::drawAlignmentPattern(int x, int y) {
	for (int dy = -2; dy <= 2; dy++) {
		for (int dx = -2; dx <= 2; dx++) {
			setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
		}
	}
}


void QrCode::setFunctionModule(int x, int y, bool isDark) {
	modules.at(y).at(x) = isDark;
	isFunction.at(y).at(x) = true;
}


bool QrCode::getBit(long val, int i) {
	return ((val >> i) & 1) != 0;
}


std::vector<int> QrCode::getAlignmentPatternPositions(int ver) {
	if (ver < 1 || ver > 40)
		throw std::domain_error("Version value out of range");
	else if (ver == 1)
		return std::vector<int>();
	else {
		int numAlign = ver / 7 + 2;
		int step = (ver == 32) ? 26 :
			(ver == 16) ? 28 :
			(ver == 8)  ? 26 :
			(ver == 4)  ? 24 :
			(ver == 2)  ? 18 :
			2 * (ver / 2 - 2) + 2;  // ceil((size - 13) / (numAlign * 2 - 2)) * 2
		std::vector<int> result;
		result.push_back(6);
		for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step)
			result.insert(result.begin(), pos);
		return result;
	}
}


int QrCode::getNumRawDataModules(int ver) {
	if (ver < 1 || ver > 40)
		throw std::domain_error("Version value out of range");
	int result = (16 * ver + 128) * ver + 64;
	if (ver >= 2) {
		int numAlign = ver / 7 + 2;
		result -= (25 * numAlign - 10) * numAlign - 55;
		if (ver >= 7)
			result -= 36;
	}
	return result;
}


int QrCode::getNumDataCodewords(int ver, Ecc ecl) {
	return getNumRawDataModules(ver) / 8 - getNumEccCodewords(ver, ecl);
}


int QrCode::getNumEccCodewords(int ver, Ecc ecl) {
	static const int TABLE[41][4] = {
		{-1, -1, -1, -1},  // Version 0
		{ 7, 10, 13, 17},  // Version 1
		{10, 16, 22, 28},  // Version 2
		{15, 26, 36, 44},  // Version 3
		{20, 36, 52, 64},  // Version 4
		{26, 48, 72, 88},  // Version 5
		{36, 64, 96, 112},  // Version 6
		{40, 78, 116, 130},  // Version 7
		{48, 98, 144, 172},  // Version 8
		{60, 122, 178, 204},  // Version 9
		{ 72, 148, 216, 252},  // Version 10
		{ 80, 178, 252, 292},  // Version 11
		{ 96, 212, 312, 364},  // Version 12
		{104, 240, 360, 416},  // Version 13
		{120, 280, 416, 480},  // Version 14
		{132, 322, 476, 552},  // Version 15
		{144, 368, 540, 620},  // Version 16
		{168, 416, 608, 696},  // Version 17
		{180, 468, 680, 776},  // Version 18
		{196, 522, 756, 868},  // Version 19
		{224, 588, 840, 960},  // Version 20
		{224, 650, 928, 1056},  // Version 21
		{252, 716, 1020, 1160},  // Version 22
		{272, 784, 1120, 1280},  // Version 23
		{296, 856, 1224, 1408},  // Version 24
		{320, 932, 1332, 1528},  // Version 25
		{344, 1012, 1444, 1656},  // Version 26
		{368, 1096, 1560, 1784},  // Version 27
		{392, 1184, 1680, 1920},  // Version 28
		{416, 1276, 1804, 2064},  // Version 29
		{440, 1372, 1932, 2208},  // Version 30
		{464, 1472, 2064, 2360},  // Version 31
		{492, 1576, 2196, 2512},  // Version 32
		{520, 1684, 2332, 2668},  // Version 33
		{548, 1796, 2472, 2828},  // Version 34
		{576, 1912, 2616, 2992},  // Version 35
		{604, 2032, 2764, 3160},  // Version 36
		{632, 2156, 2916, 3332},  // Version 37
		{660, 2284, 3072, 3508},  // Version 38
		{688, 2416, 3232, 3688},  // Version 39
		{716, 2552, 3396, 3872},  // Version 40
	};
	return TABLE[ver][static_cast<int>(ecl)];
}


int QrCode::getNumErrorCorrectionBlocks(Ecc ecl, int ver) {
	static const int TABLE[41][4] = {
		{-1, -1, -1, -1},  // Version 0
		{ 1, 1, 1, 1},  // Version 1
		{ 1, 1, 1, 1},  // Version 2
		{ 1, 1, 2, 2},  // Version 3
		{ 2, 2, 4, 4},  // Version 4
		{ 2, 4, 4, 4},  // Version 5
		{ 4, 4, 6, 6},  // Version 6
		{ 4, 4, 6, 6},  // Version 7
		{ 4, 6, 8, 8},  // Version 8
		{ 5, 6, 8, 8},  // Version 9
		{ 6, 8,10,10},  // Version 10
		{ 8, 8,12,12},  // Version 11
		{ 8, 8,12,12},  // Version 12
		{ 8,10,14,14},  // Version 13
		{ 9,12,16,16},  // Version 14
		{ 9,12,16,16},  // Version 15
		{10,12,18,18},  // Version 16
		{12,16,20,20},  // Version 17
		{12,16,20,20},  // Version 18
		{12,16,22,22},  // Version 19
		{13,18,24,24},  // Version 20
		{14,20,26,26},  // Version 21
		{15,22,28,28},  // Version 22
		{16,24,30,30},  // Version 23
		{17,26,32,32},  // Version 24
		{18,28,34,34},  // Version 25
		{19,30,36,36},  // Version 26
		{20,32,38,38},  // Version 27
		{21,34,40,40},  // Version 28
		{22,36,42,42},  // Version 29
		{24,38,44,44},  // Version 30
		{25,40,46,46},  // Version 31
		{26,42,48,48},  // Version 32
		{28,44,50,50},  // Version 33
		{29,46,52,52},  // Version 34
		{30,48,54,54},  // Version 35
		{31,50,56,56},  // Version 36
		{33,52,58,58},  // Version 37
		{35,54,60,60},  // Version 38
		{37,56,62,62},  // Version 39
		{38,58,64,64},  // Version 40
	};
	return TABLE[ver][static_cast<int>(ecl)];
}


int QrCode::getFormatBits(Ecc ecl) {
	switch (ecl) {
		case Ecc::LOW   :  return 1;
		case Ecc::MEDIUM:  return 0;
		case Ecc::QUARTILE: return 3;
		case Ecc::HIGH  :  return 2;
		default:  throw std::logic_error("Assertion error");
	}
}


void QrCode::drawCodewords(const std::vector<std::uint8_t> &data) {
	if (static_cast<int>(data.size()) != getNumDataCodewords(version, errorCorrectionLevel))
		throw std::domain_error("Invalid argument");
	
	size_t i = 0;  // Bit index into the data
	// Do the funny zigzag scan
	for (int right = size - 1; right >= 1; right -= 2) {  // Index of right column in each column pair
		if (right == 6)
			right = 5;
		for (int vert = 0; vert < size; vert++) {  // Vertical counter
			for (int j = 0; j < 2; j++) {
				int x = right - j;  // Actual x coordinate
				bool upward = ((right + 1) & 2) == 0;
				int y = upward ? size - 1 - vert : vert;  // Actual y coordinate
				if (!isFunction.at(y).at(x) && i < data.size() * 8) {
					modules.at(y).at(x) = getBit(data.at(i >> 3), 7 - (static_cast<int>(i) & 7));
					i++;
				}
				// If this QR Code has any remainder bits (0 to 7), they were assigned as
				// 0/false/light by the constructor and are left unchanged by this method
			}
		}
	}
	if (static_cast<int>(i) != getNumDataCodewords(version, errorCorrectionLevel) * 8)
		throw std::logic_error("Assertion error");
}


void QrCode::applyMask(int mask) {
	if (mask < 0 || mask > 7)
		throw std::domain_error("Mask value out of range");
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
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
				default:  throw std::logic_error("Assertion error");
			}
			modules.at(y).at(x) = modules.at(y).at(x) ^ (invert & !isFunction.at(y).at(x));
		}
	}
}


long QrCode::getPenaltyScore() const {
	long result = 0;
	
	// Adjacent modules in row having same color, and finder-like patterns
	for (int y = 0; y < size; y++) {
		bool runColor = false;
		int runX = 0;
		int runHistory[7] = {};
		for (int x = 0; x < size; x++) {
			if (modules.at(y).at(x) == runColor) {
				runX++;
				if (runX == 5)
					result += 3;
				else if (runX > 5)
					result++;
			} else {
				QrCode::finderPenaltyAddHistory(runX, runHistory);
				if (!runColor)
					result += QrCode::finderPenaltyCountPatterns(runHistory) * 40;
				runColor = modules.at(y).at(x);
				runX = 1;
			}
		}
		result += QrCode::finderPenaltyTerminateAndCount(runColor, runX, runHistory) * 40;
	}
	// Adjacent modules in column having same color, and finder-like patterns
	for (int x = 0; x < size; x++) {
		bool runColor = false;
		int runY = 0;
		int runHistory[7] = {};
		for (int y = 0; y < size; y++) {
			if (modules.at(y).at(x) == runColor) {
				runY++;
				if (runY == 5)
					result += 3;
				else if (runY > 5)
					result++;
			} else {
				QrCode::finderPenaltyAddHistory(runY, runHistory);
				if (!runColor)
					result += QrCode::finderPenaltyCountPatterns(runHistory) * 40;
				runColor = modules.at(y).at(x);
				runY = 1;
			}
		}
		result += QrCode::finderPenaltyTerminateAndCount(runColor, runY, runHistory) * 40;
	}
	
	// 2*2 blocks of modules having same color
	for (int y = 0; y < size - 1; y++) {
		for (int x = 0; x < size - 1; x++) {
			bool  color = modules.at(y).at(x);
			if (color == modules.at(y).at(x + 1) &&
			    color == modules.at(y + 1).at(x) &&
			    color == modules.at(y + 1).at(x + 1))
				result += 3;
		}
	}
	
	// Balance of dark and light modules
	int dark = 0;
	for (const std::vector<bool> &row : modules)
		for (bool color : row)
			if (color)
				dark++;
	long total = size * size;  // Note that size is odd, so dark/total != 1/2
	// Compute the smallest integer k >= 0 such that (45-5k)% <= dark/total <= (55+5k)%
	int k = static_cast<int>((std::abs(dark * 20 - total * 10) + total - 1) / total) - 1;
	result += k * 10;
	return result;
}


int QrCode::finderPenaltyCountPatterns(const int *runHistory) {
	int n = runHistory[1];
	if (n > size * 3)
		throw std::logic_error("Assertion error");
	bool core = n > 0 && runHistory[2] == n && runHistory[3] == n * 3 && runHistory[4] == n && runHistory[5] == n;
	// The maximum QR Code size is 177, hence the dark run length n <= 177.
	// Arithmetic is promoted to int, so n*4 will not overflow.
	return (core && runHistory[0] >= n * 4 && runHistory[6] >= n ? 1 : 0) +
	       (core && runHistory[6] >= n * 4 && runHistory[0] >= n ? 1 : 0);
}


int QrCode::finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, int *runHistory) {
	if (currentRunColor) {  // Terminate dark run
		QrCode::finderPenaltyAddHistory(currentRunLength, runHistory);
		currentRunLength = 0;
	}
	currentRunLength += 2;  // Add light border to final run
	QrCode::finderPenaltyAddHistory(currentRunLength, runHistory);
	return QrCode::finderPenaltyCountPatterns(runHistory);
}


void QrCode::finderPenaltyAddHistory(int currentRunLength, int *runHistory) {
	if (runHistory[0] == 0)
		currentRunLength += 1;
	std::memmove(&runHistory[1], &runHistory[0], 6 * sizeof(int));
	runHistory[0] = currentRunLength;
}


bool QrSegment::isValid() const {
	return dataLen >= 0 && dataLen <= INT_MAX && static_cast<std::size_t>(dataLen) <= data.size();
}


int QrSegment::getNumCharacterCountBits(Mode mode, int version) {
	if (version < 1 || version > 40)
		throw std::domain_error("Version value out of range");
	int ccBits[4][41] = {
		{-1, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88},  // Numeric
		{-1,  9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 87},  // Alphanumeric
		{-1,  8, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16},  // Byte
		{-1,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86},  // Kanji
	};
	return ccBits[static_cast<int>(mode)][version];
}


int QrSegment::getTotalBits(const std::vector<QrSegment> &segs, int version) {
	int result = 0;
	for (const QrSegment &seg : segs) {
		if (seg.dataLen < 0)
			return -1;
		int ccbits = getNumCharacterCountBits(seg.mode, version);
		if (ccbits < 0)
			return -1;
		long temp = 4L + ccbits + seg.data.size();
		if (temp > INT_MAX)
			return -1;
		result += static_cast<int>(temp);
	}
	return result;
}


/*---- Class BitBuffer ----*/

BitBuffer::BitBuffer() :
		std::vector<bool>() {}


void BitBuffer::appendBits(std::uint32_t val, int len) {
	if (len < 0 || len > 31 || val >> len != 0)
		throw std::domain_error("Value out of range");
	for (int i = len - 1; i >= 0; i--)  // Append bit by bit
		push_back(((val >> i) & 1) != 0);
}

}


std::vector<std::uint8_t> reedSolomonComputeRemainder(const std::vector<std::uint8_t> &data, const std::vector<std::uint8_t> &generator) {
	std::vector<std::uint8_t> result(generator.size());
	for (std::uint8_t b : data) {  // Polynomial division
		std::uint8_t factor = b ^ result.at(0);
		result.erase(result.begin());
		result.push_back(0);
		for (std::size_t i = 0; i < result.size(); i++)
			result.at(i) ^= generator.at(i) * factor;
	}
	return result;
}


std::vector<std::uint8_t> reedSolomonComputeDivisor(int degree) {
	if (degree < 1 || degree > 255)
		throw std::domain_error("Degree out of range");
	// Polynomial coefficients are stored from highest to lowest power, excluding the leading coefficient (which is always 1).
	// For example the polynomial x^3 + 255x^2 + 8x + 93 is stored as the uint8 array {255, 8, 93}.
	std::vector<std::uint8_t> result(static_cast<std::size_t>(degree));
	result.at(result.size() - 1) = 1;  // Start off with the monomial x^0
	
	// Compute the product polynomial (x - r^0) * (x - r^1) * (x - r^2) * ... * (x - r^{degree-1}),
	// and drop the highest monomial term which is always 1x^degree.
	// Note that r = 0x02, which is a generator element of this field GF(2^8/0x11D).
	std::uint8_t root = 1;
	for (int i = 0; i < degree; i++) {
		// Multiply the current product by (x - r^i)
		for (std::size_t j = 0; j < result.size(); j++) {
			result.at(j) = qrcodegen::reedSolomonMultiply(result.at(j), root);
		}
		if (i + 1 < static_cast<int>(result.size()))
			result.at(i + 1) = 1;  // Set the next monomial from highest to lowest power
		root = qrcodegen::reedSolomonMultiply(root, 0x02);
	}
	return result;
}


std::uint8_t qrcodegen::reedSolomonMultiply(std::uint8_t x, std::uint8_t y) {
	// Russian peasant multiplication
	std::uint16_t z = 0;
	for (int i = 7; i >= 0; i--) {
		z = (z << 1) ^ ((z >> 7) * 0x11D);
		z ^= ((y >> i) & 1) * x;
	}
	if (z >> 8 != 0)
		throw std::logic_error("Assertion error");
	return static_cast<std::uint8_t>(z);
}