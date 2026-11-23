// ============================================================================
// V9+: 16-EXPERT BIT-LEVEL LOGISTIC MIXER + PREFIX/SUFFIX STEM MORPHOLOGY
//      + WRD_STR LRU CACHE + WINDOWED PHONOTACTICS + 3x APM + LANGMASK
// Pure-Online Symmetric Lossless Compression Engine (Portable C++17/20)
// Compile: g++ -O3 -std=c++17 -march=native main.cpp -o v9_hutter
// Run    : ./v9_hutter --file enwik9_1mb.txt
// ============================================================================

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace std;

template <typename T>
inline constexpr const T& clamp_v(const T& v, const T& lo, const T& hi) {
	return (v < lo) ? lo : (hi < v) ? hi : v;
}

// ============================================================================
// 1. DETERMINISTIC FIXED-POINT MATH (STRETCH / SQUASH LOOKUP TABLES)
// ============================================================================

struct MathTables {
	array<float, 4097> stretchTbl{};
	array<uint16_t, 4096> squashTbl{};

	MathTables() {
		for (int i = 1; i <= 4095; i++) {
			double p = (double)i / 4096.0;
			stretchTbl[(size_t)i] = (float)log(p / (1.0 - p));
		}
		stretchTbl[0] = stretchTbl[1];
		stretchTbl[4096] = stretchTbl[4095];

		for (int i = 0; i < 4096; i++) {
			double x = ((double)i - 2048.0) / 128.0;
			double s = 1.0 / (1.0 + exp(-x));
			int q = (int)llround(s * 4096.0);
			squashTbl[(size_t)i] = (uint16_t)clamp_v(q, 1, 4095);
		}
	}

	inline float stretch(uint16_t p12) const {
		return stretchTbl[(size_t)clamp_v((int)p12, 1, 4095)];
	}

	inline uint16_t squash(float x) const {
		int idx = (int)lrintf(x * 128.0f + 2048.0f);
		return squashTbl[(size_t)clamp_v(idx, 0, 4095)];
	}
};

static const MathTables MATH;

// ============================================================================
// 2. BIT-LEVEL 32-BIT RANGE ARITHMETIC CODER & DECODER (12-BIT PROBABILITY)
// ============================================================================

struct BitArithmeticCoder {
	uint32_t low = 0, high = 0xFFFFFFFFu, pending = 0;
	vector<uint8_t> out;
	uint8_t bitBuf = 0;
	int bitCnt = 0;

	inline void writeBit(int b) {
		bitBuf = (uint8_t)((bitBuf << 1) | (b & 1));
		if (++bitCnt == 8) {
			out.push_back(bitBuf);
			bitBuf = 0;
			bitCnt = 0;
		}
	}

	inline void emit(int b) {
		writeBit(b);
		while (pending > 0) {
			writeBit(!b);
			pending--;
		}
	}

	inline void encodeBit(int bit, uint16_t p1) {
		uint32_t p = clamp_v((uint32_t)p1, 1u, 4095u);
		uint32_t mid = low + (uint32_t)(((uint64_t)(high - low) * (4096u - p)) >> 12);
		if (bit == 0) {
			high = mid;
		} else {
			low = mid + 1u;
		}
		while (true) {
			if (high < 0x80000000u) {
				emit(0);
			} else if (low >= 0x80000000u) {
				emit(1);
				low -= 0x80000000u;
				high -= 0x80000000u;
			} else if (low >= 0x40000000u && high < 0xC0000000u) {
				pending++;
				low -= 0x40000000u;
				high -= 0x40000000u;
			} else {
				break;
			}
			low <<= 1;
			high = (high << 1) | 1u;
		}
	}

	void finish() {
		pending++;
		emit(low < 0x40000000u ? 0 : 1);
		if (bitCnt > 0) {
			bitBuf <<= (8 - bitCnt);
			out.push_back(bitBuf);
		}
	}
};

struct BitArithmeticDecoder {
	uint32_t low = 0, high = 0xFFFFFFFFu, code = 0;
	const vector<uint8_t>* in = nullptr;
	size_t bytePos = 0;
	int bitPos = 0;

	inline int readBit() {
		if (!in || bytePos >= in->size()) return 0;
		int b = ((*in)[bytePos] >> (7 - bitPos)) & 1;
		if (++bitPos == 8) {
			bitPos = 0;
			bytePos++;
		}
		return b;
	}

