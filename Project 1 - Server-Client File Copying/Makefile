# Define the symbols we might want to change:
CXX	:= g++
# CXXFLAGS := -g

OBJECTS	:= source/child.o source/main.o 
INLUDES := include/

main: $(OBJECTS)
	$(CXX) -g $(OBJECTS)  -o main -lpthread

child.o: include/shared_memory.hpp
	$(CXX) $(CXXFLAGS) -c child.cpp

main.o: include/shared_memory.hpp
	$(CXX) -g $(INCLUDES) $(CXXFLAGS) -c main.cpp -o sh_mem.o


clear:
	rm source/*.o
	rm child_*
	rm log_main.txt