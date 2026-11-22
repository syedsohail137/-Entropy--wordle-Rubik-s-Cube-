#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

enum PunctClass : uint8_t {
	PUNCT_NONE     = 0,
	PUNCT_CLAUSE   = 1,
	PUNCT_SENTENCE = 2,
	PUNCT_WRAPPER  = 3
};

enum StructBucket : uint8_t {
	CTX_XML_TAG     = 0,
	CTX_WIKI_MARKUP = 1,
	CTX_WORD_START  = 2,
	CTX_MID_WORD    = 3
};

inline uint64_t fnv1a_step(uint64_t h, uint8_t b) noexcept {
	return (h ^ static_cast<uint64_t>(b)) * 1099511628211ULL;
}

inline bool is_vowel_byte(uint8_t c) noexcept {
	return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y' ||
	        c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y');
}

struct ParserState {
	std::array<uint8_t, 16> prev{};
	uint8_t p0 = 0, p1 = 0, p2 = 0, p3 = 0;
	uint32_t lineCol = 0;

	uint8_t utf8Remaining = 0;
	uint8_t utf8LeadClass = 0;

	bool inXmlTag          = false;
	bool inXmlClosing      = false;
	bool inXmlAttr         = false;
	char xmlQuoteChar      = 0;
	bool inXmlComment      = false;
	uint8_t commentDashRun = 0;

	uint64_t curTagHash    = 14695981039346656037ULL;
	std::string curTagName;
	std::vector<std::string> tagStack;
	uint8_t closingMatchIdx = 0;

	bool inEntity     = false;
	char entityBuf[8] = {0};
	uint8_t entityLen = 0;
	bool inProseQuote = false;

	uint8_t wikiLinkDepth = 0;
	uint8_t wikiTplDepth  = 0;
	bool extLinkOpen      = false;
	uint8_t aposRun       = 0;
	bool wikiItalic       = false;
	bool wikiBold         = false;
	uint8_t parenDepth    = 0;

	PunctClass punctState    = PUNCT_NONE;
	PunctClass prevWordPunct = PUNCT_NONE;
	bool postApostrophe      = false;
	bool postHyphen          = false;

	uint64_t curWordHash   = 14695981039346656037ULL;
	uint64_t subWordHash   = 14695981039346656037ULL;
	uint64_t prevWordHash  = 0;
	uint64_t prev2WordHash = 0;
	char curWordBuf[32]    = {0};
	uint8_t curWordLen     = 0;
	bool wordStartedLower  = false;
	bool wordHasUpper      = false;

	struct RecentWordEntry {
		uint64_t hash     = 0;
		uint8_t firstByte = 0;
		uint8_t count     = 0;
	};
	std::array<RecentWordEntry, 32> recentWords{};

	uint32_t shapeMask   = 0;
	uint64_t stemHash    = 14695981039346656037ULL;
	uint8_t prefixClass  = 0;
	uint8_t prefixLen    = 0;
	uint8_t suffixState  = 0;
	uint32_t last3Lower  = 0;

	uint8_t cvMask       = 0;
	uint8_t consRun      = 0;
	uint8_t vowelRun     = 0;
	uint8_t syllableCnt  = 0;
	uint8_t lastCons1    = 0;
	uint8_t lastCons2    = 0;
	uint8_t lastDigraph  = 0;

	std::vector<uint8_t> indirect1;
	std::vector<uint8_t> indirect2;
	std::vector<uint16_t> wordRecency;

	ParserState() {
		init();
	}

