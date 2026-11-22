// Persistent-file CLI for the legacy V9+ bitstream.
// Build this file (not main.cpp directly) to enable --encode/--decode:
//   g++ -O3 -std=c++17 -march=native cli.cpp -o v9_hutter
//
// The included benchmark implementation is compiled with its main renamed, so
// --benchmark continues to use the original benchmark code and bitstream.
#define main v9_legacy_benchmark_main
#include "main.cpp"
#undef main

#include <limits>

namespace {
constexpr uint64_t MAX_FILE_BYTES = 1ULL << 30; // 1 GiB safety limit
constexpr size_t LEGACY_HEADER_BYTES = 8;

bool readFile(const string& path, string& data, string& error) {
	ifstream in(path, ios::binary);
	if (!in) {
		error = "cannot open input file: " + path;
		return false;
	}
	in.seekg(0, ios::end);
	streamoff end = in.tellg();
	if (end < 0) {
		error = "cannot determine input file size: " + path;
		return false;
	}
	if (static_cast<uint64_t>(end) > MAX_FILE_BYTES) {
		error = "input exceeds the 1 GiB safety limit: " + path;
		return false;
	}
	data.resize(static_cast<size_t>(end));
	in.seekg(0, ios::beg);
	if (!data.empty()) {
		in.read(&data[0], static_cast<streamsize>(data.size()));
		if (in.gcount() != static_cast<streamsize>(data.size()) || in.bad()) {
			error = "failed while reading input file: " + path;
			return false;
		}
	}
	return true;
}

bool writeFile(const string& path, const string& data, string& error) {
	ofstream out(path, ios::binary | ios::trunc);
	if (!out) {
		error = "cannot open output file: " + path;
		return false;
	}
	if (!data.empty()) {
		out.write(data.data(), static_cast<streamsize>(data.size()));
	}
	if (!out) {
		error = "failed while writing output file: " + path;
		return false;
	}
	return true;
}

bool encodeFile(const string& inputPath, const string& outputPath, string& error) {
	string source;
	if (!readFile(inputPath, source, error)) return false;

	HutterEngineV9 enc;
	enc.init();
	BitArithmeticCoder coder;
	const uint64_t originalLength = static_cast<uint64_t>(source.size());
	for (int i = 0; i < 8; ++i) {
		coder.out.push_back(static_cast<uint8_t>((originalLength >> (8 * i)) & 0xFFu));
	}

	for (uint8_t byte : source) {
		enc.matcher.prepareByte();
		uint8_t cPart = 1;
		for (int bitIdx = 0; bitIdx < 8; ++bitIdx) {
			int bit = (byte >> (7 - bitIdx)) & 1;
			uint16_t p1 = enc.predictBit(bitIdx, cPart);
			coder.encodeBit(bit, p1);
			enc.updateBit(bit);
			cPart = static_cast<uint8_t>((cPart << 1) | bit);
		}
		enc.finishByte(byte);
	}
	coder.finish();

	string compressed;
	compressed.reserve(coder.out.size());
	for (uint8_t byte : coder.out) compressed.push_back(static_cast<char>(byte));
	if (!writeFile(outputPath, compressed, error)) return false;
	cout << "Encoded " << source.size() << " bytes to " << outputPath
	     << " (" << compressed.size() << " bytes, legacy 8-byte-length format).\n";
	return true;
}

bool decodeFile(const string& inputPath, const string& outputPath, string& error) {
	string compressed;
	if (!readFile(inputPath, compressed, error)) return false;
	if (compressed.size() < LEGACY_HEADER_BYTES + 1) {
		error = "compressed input is too short to contain the legacy header and coder payload";
		return false;
	}

	uint64_t decodedLength = 0;
	for (int i = 0; i < 8; ++i) {
		decodedLength |= static_cast<uint64_t>(static_cast<uint8_t>(compressed[static_cast<size_t>(i)])) << (8 * i);
	}
	if (decodedLength > MAX_FILE_BYTES || decodedLength > static_cast<uint64_t>(string().max_size())) {
		error = "declared output length exceeds the 1 GiB safety limit";
		return false;
	}

	vector<uint8_t> bytes;
	bytes.reserve(compressed.size());
	for (unsigned char byte : compressed) bytes.push_back(byte);

	HutterEngineV9 dec;
	dec.init();
	BitArithmeticDecoder decoder;
	decoder.init(bytes, LEGACY_HEADER_BYTES);
	string decoded;
	decoded.reserve(static_cast<size_t>(decodedLength));
	for (uint64_t i = 0; i < decodedLength; ++i) {
		dec.matcher.prepareByte();
		uint8_t cPart = 1;
		for (int bitIdx = 0; bitIdx < 8; ++bitIdx) {
			uint16_t p1 = dec.predictBit(bitIdx, cPart);
			int bit = decoder.decodeBit(p1);
			dec.updateBit(bit);
			cPart = static_cast<uint8_t>((cPart << 1) | bit);
		}
		dec.finishByte(cPart);
		decoded.push_back(static_cast<char>(cPart));
	}

	if (!writeFile(outputPath, decoded, error)) return false;
	cout << "Decoded " << decoded.size() << " bytes to " << outputPath
	     << " (legacy 8-byte-length format).\n";
	return true;
}

void printUsage(const char* program) {
	cerr << "Usage:\n"
	     << "  " << program << " --encode <input> <output.v9>\n"
	     << "  " << program << " --decode <input.v9> <output>\n"
	     << "  " << program << " --benchmark [legacy benchmark arguments]\n";
}
} // namespace

int main(int argc, char** argv) {
	if (argc < 2 || string(argv[1]) == "--help" || string(argv[1]) == "-h") {
		printUsage(argv[0]);
		return argc < 2 ? 2 : 0;
	}

	string error;
	if (string(argv[1]) == "--encode") {
		if (argc != 4) {
			printUsage(argv[0]);
			return 2;
		}
		if (!encodeFile(argv[2], argv[3], error)) {
			cerr << "Error: " << error << '\n';
			return 1;
		}
		return 0;
	}
	if (string(argv[1]) == "--decode") {
		if (argc != 4) {
			printUsage(argv[0]);
			return 2;
		}
		if (!decodeFile(argv[2], argv[3], error)) {
			cerr << "Error: " << error << '\n';
			return 1;
		}
		return 0;
	}
	if (string(argv[1]) == "--benchmark") {
		vector<char*> forwarded;
		forwarded.reserve(static_cast<size_t>(argc));
		forwarded.push_back(argv[0]);
		for (int i = 2; i < argc; ++i) forwarded.push_back(argv[i]);
		return v9_legacy_benchmark_main(static_cast<int>(forwarded.size()), forwarded.data());
	}

	printUsage(argv[0]);
	cerr << "Error: unknown mode: " << argv[1] << '\n';
	return 2;
}
