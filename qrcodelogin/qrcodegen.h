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

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>


namespace qrcodegen {

/* 
 * A segment of character/binary/control data in a QR Code symbol.
 * Instances of this class are immutable.
 * The mid-level way to create a segment is to take the payload data
 * and call a static factory function such as QrSegment::makeNumeric().
 * The low-level way to create a segment is to custom-make the bit buffer
 * and call the QrSegment() constructor with appropriate values.
 * This segment class imposes no length restrictions, but QR Codes have restrictions.
 * Even in the most favorable conditions, a QR Code can only hold 7089 characters of data.
 * Any segment longer than this is meaningless for the purpose of generating QR Codes.
 */
class QrSegment final {
	
	/* Public static factory functions */
public:
	
	// Returns a segment representing the given binary data encoded in
	// byte mode. All input byte vectors are acceptable. Any text string
	// can be converted to UTF-8 bytes and encoded as a byte mode segment.
	static QrSegment makeBytes(const std::vector<std::uint8_t> &data);
	
	// Returns a segment representing the given string of decimal digits encoded in numeric mode.
	static QrSegment makeNumeric(const char *digits);
	
	// Returns a segment representing the given text string encoded in alphanumeric mode.
	// The characters allowed are: 0 to 9, A to Z (uppercase only), space,
	// dollar, percent, asterisk, plus, hyphen, period, slash, colon.
	static QrSegment makeAlphanumeric(const char *text);
	
	// Returns a segment representing an Extended Channel Interpretation
	// (ECI) designator with the given assignment value.
	static QrSegment makeEci(long assignVal);
	
	/* Public fields */
public:
	
	// The mode indicator of this segment. Never null.
	const Mode mode;
	
	// The length of this segment's unencoded data. Measured in characters for
	// numeric/alphanumeric/kanji mode, bytes for byte mode, and 0 for ECI mode.
	// Always zero or positive. Not the same as the data's bit length.
	const int numChars;
	
	// The data bits of this segment. Accessed through getData().
	const std::vector<bool> data;
	
	
	/* Private constructor */
private:
	
	// Creates a new QR Code segment with the given attributes and data.
	// The parameter data should not be nullptr. The strings should not be empty.
	// A segment is immutable after it is constructed.
	QrSegment(Mode md, int numCh, const std::vector<bool> &dt);
	QrSegment(Mode md, int numCh, std::vector<bool> &&dt);
	
	
	/* Public methods */
public:
	
	// Returns the data bits of this segment.
	const std::vector<bool> &getData() const;
	
	// (Package-private) Calculates the number of bits needed to encode the given segments at
	// the given version. Returns a non-negative number, or throws an exception if the total
	// is too large to fit in a segment of that version at that error correction level.
	static int getTotalBits(const std::vector<QrSegment> &segs, int version);
	
	
	/* Private nested constants */
private:
	
	// Describes precisely all strings that are encodable in numeric mode.
	static bool isNumeric(const char *chars);
	
	// Describes precisely all strings that are encodable in alphanumeric mode.
	static bool isAlphanumeric(const char *chars);
	
};



/* 
 * A QR Code symbol, which is a type of two-dimension barcode.
 * Invented by Denso Wave and described in the ISO/IEC 18004 standard.
 * Instances of this class represent an immutable square grid of dark and light cells.
 * The class provides static factory functions to create a QR Code from text or binary data.
 * The class covers the QR Code Model 2 specification, supporting all versions (sizes)
 * from 1 to 40, all 4 error correction levels, and 4 character encoding modes.
 * 
 * Ways to create a QR Code object:
 * - High level: Take the payload data and call QrCode::encodeText() or QrCode::encodeBinary().
 * - Mid level: Custom-make the list of segments and call
 *   QrCode::encodeSegments() or QrCode::encodeSegmentsEcc().
 * - Low level: Custom-make the array of data codeword bytes (including
 *   segment headers and final padding, excluding error correction codewords),
 *   supply the appropriate version number, and call the QrCode() constructor.
 * (Note that all ways require supplying the desired error correction level.)
 */
class QrCode final {
	
