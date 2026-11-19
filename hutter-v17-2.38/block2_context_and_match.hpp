#pragma once
#include "block1_cube_hasher.hpp"

// Fast-adapting bit estimator tuned for small-to-medium corpora
struct BitCounter {
	uint16_t n0 = 0;
	uint16_t n1 = 0;

	inline double prob1() const {
		return (double)(n1 + 0.25) / (double)(n0 + n1 + 0.5);
	}

	inline void update(int bit, uint16_t maxCount = 96) {
		if (bit) n1++;
		else n0++;
		if (n0 + n1 > maxCount) {
			n0 >>= 1;
			n1 >>= 1;
		}
	}
};

// Orthogonal Prime-Modulus Context Table (eliminates power-of-2 bit-aliasing collisions)
template <size_t PRIME_SIZE, uint64_t SALT>
class PrimeContextTable {
private:
	std::vector<BitCounter> table;

public:
	PrimeContextTable() : table(PRIME_SIZE) {}

	inline size_t index(uint64_t ctxHash, uint32_t partialByte) const {
		uint64_t mixed = (ctxHash ^ SALT) * 0x9E3779B97F4A7C15ULL + (partialByte * 0xBF58476D1CE4E5B9ULL);
		mixed ^= mixed >> 31;
		return (size_t)(mixed % PRIME_SIZE);
	}

	inline double predict(uint64_t ctxHash, uint32_t partialByte) const {
		return table[index(ctxHash, partialByte)].prob1();
	}

	inline void update(uint64_t ctxHash, uint32_t partialByte, int bit) {
		table[index(ctxHash, partialByte)].update(bit, 96);
	}
};

// STEP 6: Bit-Accurate 3-Byte Indexed 4-Way Match Model with Learned Confidence Table
class BitMatchModel {
private:
	static constexpr size_t PRIME_SLOTS = 262139; // Largest prime < 2^18
	static constexpr int MIN_MATCH = 3;

	std::vector<int32_t> head1;
	std::vector<int32_t> head2;
	std::vector<int32_t> head3;
	std::vector<int32_t> head4;
	std::array<BitCounter, 32 * 8 * 2> matchStats{}; // [lenBucket][bitIdx][expectedBit]

	size_t matchPos = 0;
	int matchLen = 0;
	bool activeInByte = false;

	static int measureBackwardMatch(const std::vector<uint8_t>& history, size_t curIdx, int32_t candIdx) {
		if (candIdx < MIN_MATCH || (size_t)candIdx >= curIdx) return 0;
		int len = 0;
		int maxCheck = (int)std::min<size_t>((size_t)candIdx, 96);
		while (len < maxCheck && history[(size_t)candIdx - 1 - len] == history[curIdx - 1 - len]) {
			len++;
		}
		return len;
	}

	inline size_t statIndex(int bitIdx, int expectedBit) const {
		int lenB = std::min(31, matchLen);
		return (size_t)((lenB * 8 + bitIdx) * 2 + expectedBit);
	}

public:
	BitMatchModel()
		: head1(PRIME_SLOTS, -1), head2(PRIME_SLOTS, -1), head3(PRIME_SLOTS, -1), head4(PRIME_SLOTS, -1) {
		for (int len = 0; len < 32; ++len) {
			double baseConf = std::min(0.995, 0.55 + 0.44 * (1.0 - std::exp(-len * 0.22)));
			for (int b = 0; b < 8; ++b) {
				matchStats[(len * 8 + b) * 2 + 1].n1 = (uint16_t)(baseConf * 16.0);
				matchStats[(len * 8 + b) * 2 + 1].n0 = (uint16_t)((1.0 - baseConf) * 16.0);
				matchStats[(len * 8 + b) * 2 + 0].n0 = (uint16_t)(baseConf * 16.0);
				matchStats[(len * 8 + b) * 2 + 0].n1 = (uint16_t)((1.0 - baseConf) * 16.0);
			}
		}
	}

	inline int activeMatchLen() const {
		return activeInByte ? matchLen : 0;
	}

	inline double predictBit(const std::vector<uint8_t>& history, int bitIdx, uint32_t partialByte) {
		if (bitIdx == 7) {
			activeInByte = (matchLen >= MIN_MATCH && matchPos < history.size());
		}
		if (!activeInByte) return 0.5;

		// Check if the bits decoded so far in this byte still match history[matchPos]
		int bitsDecoded = 7 - bitIdx;
		if (bitsDecoded > 0) {
			uint8_t candPrefix = history[matchPos] >> (bitIdx + 1);
			uint8_t curPrefix = (uint8_t)(partialByte & ((1U << bitsDecoded) - 1U));
			if (candPrefix != curPrefix) {
				activeInByte = false;
				return 0.5;
			}
		}

		int expectedBit = (history[matchPos] >> bitIdx) & 1;
		return matchStats[statIndex(bitIdx, expectedBit)].prob1();
	}

	inline void updateBit(const std::vector<uint8_t>& history, int bitIdx, int actualBit) {
		if (!activeInByte || matchPos >= history.size()) return;
		int expectedBit = (history[matchPos] >> bitIdx) & 1;
		matchStats[statIndex(bitIdx, expectedBit)].update(actualBit, 128);
		if (actualBit != expectedBit) {
			activeInByte = false;
		}
	}

	void updateByte(const std::vector<uint8_t>& history, uint64_t hash3) {
		if (history.size() <= MIN_MATCH) return;
		size_t curIdx = history.size() - 1;

		if (matchLen > 0) {
			if (matchPos < curIdx && history[matchPos] == history[curIdx]) {
				matchPos++;
				matchLen++;
			} else {
				matchLen = 0;
			}
		}

		size_t nextIdx = history.size();
		size_t slot = (size_t)(hash3 % PRIME_SLOTS);
		int32_t c1 = head1[slot];
		int32_t c2 = head2[slot];
		int32_t c3 = head3[slot];
		int32_t c4 = head4[slot];

		if (matchLen < 4) {
			std::array<int32_t, 4> cands = {c1, c2, c3, c4};
			int bestLen = matchLen;
			int32_t bestPos = -1;

			for (int k = 0; k < 4; ++k) {
				int len = measureBackwardMatch(history, nextIdx, cands[k]);
				if (len >= MIN_MATCH && len > bestLen) {
					bestLen = len;
					bestPos = cands[k];
				}
			}

			if (bestPos != -1) {
				matchPos = (size_t)bestPos;
				matchLen = bestLen;
			}
		}

		head4[slot] = c3;
		head3[slot] = c2;
		head2[slot] = c1;
		head1[slot] = (int32_t)curIdx;
	}
};
