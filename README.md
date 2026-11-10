# GARCH-Entropy Structural-Linguistic Trader for Brent Crude Oil

A production-grade time-series forecasting framework for Brent crude oil that combines:
- GARCH volatility modeling
- Shannon conditional entropy
- symbolic market discretization
- transition-state analysis
- linguistic pattern recognition

The system transforms raw price action into an algorithmic grammar, converting continuous market behavior into discrete symbolic states for probabilistic forecasting and low-uncertainty signal discovery.

## Overview

This project blends quantitative finance and information theory to model market structure as a probabilistic system rather than a purely statistical regression problem. Instead of relying only on static volatility bands or conventional indicators, the framework encodes price movement, range behavior, and wick structure into tokenized states and evaluates their transition dynamics.

The goal is to uncover recurring market patterns with asymmetric informational value and measurable forecast potential.

## Core Architecture

### 1. Dynamic Normalization with GARCH
Traditional indicators often use fixed rolling windows and static thresholds. In this model, return brackets and volatility bands are normalized dynamically based on conditional heteroskedasticity, allowing the system to adapt to changing market regimes.

### 2. Linguistic Discretization
Raw market features such as close-to-open changes, relative range, and wick structure are converted into symbolic tokens. These tokens compress complex geometric behavior into simplified categories that are easier to analyze and compare across time.

### 3. Markov-like Transition States
Historical sequences of tokenized states are evaluated as transition patterns. This enables the model to estimate how one market state tends to evolve into another, producing a probabilistic view of future price behavior.

### 4. Information Theory and Entropy
Using Shannon-style entropy and conditional information measures, the framework quantifies the uncertainty and signal quality of observed transitions. This supports filtering of low-information states and prioritization of higher-value patterns.

## Why This Matters

This research-driven framework is designed to uncover hidden structure in noisy, nonlinear market sequences by combining:
- volatility-aware statistical modeling
- symbolic market encoding
- transition-state probability estimation
- entropy-based filtering
- pattern-based decision support

The result is a system that looks beyond raw price movement and instead evaluates market behavior as a structured, information-rich signal environment.

## Entropy Toys

The `Entrophy toys` directory contains visual and experimental artifacts from iterative prototype stages of the system. It captures exploratory work, symbolic token experiments, and early-stage model outputs that helped shape the broader framework.

### Visual gallery

![Entropy Toys 01](Entrophy%20toys/01.jpg)

![Entropy Toys 02](Entrophy%20toys/02.jpg)

![Entropy Toys 03](Entrophy%20toys/03.jpg)

![Entropy Toys 04](Entrophy%20toys/04.jpg)

![Entropy Rubik 00](Entrophy%20toys/rub00.jpeg)

![Entropy Rubik 01](Entrophy%20toys/rub01.jpeg)

![Entropy Rubik 02](Entrophy%20toys/rub02.jpeg)

### Supporting experiments
- `Entrophy toys/V4_main.cpp` — evolved implementation prototype
- `Entrophy toys/V4_output.txt` — sample output from the prototype run
- Additional `.jpg` and `.jpeg` assets document the symbolic visual exploration and design evolution

## Repository Contents

- `main.cpp` — primary implementation
- `output.csv` — generated result data
- `README.md` — project overview and repository documentation
- `readme.pdf` — packaged documentation artifact
- `Entrophy toys/` — exploratory visual and prototype assets
- `LICENSE` — MIT license

## Getting Started

1. Clone the repository
2. Build the project using the configured C++ toolchain
3. Run the main program on Brent crude data
4. Inspect generated transition states and entropy-based signal output
5. Review the visual experiments in `Entrophy toys` for conceptual context

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Notes

This project is intended as a research and trading-system prototype with emphasis on:
- statistical rigor
- symbolic abstraction
- information-theoretic filtering
- market-structure discovery

It is designed to explore the idea that pricing behavior can be modeled as a language-like system, where uncertainty and recurrence become measurable signals.
