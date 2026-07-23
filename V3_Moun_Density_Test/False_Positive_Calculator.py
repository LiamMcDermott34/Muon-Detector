

filename = "Test_DATA.CSV"

# Time between each row (milliseconds)
sample_interval_ms = 60000

# Coincidence window (milliseconds)
coincidence_window_ms = 0.05


dt = sample_interval_ms / 1000.0
tau = coincidence_window_ms / 1000.0

file = open(filename, "r")

header = file.readline()

previous_d1 = 0
previous_d2 = 0
previous_coin = 0

measured_coincidences = 0
expected_false = 0.0
rows = 0

for line in file:

    line = line.strip()

    if line == "":
        continue

    parts = line.split(",")

    d1 = int(parts[1])
    d2 = int(parts[2])
    coin = int(parts[3])

    d1_interval = d1 - previous_d1
    d2_interval = d2 - previous_d2
    coin_interval = coin - previous_coin

    previous_d1 = d1
    previous_d2 = d2
    previous_coin = coin

    r1 = d1_interval / dt
    r2 = d2_interval / dt

    false_this_interval = r1 * r2 * tau * dt

    expected_false += false_this_interval
    measured_coincidences += coin_interval

    rows += 1

file.close()

measurement_time = rows * dt
estimated_real = measured_coincidences - expected_false

print("================================")
print("Measurement time:", measurement_time, "seconds")
print("Measured coincidences:", measured_coincidences)
print("Expected false coincidences:", round(expected_false, 3))
print("Estimated real coincidences:", round(estimated_real, 3))

if measured_coincidences > 0:
    percent = expected_false / measured_coincidences * 100
    print("False positive percentage:", round(percent, 2), "%")
else:
    print("No coincidences detected.")
