# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.10

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/laswer/my_code/ChatRoom/ChatRoom

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/laswer/my_code/ChatRoom/ChatRoom/build

# Include any dependencies generated for this target.
include src/client/CMakeFiles/ChatClient.dir/depend.make

# Include the progress variables for this target.
include src/client/CMakeFiles/ChatClient.dir/progress.make

# Include the compile flags for this target's objects.
include src/client/CMakeFiles/ChatClient.dir/flags.make

src/client/CMakeFiles/ChatClient.dir/client.cpp.o: src/client/CMakeFiles/ChatClient.dir/flags.make
src/client/CMakeFiles/ChatClient.dir/client.cpp.o: ../src/client/client.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/laswer/my_code/ChatRoom/ChatRoom/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object src/client/CMakeFiles/ChatClient.dir/client.cpp.o"
	cd /home/laswer/my_code/ChatRoom/ChatRoom/build/src/client && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/ChatClient.dir/client.cpp.o -c /home/laswer/my_code/ChatRoom/ChatRoom/src/client/client.cpp

src/client/CMakeFiles/ChatClient.dir/client.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/ChatClient.dir/client.cpp.i"
	cd /home/laswer/my_code/ChatRoom/ChatRoom/build/src/client && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/laswer/my_code/ChatRoom/ChatRoom/src/client/client.cpp > CMakeFiles/ChatClient.dir/client.cpp.i

src/client/CMakeFiles/ChatClient.dir/client.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/ChatClient.dir/client.cpp.s"
	cd /home/laswer/my_code/ChatRoom/ChatRoom/build/src/client && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/laswer/my_code/ChatRoom/ChatRoom/src/client/client.cpp -o CMakeFiles/ChatClient.dir/client.cpp.s

src/client/CMakeFiles/ChatClient.dir/client.cpp.o.requires:

.PHONY : src/client/CMakeFiles/ChatClient.dir/client.cpp.o.requires

src/client/CMakeFiles/ChatClient.dir/client.cpp.o.provides: src/client/CMakeFiles/ChatClient.dir/client.cpp.o.requires
	$(MAKE) -f src/client/CMakeFiles/ChatClient.dir/build.make src/client/CMakeFiles/ChatClient.dir/client.cpp.o.provides.build
.PHONY : src/client/CMakeFiles/ChatClient.dir/client.cpp.o.provides

src/client/CMakeFiles/ChatClient.dir/client.cpp.o.provides.build: src/client/CMakeFiles/ChatClient.dir/client.cpp.o


# Object files for target ChatClient
ChatClient_OBJECTS = \
"CMakeFiles/ChatClient.dir/client.cpp.o"

# External object files for target ChatClient
ChatClient_EXTERNAL_OBJECTS =

../bin/ChatClient: src/client/CMakeFiles/ChatClient.dir/client.cpp.o
../bin/ChatClient: src/client/CMakeFiles/ChatClient.dir/build.make
../bin/ChatClient: src/client/CMakeFiles/ChatClient.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/laswer/my_code/ChatRoom/ChatRoom/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable ../../../bin/ChatClient"
	cd /home/laswer/my_code/ChatRoom/ChatRoom/build/src/client && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/ChatClient.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
src/client/CMakeFiles/ChatClient.dir/build: ../bin/ChatClient

.PHONY : src/client/CMakeFiles/ChatClient.dir/build

src/client/CMakeFiles/ChatClient.dir/requires: src/client/CMakeFiles/ChatClient.dir/client.cpp.o.requires

.PHONY : src/client/CMakeFiles/ChatClient.dir/requires

src/client/CMakeFiles/ChatClient.dir/clean:
	cd /home/laswer/my_code/ChatRoom/ChatRoom/build/src/client && $(CMAKE_COMMAND) -P CMakeFiles/ChatClient.dir/cmake_clean.cmake
.PHONY : src/client/CMakeFiles/ChatClient.dir/clean

src/client/CMakeFiles/ChatClient.dir/depend:
	cd /home/laswer/my_code/ChatRoom/ChatRoom/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/laswer/my_code/ChatRoom/ChatRoom /home/laswer/my_code/ChatRoom/ChatRoom/src/client /home/laswer/my_code/ChatRoom/ChatRoom/build /home/laswer/my_code/ChatRoom/ChatRoom/build/src/client /home/laswer/my_code/ChatRoom/ChatRoom/build/src/client/CMakeFiles/ChatClient.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : src/client/CMakeFiles/ChatClient.dir/depend