	void init() {
		prev.fill(0);
		p0 = p1 = p2 = p3 = 0;
		lineCol = 0;
		utf8Remaining = utf8LeadClass = 0;
		inXmlTag = inXmlClosing = inXmlAttr = false;
		xmlQuoteChar = 0;
		inXmlComment = false;
		commentDashRun = 0;
		curTagHash = 14695981039346656037ULL;
		curTagName.clear();
		tagStack.clear();
		closingMatchIdx = 0;
		inEntity = false;
		std::memset(entityBuf, 0, sizeof(entityBuf));
		entityLen = 0;
		inProseQuote = false;
		wikiLinkDepth = wikiTplDepth = 0;
		extLinkOpen = false;
		aposRun = 0;
		wikiItalic = wikiBold = false;
		parenDepth = 0;
		punctState = prevWordPunct = PUNCT_NONE;
		postApostrophe = postHyphen = false;
		curWordHash = subWordHash = 14695981039346656037ULL;
		prevWordHash = prev2WordHash = 0;
		std::memset(curWordBuf, 0, sizeof(curWordBuf));
		curWordLen = 0;
		wordStartedLower = false;
		wordHasUpper = false;
		recentWords.fill(RecentWordEntry{0, 0, 0});
		shapeMask = 0;
		stemHash = 14695981039346656037ULL;
		prefixClass = 0;
		prefixLen = 0;
		suffixState = 0;
		last3Lower = 0;
		cvMask = consRun = vowelRun = syllableCnt = 0;
		lastCons1 = lastCons2 = lastDigraph = 0;
		indirect1.assign(65536, 0);
		indirect2.assign(65536, 0);
		wordRecency.assign(65536, 0);
	}

	void reset() {
		init();
	}

	inline uint8_t char_shape(uint8_t c) const noexcept {
		if (std::isupper(c)) return 1;
		if (std::islower(c)) return 2;
		if (std::isdigit(c)) return 3;
		if (c == ' ' || c == '\n') return 4;
		return 5;
	}

	inline int phono_phase() const noexcept {
		bool midWord = (std::isalpha(p0) != 0);
		return ((consRun >= 2) ? 1 : 0) | ((vowelRun >= 2) ? 2 : 0) | (midWord ? 4 : 0);
	}

