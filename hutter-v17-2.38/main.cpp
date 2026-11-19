#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include "block3_mixer_and_sse.hpp"
#include "block4_arithmetic_coder.hpp"
#include "block5_word_preprocessor.hpp"

struct CubeCascadedEngine {
	FastCubeMixer cube;

	// Orthogonal Prime-Modulus Context Tables (eliminating power-of-2 aliasing)
	PrimeContextTable<16381,   0x1111111111111111ULL> o0;
	PrimeContextTable<65521,   0x2222222222222222ULL> o1;
	PrimeContextTable<524287,  0x3333333333333333ULL> o2;
	PrimeContextTable<1048573, 0x4444444444444444ULL> o3;
	PrimeContextTable<1048573, 0x5555555555555555ULL> o4;
	PrimeContextTable<1048573, 0x6666666666666666ULL> o5;
	PrimeContextTable<1048573, 0x7777777777777777ULL> o6;
	PrimeContextTable<1048573, 0x8888888888888888ULL> wordModel;
	PrimeContextTable<1048573, 0x9999999999999999ULL> phraseModel;
	PrimeContextTable<524287,  0xAAAAAAAAAAAAAAAAULL> skipWordModel;
	PrimeContextTable<262139,  0xBBBBBBBBBBBBBBBBULL> runningCubeModel;

	BitMatchModel match;
	CascadedLogisticMixer mixer;
	SSECalibrator sse;
	std::vector<uint8_t> history;

	// Running non-Abelian Cube state across the stream
	uint64_t runCubeState = 0;
	uint64_t runCubeSyndrome = 1469598103934665603ULL;

	// Word & Morphological Root history
	uint64_t curWordHash   = 0xCBF29CE484222325ULL;
	uint64_t prevWordHash  = 0x84222325CBF29CE4ULL;
	uint64_t prev2WordHash = 0x123456789ABCDEF0ULL;
	uint64_t prev3WordHash = 0x0FEDCBA987654321ULL;
	int curWordLen = 0;
	uint32_t capState = 0;
	uint32_t charClassSeq = 0;

	// Grammar state for Sudoku-style deterministic bit-constraint pruning
	bool pendingEscape = false;
	bool pendingTokenLead = false;
	bool expectTokenByte = false; // True right after CAP_FIRST_MARK or CAP_ALL_MARK
	uint8_t tokenLeadByte = 0;

	CubeCascadedEngine() {
		runCubeState = cube.solvedCubies;
	}

	static inline uint8_t classifyByte(uint8_t c) {
		if (c >= 'a' && c <= 'z') return 1;
		if (c >= 'A' && c <= 'Z') return 2;
		if (c >= '0' && c <= '9') return 3;
		if (c == ' ' || c == '\n' || c == '\t') return 4;
		if (c >= 0x80 && c <= 0xFE) return 7;
		if (MorphologicalWordPreprocessor::isSuffixCode(c)) return 5;
		return 6;
	}

	// Sudoku-style Constraint Satisfaction: prunes mathematically impossible bits
	// Returns -1 if unconstrained, or 0/1 if the bit is 100% determined by token grammar.
	inline int getForcedBit(int bitIdx, uint32_t partialByte) const {
		// Rule 1: Immediately after CAP_FIRST_MARK or CAP_ALL_MARK, next byte MUST be a token ID (0x80..0xFE)
		if (expectTokenByte) {
			if (bitIdx == 7) return 1; // Top bit is forced 1 (>= 0x80)
			// Prevent 0xFF (0b11111111): if bits 7..1 are all 1s (partialByte == 0x1FF), bit 0 MUST be 0
			if (bitIdx == 0 && partialByte == 0x1FFU) return 0;
		}
		// Rule 2: Second byte of a 2-byte token (after 0xC0..0xFE) MUST be in 0x80..0xFE
		if (pendingTokenLead) {
			if (bitIdx == 7) return 1;
			if (bitIdx == 0 && partialByte == 0x1FFU) return 0;
		}
		return -1;
	}

