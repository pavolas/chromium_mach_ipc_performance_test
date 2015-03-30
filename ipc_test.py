# This script runs many measurements of POSIX and Mach IPC, and prints the
# results.

import os
import subprocess

MACH_DEFAULT_BUFFER_SIZE = 4096

def run_mach_measurement(message_size):
  dir = os.path.dirname(os.path.realpath(__file__))
  path = dir + "/" + "mach_ipc_measurement"
  command = path + " " +  str(message_size) + " " + str(MACH_DEFAULT_BUFFER_SIZE)
  return run_measurement(command)

def run_posix_measurement(message_size):
  program_name = "posix_ipc_measurement"
  dir = os.path.dirname(os.path.realpath(__file__))
  path = dir + "/" + program_name
  command = path + " " +  str(message_size)
  return run_measurement(command)

def run_measurement(command):
  """Returns the time in microseconds that it takes to send and receive a
  message with size |message_size|."""
  try_count = 3
  for i in range(try_count):
    child = subprocess.Popen(command, stdout=subprocess.PIPE, shell=True)
    child.wait()
    rc = child.returncode
    if rc != 0:
      if i < try_count - 1:
        continue
      raise Exception("process failed with exit code: " + str(rc))

    result = child.stdout.readline()
    return int(result) / 1000


def run_measurements(packet_sizes, measurement_func):
  """Runs many measurements for each desired packet size."""
  results = {}
  for packet_size in packet_sizes:
    print 'measuring packet size: ' + str(packet_size)
    measurements = []
    sample_size = 201
    median_sample = sample_size / 2
    for _ in range(sample_size):
      measurement = measurement_func(packet_size)
      measurements.append(measurement)
    measurements.sort()

    median = measurements[median_sample]
    results[packet_size] = median
  return results

packet_sizes_to_measure = [100, 200, 500, 1024, 2048, 3072, 4096, 5120, 6144, 7168, 8192, 9216, 20000, 40000, 80000, 100000, 200000, 400000, 1048576, 4194304]

posix_results = run_measurements(packet_sizes_to_measure, run_posix_measurement)
mach_results = run_measurements(packet_sizes_to_measure, run_mach_measurement)

print "posix measurements"
print "[packet size]     [median latency]"
for key in packet_sizes_to_measure:
  print str(key) + "              " + str(posix_results[key])

print "mach measurements"
print "[packet size]     [median latency]"
for key in packet_sizes_to_measure:
  print str(key) + "              " + str(mach_results[key])
