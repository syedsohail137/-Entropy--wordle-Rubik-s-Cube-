#pragma once
#include "block1_cube_hasher.hpp"

class BinaryArithmeticEncoder {
private:
	uint32_t low = 0;
	uint32_t high = 0xFFFFFFFFU;
	uint32_t bitsToFollow = 0;
	uint8_t bitBuf = 0;
	int bitCount = 0;

	void writeBit(int b) {
		bitBuf = (bitBuf << 1) | (b & 1);
		if (++bitCount == 8) {
			out.push_back(bitBuf);
			bitBuf = 0;
			bitCount = 0;
		}
	}

	void bitPlusFollow(int b) {
		writeBit(b);
		while (bitsToFollow > 0) {
			writeBit(!b);
			bitsToFollow--;
		}
	}

public:
	std::vector<uint8_t> out;

	void encodeBit(int bit, double p1) {
		uint32_t prob12 = (uint32_t)(std::clamp(p1, 0.0001, 0.9999) * 4096.0 + 0.5);
		if (prob12 < 1) prob12 = 1;
		if (prob12 > 4095) prob12 = 4095;

		uint32_t p0_12 = 4096 - prob12;
		uint32_t mid = low + (uint32_t)(((uint64_t)(high - low) * p0_12) >> 12);

		if (bit) low = mid + 1;
		else     high = mid;

		while (true) {
			if (high < 0x80000000U) {
				bitPlusFollow(0);
			} else if (low >= 0x80000000U) {
				bitPlusFollow(1);
				low -= 0x80000000U;
				high -= 0x80000000U;
			} else if (low >= 0x40000000U && high < 0xC0000000U) {
				bitsToFollow++;
				low -= 0x40000000U;
				high -= 0x40000000U;
			} else {
				break;
			}
			low <<= 1;
			high = (high << 1) | 1;
		}
	}

	void finish() {
		bitsToFollow++;
		bitPlusFollow(low < 0x40000000U ? 0 : 1);
		if (bitCount > 0) {
			bitBuf <<= (8 - bitCount);
			out.push_back(bitBuf);
		}
	}
};

class BinaryArithmeticDecoder {
private:
	const std::vector<uint8_t>& in;
	size_t bytePos = 0;
	int bitMask = 0;
	uint8_t curByte = 0;
	uint32_t low = 0;
	uint32_t high = 0xFFFFFFFFU;
	uint32_t value = 0;

	int readBit() {
		if (bitMask == 0) {
			curByte = (bytePos < in.size()) ? in[bytePos++] : 0;
			bitMask = 0x80;
		}
		int b = (curByte & bitMask) ? 1 : 0;
		bitMask >>= 1;
		return b;
	}

public:
	BinaryArithmeticDecoder(const std::vector<uint8_t>& data, size_t startOffset = 0)
		: in(data), bytePos(startOffset) {
		for (int i = 0; i < 32; ++i) value = (value << 1) | readBit();
	}

	int decodeBit(double p1) {
		uint32_t prob12 = (uint32_t)(std::clamp(p1, 0.0001, 0.9999) * 4096.0 + 0.5);
		if (prob12 < 1) prob12 = 1;
		if (prob12 > 4095) prob12 = 4095;

		uint32_t p0_12 = 4096 - prob12;
		uint32_t mid = low + (uint32_t)(((uint64_t)(high - low) * p0_12) >> 12);

		int bit = (value > mid) ? 1 : 0;
		if (bit) low = mid + 1;
		else     high = mid;

		while (true) {
			if (high < 0x80000000U) {
			} else if (low >= 0x80000000U) {
				value -= 0x80000000U;
				low -= 0x80000000U;
				high -= 0x80000000U;
			} else if (low >= 0x40000000U && high < 0xC0000000U) {
				value -= 0x40000000U;
				low -= 0x40000000U;
				high -= 0x40000000U;
			} else {
				break;
			}
			low <<= 1;
			high = (high << 1) | 1;
			value = (value << 1) | readBit();
		}
		return bit;
	}
};
