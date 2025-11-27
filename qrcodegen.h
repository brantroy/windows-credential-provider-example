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

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>


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
	
	/*---- Public helper enumeration ----*/
	
	/* 
	 * Describes how a segment's data bits are interpreted. Immutable.
	 */
	public: enum class Mode : int {
		NUMERIC      = 0x1,
		ALPHANUMERIC = 0x2,
		BYTE         = 0x4,
		KANJI        = 0x8,
		FNC1_FIRST   = 0x5,
		FNC1_SECOND  = 0x9,
	};
	
	
	// Returns the bit width of the segment mode enum.
	public: static int getModeBits(Mode m);
	
	
	/*---- Static factory functions (mid level) ----*/
	
	/* 
	 * Returns a segment representing the given binary data encoded in
	 * byte mode. All input byte vectors are acceptable. Any text string
	 * can be converted to UTF-8 bytes and encoded as a byte mode segment.
	 */
	public: static QrSegment makeBytes(const std::vector<std::uint8_t> &data);
	
	
	/* 
	 * Returns a segment representing the given string of decimal digits encoded in numeric mode.
	 */
	public: static QrSegment makeNumeric(const char *digits);
	
	
	/* 
	 * Returns a segment representing the given text string encoded in alphanumeric mode.
	 * The characters allowed are: 0 to 9, A to Z (uppercase only), space,
	 * dollar, percent, asterisk, plus, hyphen, period, slash, colon.
	 */
	public: static QrSegment makeAlphanumeric(const char *text);
	
	
	/* 
	 * Returns a list of zero or more segments to represent the given Unicode text string.
	 * The result may use various segment modes and switch modes to optimize the length of the bit stream.
	 */
	public: static std::vector<QrSegment> makeSegments(const char *text);
	
	
	/* 
	 * Returns a segment representing an Extended Channel Interpretation
	 * (ECI) designator with the given assignment value.
	 */
	public: static QrSegment makeEci(std::int32_t assignVal);
	
	
	/*---- Public static helper functions ----*/
	
	/* 
	 * Tests whether the given string can be encoded as a segment in numeric mode.
	 * A string is encodable iff each character is in the range 0 to 9.
	 */
	public: static bool isNumeric(const char *text);
	
	
	/* 
	 * Tests whether the given string can be encoded as a segment in alphanumeric mode.
	 * A string is encodable iff each character is in the following set: 0 to 9, A to Z
	 * (uppercase only), space, dollar, percent, asterisk, plus, hyphen, period, slash, colon.
	 */
	public: static bool isAlphanumeric(const char *text);
	
	
	/*---- Instance fields ----*/
	
	/* The mode indicator of this segment. Accessed through getMode(). */
	private: Mode mode;
	
	/* The length of this segment's unencoded data. E.g. if "Hello" is encoded in numeric mode, then dataLen is 5. Accessed through getDataLength(). */
	private: int dataLen;
	
	/* The data bits of this segment. Accessed through getData(). */
	private: std::vector<bool> data;
	
	
	/*---- Constructors ----*/
	
	/* 
	 * Creates a new QR Code segment with the given attributes and data.
	 * The parameter data is owned by this instance and must not be modified or reassigned.
	 */
	public: QrSegment(Mode md, int numCh, const std::vector<bool> &dt);
	
	
	/* 
	 * Creates a new QR Code segment with the given parameters and data.
	 */
	public: QrSegment(Mode md, int numCh, std::vector<bool> &&dt);
	
	
	/*---- Methods ----*/
	
	/* Returns the mode field of this segment. */
	public: Mode getMode() const;
	
	/* Returns the character count field of this segment. */
	public: int getDataLength() const;
	
	/* Returns the data bits of this segment. */
	public: const std::vector<bool> &getData() const;
	
};

/* 
 * A QR Code symbol, which is a type of two-dimension barcode.
 * Invented by Denso Wave and described in the ISO/IEC 18004 standard.
 * Instances of this class represent an immutable square grid of dark and light cells.
 * The class provides static factory functions to create a QR Code from text or binary data.
 * The class covers the QR Code Model 2 specification, supporting all versions 1 through 40,
 * all 4 error correction levels, and 4 character encoding modes.
 * 
 * Ways to create a QR Code object:
 * - High level: Take the payload data and call QrCode::encodeText() or QrCode::encodeBinary().
 * - Mid level: Custom-make the list of segments and call QrCode::encodeSegments().
 * - Low level: Custom-make the array of data codeword bytes (including
 *   segment headers and final padding, excluding error correction codewords),
 *   supply the appropriate version number, and call the QrCode() constructor.
 * (Note that all ways require supplying the desired error correction level.)
 */
class QrCode final {
	
	/*---- Public helper enumeration ----*/
	
	/* 
	 * The error correction level in a QR Code symbol. Immutable.
	 */
	public: enum class Ecc {
		LOW = 0 ,  // The QR Code can tolerate about  7% erroneous codewords
		MEDIUM  ,  // The QR Code can tolerate about 15% erroneous codewords
		QUARTILE,  // The QR Code can tolerate about 25% erroneous codewords
		HIGH    ,  // The QR Code can tolerate about 30% erroneous codewords
	};
	
	
	// Returns the number of error correction blocks in the given QR Code version and error correction level.
	private: static int getNumErrorCorrectionBlocks(Ecc ecl, int ver);
	
