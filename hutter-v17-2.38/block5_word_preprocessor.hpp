#pragma once
#include "block1_cube_hasher.hpp"

// STEP 6: Morphological English Root + Affix Preprocessor (Arabic-Root Inspired)
class MorphologicalWordPreprocessor {
public:
	static constexpr uint8_t ESC_RAW_BYTE   = 0xFF;
	static constexpr uint8_t CAP_FIRST_MARK = 0x01;
	static constexpr uint8_t CAP_ALL_MARK   = 0x02;
	static constexpr uint8_t NO_SPACE_MARK  = 0x03;
	static constexpr uint8_t SUFFIX_BASE    = 0x04; // 0x04..0x0F -> 12 suffix codes
	static constexpr size_t  NUM_SUFFIXES   = 12;
	static constexpr size_t  MAX_DICT_SIZE  = 64 + 63 * 127;

	static constexpr std::array<const char*, NUM_SUFFIXES> SUFFIXES = {{
		"ation", "ment", "ness", "able", "ible", "tion",
		"ing", "ers", "ed", "ly", "er", "s"
	}};

	static inline bool isSuffixCode(uint8_t b) {
		return b >= SUFFIX_BASE && b < SUFFIX_BASE + NUM_SUFFIXES;
	}

private:
	struct DictState {
		std::vector<std::string> idToWord;
		std::unordered_map<std::string, uint16_t> wordToId;
		std::unordered_map<std::string, int> seenCounts;

		DictState() {
			static constexpr std::array<const char*, 64> TOP64 = {{
				"the", "and", "was", "for", "that", "with", "from", "his",
				"are", "were", "have", "this", "had", "not", "but", "which",
				"they", "their", "one", "all", "also", "been", "has", "its",
				"who", "more", "when", "into", "her", "there", "can", "time",
				"she", "other", "than", "new", "some", "could", "them", "these",
				"two", "may", "first", "after", "only", "most", "made", "over",
				"such", "where", "years", "used", "many", "during", "born", "being",
				"both", "between", "under", "while", "world", "american", "state", "university"
			}};
			idToWord.reserve(MAX_DICT_SIZE);
			for (const char* w : TOP64) {
				uint16_t id = (uint16_t)idToWord.size();
				idToWord.emplace_back(w);
				wordToId[w] = id;
			}
		}

		void observeRoot(const std::string& lowerRoot) {
			if (lowerRoot.size() < 3 || lowerRoot.size() > 24) return;
			if (wordToId.find(lowerRoot) != wordToId.end()) return;
			if (idToWord.size() >= MAX_DICT_SIZE) return;

			// Words >= 4 chars promote on 1st occurrence (2-byte ID immediately beats 4+ raw bytes)
			int thresh = (lowerRoot.size() >= 4) ? 1 : 2;
			int count = ++seenCounts[lowerRoot];
			if (count >= thresh) {
				uint16_t newId = (uint16_t)idToWord.size();
				idToWord.push_back(lowerRoot);
				wordToId[lowerRoot] = newId;
				seenCounts.erase(lowerRoot);
			}
		}
	};

	static inline bool isAlpha(uint8_t c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}

	static inline bool isReservedControl(uint8_t c) {
		return c >= 0x80 || c == CAP_FIRST_MARK || c == CAP_ALL_MARK || c == NO_SPACE_MARK || isSuffixCode(c);
	}

	static void emitTokenId(std::vector<uint8_t>& out, uint16_t id) {
		if (id < 64) {
			out.push_back((uint8_t)(0x80 + id));
		} else {
			uint16_t rem = id - 64;
			uint8_t b1 = (uint8_t)(0xC0 + (rem / 127));
			uint8_t b2 = (uint8_t)(0x80 + (rem % 127));
			out.push_back(b1);
			out.push_back(b2);
		}
	}

	static void emitLiteralByte(std::vector<uint8_t>& out, uint8_t b) {
		if (isReservedControl(b)) {
			out.push_back(ESC_RAW_BYTE);
		}
		out.push_back(b);
	}

	// Decomposes an inflected word into (knownRoot, suffixIndex) if root is already in dictionary
	static int splitMorphology(const DictState& dict, const std::string& lowerWord, std::string& outRoot) {
		if (dict.wordToId.find(lowerWord) != dict.wordToId.end()) {
			outRoot = lowerWord;
			return -1; // Exact match in dictionary
		}
		for (size_t s = 0; s < NUM_SUFFIXES; ++s) {
			std::string suf = SUFFIXES[s];
			if (lowerWord.size() >= suf.size() + 3 &&
			    lowerWord.compare(lowerWord.size() - suf.size(), suf.size(), suf) == 0) {
				std::string stem = lowerWord.substr(0, lowerWord.size() - suf.size());
				if (dict.wordToId.find(stem) != dict.wordToId.end()) {
					outRoot = stem;
					return (int)s;
				}
			}
		}
		outRoot = lowerWord;
		return -2; // Unknown root
	}