	/* Public constants */
public:
	
	// The error correction level in a QR Code symbol.
	enum class Ecc {
		LOW = 0 ,  // The QR Code can tolerate about  7% erroneous codewords
		MEDIUM  ,  // The QR Code can tolerate about 15% erroneous codewords
		QUARTILE,  // The QR Code can tolerate about 25% erroneous codewords
		HIGH    ,  // The QR Code can tolerate about 30% erroneous codewords
	};
	
	
	/* Public static factory functions */
public:
	
	// Returns a QR Code representing the given Unicode text string at the given error correction level.
	// As a conservative upper bound, this function is guaranteed to succeed for strings that have 2953 or fewer
	// UTF-8 code units (not Unicode code points) (and no more than 196868 bits of state at most).
	// The smallest possible QR Code version is automatically chosen for the output. Returns null if the
	// data is too long to fit in any version at the given ECC level.
	static QrCode encodeText(const char *text, Ecc ecl);
	
	// Returns a QR Code representing the given binary data at the given error correction level.
	// This function always encodes using the byte mode. The smallest possible QR Code version is
	// automatically chosen for the output. Returns null if the data is too long to fit in any version.
	static QrCode encodeBinary(const std::vector<std::uint8_t> &data, Ecc ecl);
	
	// Returns a QR Code representing the given segments with the given encoding parameters.
	// The smallest possible QR Code version within the given range is automatically chosen for the output.
	// Returns null if the data is too long to fit in any version within the range at the given ECC level.
	static QrCode encodeSegments(const std::vector<QrSegment> &segs, Ecc ecl,
		int minVersion=1, int maxVersion=40, int mask=-1, bool boostEcl=true);  // All optional parameters
	
	
	/* Public fields */
public:
	
	// This QR Code's version number, in the range [1, 40].
	const int version;
	
	// The width and height of this QR Code, measured in modules, between
	// 21 and 177 (inclusive). This is equal to version * 4 + 17.
	const int size;
	
	// The error correction level used in this QR Code.
	const Ecc errorCorrectionLevel;
	
	// The index of the mask pattern used in this QR Code, which is between 0 and 7 (inclusive).
	// Even if a QR Code is created with automatic masking requested (mask = -1),
	// this property yields the concrete mask used to generate this QR Code.
	const int mask;
	
	
	/* Public constructor */
public:
	
	// Creates a new QR Code with the given version number,
	// error correction level, data codeword bytes, and mask number.
	// This is a low-level constructor that controls the exact layout of the QR Code.
	QrCode(int ver, Ecc ecl, const std::vector<std::uint8_t> &dataCodewords, int msk);
	
	
	/* Public methods */
public:
	
	// Returns the color of the module (pixel) at the given coordinates, which is false
	// for light or true for dark. The top left corner has the coordinates (x=0, y=0).
	// If the given coordinates are out of bounds, then false (light) is returned.
	bool getModule(int x, int y) const;
	
	// Converts this QR Code to SVG XML text.
	// The string always uses Unix newlines (\n), regardless of the platform.
	std::string toSvgString(int border) const;
	
	
	/* Private fields */
private:
	
	// Immutable scalar parameters:
	const std::vector<std::uint8_t> modules;     // The modules of this QR Code (false = light, true = dark)
	const std::vector<std::uint8_t> *alignmentPatternPositions;
	const int numErrorCorrectionCodewords;
	const std::vector<int> *versionInfoBits;
	const std::vector<int> *formatInfoBits;
	
	// Constructor (low level) and fields
	QrCode(int ver, Ecc ecl, std::vector<std::uint8_t> &&dataCodewords, int msk);
	
	/* Private static helper functions */
private:
	
	// Returns a Reed-Solomon ECC generator polynomial for the given degree. This could be
	// implemented as a lookup table over all possible parameter values, instead of as an algorithm.
	static std::vector<std::uint8_t> reedSolomonComputeDivisor(int degree);
	
