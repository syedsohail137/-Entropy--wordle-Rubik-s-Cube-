#pragma once
#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <unordered_map>

// Non-Abelian Rubik's Cube Permutation Group Mixer + Prime Syndrome Accumulator
class FastCubeMixer {
private:
	static constexpr std::array<std::array<uint8_t, 20>, 6> BASE_MOVES = {{
		{1,2,3,0, 4,5,6,7,  9,10,11,8, 12,13,14,15, 16,17,18,19},
		{0,1,2,3, 7,4,5,6,  8,9,10,11, 15,12,13,14, 16,17,18,19},
		{3,1,2,7, 0,5,6,4,  8,9,10,19, 12,13,14,16, 11,17,18,15},
		{0,5,1,3, 4,6,2,7,  8,17,10,11, 12,18,14,15, 16,13,9,19},
		{4,0,2,3, 5,1,6,7,  16,9,10,11, 17,13,14,15, 12,8,18,19},
		{0,1,6,2, 4,5,7,3,  8,9,18,11, 12,13,19,15, 16,17,14,10}
	}};
	static constexpr std::array<uint64_t, 6> FACE_PRIMES = {2, 13, 7, 5, 3, 11};
	std::array<std::array<uint8_t, 20>, 18> permTable{};

	static std::array<uint8_t, 20> compose(const std::array<uint8_t, 20>& a, const std::array<uint8_t, 20>& b) {
		std::array<uint8_t, 20> out{};
		for (int i = 0; i < 20; ++i) out[i] = a[b[i]];
		return out;
	}

public:
	uint64_t solvedCubies = 0;

	FastCubeMixer() {
		for (int i = 0; i < 20; ++i) solvedCubies |= (uint64_t(i & 7) << (i * 3));
		for (int f = 0; f < 6; ++f) {
			auto m1 = BASE_MOVES[f];
			auto m2 = compose(m1, m1);
			auto m3 = compose(m2, m1);
			permTable[f * 3 + 0] = m1;
			permTable[f * 3 + 1] = m2;
			permTable[f * 3 + 2] = m3;
		}
	}

	inline uint64_t applyMove(uint64_t state, int moveIdx, uint64_t &syndrome) const {
		const auto& p = permTable[moveIdx];
		uint64_t next = 0;
		for (int i = 0; i < 20; ++i) {
			uint64_t cubie = (state >> (p[i] * 3)) & 7ULL;
			next |= (cubie << (i * 3));
		}
		syndrome = (syndrome * FACE_PRIMES[moveIdx / 3]) ^ (next >> 17) ^ (moveIdx * 0x9E3779B97F4A7C15ULL);
		return next;
	}

	// O(1) incremental step for running non-Abelian cube state
	inline void stepByte(uint8_t b, uint64_t& state, uint64_t& syndrome) const {
		int m1 = b % 18;
		int m2 = ((b >> 3) ^ (b << 2) ^ (syndrome & 0xFF)) % 18;
		state = applyMove(state, m1, syndrome);
		state = applyMove(state, m2, syndrome);
	}

	uint64_t hashContext(const uint8_t* ctx, int len) const {
		uint64_t state = solvedCubies;
		uint64_t syndrome = 1469598103934665603ULL;
		for (int i = 0; i < len; ++i) {
			uint8_t b = ctx[i];
			int m1 = (b + i * 5) % 18;
			int m2 = ((b >> 3) ^ (b << 2) ^ (i * 7)) % 18;
			state = applyMove(state, m1, syndrome);
			state = applyMove(state, m2, syndrome);
		}
		uint64_t h = state ^ syndrome;
		h ^= h >> 33;
		h *= 0xff51afd7ed558ccdULL;
		h ^= h >> 33;
		return h;
	}

	uint64_t hashWord64(uint64_t val) const {
		std::array<uint8_t, 8> bytes{};
		for (int i = 0; i < 8; ++i) bytes[i] = (uint8_t)((val >> (i * 8)) & 0xFF);
		return hashContext(bytes.data(), 8);
	}
};
