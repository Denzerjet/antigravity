CXX = clang++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2

.PHONY: all clean

all: receiver sender

receiver: receiver.cpp
	$(CXX) $(CXXFLAGS) receiver.cpp -o receiver

sender: sender.cpp
	$(CXX) $(CXXFLAGS) sender.cpp -o sender

clean:
	rm -f receiver sender