	// Returns the Reed-Solomon error correction codeword for the given data and divisor polynomials.
	static std::vector<std::uint8_t> reedSolomonComputeRemainder(const std::vector<std::uint8_t> &data, const std::vector<std::uint8_t> &divisor);
	
	// Returns the product of the two given field elements modulo GF(2^8/0x11D).
	// All inputs are valid. This could be implemented as a 256*256 lookup table.
	static std::uint8_t reedSolomonMultiply(std::uint8_t x, std::uint8_t y);
	
	// Can only be called immediately after a light run is added, and
	// returns either 0, 1, or 2. A helper function for getPenaltyScore().
	int finderPenaltyCountPatterns(const std::vector<int> &runHistory) const;
	
	// Must be called at the end of a line (row or column) of modules. A helper function for getPenaltyScore().
	int finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::vector<int> &runHistory) const;
	
	// Pushes the given value to the front and drops the last value. A helper function for getPenaltyScore().
	void finderPenaltyAddHistory(int currentRunLength, std::vector<int> &runHistory) const;
	
	// Returns true iff the i'th bit of x is set to 1.
	static bool getBit(long x, int i);
	
	/* Private static function and constants */
private:
	
	// Returns the number of data bits that can be stored in a QR Code of the given version number, after
	// all function modules are excluded. This includes remainder bits, so it might not be a multiple of 8.
	// The result is in the range [208, 29648]. This could be implemented as a 40-entry lookup table.
	static int getNumRawDataModules(int ver);
	
	// Returns the number of 8-bit data (i.e. not error correction) codewords contained in any
	// QR Code of the given version number and error correction level, with remainder bits discarded.
	// This state is not a multiple of 8, so it cannot be used directly in byte-oriented code.
	// This could be implemented as a (40*4)-cell lookup table.
	static int getNumDataCodewords(int ver, Ecc ecl);
	
	// Returns a set of positions of the alignment patterns in a QR Code of the given version number.
	// Each position is in the range [0,177), and are in ascending order.
	// This could be implemented as a 40-entry lookup table.
	static const std::vector<int> &getAlignmentPatternPositions(int ver);
	
	static const int PENALTY_N1;
	static const int PENALTY_N2;
	static const int PENALTY_N3;
	static const int PENALTY_N4;
	
	static const std::vector<int> VERSION_INFO_BITS;
	
};



/* 
 * The set of all possible data mode indicators.
 * Not public. See the QrSegment class for why this is never used in the code.
 */
enum class Mode {
	NUMERIC      = 0x1,
	ALPHANUMERIC = 0x2,
	BYTE         = 0x4,
	KANJI        = 0x8,
	ECI          = 0x7,
};

}

/* 
 * QR Code generator library (C)
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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/*
 * The type of QR Code error correction level. See the class level comment for details.
 */
typedef enum {
	qrcodegen_Ecc_LOW = 0 ,  // The QR Code can tolerate about  7% erroneous codewords
	qrcodegen_Ecc_MEDIUM  ,  // The QR Code can tolerate about 15% erroneous codewords
	qrcodegen_Ecc_QUARTILE,  // The QR Code can tolerate about 25% erroneous codewords
	qrcodegen_Ecc_HIGH    ,  // The QR Code can tolerate about 30% erroneous codewords
} qrcodegen_Ecc;


/*
 * The type of QR Code data mode. See the class level comment for details.
 */
typedef enum {
	qrcodegen_Mode_NUMERIC      = 0x1,
	qrcodegen_Mode_ALPHANUMERIC = 0x2,
	qrcodegen_Mode_BYTE         = 0x4,
	qrcodegen_Mode_KANJI        = 0x8,
	qrcodegen_Mode_ECI          = 0x7,
} qrcodegen_Mode;