	bool isAbbreviation() const noexcept {
		if (curWordLen == 1) return true;
		char w[8] = {0};
		uint8_t n = std::min<uint8_t>(curWordLen, 7);
		for (uint8_t i = 0; i < n; i++) {
			w[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(curWordBuf[i])));
		}
		if (n == 2) {
			return (std::memcmp(w, "dr", 2) == 0 || std::memcmp(w, "mr", 2) == 0 ||
			        std::memcmp(w, "ms", 2) == 0 || std::memcmp(w, "st", 2) == 0 ||
			        std::memcmp(w, "vs", 2) == 0 || std::memcmp(w, "cf", 2) == 0 ||
			        std::memcmp(w, "ed", 2) == 0 || std::memcmp(w, "al", 2) == 0 ||
			        std::memcmp(w, "pp", 2) == 0 || std::memcmp(w, "no", 2) == 0 ||
			        std::memcmp(w, "co", 2) == 0 || std::memcmp(w, "ca", 2) == 0 ||
			        std::memcmp(w, "fl", 2) == 0 || std::memcmp(w, "ch", 2) == 0 ||
			        std::memcmp(w, "pt", 2) == 0 || std::memcmp(w, "jr", 2) == 0 ||
			        std::memcmp(w, "sr", 2) == 0 || std::memcmp(w, "op", 2) == 0);
		}
		if (n == 3) {
			return (std::memcmp(w, "mrs", 3) == 0 || std::memcmp(w, "etc", 3) == 0 ||
			        std::memcmp(w, "inc", 3) == 0 || std::memcmp(w, "ltd", 3) == 0 ||
			        std::memcmp(w, "vol", 3) == 0 || std::memcmp(w, "eds", 3) == 0 ||
			        std::memcmp(w, "rev", 3) == 0 || std::memcmp(w, "est", 3) == 0 ||
			        std::memcmp(w, "gen", 3) == 0 || std::memcmp(w, "sen", 3) == 0 ||
			        std::memcmp(w, "rep", 3) == 0 || std::memcmp(w, "gov", 3) == 0 ||
			        std::memcmp(w, "fig", 3) == 0 || std::memcmp(w, "sec", 3) == 0 ||
			        std::memcmp(w, "ser", 3) == 0);
		}
		if (n == 4) {
			return (std::memcmp(w, "prof", 4) == 0 || std::memcmp(w, "dept", 4) == 0 ||
			        std::memcmp(w, "univ", 4) == 0 || std::memcmp(w, "assn", 4) == 0 ||
			        std::memcmp(w, "bros", 4) == 0);
		}
		if (n == 6) {
			return (std::memcmp(w, "approx", 6) == 0);
		}
		return false;
	}

	StructBucket currentBucket() const noexcept {
		if (inXmlTag || inXmlComment) return CTX_XML_TAG;
		if (wikiLinkDepth > 0 || wikiTplDepth > 0 || extLinkOpen || inEntity || wikiBold || wikiItalic) {
			return CTX_WIKI_MARKUP;
		}
		if (curWordLen > 0) return CTX_MID_WORD;
		return CTX_WORD_START;
	}

	uint16_t predictWordStartFromCache(uint8_t bitIdx, uint8_t cPart) const noexcept {
		int shift = 7 - bitIdx;
		uint8_t prefixMask = static_cast<uint8_t>(cPart - (1u << bitIdx));
		int w0 = 0, w1 = 0;
		for (int i = 0; i < 32; i++) {
			const auto& entry = recentWords[static_cast<size_t>(i)];
			if (entry.count == 0) continue;
			uint8_t fb = entry.firstByte;
			if ((fb >> (shift + 1)) == prefixMask) {
				int b = (fb >> shift) & 1;
				int weight = (32 - i) * static_cast<int>(entry.count);
				if (b == 1) w1 += weight;
				else w0 += weight;
			}
		}
		int tot = w0 + w1;
		if (tot == 0) return 2048;
		int p = 2048 + ((w1 - w0) * 1024) / (tot + 64);
		return static_cast<uint16_t>(std::clamp(p, 512, 3584));
	}

	uint16_t applyBitLevelLanguageMask(uint16_t prob12, uint8_t bitIdx, uint8_t cPart) const noexcept {
		if (utf8Remaining > 0) {
			if (bitIdx == 0) return 4092;
			if (bitIdx == 1) return 4;
			return prob12;
		}

		if (bitIdx == 0) {
			if (inXmlTag) {
				return std::min<uint16_t>(prob12, 8);
			}
			if (p0 == ' ') {
				return std::clamp<uint16_t>(prob12, 24, 192);
			}
		}

		if (inXmlClosing && !tagStack.empty()) {
			const std::string& expected = tagStack.back();
			uint8_t targetChar = (closingMatchIdx < expected.size())
			                     ? static_cast<uint8_t>(expected[closingMatchIdx])
			                     : static_cast<uint8_t>('>');
			uint8_t mask = (bitIdx == 0) ? 0 : static_cast<uint8_t>(0xFF << (8 - bitIdx));
			if ((cPart & mask) == (targetChar & mask)) {
				uint8_t targetBit = (targetChar >> (7 - bitIdx)) & 1;
				return targetBit ? std::max<uint16_t>(prob12, 3930)
				       : std::min<uint16_t>(prob12, 165);
			}
		}

		if (inEntity && entityLen >= 2) {
			uint8_t nextExpected = 0;
			if (entityLen == 4 && std::memcmp(entityBuf, "quot", 4) == 0) nextExpected = ';';
			else if (entityLen == 3 && std::memcmp(entityBuf, "amp", 3) == 0) nextExpected = ';';
			else if (entityLen == 2 && (std::memcmp(entityBuf, "lt", 2) == 0 ||
			                            std::memcmp(entityBuf, "gt", 2) == 0)) nextExpected = ';';
			if (nextExpected != 0) {
				uint8_t mask = (bitIdx == 0) ? 0 : static_cast<uint8_t>(0xFF << (8 - bitIdx));
				if ((cPart & mask) == (nextExpected & mask)) {
					uint8_t targetBit = (nextExpected >> (7 - bitIdx)) & 1;
					return targetBit ? std::max<uint16_t>(prob12, 3850)
					       : std::min<uint16_t>(prob12, 245);
				}
			}
		}

		if (!inXmlTag && p0 == ' ' && (cPart & 0x80) == 0) {
			if (punctState == PUNCT_SENTENCE) {
				if (bitIdx == 2 && (cPart & 0x40)) {
					return std::min<uint16_t>(prob12, 1350);
				}
			} else if (punctState == PUNCT_CLAUSE) {
				if (bitIdx == 2 && (cPart & 0x40)) {
					return std::max<uint16_t>(prob12, 3150);
				}
			}
		}

		if (curWordLen >= 2 && wordStartedLower && !wordHasUpper && (cPart & 0x80) == 0) {
			if (bitIdx == 2 && (cPart & 0x40)) {
				return std::max<uint16_t>(prob12, 3550);
			}
		}

		return prob12;
	}

	void updateMorphologyState(uint8_t lc) noexcept {
		last3Lower = ((last3Lower << 8) | lc) & 0x00FFFFFFu;

		if (prefixClass == 0 && (curWordLen == 2 || curWordLen == 3 || curWordLen == 4 || curWordLen == 5)) {
			uint8_t matchedPrefix = 0;
			if (curWordLen == 2) {
				if (std::memcmp(curWordBuf, "un", 2) == 0) matchedPrefix = 1;
				else if (std::memcmp(curWordBuf, "re", 2) == 0) matchedPrefix = 2;
				else if (std::memcmp(curWordBuf, "in", 2) == 0) matchedPrefix = 3;
				else if (std::memcmp(curWordBuf, "im", 2) == 0) matchedPrefix = 4;
				else if (std::memcmp(curWordBuf, "en", 2) == 0) matchedPrefix = 5;
				else if (std::memcmp(curWordBuf, "de", 2) == 0) matchedPrefix = 6;
				else if (std::memcmp(curWordBuf, "ex", 2) == 0) matchedPrefix = 7;
			} else if (curWordLen == 3) {
				if (std::memcmp(curWordBuf, "dis", 3) == 0) matchedPrefix = 8;
				else if (std::memcmp(curWordBuf, "non", 3) == 0) matchedPrefix = 9;
				else if (std::memcmp(curWordBuf, "mis", 3) == 0) matchedPrefix = 10;
				else if (std::memcmp(curWordBuf, "sub", 3) == 0) matchedPrefix = 11;
				else if (std::memcmp(curWordBuf, "pre", 3) == 0) matchedPrefix = 12;
				else if (std::memcmp(curWordBuf, "pro", 3) == 0) matchedPrefix = 13;
				else if (std::memcmp(curWordBuf, "con", 3) == 0) matchedPrefix = 14;
				else if (std::memcmp(curWordBuf, "com", 3) == 0) matchedPrefix = 15;
			} else if (curWordLen == 4) {
				if (std::memcmp(curWordBuf, "over", 4) == 0) matchedPrefix = 16;
				else if (std::memcmp(curWordBuf, "anti", 4) == 0) matchedPrefix = 17;
				else if (std::memcmp(curWordBuf, "auto", 4) == 0) matchedPrefix = 18;
			} else if (curWordLen == 5) {
				if (std::memcmp(curWordBuf, "inter", 5) == 0) matchedPrefix = 19;
				else if (std::memcmp(curWordBuf, "trans", 5) == 0) matchedPrefix = 20;
				else if (std::memcmp(curWordBuf, "under", 5) == 0) matchedPrefix = 21;
			}
			if (matchedPrefix != 0) {
				prefixClass = matchedPrefix;
				prefixLen = curWordLen;
				stemHash = 14695981039346656037ULL;
			} else {
				stemHash = fnv1a_step(stemHash, lc);
			}
		} else {
			stemHash = fnv1a_step(stemHash, lc);
		}
		suffixState = 0;
		if (curWordLen >= 3) {
			uint32_t t2 = last3Lower & 0xFFFFu;
			uint32_t t3 = last3Lower;
			if (t3 == (('t' << 16) | ('i' << 8) | 'o')) suffixState = 1;
			else if (t3 == (('s' << 16) | ('i' << 8) | 'o')) suffixState = 2;
			else if (t3 == (('i' << 16) | ('o' << 8) | 'n')) suffixState = 3;
			else if (t3 == (('i' << 16) | ('n' << 8) | 'g')) suffixState = 4;
			else if (t3 == (('m' << 16) | ('e' << 8) | 'n')) suffixState = 5;
			else if (t3 == (('n' << 16) | ('e' << 8) | 's')) suffixState = 6;
			else if (t3 == (('a' << 16) | ('b' << 8) | 'l')) suffixState = 7;
			else if (t3 == (('i' << 16) | ('b' << 8) | 'l')) suffixState = 8;
			else if (t3 == (('o' << 16) | ('u' << 8) | 's')) suffixState = 9;
			else if (t3 == (('i' << 16) | ('v' << 8) | 'e')) suffixState = 10;
			else if (t3 == (('i' << 16) | ('t' << 8) | 'y')) suffixState = 11;
			else if (t3 == (('i' << 16) | ('s' << 8) | 'm')) suffixState = 12;
			else if (t3 == (('i' << 16) | ('s' << 8) | 't')) suffixState = 13;
			else if (t3 == (('l' << 16) | ('o' << 8) | 'g')) suffixState = 14;
			else if (t2 == (('e' << 8) | 'd')) suffixState = 15;
			else if (t2 == (('l' << 8) | 'y')) suffixState = 16;
			else if (t2 == (('e' << 8) | 'r')) suffixState = 17;
			else if (t2 == (('a' << 8) | 'l')) suffixState = 18;
			else if (t2 == (('i' << 8) | 'c')) suffixState = 19;
		}
	}
	uint64_t computeMorphStemContext(uint8_t cPart) const noexcept {
		uint8_t posBucket = (curWordLen == 0) ? 0
		                    : (curWordLen == 1) ? 1
		                    : (curWordLen == 2) ? 2
		                    : (curWordLen == 3) ? 3
		                    : (curWordLen <= 5) ? 4
		                    : (curWordLen <= 8) ? 5 : 6;
		uint64_t h = stemHash ^
		             (static_cast<uint64_t>(prefixClass) << 24) ^
		             (static_cast<uint64_t>(suffixState) << 18) ^
		             (static_cast<uint64_t>(posBucket)   << 15) ^
		             (static_cast<uint64_t>(last3Lower & 0xFFFFu) << 8) ^
		             static_cast<uint64_t>(cPart);
		return h;
	}
	uint64_t computePhonoCVContext(uint8_t cPart) const noexcept {
		uint64_t skel = (static_cast<uint64_t>(lastCons2)   << 24) |
		                (static_cast<uint64_t>(lastCons1)   << 16) |
		                (static_cast<uint64_t>(syllableCnt) << 12) |
		                (static_cast<uint64_t>(lastDigraph) << 8)  |
		                static_cast<uint64_t>(cvMask);
		return skel ^ (static_cast<uint64_t>(phono_phase()) << 32) ^ cPart;
	}
	void updateByte(uint8_t b) {
		uint16_t ctx2 = static_cast<uint16_t>((static_cast<uint16_t>(p1) << 8) | p0);
		indirect2[ctx2] = indirect1[ctx2];
		indirect1[ctx2] = b;
		if (utf8Remaining > 0) {
			if ((b & 0xC0) == 0x80) {
				utf8Remaining--;
			} else {
				utf8Remaining = 0;
				utf8LeadClass = 0;
			}
		} else if ((b & 0x80) != 0) {
			if ((b & 0xE0) == 0xC0 && b >= 0xC2) {
				utf8Remaining = 1;
				utf8LeadClass = 2;
			} else if ((b & 0xF0) == 0xE0) {
				utf8Remaining = 2;
				utf8LeadClass = 3;
			} else if ((b & 0xF8) == 0xF0 && b <= 0xF4) {
				utf8Remaining = 3;
				utf8LeadClass = 4;
			}
		} else {
			utf8LeadClass = 0;
		}
		if (b == '\n') {
			lineCol = 0;
			wikiItalic = false;
			wikiBold = false;
			aposRun = 0;
		} else {
			lineCol++;
			if (b == 0x27 && !inXmlTag) { // 0x27 = '''
				aposRun++;
			} else if (aposRun > 0) {
				if (aposRun == 2) wikiItalic = !wikiItalic;
				else if (aposRun == 3) wikiBold = !wikiBold;
				else if (aposRun >= 5) {
					wikiItalic = !wikiItalic;
					wikiBold = !wikiBold;
				}
				aposRun = 0;
			}
		}
		uint8_t lc = static_cast<uint8_t>(std::tolower(b));
		uint8_t pl = static_cast<uint8_t>(std::tolower(p0));
		lastDigraph = 0;
		if (pl == 't' && lc == 'h') lastDigraph = 1;
		else if (pl == 's' && lc == 'h') lastDigraph = 2;
		else if (pl == 'c' && lc == 'h') lastDigraph = 3;
		else if (pl == 'q' && lc == 'u') lastDigraph = 4;
		else if (pl == 'n' && lc == 'g') lastDigraph = 5;
		if (inXmlComment) {
			if (b == '-') commentDashRun++;
			else if (b == '>' && commentDashRun >= 2) {
				inXmlComment = false;
				commentDashRun = 0;
			} else {
				commentDashRun = 0;
			}
			shiftHistory(b);
			return;
		}
		if (p2 == '<' && p1 == '!' && p0 == '-' && b == '-') {
			inXmlComment = true;
			inXmlTag = false;
			commentDashRun = 0;
			shiftHistory(b);
			return;
		}
		if (b == '<' && !inXmlAttr) {
			inXmlTag = true;
			inXmlClosing = false;
			curTagHash = 14695981039346656037ULL;
			curTagName.clear();
			closingMatchIdx = 0;
		} else if (inXmlTag) {
			if (!inXmlAttr && (b == '"' || b == 0x27)) {
				inXmlAttr = true;
				xmlQuoteChar = (char)b;
			} else if (inXmlAttr && b == static_cast<uint8_t>(xmlQuoteChar)) {
				inXmlAttr = false;
				xmlQuoteChar = 0;
			} else if (!inXmlAttr) {
				if (b == '/' && p0 == '<') {
					inXmlClosing = true;
					closingMatchIdx = 0;
				} else if (b == '>') {
					if (!inXmlClosing && p0 != '/' && !curTagName.empty()) {
						if (tagStack.size() < 32) tagStack.push_back(curTagName);
					} else if (inXmlClosing && !tagStack.empty()) {
						tagStack.pop_back();
					}
					inXmlTag = false;
					inXmlClosing = false;
				} else if (std::isalnum(b) || b == ':' || b == '_') {
					if (inXmlClosing) {
						closingMatchIdx++;
					} else if (curTagName.size() < 24 && (p0 == '<' || !curTagName.empty())) {
						curTagName.push_back((char)b);
						curTagHash = fnv1a_step(curTagHash, b);
					}
				}
			}
		}
		if (!inXmlTag) {
			if (b == '&') {
				inEntity = true;
				entityLen = 0;
			} else if (inEntity) {
				if (b == ';') {
					if (entityLen == 4 && std::memcmp(entityBuf, "quot", 4) == 0) {
						inProseQuote = !inProseQuote;
						punctState = PUNCT_WRAPPER;
					}
					inEntity = false;
					entityLen = 0;
				} else if (std::isalnum(b) && entityLen < 7) {
					entityBuf[entityLen++] = (char)b;
				} else {
					inEntity = false;
					entityLen = 0;
				}
			}
		}
		if (!inXmlTag) {
			if (b == '[' && p0 == '[') {
				if (wikiLinkDepth < 4) wikiLinkDepth++;
				extLinkOpen = false;
			} else if (b == '[' && p0 != '[') {
				extLinkOpen = true;
			} else if (b == ']' && p0 == ']') {
				if (wikiLinkDepth > 0) wikiLinkDepth--;
			} else if (b == ']' && p0 != ']') {
				extLinkOpen = false;
			} else if (b == '{' && p0 == '{') {
				if (wikiTplDepth < 4) wikiTplDepth++;
			} else if (b == '}' && p0 == '}') {
				if (wikiTplDepth > 0) wikiTplDepth--;
			} else if (b == '(') {
				if (parenDepth < 3) parenDepth++;
			} else if (b == ')') {
				if (parenDepth > 0) parenDepth--;
			}
		}
		bool isAlpha = (std::isalpha(b) != 0) || (utf8Remaining > 0) || (utf8LeadClass > 0);
		bool isInternalJoiner = ((b == 0x27 || b == '-') && curWordLen > 0 && std::isalpha(p0));
		if (isAlpha && !inEntity) {
			if (curWordLen == 0) {
				punctState = PUNCT_NONE;
				postApostrophe = false;
				postHyphen = false;
				wordStartedLower = (std::islower(b) != 0);
				wordHasUpper = (std::isupper(b) != 0);
				stemHash = 14695981039346656037ULL;
				prefixClass = 0;
				prefixLen = 0;
				suffixState = 0;
				last3Lower = 0;
				syllableCnt = 0;
				lastCons1 = 0;
				lastCons2 = 0;
			} else if (std::isupper(b)) {
				wordHasUpper = true;
			}
			curWordHash = fnv1a_step(curWordHash, lc);
			subWordHash = fnv1a_step(subWordHash, lc);
			if (curWordLen < 28) curWordBuf[curWordLen++] = (char)lc;;
			updateMorphologyState(lc);
			uint32_t sCode = (utf8LeadClass > 0 || utf8Remaining > 0) ? 3u : (std::isupper(b) ? 1u : 2u);
			shapeMask = (shapeMask << 2) | sCode;
			bool isV = is_vowel_byte(b);
			cvMask = static_cast<uint8_t>((cvMask << 1) | (isV ? 1 : 0));
			if (isV) {
				if (vowelRun == 0 && syllableCnt < 15) syllableCnt++;
				vowelRun = (vowelRun < 7) ? static_cast<uint8_t>(vowelRun + 1) : 7u;
				consRun = 0;
			} else {
				consRun = (consRun < 7) ? static_cast<uint8_t>(consRun + 1) : 7u;
				vowelRun = 0;
				lastCons2 = lastCons1;
				lastCons1 = lc;
			}
		} else if (isInternalJoiner) {
			curWordHash = fnv1a_step(curWordHash, b);
			subWordHash = 14695981039346656037ULL;
			stemHash    = 14695981039346656037ULL;
			prefixClass = 0;
			prefixLen   = 0;
			suffixState = 0;
			postApostrophe = (b == 0x27);
			postHyphen = (b == '-');
		} else {
			if (curWordLen > 0) {
				curWordBuf[curWordLen] = '\0';
				uint8_t firstByte = static_cast<uint8_t>(curWordBuf[0]);
				int foundIdx = -1;
				for (int i = 0; i < 32; i++) {
					if (recentWords[static_cast<size_t>(i)].count > 0 &&
					        recentWords[static_cast<size_t>(i)].hash == curWordHash) {
						foundIdx = i;
						break;
					}
				}
				if (foundIdx >= 0) {
					RecentWordEntry hit = recentWords[static_cast<size_t>(foundIdx)];
					hit.count = static_cast<uint8_t>(std::min(15, (int)hit.count + 1));
					for (int i = foundIdx; i > 0; i--) {
						recentWords[static_cast<size_t>(i)] = recentWords[static_cast<size_t>(i - 1)];
					}
					recentWords[0] = hit;
				} else {
					for (int i = 31; i > 0; i--) {
						recentWords[static_cast<size_t>(i)] = recentWords[static_cast<size_t>(i - 1)];
					}
					recentWords[0] = RecentWordEntry{curWordHash, firstByte, 1};
				}
				uint16_t slot = static_cast<uint16_t>(curWordHash ^ (curWordHash >> 16));
				wordRecency[slot] = static_cast<uint16_t>(std::min(255, (int)wordRecency[slot] + 32));
				prev2WordHash = prevWordHash;
				prevWordHash  = curWordHash;
			}
			if (b == '.' || b == '!' || b == '?') {
				bool afterDigit = (std::isdigit(p0) != 0);
				if (b == '.' && (afterDigit || isAbbreviation())) {
					punctState = PUNCT_CLAUSE;
				} else {
					punctState = PUNCT_SENTENCE;
				}
			} else if (b == ',' || b == ';' || b == ':') {
				punctState = PUNCT_CLAUSE;
			} else if (b == '(' || b == ')' || b == '[' || b == ']' || b == '"') {
				if (b == '"') inProseQuote = !inProseQuote;
				punctState = PUNCT_WRAPPER;
			} else if (b != ' ' && b != '\n' && b != '\t') {
				punctState = PUNCT_NONE;
			}
			if (curWordLen > 0) {
				prevWordPunct = punctState;
				shapeMask = (shapeMask << 2) | static_cast<uint32_t>(punctState);
			}
			curWordHash      = 14695981039346656037ULL;
			subWordHash      = 14695981039346656037ULL;
			stemHash         = 14695981039346656037ULL;
			prefixClass      = 0;
			prefixLen        = 0;
			suffixState      = 0;
			last3Lower       = 0;
			curWordLen       = 0;
			wordStartedLower = false;
			wordHasUpper     = false;
			consRun          = 0;
			vowelRun         = 0;
			syllableCnt      = 0;
			lastCons1        = 0;
			lastCons2        = 0;
			postApostrophe   = false;
			postHyphen       = false;
		}
		if ((lineCol & 31u) == 0 && b == ' ') {
			for (size_t i = 0; i < 65536; i += 16) {
				wordRecency[i] >>= 1;
			}
		}
		shiftHistory(b);
	}
	uint64_t computeXmlWikiFSMContext(uint8_t bitIdx, uint8_t cPart) const noexcept {
		uint64_t topTag = tagStack.empty() ? 0ULL : fnv1a_step(curTagHash, static_cast<uint8_t>(tagStack.size()));
		uint32_t flags = (static_cast<uint32_t>(inXmlTag)      << 0)  |
		                 (static_cast<uint32_t>(inXmlClosing)  << 1)  |
		                 (static_cast<uint32_t>(inXmlAttr)     << 2)  |
		                 (static_cast<uint32_t>(inXmlComment)  << 3)  |
		                 (static_cast<uint32_t>(inEntity)      << 4)  |
		                 (static_cast<uint32_t>(inProseQuote)  << 5)  |
		                 (static_cast<uint32_t>(wikiItalic)    << 6)  |
		                 (static_cast<uint32_t>(wikiBold)      << 7)  |
		                 (static_cast<uint32_t>(extLinkOpen)   << 8)  |
		                 (static_cast<uint32_t>(wikiLinkDepth) << 9)  |
		                 (static_cast<uint32_t>(wikiTplDepth)  << 12) |
		                 (static_cast<uint32_t>(parenDepth)    << 15) |
		                 (static_cast<uint32_t>(punctState)    << 17) |
		                 (static_cast<uint32_t>(utf8LeadClass) << 19);
		uint64_t h = topTag ^ (static_cast<uint64_t>(flags) * 0x9E3779B97F4A7C15ULL);
		h = fnv1a_step(h, static_cast<uint8_t>(std::min<uint32_t>(lineCol, 255)));
		h = fnv1a_step(h, p0);
		h = fnv1a_step(h, static_cast<uint8_t>((bitIdx << 4) ^ cPart));
		return h;
	}
	uint32_t computeMixerContextIdx(StructBucket bucket, uint8_t bitIdx) const noexcept {
		uint32_t parityBits = (static_cast<uint32_t>(wikiBold)     << 0) |
		                      (static_cast<uint32_t>(wikiItalic)   << 1) |
		                      (static_cast<uint32_t>(inProseQuote) << 2) |
		                      (static_cast<uint32_t>(inEntity)     << 3);
		uint32_t idx = (static_cast<uint32_t>(bucket)        << 10) |
		               (static_cast<uint32_t>(punctState)    << 8)  |
		               (parityBits                           << 4)  |
		               (static_cast<uint32_t>(bitIdx & 0x07) << 1)  |
		               (static_cast<uint32_t>(postApostrophe || postHyphen));
		return idx & 0x0FFF;
	}
	float adaptiveMixerLR(StructBucket bucket) const noexcept {
		if (inXmlTag || inXmlComment) return 0.010f;
		if (bucket == CTX_WORD_START || punctState == PUNCT_SENTENCE) return 0.024f;
		return 0.015f;
	}
private:
	void shiftHistory(uint8_t b) noexcept {
		for (int i = 15; i > 0; i--) prev[static_cast<size_t>(i)] = prev[static_cast<size_t>(i - 1)];
		prev[0] = b;
		p3 = p2;
		p2 = p1;
		p1 = p0;
		p0 = b;
	}
};