	// Returns the number of raw data modules (bits) that can be stored in a QR Code of the given version number, after removing
	// the function patterns. This includes remainder bits, so it might not be a multiple of 8. The result is in the range [208, 29648].
	private: static int getNumRawDataModules(int ver);
	
	
	/*---- Static factory functions (high level) ----*/
	
	/* 
	 * Returns a QR Code representing the given Unicode text string at the given error correction level.
	 * As a conservative upper bound, this function is guaranteed to succeed for strings that have 2953 or fewer
	 * UTF-8 code units (not Unicode code points) if the low error correction level is used. The smallest possible
	 * QR Code version is automatically chosen for the output. If the data is too long to fit in any version
	 * at the given error correction level, then a data_too_long exception is thrown.
	 */
	public: static QrCode encodeText(const char *text, Ecc ecl);
	
	
	/* 
	 * Returns a QR Code representing the given binary data at the given error correction level.
	 * This function always encodes using the byte format, because it's the most compact.
	 * The smallest possible QR Code version is automatically chosen for the output. If the data is too long to
	 * fit in any version at the given error correction level, then a data_too_long exception is thrown.
	 */
	public: static QrCode encodeBinary(const std::vector<std::uint8_t> &data, Ecc ecl);
	
	
	/* 
	 * Returns a QR Code representing the given segments with the given encoding parameters.
	 * The smallest possible QR Code version within the given range is automatically
	 * chosen for the output. Iff boostEcl is true, then the ECC level of the result may be higher
	 * than the ecl argument if it can be done without increasing the version. If the data is too
	 * long to fit in any version with the given ECC level, then a data_too_long exception is thrown.
	 */
	public: static QrCode encodeSegments(const std::vector<QrSegment> &segs, Ecc ecl,
		int minVersion=1, int maxVersion=40, bool boostEcl=true);
	
	
	/*---- Instance fields ----*/
	
	// Immutable scalar parameters:
	
	/* The version number of this QR Code, which is between 1 and 40 (inclusive). This determines the size of this barcode. */
	private: int version;
	
	/* The width and height of this QR Code, measured in modules, between 21 and 177 (inclusive). This is equal to version * 4 + 17. */
	private: int size;
	
	/* The error correction level used in this QR Code. */
	private: Ecc errorCorrectionLevel;
	
	/* The index of the mask pattern used in this QR Code, which is between 0 and 7 (inclusive). Even if a QR Code is created with automatic masking requested (mask = -1), this field holds the mask index that was actually chosen. */
	private: int mask;
	
	// Private grids of modules/pixels, with dimensions of size*size:
	
	/* The modules of this QR Code (false = light, true = dark). Immutable after constructor finishes. Accessed through getModule(). */
	private: std::vector<std::vector<bool>> modules;
	
	/* Indicates function modules that are not subjected to masking. Discarded when constructor finishes. */
	private: std::vector<std::vector<bool>> isFunction;
	
	
	/*---- Constructors ----*/
	
	/* 
	 * Creates a new QR Code with the given version number, error correction level, binary data array, and mask number.
	 * This is a low-level constructor that you should not invoke directly - instead, use a static factory function above.
	 */
	public: QrCode(int ver, Ecc ecl, const std::vector<std::uint8_t> &dataCodewords, int mask);
	
	
	/*---- Public methods ----*/
	
	/* Returns this QR Code's version, in the range [1, 40]. */
	public: int getVersion() const;
	
	/* Returns this QR Code's size, in the range [21, 177]. */
	public: int getSize() const;
	
	/* Returns this QR Code's error correction level. */
	public: Ecc getErrorCorrectionLevel() const;
	
	/* Returns this QR Code's mask, in the range [0, 7]. */
	public: int getMask() const;
	
	/* 
	 * Returns the color of the module (pixel) at the given coordinates, which is false
	 * for light or true for dark. The top left corner has the coordinates (x=0, y=0).
	 * If the given coordinates are out of bounds, then false (light) is returned.
	 */
	public: bool getModule(int x, int y) const;
	
};

/* 
 * An appendable sequence of bits (0s and 1s). Mainly used by QrSegment.
 */
class BitBuffer final : public std::vector<bool> {
	
	/*---- Constructor ----*/
	
	// Creates an empty bit buffer (length 0).
	public: BitBuffer();
	
	
	/*---- Method ----*/
	
	// Appends the given number of low-order bits of the given value
	// to this buffer. Requires 0 <= len <= 31 and val < 2^len.
	public: void appendBits(std::uint32_t val, int len);
	
};

}

/*---- Global free functions ----*/

/* 
 * Computes the Reed-Solomon error correction codewords for the given sequence of data codewords
 * at the given degree. Objects are allocated and returned dynamically.
 */
std::vector<std::uint8_t> reedSolomonComputeRemainder(const std::vector<std::uint8_t> &data, const std::vector<std::uint8_t> &generator);

/* 
 * Returns a Reed-Solomon generator polynomial for the given degree. This could be
 * implemented as a lookup table over all possible parameter values, instead of as an algorithm.
 */
std::vector<std::uint8_t> reedSolomonComputeDivisor(int degree);

/* 
 * Returns the product of the two given field elements modulo GF(2^8/0x11D).
 * All inputs are valid. This could be implemented as a 256*256 lookup table.
 */
std::uint8_t reedSolomonMultiply(std::uint8_t x, std::uint8_t y);

}