	std::array<uint64_t, 11> getContextHashes() const {
		size_t n = history.size();
		std::array<uint64_t, 11> h{};
		h[0] = 0x123456789ABCDEF0ULL;
		h[1] = (n >= 1) ? cube.hashContext(&history[n - 1], 1) : h[0];
		h[2] = (n >= 2) ? cube.hashContext(&history[n - 2], 2) : h[1];
		h[3] = (n >= 3) ? cube.hashContext(&history[n - 3], 3) : h[2];
		h[4] = (n >= 4) ? cube.hashContext(&history[n - 4], 4) : h[3];
		h[5] = (n >= 5) ? cube.hashContext(&history[n - 5], 5) : h[4];
		h[6] = (n >= 6) ? cube.hashContext(&history[n - 6], 6) : h[5];

		// Order-1 and Order-2 Word/Root Contexts
		h[7] = cube.hashWord64(curWordHash ^ (prevWordHash * 0x9E3779B97F4A7C15ULL));
		h[8] = cube.hashWord64(curWordHash ^ (prevWordHash * 0xBF58476D1CE4E5B9ULL) ^ (prev2WordHash * 0x94D049BB133111EBULL));

		// Skip-bigram Word Context (prev2Word + prev3Word + charClass)
		h[9] = cube.hashWord64(prev2WordHash ^ (prev3WordHash * 0xD6E8FEB86659FD93ULL) ^ ((uint64_t)charClassSeq << 16));

		// Running Non-Abelian Cube State + Morphology/Cap State
		uint64_t rcState = runCubeState ^ runCubeSyndrome ^ ((uint64_t)capState << 48) ^ ((uint64_t)charClassSeq << 12);
		h[10] = cube.hashWord64(rcState);

		return h;
	}

	double predictBit(int bitIdx, uint32_t partialByte, const std::array<uint64_t, 11>& h) {
		int forced = getForcedBit(bitIdx, partialByte);
		if (forced == 1) return 0.9999;
		if (forced == 0) return 0.0001;

		std::array<double, 12> p{};
		p[0]  = o0.predict(h[0], partialByte);
		p[1]  = o1.predict(h[1], partialByte);
		p[2]  = o2.predict(h[2], partialByte);
		p[3]  = o3.predict(h[3], partialByte);
		p[4]  = o4.predict(h[4], partialByte);
		p[5]  = o5.predict(h[5], partialByte);
		p[6]  = o6.predict(h[6], partialByte);
		p[7]  = match.predictBit(history, bitIdx, partialByte);
		p[8]  = wordModel.predict(h[7], partialByte);
		p[9]  = phraseModel.predict(h[8], partialByte);
		p[10] = skipWordModel.predict(h[9], partialByte);
		p[11] = runningCubeModel.predict(h[10], partialByte);

		uint8_t lastByte = history.empty() ? 0 : history.back();
		uint64_t ctxByte  = ((uint64_t)lastByte << 8) ^ partialByte;
		uint64_t ctxSem   = ((uint64_t)capState << 8) ^ ((uint64_t)(lastByte >> 4) << 4) ^ (uint64_t)bitIdx;
		uint64_t matchBucket = (uint64_t)std::min(7, match.activeMatchLen() / 2);
		uint64_t wordBucket  = (uint64_t)std::min(7, curWordLen);
		uint64_t ctxMatch = (matchBucket << 7) ^ (wordBucket << 4) ^ (partialByte & 0x0F);
		uint64_t ctxMeta  = ((uint64_t)(matchBucket > 0) << 8) ^ partialByte;

		double mixedProb = mixer.mix(p, ctxByte, ctxSem, ctxMatch, ctxMeta);
		return sse.calibrate(mixedProb, partialByte, match.activeMatchLen());
	}