/*
 * A segment of character/binary/control data in a QR Code symbol.
 * The numChars field has a different meaning depending on the mode:
 * - Numeric:  Number of 3-character groups (rounded up)
 * - Alphanumeric: Number of 2-character groups (rounded up)
 * - Byte:   Number of bytes
 * - Kanji:  Number of 2-byte pairs
 * - ECI:    Exactly 1
 * For more details, see the QR Code specification.
 */
struct qrcodegen_Segment {
	qrcodegen_Mode mode;
	int numChars;
	const uint8_t *data;
	int bitLength;
};
typedef struct qrcodegen_Segment qrcodegen_Segment;


/*
 * The worst-case number of bytes needed to store one QR Code, up to version 40.
 * When creating and populating the QR Code struct, ensure that the buffer size
 * is at least this length. This value equals 3918, which is just under 4 kilobytes.
 */
#define qrcodegen_BUFFER_LEN_MAX  3918


/*
 * A mutable mask of n bits (in an array of bytes), where the least significant
 * bit of the last byte indicates the nth bit. The length of this array is
 * ceil(len / 8) bytes (ceiling of len/8). The bits are packed in little endian.
 */
typedef uint8_t qrcodegen_Mask[len];


/*
 * The QR Code structure represents one whole QR Code symbol up to version 40.
 * The fields qr, mask, and dataMask are managed by the construction functions.
 * The field size is set to the size of the QR Code (4 * version + 17).
 * The field version is set to the version of the QR Code (1-40).
 * The field errorCorrectionLevel is set to the ECC level of the QR Code (0-3).
 */
struct qrcodegen_QrCode {
	uint8_t *qr;
	int mask;
	int size;
	int version;
	int errorCorrectionLevel;
	uint8_t *dataMask;
};
typedef struct qrcodegen_QrCode qrcodegen_QrCode;


/*---- Macro constants and functions ----*/

#define qrcodegen_VERSION_MIN  1
#define qrcodegen_VERSION_MAX  40

// Calculates the number of bytes needed to store the QR Code's data bits, given its version.
// The result is ceil(version * 4 + 17)^2 / 8 + 1. Always returns a positive integer.
// Example: 1 -> 40, 10 -> 1342, 20 -> 3392, 30 -> 6284, 40 -> 9988.
#define qrcodegen_BUFFER_LEN_FOR_VERSION(version)  ((((version) * 4 + 17) * ((version) * 4 + 17) + 7) / 8 + 1)


/*---- Public function prototypes ----*/

/* 
 * Encodes the given text string to a QR Code, storing it in the given array of
 * uint8_t variables. Returns true if encoding succeeded, or false if the data
 * is too long to fit in any version at the given error correction level.
 * 
 * The capacity of the array is set by the caller and must be at least
 * qrcodegen_BUFFER_LEN_FOR_VERSION(version) bytes long. The length of the
 * resulting QR Code can be found by calling qrcodegen_getSize().
 * 
 * The arrays tempBuffer and eclBuffer can be viewed as scratch space, and can
 * be the same as each other. They must be at least as long as buffer.
 * 
 * If version = -1, then the smallest version QR Code that fits the data is
 * automatically chosen for the output. Otherwise if version >= 1, then the
 * specified version is used if it fits the data, otherwise -1 is returned.
 */
bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[], uint8_t eclBuffer[], uint8_t buffer[], int version, qrcodegen_Ecc ecl);

/* 
 * Encodes the given binary data to a QR Code, storing it in the given array of
 * uint8_t variables. Returns true if encoding succeeded, or false if the data
 * is too long to fit in any version at the given error correction level.
 * 
 * The capacity of the array is set by the caller and must be at least
 * qrcodegen_BUFFER_LEN_FOR_VERSION(version) bytes long. The length of the
 * resulting QR Code can be found by calling qrcodegen_getSize().
 * 
 * The arrays tempBuffer and eclBuffer can be viewed as scratch space, and can
 * be the same as each other. They must be at least as long as buffer.
 * 
 * If version = -1, then the smallest version QR Code that fits the data is
 * automatically chosen for the output. Otherwise if version >= 1, then the
 * specified version is used if it fits the data, otherwise -1 is returned.
 */