	void init(const vector<uint8_t>& data, size_t startOffset = 0) {
		in = &data;
		bytePos = startOffset;
		bitPos = 0;
		code = 0;
		for (int i = 0; i < 32; i++) code = (code << 1) | (uint32_t)readBit();
	}

	inline int decodeBit(uint16_t p1) {
		uint32_t p = clamp_v((uint32_t)p1, 1u, 4095u);
		uint32_t mid = low + (uint32_t)(((uint64_t)(high - low) * (4096u - p)) >> 12);
		int bit = (code > mid) ? 1 : 0;
		if (bit == 0) {
			high = mid;
		} else {
			low = mid + 1u;
		}
		while (true) {
			if (high < 0x80000000u) {
			} else if (low >= 0x80000000u) {
				low -= 0x80000000u;
				high -= 0x80000000u;
				code -= 0x80000000u;
			} else if (low >= 0x40000000u && high < 0xC0000000u) {
				low -= 0x40000000u;
				high -= 0x40000000u;
				code -= 0x40000000u;
			} else {
				break;
			}
			low <<= 1;
			high = (high << 1) | 1u;
			code = (code << 1) | (uint32_t)readBit();
		}
		return bit;
	}
};

// ============================================================================
// 3. DUAL-ESTIMATOR BIT-HISTORY TABLE (FAST EMA + STATIONARY COUNTS)
// ============================================================================

struct BitHistoryTable {
	struct Slot {
		uint32_t tag = 0;
		uint8_t n0 = 0;
		uint8_t n1 = 0;
		uint16_t fastP = 2048;
	};

	vector<Slot> table;
	size_t mask = 0;
	uint8_t maxCount = 32;
	uint32_t lastIdx = 0;
	uint32_t lastTag = 0;

	void init(size_t pow2Size, uint8_t cap = 32) {
		table.assign(pow2Size, Slot{});
		mask = pow2Size - 1;
		maxCount = cap;
	}

	inline uint32_t probeSlot(uint64_t h, bool& hit) {
		uint32_t idx = (uint32_t)(h & mask);
		uint32_t tag = (uint32_t)(h >> 32) | 1u;
		if (table[idx].tag != tag && table[idx].tag != 0) {
			uint32_t idx2 = (idx ^ 0x9E3779B9u) & (uint32_t)mask;
			if (table[idx2].tag == tag || table[idx2].tag == 0 ||
			        (table[idx2].n0 + table[idx2].n1) < (table[idx].n0 + table[idx].n1)) {
				idx = idx2;
			}
		}
		lastIdx = idx;
		lastTag = tag;
		hit = (table[idx].tag == tag && (table[idx].n0 + table[idx].n1) > 0);
		return idx;
	}

	inline uint16_t evalSlot(uint32_t idx) const {
		const Slot& s = table[idx];
		int tot = (int)s.n0 + (int)s.n1;
		int countP = ((int)s.n1 * 4096 + 2048) / (tot + 1);
		int blended = (countP * 13 + (int)s.fastP * 7) / 20;
		return (uint16_t)clamp_v(blended, 2, 4094);
	}

	inline uint16_t predict(uint64_t h, uint16_t fallbackP = 2048) {
		bool hit = false;
		uint32_t idx = probeSlot(h, hit);
		if (!hit) return fallbackP;
		return evalSlot(idx);
	}

	inline void update(int bit) {
		Slot& s = table[lastIdx];
		if (s.tag != lastTag) {
			s.tag = lastTag;
			s.n0 = 0;
			s.n1 = 0;
			s.fastP = 2048;
		}
		int target = bit ? 4095 : 1;
		s.fastP = (uint16_t)((s.fastP * 7 + target) >> 3);
		if (bit == 0) {
			if (s.n0 >= maxCount) {
				s.n0 = (uint8_t)((s.n0 >> 1) + 1);
				s.n1 = (uint8_t)(s.n1 >> 1);
			} else {
				s.n0++;
			}
		} else {
			if (s.n1 >= maxCount) {
				s.n1 = (uint8_t)((s.n1 >> 1) + 1);
				s.n0 = (uint8_t)(s.n0 >> 1);
			} else {
				s.n1++;
			}
		}
	}
};

// ============================================================================
// 4. DETERMINISTIC XML / MEDIAWIKI / PHONOTACTIC / MORPH STEM PARSER STATE
// ============================================================================
#include "V9_LangMaskUpgrade.hpp"