	void updateBit(int bit, int bitIdx, uint32_t partialByte, const std::array<uint64_t, 11>& h) {
		if (getForcedBit(bitIdx, partialByte) != -1) return; // Skip training on deterministic forced bits

		match.updateBit(history, bitIdx, bit);
		sse.update(bit);
		mixer.train(bit);
		o0.update(h[0], partialByte, bit);
		o1.update(h[1], partialByte, bit);
		o2.update(h[2], partialByte, bit);
		o3.update(h[3], partialByte, bit);
		o4.update(h[4], partialByte, bit);
		o5.update(h[5], partialByte, bit);
		o6.update(h[6], partialByte, bit);
		wordModel.update(h[7], partialByte, bit);
		phraseModel.update(h[8], partialByte, bit);
		skipWordModel.update(h[9], partialByte, bit);
		runningCubeModel.update(h[10], partialByte, bit);
	}

	void commitWordHash(uint64_t finishedHash) {
		prev3WordHash = prev2WordHash;
		prev2WordHash = prevWordHash;
		prevWordHash = finishedHash;
		curWordHash = 0xCBF29CE484222325ULL;
		curWordLen = 0;
	}

	void finishByte(uint8_t sym, uint64_t h3) {
		history.push_back(sym);
		match.updateByte(history, h3);
		cube.stepByte(sym, runCubeState, runCubeSyndrome);
		charClassSeq = (charClassSeq << 3) | classifyByte(sym);

		if (pendingEscape) {
			pendingEscape = false;
			if (curWordLen > 0) commitWordHash(curWordHash);
			capState = 0;
			return;
		}

		if (pendingTokenLead) {
			pendingTokenLead = false;
			uint64_t tokHash = (0xCBF29CE484222325ULL ^ tokenLeadByte) * 1099511628211ULL;
			tokHash = (tokHash ^ sym) * 1099511628211ULL;
			commitWordHash(tokHash);
			capState = 0;
			return;
		}

		if (expectTokenByte) {
			expectTokenByte = false;
		}

		if (sym == MorphologicalWordPreprocessor::ESC_RAW_BYTE) {
			pendingEscape = true;
			return;
		}

		if (sym == MorphologicalWordPreprocessor::CAP_FIRST_MARK) {
			if (curWordLen > 0) commitWordHash(curWordHash);
			capState = 2;
			expectTokenByte = true;
			return;
		}

		if (sym == MorphologicalWordPreprocessor::CAP_ALL_MARK) {
			if (curWordLen > 0) commitWordHash(curWordHash);
			capState = 3;
			expectTokenByte = true;
			return;
		}

		// Fold suffix code into previous word hash so root+suffix forms a unified inflectional state
		if (MorphologicalWordPreprocessor::isSuffixCode(sym)) {
			prevWordHash = (prevWordHash ^ sym) * 1099511628211ULL;
			return;
		}

		if (sym >= 0x80 && sym <= 0xBF) {
			if (curWordLen > 0) commitWordHash(curWordHash);
			uint64_t tokHash = (0xCBF29CE484222325ULL ^ sym) * 1099511628211ULL;
			commitWordHash(tokHash);
			capState = 0;
			return;
		}

		if (sym >= 0xC0 && sym <= 0xFE) {
			if (curWordLen > 0) commitWordHash(curWordHash);
			pendingTokenLead = true;
			tokenLeadByte = sym;
			curWordHash = (0xCBF29CE484222325ULL ^ sym) * 1099511628211ULL;
			curWordLen = 1;
			return;
		}

		if ((sym >= 'a' && sym <= 'z') || (sym >= 'A' && sym <= 'Z')) {
			bool isUpper = (sym >= 'A' && sym <= 'Z');
			uint8_t lower = isUpper ? (sym + 32) : sym;
			if (curWordLen == 0) {
				capState = isUpper ? 2 : 1;
			} else if (isUpper && capState == 2) {
				capState = 3;
			} else if (!isUpper && capState == 3) {
				capState = 1;
			}
			curWordHash = (curWordHash ^ lower) * 1099511628211ULL;
			curWordLen++;
		} else {
			if (curWordLen > 0) commitWordHash(curWordHash);
			capState = 0;
		}
	}
};

