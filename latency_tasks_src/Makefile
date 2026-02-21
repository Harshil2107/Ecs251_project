CXX      = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
TARGET   = static_thread_pool
SRC      = static_thread_pool.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) task_queue.h latency_tasks.cpp
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
