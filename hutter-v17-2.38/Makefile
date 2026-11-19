CXX = g++
CXXFLAGS = -O3 -std=c++17 -march=native -Wall -Wextra

TARGET = cube_compressor_step6
SRCS = block6_main.cpp
HDRS = block1_cube_hasher.hpp block2_context_and_match.hpp block3_mixer_and_sse.hpp block4_arithmetic_coder.hpp block5_word_preprocessor.hpp

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)
