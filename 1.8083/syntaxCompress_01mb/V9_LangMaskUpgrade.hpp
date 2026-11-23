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

enum PosTag : uint8_t {
	POS_NONE     = 0,
	POS_DET      = 1,
	POS_PREP     = 2,
	POS_AUX_SG   = 3,
	POS_AUX_PL   = 4,
	POS_CONJ     = 5,
	POS_PRON     = 6,
	POS_NOUN_SG  = 7,
	POS_NOUN_PL  = 8,
	POS_VERB_ING = 9,
	POS_VERB_ED  = 10,
	POS_ADV      = 11,
	POS_NUM      = 12,
	POS_PROPN    = 13
};

enum XmlFieldType : uint8_t {
	XML_FIELD_NONE      = 0,
	XML_FIELD_TIMESTAMP = 1, // <timestamp>YYYY-MM-DDTHH:MM:SSZ</timestamp>
	XML_FIELD_NUM_ID    = 2, // <id>, <ns>, <minor>
	XML_FIELD_IP        = 3, // <ip>
	XML_FIELD_TITLE     = 4, // <title>
	XML_FIELD_TEXT      = 5  // <text>
};

enum DiscourseZone : uint8_t {
	DISC_PROSE       = 0,
	DISC_LEAD_DEF    = 1, // Opening definition paragraph before first == heading
	DISC_HEADING     = 2, // == Section Heading ==
	DISC_LIST_ITEM   = 3, // *, #, :, ; list lines
	DISC_INFOBOX_TBL = 4, // {{Infobox / {{cite / {| table
	DISC_FOOTER_CAT  = 5  // [[Category:...]] / interwiki links
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

	// --- LAYER 1: UTF-8 & SCRIPT GRAMMAR ---
	uint8_t utf8Remaining    = 0;
	uint8_t utf8LeadClass    = 0;
	uint8_t utf8LastLeadByte = 0;
	uint8_t utf8ScriptBlock  = 0;
	uint8_t wordScriptLock   = 0;
	uint8_t scriptCharRun    = 0;

	// --- LAYER 2: PHONETICS & IPA GRAMMAR ---
	uint8_t prevArticleType  = 0;
	uint8_t syllableRole     = 0;
	bool    doubleConsFlag   = false;
	bool    inIpaTemplate    = false;

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
	char curWordBuf[32] = {0};
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

	// --- LAYER 3: STENOGRAPHY & SUBWORD COMPRESSION GRAMMAR ---
	uint64_t consSkelHash   = 14695981039346656037ULL;
	uint8_t consSkelLen     = 0;
	uint8_t lastSkelCons    = 0;

	uint64_t curSyllHash    = 14695981039346656037ULL;
	uint64_t prevSyllHash   = 0;
	uint8_t syllCharLen     = 0;
	bool syllHasVowel       = false;

	struct SubwordBriefSlot {
		uint32_t tag      = 0;
		uint8_t nextChar  = 0;
		uint8_t count     = 0;
	};
	std::array<SubwordBriefSlot, 256> briefCache{};

	// --- LAYER 4: WORD SYNTAX & PHRASE VALENCY GRAMMAR ---
	uint16_t posHist        = 0;
	uint8_t lastPos         = POS_NONE;
	bool inNounPhrase       = false;
	bool subjectIsPlural    = false;
	uint8_t prepFrameType   = 0;
	bool inCiteTemplate     = false;
	bool inKeyValueSlot     = false;

	// --- LAYER 5: SEMANTIC DISCOURSE, FIELD & URL/HTML GRAMMAR ---
	XmlFieldType activeXmlField  = XML_FIELD_NONE;
	uint8_t xmlFieldBytePos      = 0;
	DiscourseZone discourseZone  = DISC_PROSE;
	bool seenFirstHeading        = false;
	bool inUrl                   = false;
	uint8_t urlSegment           = 0; // 0=none, 1=scheme, 2=host, 3=path, 4=query
	bool inWikiTable             = false;
	uint64_t articleTitleHash    = 0;
	uint64_t sectionHash         = 0;
	std::array<RecentWordEntry, 32> articleKeywords{}; // Persistent per-article top-32 entities/keywords

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

		utf8Remaining    = 0;
		utf8LeadClass    = 0;
		utf8LastLeadByte = 0;
		utf8ScriptBlock  = 0;
		wordScriptLock   = 0;
		scriptCharRun    = 0;

		prevArticleType  = 0;
		syllableRole     = 0;
		doubleConsFlag   = false;
		inIpaTemplate    = false;

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
		consSkelHash = 14695981039346656037ULL;
		consSkelLen = 0;
		lastSkelCons = 0;
		curSyllHash = 14695981039346656037ULL;
		prevSyllHash = 0;
		syllCharLen = 0;
		syllHasVowel = false;
		briefCache.fill(SubwordBriefSlot{0, 0, 0});

		posHist         = 0;
		lastPos         = POS_NONE;
		inNounPhrase    = false;
		subjectIsPlural = false;
		prepFrameType   = 0;
		inCiteTemplate  = false;
		inKeyValueSlot  = false;

