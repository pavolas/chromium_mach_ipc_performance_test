# chromium_mach_ipc_performance_test

This project tests the performance of two different inter-process communication
channels on OSX: inline Mach messages and POSIX pipes.

## Measurement 1: Inline Mach Message.
### Variables: Size of initial message M, size of pre-constructed buffer K.
1. Process A starts with message M, a char* buffer.
2. Process A constructs a Mach Message, and copies M into the Mach Message.
3. Process A sends the Mach Message to a port owned by Process B.
4. Process B tries to read the Mach Message into a pre-constructed buffer of size K. If this buffer is too small, Process B allocates a new buffer K2.
5. Process B copies M out of K/K2 into a new buffer M2.
6. Process B frees K2, if K2 was allocated.
7. Process B ends with message M2, a char* buffer.

## Measurement 2: POSIX pipe.
### Variables: Size of initial message M.
### Constants: The number of bytes to read from the pipe, L. A Chromium constant set to 4096.
1. Process A starts with message M, a char* buffer.
2. Process A writes the message into a POSIX pipe.
3. Process B knows the size of the message being sent, and allocates a buffer M2.
4. Process B reads from the pipe L-bytes at a time.
5. Process B ends with the message M2, a char* buffer.

It is difficult to synchronize a point in time in Process B against a point in
time in Process A. In the actual experiment, as soon as Process B receives the
message M2, it sends it back to Process A. Process A records the time before
sending message M, and after receiving message M2. This recorded time is
exactly twice the time we are attempting to measure.

Compile mach_ipc_measurement.cc:
  clang++ -std=c++11 -stdlib=libc++ -o mach_ipc_measurement mach_ipc_measurement.cc

Compile posix_ipc_measurement.cc:
  clang++ -std=c++11 -stdlib=libc++ -o posix_ipc_measurement posix_ipc_measurement.cc

Run the python script.
  python ipc_test.py

