#pragma once
#include "block2_context_and_match.hpp"

template <int N_IN>
class LogisticNode {
private:
	size_t numSlots;
	std::vector<std::array<double, N_IN + 1>> weights;
	std::array<double, N_IN + 1> lastInputs{};
	double lastProb = 0.5;
	size_t lastSlot = 0;

public:
	static inline double stretch(double p) {
		p = std::clamp(p, 0.0005, 0.9995);
		return std::log(p / (1.0 - p));
	}

	static inline double squash(double x) {
		x = std::clamp(x, -12.0, 12.0);
		return 1.0 / (1.0 + std::exp(-x));
	}

	LogisticNode(size_t primeSlots) : numSlots(primeSlots), weights(primeSlots) {
		for (size_t s = 0; s < primeSlots; ++s) {
			for (int i = 0; i < N_IN; ++i) weights[s][i] = 1.0 / N_IN;
			weights[s][N_IN] = 0.0;
		}
	}

	double predict(const std::array<double, N_IN>& probs, uint64_t ctx) {
		lastSlot = (size_t)(ctx % numSlots);
		double sum = 0.0;
		for (int i = 0; i < N_IN; ++i) {
			lastInputs[i] = stretch(probs[i]);
			sum += weights[lastSlot][i] * lastInputs[i];
		}
		lastInputs[N_IN] = 1.0;
		sum += weights[lastSlot][N_IN];
		lastProb = squash(sum);
		return lastProb;
	}

	void train(int actualBit, double lr) {
		double error = (double)actualBit - lastProb;
		for (int i = 0; i <= N_IN; ++i) {
			weights[lastSlot][i] += lr * error * lastInputs[i];
			weights[lastSlot][i] = std::clamp(weights[lastSlot][i], -6.0, 10.0);
		}
	}
};

class CascadedLogisticMixer {
private:
	LogisticNode<12> layer1Byte;
	LogisticNode<12> layer1Semantic;
	LogisticNode<12> layer1Match;
	LogisticNode<3>  layer2Meta;

public:
	// Prime slot counts prevent power-of-2 hash collisions in mixer weights
	CascadedLogisticMixer()
		: layer1Byte(4093), layer1Semantic(2039), layer1Match(1021), layer2Meta(1021) {}

	double mix(const std::array<double, 12>& probs, uint64_t ctxByte, uint64_t ctxSem, uint64_t ctxMatch, uint64_t ctxMeta) {
		double pA = layer1Byte.predict(probs, ctxByte);
		double pB = layer1Semantic.predict(probs, ctxSem);
		double pC = layer1Match.predict(probs, ctxMatch);

		std::array<double, 3> metaIn = {pA, pB, pC};
		return layer2Meta.predict(metaIn, ctxMeta);
	}

	void train(int actualBit) {
		layer1Byte.train(actualBit, 0.055);
		layer1Semantic.train(actualBit, 0.060);
		layer1Match.train(actualBit, 0.065);
		layer2Meta.train(actualBit, 0.075);
	}
};

class SSECalibrator {
private:
	static constexpr size_t PROB_BINS = 64;
	static constexpr size_t CTX_BINS = 256;
	std::vector<BitCounter> grid;
	size_t lastIdx = 0;

public:
	SSECalibrator() : grid(PROB_BINS * CTX_BINS) {
		for (size_t c = 0; c < CTX_BINS; ++c) {
			for (size_t b = 0; b < PROB_BINS; ++b) {
				double p = ((double)b + 0.5) / (double)PROB_BINS;
				grid[c * PROB_BINS + b].n1 = (uint16_t)(p * 16.0);
				grid[c * PROB_BINS + b].n0 = (uint16_t)((1.0 - p) * 16.0);
			}
		}
	}

	double calibrate(double rawProb, uint32_t partialByte, int matchLen) {
		size_t pBin = std::min<size_t>(PROB_BINS - 1, (size_t)(std::clamp(rawProb, 0.0, 0.9999) * PROB_BINS));
		size_t cBin = partialByte & (CTX_BINS - 1);
		lastIdx = cBin * PROB_BINS + pBin;

		if (matchLen >= 10) return rawProb;
		double sseProb = grid[lastIdx].prob1();
		return 0.80 * rawProb + 0.20 * sseProb;
	}

	void update(int bit) {
		grid[lastIdx].update(bit, 256);
	}
};