		activeXmlField   = XML_FIELD_NONE;
		xmlFieldBytePos  = 0;
		discourseZone    = DISC_PROSE;
		seenFirstHeading = false;
		inUrl            = false;
		urlSegment       = 0;
		inWikiTable      = false;
		articleTitleHash = 0;
		sectionHash      = 0;
		articleKeywords.fill(RecentWordEntry{0, 0, 0});

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
		if (wikiLinkDepth > 0 || wikiTplDepth > 0 || extLinkOpen || inEntity || wikiBold || wikiItalic || inUrl || inWikiTable) {
			return CTX_WIKI_MARKUP;
		}
		if (curWordLen > 0) return CTX_MID_WORD;
		return CTX_WORD_START;
	}

	// Blends local 32-word LRU cache + persistent per-article top-32 entity/keyword register
	uint16_t predictWordStartFromCache(uint8_t bitIdx, uint8_t cPart) const noexcept {
		int shift = 7 - bitIdx;
		uint8_t prefixMask = static_cast<uint8_t>(cPart - (1u << bitIdx));
		int w0 = 0, w1 = 0;
		for (int i = 0; i < 32; i++) {
			const auto& entry = recentWords[static_cast<size_t>(i)];
			if (entry.count > 0) {
				uint8_t fb = entry.firstByte;
				if ((fb >> (shift + 1)) == prefixMask) {
					int b = (fb >> shift) & 1;
					int weight = (32 - i) * int(entry.count);
					if (b == 1) w1 += weight;
					else w0 += weight;
				}
			}
			const auto& kw = articleKeywords[static_cast<size_t>(i)];
			if (kw.count > 0) {
				uint8_t fb = kw.firstByte;
				if ((fb >> (shift + 1)) == prefixMask) {
					int b = (fb >> shift) & 1;
					int weight = 12 * int(kw.count);
					if (b == 1) w1 += weight;
					else w0 += weight;
				}
			}
		}
		int tot = w0 + w1;
		if (tot == 0) return 2048;
		int p = 2048 + ((w1 - w0) * 1024) / (tot + 64);
		return static_cast<uint16_t>(std::clamp(p, 512, 3584));
	}

	uint16_t applyBitLevelLanguageMask(uint16_t prob12, uint8_t bitIdx, uint8_t cPart) const noexcept {
		if (utf8Remaining > 0) {
			if (bitIdx == 0) return std::max<uint16_t>(prob12, 4088);
			if (bitIdx == 1) return std::min<uint16_t>(prob12, 8);
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

		// Layer 5: ISO-8601 <timestamp> (YYYY-MM-DDTHH:MM:SSZ) & Numeric <id> Field Grammar
		if (!inXmlTag && activeXmlField == XML_FIELD_TIMESTAMP && xmlFieldBytePos < 20) {
			uint8_t expectedFixed = 0;
			if (xmlFieldBytePos == 4 || xmlFieldBytePos == 7) expectedFixed = '-';
			else if (xmlFieldBytePos == 10) expectedFixed = 'T';
			else if (xmlFieldBytePos == 13 || xmlFieldBytePos == 16) expectedFixed = ':';
			else if (xmlFieldBytePos == 19) expectedFixed = 'Z';

			if (expectedFixed != 0) {
				uint8_t mask = (bitIdx == 0) ? 0 : static_cast<uint8_t>(0xFF << (8 - bitIdx));
				if ((cPart & mask) == (expectedFixed & mask)) {
					uint8_t targetBit = (expectedFixed >> (7 - bitIdx)) & 1;
					return targetBit ? std::max<uint16_t>(prob12, 3950)
					       : std::min<uint16_t>(prob12, 145);
				}
			} else {
				// Digit position '0'..'9' (0x30..0x39 -> top 4 bits = 0011)
				if (bitIdx == 0) return std::min<uint16_t>(prob12, 16);
				if (bitIdx == 1) return std::min<uint16_t>(prob12, 16);
				if (bitIdx == 2) return std::max<uint16_t>(prob12, 4060);
				if (bitIdx == 3) return std::max<uint16_t>(prob12, 4060);
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
	void updateStenoAndMorphState(uint8_t lc) noexcept {
		last3Lower = ((last3Lower << 8) | lc) & 0x00FFFFFFu;

		bool isV = is_vowel_byte(lc);
		if (syllHasVowel && !isV && syllCharLen >= 2) {
			uint32_t bSlot = static_cast<uint32_t>(curSyllHash & 0xFFu);
			uint32_t bTag  = static_cast<uint32_t>(curSyllHash >> 32) | 1u;
			if (briefCache[bSlot].tag == bTag && briefCache[bSlot].nextChar == lc) {
				if (briefCache[bSlot].count < 255) briefCache[bSlot].count++;
			} else if (briefCache[bSlot].count <= 1) {
				briefCache[bSlot] = SubwordBriefSlot{bTag, lc, 1};
			} else {
				briefCache[bSlot].count--;
			}

			prevSyllHash = curSyllHash;
			curSyllHash  = 14695981039346656037ULL;
			syllCharLen  = 0;
			syllHasVowel = false;
		}
		curSyllHash = fnv1a_step(curSyllHash, lc);
		syllCharLen++;
		if (isV) syllHasVowel = true;

		if (!isV && std::isalpha(lc)) {
			if (lc != lastSkelCons) {
				consSkelHash = fnv1a_step(consSkelHash, lc);
				if (consSkelLen < 15) consSkelLen++;
				lastSkelCons = lc;
			}
		}

		if (prefixClass == 0 && curWordLen >= 2 && curWordLen <= 6) {
			uint8_t matchedPrefix = 0;
			if (curWordLen == 2) {
				if (std::memcmp(curWordBuf, "un", 2) == 0) matchedPrefix = 1;
				else if (std::memcmp(curWordBuf, "re", 2) == 0) matchedPrefix = 2;
				else if (std::memcmp(curWordBuf, "in", 2) == 0) matchedPrefix = 3;
				else if (std::memcmp(curWordBuf, "im", 2) == 0) matchedPrefix = 4;
				else if (std::memcmp(curWordBuf, "en", 2) == 0) matchedPrefix = 5;
				else if (std::memcmp(curWordBuf, "de", 2) == 0) matchedPrefix = 6;
				else if (std::memcmp(curWordBuf, "ex", 2) == 0) matchedPrefix = 7;
				else if (std::memcmp(curWordBuf, "co", 2) == 0) matchedPrefix = 8;
				else if (std::memcmp(curWordBuf, "bi", 2) == 0) matchedPrefix = 9;
			} else if (curWordLen == 3) {
				if (std::memcmp(curWordBuf, "dis", 3) == 0) matchedPrefix = 10;
				else if (std::memcmp(curWordBuf, "non", 3) == 0) matchedPrefix = 11;
				else if (std::memcmp(curWordBuf, "mis", 3) == 0) matchedPrefix = 12;
				else if (std::memcmp(curWordBuf, "sub", 3) == 0) matchedPrefix = 13;
				else if (std::memcmp(curWordBuf, "pre", 3) == 0) matchedPrefix = 14;
				else if (std::memcmp(curWordBuf, "pro", 3) == 0) matchedPrefix = 15;
				else if (std::memcmp(curWordBuf, "con", 3) == 0) matchedPrefix = 16;
				else if (std::memcmp(curWordBuf, "com", 3) == 0) matchedPrefix = 17;
				else if (std::memcmp(curWordBuf, "per", 3) == 0) matchedPrefix = 18;
				else if (std::memcmp(curWordBuf, "out", 3) == 0) matchedPrefix = 19;
			} else if (curWordLen == 4) {
				if (std::memcmp(curWordBuf, "over", 4) == 0) matchedPrefix = 20;
				else if (std::memcmp(curWordBuf, "anti", 4) == 0) matchedPrefix = 21;
				else if (std::memcmp(curWordBuf, "auto", 4) == 0) matchedPrefix = 22;
				else if (std::memcmp(curWordBuf, "poly", 4) == 0) matchedPrefix = 23;
				else if (std::memcmp(curWordBuf, "mono", 4) == 0) matchedPrefix = 24;
				else if (std::memcmp(curWordBuf, "semi", 4) == 0) matchedPrefix = 25;
				else if (std::memcmp(curWordBuf, "post", 4) == 0) matchedPrefix = 26;
			} else if (curWordLen == 5) {
				if (std::memcmp(curWordBuf, "inter", 5) == 0) matchedPrefix = 27;
				else if (std::memcmp(curWordBuf, "trans", 5) == 0) matchedPrefix = 28;
				else if (std::memcmp(curWordBuf, "under", 5) == 0) matchedPrefix = 29;
				else if (std::memcmp(curWordBuf, "super", 5) == 0) matchedPrefix = 30;
				else if (std::memcmp(curWordBuf, "micro", 5) == 0) matchedPrefix = 31;
				else if (std::memcmp(curWordBuf, "macro", 5) == 0) matchedPrefix = 32;
				else if (std::memcmp(curWordBuf, "multi", 5) == 0) matchedPrefix = 33;
			} else if (curWordLen == 6) {
				if (std::memcmp(curWordBuf, "contra", 6) == 0) matchedPrefix = 34;
				else if (std::memcmp(curWordBuf, "circum", 6) == 0) matchedPrefix = 35;
				else if (std::memcmp(curWordBuf, "pseudo", 6) == 0) matchedPrefix = 36;
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
			else if (t3 == (('a' << 16) | ('n' << 8) | 'c')) suffixState = 15;
			else if (t3 == (('e' << 16) | ('n' << 8) | 'c')) suffixState = 16;
			else if (t3 == (('i' << 16) | ('z' << 8) | 'e')) suffixState = 17;
			else if (t3 == (('i' << 16) | ('s' << 8) | 'e')) suffixState = 18;
			else if (t3 == (('a' << 16) | ('t' << 8) | 'e')) suffixState = 19;
			else if (t3 == (('i' << 16) | ('f' << 8) | 'y')) suffixState = 20;
			else if (t3 == (('f' << 16) | ('u' << 8) | 'l')) suffixState = 21;
			else if (t3 == (('l' << 16) | ('e' << 8) | 's')) suffixState = 22;
			else if (t3 == (('a' << 16) | ('r' << 8) | 'y')) suffixState = 23;
			else if (t3 == (('o' << 16) | ('r' << 8) | 'y')) suffixState = 24;
			else if (t3 == (('i' << 16) | ('a' << 8) | 'n')) suffixState = 25;
			else if (t3 == (('s' << 16) | ('h' << 8) | 'i')) suffixState = 26;
			else if (t3 == (('h' << 16) | ('o' << 8) | 'o')) suffixState = 27;
			else if (t2 == (('e' << 8) | 'd')) suffixState = 28;
			else if (t2 == (('l' << 8) | 'y')) suffixState = 29;
			else if (t2 == (('e' << 8) | 'r')) suffixState = 30;
			else if (t2 == (('a' << 8) | 'l')) suffixState = 31;
			else if (t2 == (('i' << 8) | 'c')) suffixState = 32;
		}
	}

	PosTag classifyWordPOS() noexcept {
		if (curWordLen == 0) return POS_NONE;
		if (curWordLen == 1) {
			if (*curWordBuf == 'a') {
				prepFrameType = 0;
				return POS_DET;
			}
			if (*curWordBuf == 'i') {
				subjectIsPlural = false;
				return POS_PRON;
			}
		} else if (curWordLen == 2) {
			if (std::memcmp(curWordBuf, "an", 2) == 0) {
				prepFrameType = 0;
				return POS_DET;
			}
			if (std::memcmp(curWordBuf, "of", 2) == 0) {
				prepFrameType = 2;
				return POS_PREP;
			}
			if (std::memcmp(curWordBuf, "to", 2) == 0) {
				prepFrameType = 1;
				return POS_PREP;
			}
			if (std::memcmp(curWordBuf, "in", 2) == 0 || std::memcmp(curWordBuf, "on", 2) == 0 ||
			        std::memcmp(curWordBuf, "at", 2) == 0) {
				prepFrameType = 3;
				return POS_PREP;
			}
			if (std::memcmp(curWordBuf, "by", 2) == 0 || std::memcmp(curWordBuf, "as", 2) == 0) {
				prepFrameType = 4;
				return POS_PREP;
			}
			if (std::memcmp(curWordBuf, "is", 2) == 0) return POS_AUX_SG;
			if (std::memcmp(curWordBuf, "or", 2) == 0) return POS_CONJ;
			if (std::memcmp(curWordBuf, "he", 2) == 0 || std::memcmp(curWordBuf, "it", 2) == 0) {
				subjectIsPlural = false;
				return POS_PRON;
			}
			if (std::memcmp(curWordBuf, "we", 2) == 0) {
				subjectIsPlural = true;
				return POS_PRON;
			}
			if (std::memcmp(curWordBuf, "do", 2) == 0) return POS_AUX_PL;
		} else if (curWordLen == 3) {
			if (std::memcmp(curWordBuf, "the", 3) == 0) {
				prepFrameType = 0;
				return POS_DET;
			}
			if (std::memcmp(curWordBuf, "and", 3) == 0 || std::memcmp(curWordBuf, "but", 3) == 0 ||
			        std::memcmp(curWordBuf, "nor", 3) == 0 || std::memcmp(curWordBuf, "yet", 3) == 0) {
				return POS_CONJ;
			}
			if (std::memcmp(curWordBuf, "for", 3) == 0) {
				prepFrameType = 4;
				return POS_PREP;
			}
			if (std::memcmp(curWordBuf, "was", 3) == 0 || std::memcmp(curWordBuf, "has", 3) == 0) {
				return POS_AUX_SG;
			}
			if (std::memcmp(curWordBuf, "are", 3) == 0) return POS_AUX_PL;
			if (std::memcmp(curWordBuf, "she", 3) == 0 || std::memcmp(curWordBuf, "who", 3) == 0) {
				subjectIsPlural = false;
				return POS_PRON;
			}
			if (std::memcmp(curWordBuf, "you", 3) == 0) {
				subjectIsPlural = true;
				return POS_PRON;
			}
		} else if (curWordLen == 4) {
			if (std::memcmp(curWordBuf, "this", 4) == 0 || std::memcmp(curWordBuf, "that", 4) == 0) {
				subjectIsPlural = false;
				return POS_DET;
			}
			if (std::memcmp(curWordBuf, "with", 4) == 0 || std::memcmp(curWordBuf, "from", 4) == 0 ||
			        std::memcmp(curWordBuf, "into", 4) == 0 || std::memcmp(curWordBuf, "over", 4) == 0) {
				prepFrameType = 4;
				return POS_PREP;
			}
			if (std::memcmp(curWordBuf, "were", 4) == 0 || std::memcmp(curWordBuf, "have", 4) == 0) {
				return POS_AUX_PL;
			}
			if (std::memcmp(curWordBuf, "does", 4) == 0) return POS_AUX_SG;
			if (std::memcmp(curWordBuf, "they", 4) == 0) {
				subjectIsPlural = true;
				return POS_PRON;
			}
		} else if (curWordLen == 5) {
			if (std::memcmp(curWordBuf, "these", 5) == 0 || std::memcmp(curWordBuf, "those", 5) == 0) {
				subjectIsPlural = true;
				return POS_DET;
			}
			if (std::memcmp(curWordBuf, "which", 5) == 0) return POS_PRON;
			if (std::memcmp(curWordBuf, "under", 5) == 0 || std::memcmp(curWordBuf, "after", 5) == 0) {
				prepFrameType = 4;
				return POS_PREP;
			}
		}

		if (suffixState == 4) return POS_VERB_ING;
		if (suffixState == 28) return POS_VERB_ED;
		if (suffixState == 29) return POS_ADV;

		if (!wordStartedLower && prevWordPunct != PUNCT_SENTENCE) {
			subjectIsPlural = false;
			return POS_PROPN;
		}
		if (curWordLen >= 4 && curWordBuf[curWordLen - 1] == 's' &&
		        curWordBuf[curWordLen - 2] != 's' && curWordBuf[curWordLen - 2] != 'u' &&
		        curWordBuf[curWordLen - 2] != 'i') {
			subjectIsPlural = true;
			return POS_NOUN_PL;
		}
		subjectIsPlural = false;
		return POS_NOUN_SG;
	}
	uint16_t predictSubwordBriefBit(uint8_t bitIdx, uint8_t cPart) const noexcept {
		if (curWordLen < 2) return 2048;
		uint32_t bSlot = static_cast<uint32_t>(curSyllHash & 0xFFu);
		uint32_t bTag  = static_cast<uint32_t>(curSyllHash >> 32) | 1u;
		const auto& slot = briefCache[bSlot];
		if (slot.tag != bTag || slot.count < 2) return 2048;
		int shift = 7 - bitIdx;
		uint8_t prefixMask = static_cast<uint8_t>(cPart - (1u << bitIdx));
		if ((slot.nextChar >> (shift + 1)) != prefixMask) return 2048;
		int targetBit = (slot.nextChar >> shift) & 1;
		int conf = std::min(1400, 350 + int(slot.count) * 65);
		int p = targetBit ? (2048 + conf) : (2048 - conf);
		return static_cast<uint16_t>(std::clamp(p, 384, 3712));
	}
	uint64_t computeStenoSubwordContext(uint8_t bitIdx, uint8_t cPart) const noexcept {
		uint32_t skel16 = static_cast<uint32_t>(consSkelHash ^ (consSkelHash >> 32)) & 0xFFFFu;
		uint32_t syll16 = static_cast<uint32_t>(curSyllHash  ^ (curSyllHash  >> 32)) & 0xFFFFu;
		if (curWordLen <= 3) {
			return (static_cast<uint64_t>(syll16)      << 24) ^
			       (static_cast<uint64_t>(prefixClass) << 16) ^
			       (static_cast<uint64_t>(curWordLen)  << 12) ^
			       (static_cast<uint64_t>(bitIdx)      << 8)  ^
			       static_cast<uint64_t>(cPart);
		}
		return (static_cast<uint64_t>(skel16)      << 24) ^
		       (static_cast<uint64_t>(suffixState) << 16) ^
		       (static_cast<uint64_t>(prefixClass) << 10) ^
		       (static_cast<uint64_t>(bitIdx)      << 8)  ^
		       static_cast<uint64_t>(cPart);
	}
	uint64_t computePhonoCVContext(uint8_t cPart) const noexcept {
		uint8_t artCtx = (curWordLen == 0) ? prevArticleType : 0;
		uint8_t scriptCtx = (wordScriptLock >= 3) ? wordScriptLock : 0;
		uint64_t skel = (static_cast<uint64_t>(cvMask & 0x0FFFu)       << 36) |
		                (static_cast<uint64_t>(scriptCtx & 0x0Fu)      << 32) |
		                (static_cast<uint64_t>(syllableRole & 0x03u)   << 30) |
		                (static_cast<uint64_t>(artCtx & 0x03u)         << 28) |
		                (static_cast<uint64_t>(inIpaTemplate ? 1 : 0)  << 27) |
		                (static_cast<uint64_t>(doubleConsFlag ? 1 : 0) << 26) |
		                (static_cast<uint64_t>(consRun & 0x07u)        << 23) |
		                (static_cast<uint64_t>(vowelRun & 0x07u)       << 20) |
		                (static_cast<uint64_t>(lastCons1)              << 12) |
		                (static_cast<uint64_t>(phono_phase())          << 8)  |
		                static_cast<uint64_t>(cPart);
		return skel;
	}
	uint64_t computeWordSyntaxContext(uint8_t cPart) const noexcept {
		uint32_t synFlags = (static_cast<uint32_t>(posHist)                 << 16) |
		                    (static_cast<uint32_t>(prepFrameType & 0x07u)   << 13) |
		                    (static_cast<uint32_t>(prevArticleType & 0x03u) << 11) |
		                    (static_cast<uint32_t>(inNounPhrase ? 1 : 0)    << 10) |
		                    (static_cast<uint32_t>(subjectIsPlural ? 1 : 0) << 9)  |
		                    (static_cast<uint32_t>(inCiteTemplate ? 1 : 0)  << 8)  |
		                    (static_cast<uint32_t>(inKeyValueSlot ? 1 : 0)  << 7)  |
		                    (static_cast<uint32_t>(parenDepth & 0x03u)      << 5)  |
		                    (static_cast<uint32_t>(punctState & 0x03u)      << 3)  |
		                    (static_cast<uint32_t>(curWordLen == 0 ? 1 : 0) << 2);
		uint64_t h = (static_cast<uint64_t>(synFlags) << 24) ^
		             (static_cast<uint64_t>(prefixClass) << 16) ^
		             (static_cast<uint64_t>(suffixState) << 10) ^
		             static_cast<uint64_t>(cPart);
		return h;
	}
	void updateByte(uint8_t b) {
		uint16_t ctx2 = static_cast<uint16_t>((static_cast<uint16_t>(p1) << 8) | p0);
		indirect2[ctx2] = indirect1[ctx2];
		indirect1[ctx2] = b;

		// Layer 1: UTF-8 & Unicode Script-Block Grammar
		if (utf8Remaining > 0) {
			if ((b & 0xC0u) == 0x80u) {
				if (utf8Remaining == utf8LeadClass - 1) {
					uint8_t block = 13;
					if (utf8LeadClass == 2) {
						if (utf8LastLeadByte <= 0xC9u) block = 1;
						else if (utf8LastLeadByte <= 0xCBu) block = 2;
						else if (utf8LastLeadByte <= 0xCFu) block = 3;
						else if (utf8LastLeadByte <= 0xD4u) block = 4;
						else if (utf8LastLeadByte <= 0xD7u) block = 5;
						else if (utf8LastLeadByte <= 0xDBu) block = 6;
						else block = 13;
					} else if (utf8LeadClass == 3) {
						if (utf8LastLeadByte == 0xE0u) block = 7;
						else if (utf8LastLeadByte == 0xE1u) block = 13;
						else if (utf8LastLeadByte == 0xE2u) block = 12;
						else if (utf8LastLeadByte == 0xE3u) {
							block = (b <= 0x80u) ? 8 : 9;
						} else if (utf8LastLeadByte >= 0xE4u && utf8LastLeadByte <= 0xE9u) {
							block = 10;
						} else if (utf8LastLeadByte >= 0xEAu && utf8LastLeadByte <= 0xEDu) {
							block = 11;
						}
					} else if (utf8LeadClass == 4) {
						block = 14;
					}
					utf8ScriptBlock = block;
					if (wordScriptLock == 0) {
						wordScriptLock = block;
						scriptCharRun = 1;
					} else if (wordScriptLock == block) {
						if (scriptCharRun < 255) scriptCharRun++;
					} else {
						wordScriptLock = block;
						scriptCharRun = 1;
					}
				}
				utf8Remaining--;
			} else {
				utf8Remaining = 0;
				utf8LeadClass = 0;
				utf8ScriptBlock = 0;
			}
		} else if (b >= 0xC2u && b <= 0xF4u) {
			utf8LeadClass = (b < 0xE0u) ? 2 : (b < 0xF0u) ? 3 : 4;
			utf8Remaining = utf8LeadClass - 1;
			utf8LastLeadByte = b;
		} else {
			utf8LeadClass = 0;
			utf8ScriptBlock = 0;
			if (b <= 0x20u || b == '[' || b == ']' || b == '|' || b == ':' || b == '<' || b == '>') {
				wordScriptLock = 0;
				scriptCharRun = 0;
			}
		}

		// Layer 5: Track active XML field byte position & URL state
		if (!inXmlTag && activeXmlField != XML_FIELD_NONE) {
			if (b == '<') {
				activeXmlField = XML_FIELD_NONE;
				xmlFieldBytePos = 0;
			} else {
				if (activeXmlField == XML_FIELD_TITLE) {
					articleTitleHash = fnv1a_step(articleTitleHash, b);
				}
				if (xmlFieldBytePos < 255) xmlFieldBytePos++;
			}
		}

		if (inUrl) {
			if (b <= 0x20u || b == ']' || b == '|' || b == '<' || b == '>' || b == '"') {
				inUrl = false;
				urlSegment = 0;
			} else if (b == '/' && urlSegment == 2 && p0 != '/') {
				urlSegment = 3; // path
			} else if (b == '?' && urlSegment >= 2) {
				urlSegment = 4; // query
			}
		} else if (p2 == ':' && p1 == '/' && b == '/') {
			inUrl = true;
			urlSegment = 2; // host
		}

				if (b == '\n') {
			if (p0 == '\n') {
				inWikiTable = false;
				extLinkOpen = false;
			}
			lineCol = 0;
			wikiItalic = false;
			wikiBold = false;
			aposRun = 0;
			inKeyValueSlot = false;
			if (discourseZone == DISC_HEADING || discourseZone == DISC_LIST_ITEM) {
				discourseZone = seenFirstHeading ? DISC_PROSE : DISC_LEAD_DEF;
			}
		} else {
			if (lineCol == 0 && !inXmlTag) {
				if (b == '=') {
					discourseZone = DISC_HEADING;
					seenFirstHeading = true;
					sectionHash = 14695981039346656037ULL;
				} else if (b == '*' || b == '#' || b == ':' || b == ';') {
					discourseZone = DISC_LIST_ITEM;
				}
			}
			if (lineCol == 1 && !inXmlTag) {
				if (p0 == '{' && b == '|') {
					inWikiTable = true;
					discourseZone = DISC_INFOBOX_TBL;
				} else if (p0 == '|' && b == '}') {
					inWikiTable = false;
					discourseZone = seenFirstHeading ? DISC_PROSE : DISC_LEAD_DEF;
				}
			}

			if (discourseZone == DISC_HEADING) {
				sectionHash = fnv1a_step(sectionHash, b);
			}
			lineCol++;
			if (b == 0x27 && !inXmlTag) {
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
						// Layer 5: Classify XML field & reset per-article register on <page>
						if (curTagName == "page") {
							articleKeywords.fill(RecentWordEntry{0, 0, 0});
							articleTitleHash = 14695981039346656037ULL;
							sectionHash = 0;
							seenFirstHeading = false;
							discourseZone = DISC_PROSE;
						} else if (curTagName == "timestamp") {
							activeXmlField = XML_FIELD_TIMESTAMP;
							xmlFieldBytePos = 0;
						} else if (curTagName == "id" || curTagName == "ns" || curTagName == "minor") {
							activeXmlField = XML_FIELD_NUM_ID;
							xmlFieldBytePos = 0;
						} else if (curTagName == "ip") {
							activeXmlField = XML_FIELD_IP;
							xmlFieldBytePos = 0;
						} else if (curTagName == "title") {
							activeXmlField = XML_FIELD_TITLE;
							xmlFieldBytePos = 0;
							articleTitleHash = 14695981039346656037ULL;
						} else if (curTagName == "text") {
							activeXmlField = XML_FIELD_TEXT;
							xmlFieldBytePos = 0;
							seenFirstHeading = false;
							discourseZone = DISC_LEAD_DEF;
						}
					} else if (inXmlClosing && !tagStack.empty()) {
						tagStack.pop_back();
						activeXmlField = XML_FIELD_NONE;
						xmlFieldBytePos = 0;
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
				if (discourseZone == DISC_FOOTER_CAT && wikiLinkDepth == 0) {
					discourseZone = seenFirstHeading ? DISC_PROSE : DISC_LEAD_DEF;
				}
			} else if (b == ']' && p0 != ']') {
				extLinkOpen = false;
			} else if (b == ':' && wikiLinkDepth > 0 && curWordLen == 8 &&
			           std::memcmp(curWordBuf, "category", 8) == 0) {
				discourseZone = DISC_FOOTER_CAT;
			} else if (b == '{' && p0 == '{') {
				if (wikiTplDepth < 4) wikiTplDepth++;
				inKeyValueSlot = false;
					} else if (b == '|' && wikiTplDepth > 0) {
				inKeyValueSlot = false;
				if (curWordLen == 3 && std::memcmp(curWordBuf, "ipa", 3) == 0) {
					inIpaTemplate = true;
				} else if (curWordLen >= 4 && std::memcmp(curWordBuf, "cite", 4) == 0) {
					inCiteTemplate = true;
					discourseZone = DISC_INFOBOX_TBL;
				} else if (curWordLen >= 7 && std::memcmp(curWordBuf, "infobox", 7) == 0) {
					discourseZone = DISC_INFOBOX_TBL;
				}
			} else if (b == '=' && wikiTplDepth > 0) {
				inKeyValueSlot = true;
			} else if (b == '}' && p0 == '}') {
				if (wikiTplDepth > 0) wikiTplDepth--;
				if (wikiTplDepth == 0) {
					inIpaTemplate = false;
					inCiteTemplate = false;
					inKeyValueSlot = false;
					if (discourseZone == DISC_INFOBOX_TBL && !inWikiTable) {
						discourseZone = seenFirstHeading ? DISC_PROSE : DISC_LEAD_DEF;
					}
				}
			} else if (b == '(') {
				if (parenDepth < 3) parenDepth++;
			} else if (b == ')') {
				if (parenDepth > 0) parenDepth--;
			}
		}

		// Keep exact alphabetic word boundaries so WRD_STR / MID_WRD telemetry stays aligned
		bool isAlpha = (std::isalpha(b) != 0) || (utf8Remaining > 0) || (utf8LeadClass > 0);
		bool isInternalJoiner = ((b == 0x27 || b == '-') && curWordLen > 0 && std::isalpha(p0));

		if (isAlpha && !inEntity) {
			bool isV = is_vowel_byte(lc);
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
				consSkelHash = 14695981039346656037ULL;
				consSkelLen = 0;
				lastSkelCons = 0;
				curSyllHash = 14695981039346656037ULL;
				prevSyllHash = 0;
				syllCharLen = 0;
				syllHasVowel = false;
				syllableRole = isV ? 2 : 1;
				doubleConsFlag = false;
			} else {
				if (std::isupper(b)) wordHasUpper = true;
				if (isV) {
					syllableRole = 2;
					doubleConsFlag = false;
				} else {
					doubleConsFlag = (lastCons1 == lc && syllableRole == 3);
					syllableRole = (syllableRole == 2 || syllableRole == 3) ? 3 : 1;
				}
			}
			curWordHash = fnv1a_step(curWordHash, lc);
			subWordHash = fnv1a_step(subWordHash, lc);
			if (curWordLen < 28) curWordBuf[curWordLen++] = (char)lc;
			updateStenoAndMorphState(lc);
			uint32_t sCode = (utf8LeadClass > 0 || utf8Remaining > 0) ? 3u : (std::isupper(b) ? 1u : 2u);
			shapeMask = (shapeMask << 2) | sCode;
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
			curWordHash    = fnv1a_step(curWordHash, b);
			subWordHash    = 14695981039346656037ULL;
			stemHash       = 14695981039346656037ULL;
			prefixClass    = 0;
			prefixLen      = 0;
			suffixState    = 0;
			consSkelHash   = 14695981039346656037ULL;
			consSkelLen    = 0;
			lastSkelCons   = 0;
			curSyllHash    = 14695981039346656037ULL;
			prevSyllHash   = 0;
			syllCharLen    = 0;
			syllHasVowel   = false;
			syllableRole   = 0;
			doubleConsFlag = false;
			postApostrophe = (b == 0x27);
			postHyphen     = (b == '-');
		}
		else {
			if (curWordLen > 0) {
				curWordBuf[curWordLen] = '\0';
				if (curWordLen == 1 && *curWordBuf == 'a') {
					prevArticleType = 1;
				} else if (curWordLen == 2 && std::memcmp(curWordBuf, "an", 2) == 0) {
					prevArticleType = 2;
				} else {
					prevArticleType = 0;
				}
				PosTag tag = classifyWordPOS();
				lastPos = tag;
				posHist = static_cast<uint16_t>((posHist << 4) | (static_cast<uint16_t>(tag) & 0x0Fu));
				if (tag == POS_DET) {
					inNounPhrase = true;
				} else if (tag == POS_NOUN_SG || tag == POS_NOUN_PL || tag == POS_PROPN ||
				           tag == POS_PREP || tag == POS_CONJ || tag == POS_AUX_SG || tag == POS_AUX_PL) {
					inNounPhrase = false;
				}
				uint8_t firstByte = static_cast<uint8_t>(*curWordBuf);
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
					recentWords.front() = hit;
				} else {
					for (int i = 31; i > 0; i--) {
						recentWords[static_cast<size_t>(i)] = recentWords[static_cast<size_t>(i - 1)];
					}
					recentWords.front() = RecentWordEntry{curWordHash, firstByte, 1};
				}
				// Layer 5: Update persistent per-article top-32 keyword/entity register
				if (curWordLen >= 4 && !inXmlTag) {
					int kwIdx = -1;
					for (int i = 0; i < 32; i++) {
						if (articleKeywords[static_cast<size_t>(i)].count > 0 &&
						        articleKeywords[static_cast<size_t>(i)].hash == curWordHash) {
							kwIdx = i;
							break;
						}
					}
					if (kwIdx >= 0) {
						if (articleKeywords[static_cast<size_t>(kwIdx)].count < 31) {
							articleKeywords[static_cast<size_t>(kwIdx)].count++;
						}
						while (kwIdx > 0 &&
						        articleKeywords[static_cast<size_t>(kwIdx)].count >
						        articleKeywords[static_cast<size_t>(kwIdx - 1)].count) {
							std::swap(articleKeywords[static_cast<size_t>(kwIdx)],
							          articleKeywords[static_cast<size_t>(kwIdx - 1)]);
							kwIdx--;
						}
					} else {
						if (articleKeywords.back().count <= 1) {
							articleKeywords.back() = RecentWordEntry{curWordHash, firstByte, 1};
						} else {
							articleKeywords.back().count--;
						}
					}
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
					prepFrameType = 0;
					inNounPhrase = false;
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
			consSkelHash     = 14695981039346656037ULL;
			consSkelLen      = 0;
			lastSkelCons     = 0;
			curSyllHash      = 14695981039346656037ULL;
			prevSyllHash     = 0;
			syllCharLen      = 0;
			syllHasVowel     = false;
			syllableRole     = 0;
			doubleConsFlag   = false;
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
		uint32_t flags = (static_cast<uint32_t>(inXmlTag)       << 0)  |
		                 (static_cast<uint32_t>(inXmlClosing)   << 1)  |
		                 (static_cast<uint32_t>(inXmlAttr)      << 2)  |
		                 (static_cast<uint32_t>(inXmlComment)   << 3)  |
		                 (static_cast<uint32_t>(inEntity)       << 4)  |
		                 (static_cast<uint32_t>(inProseQuote)   << 5)  |
		                 (static_cast<uint32_t>(wikiItalic)     << 6)  |
		                 (static_cast<uint32_t>(wikiBold)       << 7)  |
		                 (static_cast<uint32_t>(extLinkOpen)    << 8)  |
		                 (static_cast<uint32_t>(wikiLinkDepth)  << 9)  |
		                 (static_cast<uint32_t>(wikiTplDepth)   << 12) |
		                 (static_cast<uint32_t>(parenDepth)     << 15) |
		                 (static_cast<uint32_t>(punctState)     << 17) |
		                 (static_cast<uint32_t>(utf8LeadClass)  << 19) |
		                 (static_cast<uint32_t>(inCiteTemplate) << 22) |
		                 (static_cast<uint32_t>(inKeyValueSlot) << 23) |
		                 (static_cast<uint32_t>(activeXmlField) << 24) |
		                 (static_cast<uint32_t>(discourseZone)  << 27) |
		                 (static_cast<uint32_t>(urlSegment)     << 30);
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
		prev.front() = b;
		p3 = p2;
		p2 = p1;
		p1 = p0;
		p0 = b;
	}
};

				
				