	// Extracts root for observation even when unseen, so inflected forms teach the shared stem
	static std::string extractCanonicalRoot(const std::string& lowerWord) {
		for (size_t s = 0; s < NUM_SUFFIXES; ++s) {
			std::string suf = SUFFIXES[s];
			if (lowerWord.size() >= suf.size() + 4 &&
			    lowerWord.compare(lowerWord.size() - suf.size(), suf.size(), suf) == 0) {
				return lowerWord.substr(0, lowerWord.size() - suf.size());
			}
		}
		return lowerWord;
	}

public:
	static std::vector<uint8_t> transform(const std::string& input) {
		DictState dict;
		std::vector<uint8_t> out;
		out.reserve(input.size());

		size_t i = 0;
		const size_t n = input.size();
		while (i < n) {
			uint8_t c = (uint8_t)input[i];
			if (isAlpha(c)) {
				size_t start = i;
				bool allUpper = true;
				bool firstUpper = (c >= 'A' && c <= 'Z');
				bool restLower = true;

				std::string lowerWord;
				while (i < n && isAlpha((uint8_t)input[i])) {
					uint8_t ch = (uint8_t)input[i];
					if (ch >= 'a' && ch <= 'z') {
						allUpper = false;
						lowerWord.push_back((char)ch);
					} else {
						if (i > start) restLower = false;
						lowerWord.push_back((char)(ch + 32));
					}
					i++;
				}

				size_t wordLen = i - start;
				bool validCase = restLower || (allUpper && wordLen > 1);
				std::string matchedRoot;
				int suffixIdx = validCase ? splitMorphology(dict, lowerWord, matchedRoot) : -2;

				if (suffixIdx >= -1) {
					if (allUpper && wordLen > 1) {
						out.push_back(CAP_ALL_MARK);
					} else if (firstUpper) {
						out.push_back(CAP_FIRST_MARK);
					}
					emitTokenId(out, dict.wordToId.at(matchedRoot));
					if (suffixIdx >= 0) {
						out.push_back((uint8_t)(SUFFIX_BASE + suffixIdx));
					}

					if (i < n && input[i] == ' ') {
						i++;
					} else {
						out.push_back(NO_SPACE_MARK);
					}
				} else {
					for (size_t k = start; k < i; ++k) {
						emitLiteralByte(out, (uint8_t)input[k]);
					}
					// Observe both the full word and its morphological stem
					dict.observeRoot(lowerWord);
					std::string stem = extractCanonicalRoot(lowerWord);
					if (stem != lowerWord) dict.observeRoot(stem);
				}
			} else {
				emitLiteralByte(out, c);
				i++;
			}
		}
		return out;
	}

	static std::string inverseTransform(const std::vector<uint8_t>& stream) {
		DictState dict;
		std::string out;
		out.reserve(stream.size() * 2);

		std::string pendingLowerWord;
		auto flushPendingLiteralWord = [&]() {
			if (!pendingLowerWord.empty()) {
				dict.observeRoot(pendingLowerWord);
				std::string stem = extractCanonicalRoot(pendingLowerWord);
				if (stem != pendingLowerWord) dict.observeRoot(stem);
				pendingLowerWord.clear();
			}
		};

		auto appendLiteralChar = [&](uint8_t ch) {
			out.push_back((char)ch);
			if (isAlpha(ch)) {
				uint8_t lower = (ch >= 'A' && ch <= 'Z') ? (ch + 32) : ch;
				pendingLowerWord.push_back((char)lower);
			} else {
				flushPendingLiteralWord();
			}
		};

		size_t i = 0;
		const size_t n = stream.size();
		while (i < n) {
			uint8_t b = stream[i++];

			if (b == ESC_RAW_BYTE) {
				if (i < n) appendLiteralChar(stream[i++]);
				continue;
			}

			int capMode = 0;
			if (b == CAP_FIRST_MARK || b == CAP_ALL_MARK) {
				flushPendingLiteralWord();
				capMode = (b == CAP_FIRST_MARK) ? 1 : 2;
				if (i >= n) break;
				b = stream[i++];
			}

			if (b >= 0x80 && b <= 0xFE) {
				flushPendingLiteralWord();
				uint16_t id = 0;
				if (b <= 0xBF) {
					id = (uint16_t)(b - 0x80);
				} else {
					uint8_t b2 = (i < n) ? stream[i++] : 0x80;
					id = (uint16_t)(64 + (b - 0xC0) * 127 + (b2 - 0x80));
				}
				std::string word = dict.idToWord.at(id);
				if (i < n && isSuffixCode(stream[i])) {
					word.append(SUFFIXES[stream[i] - SUFFIX_BASE]);
					i++;
				}

				if (capMode == 1 && !word.empty() && word.front() >= 'a' && word.front() <= 'z') {
					word.front() = (char)(word.front() - 32);
				} else if (capMode == 2) {
					for (char& ch : word) {
						if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
					}
				}
				out.append(word);

				if (i < n && stream[i] == NO_SPACE_MARK) {
					i++;
				} else {
					out.push_back(' ');
				}
			} else {
				appendLiteralChar(b);
			}
		}
		flushPendingLiteralWord();
		return out;
	}
};