bool qrcodegen_encodeBinary(uint8_t data[], size_t dataLen, uint8_t tempBuffer[], uint8_t eclBuffer[], uint8_t buffer[], int version, qrcodegen_Ecc ecl);

/* 
 * Tests whether the given string can be encoded as a segment in numeric mode.
 * A string is encodable iff each character is in the range 0 to 9.
 */
bool qrcodegen_isNumeric(const char *str);

/* 
 * Tests whether the given string can be encoded as a segment in alphanumeric mode.
 * A string is encodable iff each character is in the following set: 0 to 9, A to Z
 * (uppercase only), space, dollar, percent, asterisk, plus, hyphen, period, slash, colon.
 */
bool qrcodegen_isAlphanumeric(const char *str);

/* 
 * Returns the number of bits needed to encode the given string in the given mode.
 * The string is guaranteed to be encodable in the given mode. Returns 0 if the string is empty.
 */
int qrcodegen_calcSegmentBits(const char *str, qrcodegen_Mode mode);

/* 
 * Encodes the given string as a segment in numeric mode, storing the segment
 * data in the given array of uint8_t variables. Returns the number of bytes
 * written to the array, or 0 if the string is too long to fit.
 */
int qrcodegen_makeNumeric(const char *str, uint8_t buffer[]);

/* 
 * Encodes the given string as a segment in alphanumeric mode, storing the segment
 * data in the given array of uint8_t variables. Returns the number of bytes
 * written to the array, or 0 if the string is too long to fit.
 */
int qrcodegen_makeAlphanumeric(const char *str, uint8_t buffer[]);

/* 
 * Encodes the given string as a segment in byte mode, storing the segment
 * data in the given array of uint8_t variables. Returns the number of bytes
 * written to the array, or 0 if the string is too long to fit.
 */
int qrcodegen_makeBytes(const uint8_t *data, size_t len, uint8_t buffer[]);

/* 
 * Returns the number of bits needed to encode the given segment array data.
 */
int qrcodegen_calcSegmentBitLength(qrcodegen_Mode mode, int numChars);

/* 
 * Returns the size of the given QR Code, in modules.
 * The size is a positive integer between 21 and 177 (inclusive).
 * This is used when reading the version field of the QR Code.
 */
int qrcodegen_getSize(const qrcodegen_QrCode *qr);

/* 
 * Returns the color of the module (pixel) at the given coordinates, which is false
 * for light or true for dark. The top left corner has the coordinates (x=0, y=0).
 * If the given coordinates are out of bounds, then false (light) is returned.
 */
bool qrcodegen_getModule(const qrcodegen_QrCode *qr, int x, int y);

/* 
 * Returns a segment representing the given string of decimal digits encoded in numeric mode.
 */
qrcodegen_Segment qrcodegen_makeSegment(const char *str, qrcodegen_Mode mode);

/* 
 * Returns a segment representing an Extended Channel Interpretation
 * (ECI) designator with the given assignment value.
 */
qrcodegen_Segment qrcodegen_makeEci(long assignVal);

/* 
 * Creates a new QR Code with the given version number, error correction level, and data segments.
 * Returns true if the QR code was created successfully, or false if the version is too small
 * to fit the given data segments.
 */
bool qrcodegen_encodeSegments(const qrcodegen_Segment segs[], size_t len, qrcodegen_Ecc ecl, uint8_t buffer[], int version, int mask);

/* 
 * Creates a new QR Code with the given version number, error correction level, and data segments.
 * Returns true if the QR code was created successfully, or false if the version is too small
 * to fit the given data segments.
 */
bool qrcodegen_encodeSegmentsAdvanced(const qrcodegen_Segment segs[], size_t len, qrcodegen_Ecc ecl, int minVersion, int maxVersion, int mask, bool boostEcl, uint8_t buffer[]);

#ifdef __cplusplus
}
#endif