int main(int argc, char** argv) {
	std::string inputPath = (argc > 1) ? argv[1] : "enwik_sample.txt";

	std::cout << "=========================================================\n";
	std::cout << " STEP 6: MORPHOLOGICAL ROOT + CONSTRAINT-PRUNED CUBE ENGINE\n";
	std::cout << "=========================================================\n";

	std::string text;
	std::ifstream inFile(inputPath, std::ios::binary);
	if (inFile) {
		std::ostringstream ss;
		ss << inFile.rdbuf();
		text = ss.str();
		std::cout << " Source File     : " << inputPath << " (" << text.size() << " bytes)\n";
	} else {
		std::cout << " Please place '" << inputPath << "' in the working directory!\n";
		return 1;
	}

	auto t0 = std::chrono::high_resolution_clock::now();
	std::vector<uint8_t> tokStream = MorphologicalWordPreprocessor::transform(text);
	std::cout << " Preprocessed    : " << tokStream.size() << " bytes ("
	          << std::fixed << std::setprecision(1)
	          << (100.0 * tokStream.size() / text.size()) << "% of raw size)\n";

	CubeCascadedEngine encEngine;
	encEngine.history.reserve(tokStream.size());
	BinaryArithmeticEncoder encoder;

	uint32_t tokLen = (uint32_t)tokStream.size();
	for (int b = 0; b < 4; ++b) {
		encoder.out.push_back((uint8_t)((tokLen >> (b * 8)) & 0xFF));
	}

	for (uint8_t sym : tokStream) {
		auto h = encEngine.getContextHashes();
		uint32_t partial = 1;
		for (int bitIdx = 7; bitIdx >= 0; --bitIdx) {
			int bit = (sym >> bitIdx) & 1;
			double p1 = encEngine.predictBit(bitIdx, partial, h);
			encoder.encodeBit(bit, p1);
			encEngine.updateBit(bit, bitIdx, partial, h);
			partial = (partial << 1) | bit;
		}
		encEngine.finishByte(sym, h[3]);
	}
	encoder.finish();
	auto t1 = std::chrono::high_resolution_clock::now();
	double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
	double bpb = (encoder.out.size() * 8.0) / (double)text.size();

	std::cout << "\n Compression Results (Step 6: Morphological + Constraint Upgrade)\n";
	std::cout << "    Original size    : " << text.size() << " bytes\n";
	std::cout << "    Compressed size  : " << encoder.out.size() << " bytes (Step 5: 115956 bytes)\n";
	std::cout << "    Actual coder     : " << std::fixed << std::setprecision(4) << bpb << " bits/byte (Step 5: 2.3814 bpb)\n";
	std::cout << "    Hutter Record    : 0.8034 bits/byte (enwik9, 1 GB)\n";
	std::cout << "    Gap to Record    : " << (bpb - 0.8034) << " bits/byte\n";
	std::cout << "    Encode time      : " << std::setprecision(2) << ms << " ms\n";

	uint32_t decodedLen = 0;
	for (int b = 0; b < 4; ++b) {
		decodedLen |= ((uint32_t)encoder.out[b]) << (b * 8);
	}

	CubeCascadedEngine decEngine;
	decEngine.history.reserve(decodedLen);
	BinaryArithmeticDecoder decoder(encoder.out, 4);
	std::vector<uint8_t> decodedTokStream;
	decodedTokStream.reserve(decodedLen);

	for (size_t i = 0; i < decodedLen; ++i) {
		auto h = decEngine.getContextHashes();
		uint32_t partial = 1;
		uint8_t sym = 0;
		for (int bitIdx = 7; bitIdx >= 0; --bitIdx) {
			double p1 = decEngine.predictBit(bitIdx, partial, h);
			int bit = decoder.decodeBit(p1);
			sym = (sym << 1) | bit;
			decEngine.updateBit(bit, bitIdx, partial, h);
			partial = (partial << 1) | bit;
		}
		decEngine.finishByte(sym, h[3]);
		decodedTokStream.push_back(sym);
	}
	std::string decoded = MorphologicalWordPreprocessor::inverseTransform(decodedTokStream);
	std::cout << "\n Lossless Verification\n";
	std::cout << "    STATUS: " << (decoded == text ? "PASS (100% byte-for-byte identical!)" : "FAIL") << "\n";
	std::cout << "=========================================================\n";
	return 0;
}