// ============================================================================
// 5. MULTI-MATCH & FUZZY-GAP RING BUFFER EXPERT (BIT-LEVEL)
// ============================================================================
struct MultiMatchModel {
	static constexpr size_t WIN_SIZE  = 1048576;
	static constexpr size_t WIN_MASK  = WIN_SIZE - 1;
	static constexpr size_t HASH_SIZE = 262144;
	static constexpr size_t HASH_MASK = HASH_SIZE - 1;
	vector<uint8_t> ring;
	vector<int32_t> head3;
	vector<int32_t> head6;
	vector<int32_t> prev3;
	vector<int32_t> prev6;
	int32_t pos = 0;
	struct Candidate {
		int len;
		uint8_t nextByte;
	};
	array<Candidate, 3> cands{};
	int bestLen = 0;

	void init() {
		ring.assign(WIN_SIZE, 0);
		head3.assign(HASH_SIZE, -1);
		head6.assign(HASH_SIZE, -1);
		prev3.assign(WIN_SIZE, -1);
		prev6.assign(WIN_SIZE, -1);
		pos = 0;
		bestLen = 0;
	}

	inline uint32_t hash_seq(int32_t endPos, int len) const {
		uint32_t h = 2166136261u;
		for (int k = 0; k < len; k++) {
			h ^= ring[(size_t)(endPos - len + k) & WIN_MASK];
			h *= 16777619u;
		}
		return h & HASH_MASK;
	}

