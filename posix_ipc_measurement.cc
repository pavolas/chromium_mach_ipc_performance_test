// This program measures the time it takes to send a message to and from
// another process with posix pipes. The message size is determined by the
// first parameter passed to the program.
// Compile with: clang++ -std=c++11 -stdlib=libc++ -o posix_ipc_measurement posix_ipc_measurement.cc
// Example usage: ./posix_ipc_measurement 2000
// Output: 12741 (exit code 0) indicates that it took 12741 nanoseconds to
// pass a 2000 byte message.


#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include "measurement_common.h"

// The amount of data to read at a time from the posix pipe.
int kChromeReadBufferSize = 4 * 1024;

// Reads |size| bytes from |fd|. After the first set of bytes is read,
// allocates sufficient memory to hold the entire message. Returns a pointer to
// the received message.
char* ReceiveMessage(int size, int fd) {
  // The amount of data to read at a time from the posix pipe.
  char read_buffer[kChromeReadBufferSize];
  int total_read_amount = 0;
  int nbytes = 0;
  char* receive_message_buffer = NULL;
  while (total_read_amount < size) {
    nbytes = read(fd, read_buffer, kChromeReadBufferSize);

    if (receive_message_buffer == NULL) {
      // Allocate space for the message.
      receive_message_buffer = (char*)malloc(size);
    }

    memcpy(receive_message_buffer + total_read_amount, read_buffer, nbytes);
    total_read_amount += nbytes;
  }
  return receive_message_buffer;
}

int main(int argc, char* argv[]) {
  assert(argc == 2);
  int message_size = atoi(argv[1]);

  char* msg1 = GenerateMessage(message_size);

  int parent_send_fd[2], child_send_fd[2];
  pid_t childpid;

  pipe(parent_send_fd);
  pipe(child_send_fd);

  if ((childpid = fork()) == -1) {
    perror("fork");
    exit(1);
  }

  if (childpid == 0) {
    // The child echoes 2 messages that are sent to it.
    for (int i = 0; i < 2; ++i) {
      char* received_message = ReceiveMessage(message_size, parent_send_fd[0]);
      write(child_send_fd[1], received_message, message_size);
    }
  } else {
    // Send and receive a message as warm up.
    write(parent_send_fd[1], msg1, message_size);
    char* received_message = ReceiveMessage(message_size, child_send_fd[0]);
    assert(VerifyMessage(received_message, message_size));

    // Send and receive a message, and measure the interval.
    auto start_time = std::chrono::high_resolution_clock::now();
    write(parent_send_fd[1], msg1, message_size);
    received_message = ReceiveMessage(message_size, child_send_fd[0]);
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::nanoseconds elapsed_seconds = end_time - start_time;
    assert(VerifyMessage(received_message, message_size));

    // Divide by 2 to account for sending, and then receiving the exact same
    // message.
    elapsed_seconds /= 2;

    // Print the result and exit.
    printf("%lld\n", elapsed_seconds.count());
  }

  return (0);
}
