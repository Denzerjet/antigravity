CXX = clang++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -Iheaders

all: build/receiver build/sender build/compare

build/receiver: cpp/receiver.cpp headers/stb_image_write.h
	$(CXX) $(CXXFLAGS) cpp/receiver.cpp -o build/receiver

build/sender: cpp/sender.cpp headers/stb_image.h
	$(CXX) $(CXXFLAGS) cpp/sender.cpp -o build/sender

build/compare: cpp/compare.cpp headers/stb_image.h
	$(CXX) $(CXXFLAGS) cpp/compare.cpp -o build/compare

clean:
	rm -f build/receiver build/sender build/compare images/output_image.png