	void prepareByte() {
		cands.fill(Candidate{0, 0});
		bestLen = 0;
		if (pos < 4) return;
		int32_t minValid = max(0, pos - (int32_t)WIN_SIZE + 512);
		auto insertCand = [&](int mlen, uint8_t nxt) {
			if (mlen > bestLen) bestLen = mlen;
			for (int i = 0; i < 3; i++) {
				if (cands[(size_t)i].len > 0 && cands[(size_t)i].nextByte == nxt) {
					if (mlen > cands[(size_t)i].len) cands[(size_t)i].len = mlen;
					return;
				}
			}
			int worstIdx = 0;
			for (int i = 1; i < 3; i++) {
				if (cands[(size_t)i].len < cands[(size_t)worstIdx].len) worstIdx = i;
			}
			if (mlen > cands[(size_t)worstIdx].len) {
				cands[(size_t)worstIdx] = Candidate{mlen, nxt};
			}
		};

		if (pos >= 7) {
			uint32_t h6 = hash_seq(pos, 6);
			int32_t ptr = head6[h6];
			int steps = 0;
			while (ptr >= minValid + 6 && steps < 48) {
				int mlen = 0;
				int32_t maxLook = min(512, ptr - minValid);
				while (mlen < maxLook &&
				        ring[(size_t)(ptr - 1 - mlen) & WIN_MASK] ==
				        ring[(size_t)(pos - 1 - mlen) & WIN_MASK]) {
					mlen++;
				}
				if (mlen >= 6) {
					insertCand(mlen, ring[(size_t)ptr & WIN_MASK]);
					if (mlen >= 64) break;
				}
				ptr = prev6[(size_t)ptr & WIN_MASK];
				steps++;
			}
		}

		if (bestLen < 12) {
			uint32_t h3 = hash_seq(pos, 3);
			int32_t ptr = head3[h3];
			int steps = 0;
			while (ptr >= minValid + 4 && steps < 64) {
				int mlen = 0;
				int gaps = 0;
				int32_t maxLook = min(128, ptr - minValid);
				while (mlen < maxLook) {
					if (ring[(size_t)(ptr - 1 - mlen) & WIN_MASK] ==
					        ring[(size_t)(pos - 1 - mlen) & WIN_MASK]) {
						mlen++;
					} else if (mlen >= 3 && gaps == 0 && (mlen + 1) < maxLook &&
					           ring[(size_t)(ptr - 2 - mlen) & WIN_MASK] ==
					           ring[(size_t)(pos - 2 - mlen) & WIN_MASK]) {
						gaps = 1;
						mlen += 2;
					} else {
						break;
					}
				}
				int effLen = mlen - gaps * 2;
				if (effLen >= 3) {
					insertCand(effLen, ring[(size_t)ptr & WIN_MASK]);
				}
				ptr = prev3[(size_t)ptr & WIN_MASK];
				steps++;
			}
		}
	}
	inline uint16_t predictBit(int bitIdx, uint8_t cPart) const {
		if (bestLen < 3) return 2048;
		int shift = 7 - bitIdx;
		uint8_t prefixMask = (uint8_t)(cPart - (1u << bitIdx));
		double w0 = 0.0, w1 = 0.0;
		for (int i = 0; i < 3; i++) {
			int L = cands[(size_t)i].len;
			if (L < 3) continue;
			uint8_t nb = cands[(size_t)i].nextByte;
			if ((nb >> (shift + 1)) == prefixMask) {
				int b = (nb >> shift) & 1;
				double w = (double)(L * L);
				if (b == 1) w1 += w;
				else w0 += w;
			}
		}
		if (w0 + w1 < 1e-6) return 2048;
		double conf = min(0.996, 0.58 + 0.045 * (bestLen - 3));
		double p1 = 0.5 + (conf - 0.5) * ((w1 - w0) / (w1 + w0));
		return (uint16_t)clamp_v((int)llround(p1 * 4096.0), 4, 4092);
	}
	void updateByte(uint8_t b) {
		if (pos >= 3) {
			uint32_t h3 = hash_seq(pos, 3);
			prev3[(size_t)pos & WIN_MASK] = head3[h3];
			head3[h3] = pos;
		}
		if (pos >= 6) {
			uint32_t h6 = hash_seq(pos, 6);
			prev6[(size_t)pos & WIN_MASK] = head6[h6];
			head6[h6] = pos;
		}
		ring[(size_t)pos & WIN_MASK] = b;
		pos++;
	}
};
// ============================================================================
// 6. 16-EXPERT MULTI-CONTEXT STRETCHED-LOGIT LOGISTIC MIXER (ONLINE SGD)
// ============================================================================
struct StretchedLogitMixer {
	static constexpr int N_MODELS = 16;
	static constexpr int N_TABLES = 5;
	static constexpr size_t TABLE_SZ = 4096;
	vector<array<float, N_MODELS>> wTables[N_TABLES];
	array<uint32_t, N_TABLES> activeCtx{};
	array<float, N_MODELS> lastX{};
	float lastPred = 0.5f;
	void init() {
		for (int t = 0; t < N_TABLES; t++) {
			wTables[t].assign(TABLE_SZ, array<float, N_MODELS> {});
			for (size_t c = 0; c < TABLE_SZ; c++) {
				wTables[t][c].fill(0.16f);
			}
		}
	}
	inline uint16_t mix(const array<uint16_t, N_MODELS>& probs,
	                    const array<uint32_t, N_TABLES>& ctxHashes) {
		float sumLogit = 0.0f;
		for (int m = 0; m < N_MODELS; m++) {
			lastX[(size_t)m] = MATH.stretch(probs[(size_t)m]);
		}
		for (int t = 0; t < N_TABLES; t++) {
			uint32_t idx = ctxHashes[(size_t)t] & (uint32_t)(TABLE_SZ - 1);
			activeCtx[(size_t)t] = idx;
			const auto& w = wTables[t][idx];
			for (int m = 0; m < N_MODELS; m++) {
				sumLogit += w[(size_t)m] * lastX[(size_t)m];
			}
		}
		uint16_t p12 = MATH.squash(sumLogit * 0.20f);
		lastPred = (float)p12 / 4096.0f;
		return p12;
	}
	inline void update(int bit, float lr = 0.015f) {
		float err = ((float)bit - lastPred) * lr;
		for (int t = 0; t < N_TABLES; t++) {
			auto& w = wTables[t][activeCtx[(size_t)t]];
			for (int m = 0; m < N_MODELS; m++) {
				w[(size_t)m] = clamp_v(w[(size_t)m] * 0.9999f + err * lastX[(size_t)m], -2.5f, 3.5f);
			}
		}
	}
};
// ============================================================================
// 7. 3-STAGE CASCADED ADAPTIVE PROBABILITY MAP (APM / SSE)
// ============================================================================
struct CascadedAPM {
	static constexpr size_t N_CTX = 2048;
	static constexpr int N_BINS = 33;
	vector<array<uint16_t, N_BINS>> tbl;
	uint32_t lastCtx = 0;
	int lastBin = 0;
	int lastFrac = 0;
	void init() {
		tbl.assign(N_CTX, array<uint16_t, N_BINS> {});
		for (size_t c = 0; c < N_CTX; c++) {
			for (int b = 0; b < N_BINS; b++) {
				double x = -8.0 + 0.5 * b;
				double p = 1.0 / (1.0 + exp(-x));
				tbl[c][(size_t)b] = (uint16_t)clamp_v((int)llround(p * 4096.0), 1, 4095);
			}
		}
	}
	inline uint16_t refine(uint16_t p12, uint32_t ctxHash) {
		float x = MATH.stretch(p12);
		float pos = clamp_v((x + 8.0f) * 2.0f, 0.0f, (float)(N_BINS - 2));
		int bin = (int)pos;
		int frac = (int)lrintf((pos - (float)bin) * 256.0f);
		lastCtx = ctxHash & (uint32_t)(N_CTX - 1);
		lastBin = bin;
		lastFrac = frac;
		int p0 = tbl[lastCtx][(size_t)bin];
		int p1 = tbl[lastCtx][(size_t)bin + 1];
		int interp = (p0 * (256 - frac) + p1 * frac) >> 8;
		return (uint16_t)clamp_v(interp, 1, 4095);
	}
	inline void update(int bit, int shiftRate = 4) {
		int target = bit ? 4095 : 1;
		int p0 = tbl[lastCtx][(size_t)lastBin];
		int p1 = tbl[lastCtx][(size_t)lastBin + 1];
		int d0 = ((target - p0) * (256 - lastFrac)) >> (8 + shiftRate);
		int d1 = ((target - p1) * lastFrac) >> (8 + shiftRate);
		tbl[lastCtx][(size_t)lastBin]     = (uint16_t)clamp_v(p0 + d0, 1, 4095);
		tbl[lastCtx][(size_t)lastBin + 1] = (uint16_t)clamp_v(p1 + d1, 1, 4095);
	}
};
// ============================================================================
// 8. UNIFIED V9+ ENGINE (16 ORTHOGONAL BIT MODELS + MORPH STEM + WRD_STR CACHE)
// ============================================================================
struct HutterEngineV9 {
	ParserState parser;
	MultiMatchModel matcher;
	BitHistoryTable m0_order0;
	BitHistoryTable m1_order1;
	BitHistoryTable m2_order2;
	BitHistoryTable m3_order3;
	BitHistoryTable m4_order4;
	BitHistoryTable m5_order6;
	BitHistoryTable m6_order9;
	BitHistoryTable m7_sparse13;
	BitHistoryTable m8_indirect;
	BitHistoryTable m9_wordChain;
	BitHistoryTable m10_wordSkip;
	BitHistoryTable m11_shapeMorph;
	BitHistoryTable m12_xmlWikiFSM;
	BitHistoryTable m13_morphStem;
	BitHistoryTable m14_phonoCV;
	StretchedLogitMixer mixer;
	CascadedAPM apm1;
	CascadedAPM apm2;
	CascadedAPM apm3;
	array<double, 16> expertBitLL{};
	double mixerBitLL = 0.0;
	array<uint16_t, 16> lastProbs{};
	uint16_t lastFinalP = 2048;
	void init() {
		parser.init();
		matcher.init();
		m0_order0.init(1 << 16, 36);
		m1_order1.init(1 << 17, 32);
		m2_order2.init(1 << 19, 28);
		m3_order3.init(1 << 20, 26);
		m4_order4.init(1 << 21, 24);
		m5_order6.init(1 << 21, 22);
		m6_order9.init(1 << 21, 20);
		m7_sparse13.init(1 << 19, 26);
		m8_indirect.init(1 << 19, 26);
		m9_wordChain.init(1 << 20, 24);
		m10_wordSkip.init(1 << 20, 22);
		m11_shapeMorph.init(1 << 19, 26);
		m12_xmlWikiFSM.init(1 << 19, 28);
		m13_morphStem.init(1 << 19, 28);
		m14_phonoCV.init(1 << 19, 28);
		mixer.init();
		apm1.init();
		apm2.init();
		apm3.init();
		expertBitLL.fill(0.0);
		mixerBitLL = 0.0;
	}
	static inline uint64_t mix64(uint64_t x) {
		x ^= x >> 30;
		x *= 0xbf58476d1ce4e5b9ULL;
		x ^= x >> 27;
		x *= 0x94d049bb133111ebULL;
		x ^= x >> 31;
		return x;
	}
	uint16_t predictBit(int bitIdx, uint8_t cPart) {
		StructBucket sbkt = parser.currentBucket();
		uint64_t bkt = (uint64_t)sbkt;
		uint64_t p0 = parser.prev[0];
		uint64_t p1 = parser.prev[1];
		uint64_t p2 = parser.prev[2];
		uint64_t p3 = parser.prev[3];
		uint64_t h1 = (p0 << 8) | cPart;
		uint64_t h2 = (h1 << 8) | p1;
		uint64_t h3 = (h2 << 8) | p2;
		uint64_t h4 = (h3 << 8) | p3;
		uint64_t h6 = h4;
		for (int i = 4; i < 6; i++) h6 = (h6 * 1315423911ULL) ^ parser.prev[(size_t)i];
		uint64_t h9 = h6;
		for (int i = 6; i < 9; i++) h9 = (h9 * 1315423911ULL) ^ parser.prev[(size_t)i];
		uint16_t ctx2 = (uint16_t)((p1 << 8) | p0);
		uint64_t indB1 = parser.indirect1[ctx2];
		uint64_t indB2 = parser.indirect2[ctx2];
		uint16_t wSlot = (uint16_t)(parser.curWordHash ^ (parser.curWordHash >> 16));
		uint64_t recBucket = parser.wordRecency[wSlot] >> 4;
		uint64_t o0Ctx = (sbkt == CTX_WORD_START)
		                 ? ((bkt << 24) | ((uint64_t)parser.punctState << 22) |
		                    ((parser.prevWordHash & 0x3FFULL) << 12) | (p0 << 4) | cPart)
		                 : ((bkt << 12) | cPart);
		uint16_t cachePrior = (sbkt == CTX_WORD_START)
		                      ? parser.predictWordStartFromCache((uint8_t)bitIdx, cPart)
		                      : 2048;
		array<uint16_t, 16> probs;
		probs[0]  = m0_order0.predict(mix64(o0Ctx), cachePrior);
		probs[1]  = m1_order1.predict(mix64(h1), probs[0]);
		probs[2]  = m2_order2.predict(mix64(h2), probs[1]);
		probs[3]  = m3_order3.predict(mix64(h3), probs[2]);
		probs[4]  = m4_order4.predict(mix64(h4), probs[3]);
		probs[5]  = m5_order6.predict(mix64(h6), probs[4]);
		probs[6] = m6_order9.predict(mix64(h9), probs[5]);
		probs[7]  = m7_sparse13.predict(mix64((p0 << 16) | (p2 << 8) | cPart), probs[2]);
		probs[8]  = m8_indirect.predict(mix64((indB1 << 24) | (indB2 << 16) | (p0 << 8) | cPart), probs[1]);
		probs[11] = m11_shapeMorph.predict(mix64(((uint64_t)parser.shapeMask << 20) |
		                                   ((uint64_t)min<uint8_t>(15, parser.curWordLen) << 16) |
		                                   (recBucket << 12) | (p0 << 8) | cPart), probs[2]);
		uint16_t morphBase = (uint16_t)(((int)probs[3] + (int)probs[4]) >> 1);
		uint16_t briefPrior = parser.predictSubwordBriefBit((uint8_t)bitIdx, cPart);
		uint16_t stenoFallback = (briefPrior != 2048)
		                         ? (uint16_t)(((int)morphBase + (int)briefPrior) >> 1)
		                         : morphBase;
		uint64_t stenoCtx = parser.computeStenoSubwordContext((uint8_t)bitIdx, cPart);
		probs[13] = m13_morphStem.predict(mix64(stenoCtx), stenoFallback);

		uint64_t phonoCtx = parser.computePhonoCVContext(cPart);
		probs[14] = m14_phonoCV.predict(mix64(phonoCtx), probs[1]);
		uint16_t wordFallback = (sbkt == CTX_WORD_START)
		                        ? cachePrior
		                        : (uint16_t)(((int)probs[0] + (int)probs[11]) >> 1);
		probs[9]  = m9_wordChain.predict(mix64(parser.prevWordHash ^ (parser.curWordHash * 31ULL) ^ cPart), wordFallback);
		probs[10] = m10_wordSkip.predict(mix64(parser.prev2WordHash ^ (parser.curWordHash * 37ULL) ^ cPart), wordFallback);
		uint64_t fsmCtx = parser.computeXmlWikiFSMContext((uint8_t)bitIdx, cPart);
		probs[12] = m12_xmlWikiFSM.predict(mix64(fsmCtx), probs[2]);
		probs[15] = matcher.predictBit(bitIdx, cPart);
		uint32_t langMixIdx = parser.computeMixerContextIdx(sbkt, (uint8_t)bitIdx);
		array<uint32_t, 5> mixCtx = {
			langMixIdx,
			(uint32_t)mix64((bkt << 10) | ((uint64_t)min(7, matcher.bestLen) << 8) | cPart),
			(uint32_t)mix64((p0 << 8) | cPart),
			(uint32_t)mix64(((uint64_t)min<uint8_t>(7, parser.curWordLen) << 11) |
			                ((uint64_t)parser.char_shape((uint8_t)p0) << 8) | cPart),
			(uint32_t)mix64(((uint64_t)parser.suffixState << 12) |
			                ((uint64_t)parser.phono_phase() << 8) | cPart)
		};
		uint16_t pMix = mixer.mix(probs, mixCtx);
		uint16_t pA1 = apm1.refine(pMix, (uint32_t)mix64((p0 << 8) | cPart));
		uint16_t pA2 = apm2.refine(pA1,  (uint32_t)mix64(((parser.prevWordHash & 0xFFULL) << 8) | cPart));
		uint16_t pA3 = apm3.refine(pA2,  (uint32_t)mix64(((uint64_t)min(7, matcher.bestLen) << 10) |
		                           (bkt << 8) | cPart));
		int blended = (pMix * 2 + pA1 * 3 + pA2 * 3 + pA3 * 2 + 5) / 10;
		uint16_t rawFinalP = (uint16_t)clamp_v(blended, 1, 4095);
		uint16_t finalP = parser.applyBitLevelLanguageMask(rawFinalP, (uint8_t)bitIdx, cPart);
		lastProbs = probs;
		lastFinalP = finalP;
		return finalP;
	}
	void updateBit(int bit) {
		for (int m = 0; m < 16; m++) {
			double p = (double)lastProbs[(size_t)m] / 4096.0;
			double pActual = bit ? p : (1.0 - p);
			expertBitLL[(size_t)m] += -log2(max(1e-9, pActual));
		}
		double pf = (double)lastFinalP / 4096.0;
		mixerBitLL += -log2(max(1e-9, bit ? pf : (1.0 - pf)));
		m0_order0.update(bit);
		m1_order1.update(bit);
		m2_order2.update(bit);
		m3_order3.update(bit);
		m4_order4.update(bit);
		m5_order6.update(bit);
		m6_order9.update(bit);
		m7_sparse13.update(bit);
		m8_indirect.update(bit);
		m9_wordChain.update(bit);
		m10_wordSkip.update(bit);
		m11_shapeMorph.update(bit);
		m12_xmlWikiFSM.update(bit);
		m13_morphStem.update(bit);
		m14_phonoCV.update(bit);
		float lr = parser.adaptiveMixerLR(parser.currentBucket());
		mixer.update(bit, lr);
		apm1.update(bit, 4);
		apm2.update(bit, 4);
		apm3.update(bit, 4);
	}
	void finishByte(uint8_t b) {
		matcher.updateByte(b);
		parser.updateByte(b);
	}
};
// ============================================================================
// 9. BENCHMARK HARNESS & LOSSLESS VERIFICATION
// ============================================================================
inline string build_realistic_enwik_fallback() {
	return R"XML(
Anarchism
12

42136831
2006-03-04T01:41:25Z
CJames745832382
)XML";
}
int main(int argc, char** argv) {
auto t0 = chrono::steady_clock::now();
string text, sourceName = "Built-in Fallback";
size_t maxBytes = 0;
bool verifyDecode = true;
std::vector<std::string> candidates;
for (int i = 1; i < argc; i++) {
string arg = argv[i];
if (arg == "--encode-only") {
verifyDecode = false;
} else if (arg == "--cold") {
// V9+ is 100% pure cold-start online by design
} else if (arg == "--file" && i + 1 < argc) {
candidates.push_back(argv[++i]);
} else if (!arg.empty() && isdigit((unsigned char)arg.front())) {
maxBytes = (size_t)stoull(arg);
} else {
candidates.push_back(arg);
}
}
if (candidates.empty()) candidates = {"enwik9_1mb.txt"};
for (const string& cand : candidates) {
ifstream in(cand, ios::binary);
if (in.good()) {
if (maxBytes > 0) {
text.resize(maxBytes);
in.read(&text[0], (streamsize)maxBytes);
text.resize((size_t)in.gcount());
} else {
text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
if (!text.empty()) {
sourceName = cand;
break;
}
}
}
if (text.empty()) text = build_realistic_enwik_fallback();
uint64_t origLen = (uint64_t)text.size();
array<uint64_t, 4> bucketBytes{};
array<double, 4> bucketBits{};
// 1. ONLINE BIT-LEVEL ENCODE
HutterEngineV9 enc;
enc.init();
BitArithmeticCoder coder;
for (int b = 0; b < 8; b++) {
coder.out.push_back((uint8_t)((origLen >> (8 * b)) & 0xFFu));
}
for (size_t i = 0; i < text.size(); i++) {
uint8_t cur = (uint8_t)text[i];
int bkt = (int)enc.parser.currentBucket();
enc.matcher.prepareByte();
double beforeLL = enc.mixerBitLL;
uint8_t cPart = 1;
for (int bitIdx = 0; bitIdx < 8; bitIdx++) {
int bit = (cur >> (7 - bitIdx)) & 1;
uint16_t p1 = enc.predictBit(bitIdx, cPart);
coder.encodeBit(bit, p1);
enc.updateBit(bit);
cPart = (uint8_t)((cPart << 1) | bit);
}
enc.finishByte(cur);
bucketBytes[(size_t)bkt]++;
bucketBits[(size_t)bkt] += (enc.mixerBitLL - beforeLL);
}
coder.finish();
// 2. BIT-EXACT ONLINE DECODE VERIFICATION
bool losslessOK = true;
string decoded;
if (verifyDecode) {
uint64_t decodedLen = 0;
for (int b = 0; b < 8; b++) {
decodedLen |= ((uint64_t)coder.out[(size_t)b]) << (8 * b);
}
HutterEngineV9 dec;
dec.init();
BitArithmeticDecoder decoder;
decoder.init(coder.out, 8);
decoded.reserve((size_t)decodedLen);
for (uint64_t i = 0; i < decodedLen; i++) {
dec.matcher.prepareByte();
uint8_t cPart = 1;
for (int bitIdx = 0; bitIdx < 8; bitIdx++) {
uint16_t p1 = dec.predictBit(bitIdx, cPart);
int bit = decoder.decodeBit(p1);
dec.updateBit(bit);
cPart = (uint8_t)((cPart << 1) | bit);
}
dec.finishByte(cPart);
decoded.push_back((char)cPart);
}
losslessOK = (decoded == text);
}
auto t1 = chrono::steady_clock::now();
double sec = chrono::duration_cast<chrono::microseconds>(t1 - t0).count() / 1e6;
array<const char*, 4> bNames = {{"XML_TAG", "WIKI_MK", "WRD_STR", "MID_WRD"}};
array<const char*, 16> mNames = {{
"O0", "O1", "O2", "O3", "O4", "O6", "O9", "SP13",
"IND", "WRD1", "WRD2", "SHP", "FSM", "STEM", "PHON", "MTCH"
}};
double actualBpb = (double)coder.out.size() * 8.0 / max(1.0, (double)text.size());
cout << "=== V9+: 16-EXPERT BIT-LEVEL MIXER + MORPH STEM + WRD_STR CACHE + 3x APM ===\n";
cout << "Source          : " << sourceName << " (Total: " << text.size() << " B)\n";
cout << "Mode            : 100% Pure Cold-Start Online (Hierarchical Inheritance)\n";
cout << "Compressed bytes: " << coder.out.size() << " (incl. 8B header)\n";
cout << "Model bpb       : " << fixed << setprecision(4) << enc.mixerBitLL / max(1.0, (double)text.size()) << "\n";
cout << "Actual bpb      : " << fixed << setprecision(4) << actualBpb << "\n";
cout << "V8 (2.1638) gain: " << showpos << fixed << setprecision(4) << (actualBpb - 2.1638) << noshowpos << " bpb\n";
cout << "Elapsed time    : " << fixed << setprecision(2) << sec << " s ("
<< fixed << setprecision(1) << (text.size() / 1024.0) / max(1e-6, sec) << " KB/s)\n";
cout << "Lossless Verify : " << (!verifyDecode ? "SKIPPED" : (losslessOK ? "PASS (100% EXACT MATCH)" : "FAIL")) << "\n\n";
cout << "--- Individual Bit-Level Expert Standalone bpb (with Inheritance) ---\n";
for (int m = 0; m < 16; m++) {
cout << mNames[(size_t)m] << "=" << fixed << setprecision(3)
<< enc.expertBitLL[(size_t)m] / max(1.0, (double)text.size()) << " ";
if (m == 7) cout << "\n";
}
cout << "\n\n--- Per-Bucket Telemetry Breakdown ---\n";
for (int b = 0; b < 4; b++) {
if (bucketBytes[(size_t)b] > 0) {
cout << setw(8) << left << bNames[(size_t)b]
<< " (" << setw(7) << right << bucketBytes[(size_t)b] << " B) | Mix="
<< fixed << setprecision(3) << bucketBits[(size_t)b] / (double)bucketBytes[(size_t)b] << " bpb\n";
}
}
return losslessOK ? 0 : 1;
}