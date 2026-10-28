# PB2 multitool runtime architecture

## Engines

The three instruments are independent. There is no global mode switch.

| Engine | Owner | Firmware or driver | Data path |
| --- | --- | --- | --- |
| Logic analyzer | PRU0 | `am62x-pru0-fw` | finite capture in PRU DMEM, RPMsg download |
| JTAG/SWD programmer | PRU1 | `am62x-pru1-fw` | native queued JTAG and SWD over RPMsg |
| Oscilloscope | Linux SPI2 | `ti-ads8688` IIO driver | IIO kfifo to shared mmap ring |

`multitoold` is the only process that owns RemoteProc and IIO configuration.
The local UI and Web service should connect to it rather than writing sysfs.

## PRU firmware

Both firmwares use protocol version 3 from `multitool_protocol.h` and expose
`rpmsg-raw` endpoints on ports 30 and 31.

PRU0 captures IO8..IO11 from R31. Its four data pads are configured as PRU0
GPI while the four level-translator direction pads remain PRU0 outputs. A
capture supports masked level or edge triggering, pre-trigger history and a
finite post-trigger region. Samples are packed into 16 bits in ascending
R31-bit order selected by `input_mask`. The initial DMEM implementation limits
a capture to 2048 samples. Bulk DDR transport is the next step when longer
digital captures are required.

PRU1 is the JTAG/SWD data plane. JTAG messages contain up to 400 logical pin
transitions and return packed TDO samples. SWD messages contain up to 32
complete DP/AP transfers; PRU1 generates the request, both turnaround phases,
ACK, 32-bit data, parity and idle clocks. Standard line-reset and JTAG/SWD/
dormant switching sequences are also executed entirely by PRU1.

Pin assignment, level-translator direction, safe output values and SWDIO
padconf values are supplied by `multitoold` when OpenOCD connects. The
half-period delay is updated by OpenOCD `adapter speed`. It is functional but
not cycle-calibrated and must be measured on hardware before a maximum clock
rate is advertised.

## Oscilloscope ring

The schematic connects the ADS8688A to Linux `main_spi2`; no MCU SPI is routed
to the PocketBeagle 2 headers. The DT therefore keeps the standard IIO driver
and raises the conservative SPI limit from 1 MHz to 16 MHz.

`multitoold` configures the IIO scan mask, trigger and kernel kfifo, then drains
`/dev/iio:deviceN` from a dedicated thread. Every read is copied into:

```text
/run/multitool/scope.ring
```

This file is a 16 MiB shared mmap ring (256 x 64 KiB blocks). The producer
never waits for UI or Web clients. Every block has a release-published sequence
number, payload length and `CLOCK_MONOTONIC_RAW` timestamp. A reader detects an
overrun when the sequence in its expected slot has advanced. No discontinuity
is silently hidden. The service also exports byte, short-read and read-error
counters through `STATUS`.

Example control socket commands:

```sh
printf 'STATUS\n' | nc -U /run/multitool/multitoold.sock
printf 'PRU START\n' | nc -U /run/multitool/multitoold.sock
printf 'SCOPE START ads8688 multitool-scope 01 262144\n' | \
    nc -U /run/multitool/multitoold.sock
printf 'SCOPE STOP\n' | nc -U /run/multitool/multitoold.sock
```

The channel mask is hexadecimal (`01` selects channel 0). The trigger must
already exist in IIO; `-` leaves the current trigger unchanged.

## Important ADS8688A limitation

The Linux 6.12 `ti-ads8688` driver has an IIO triggered buffer, but its handler
performs a separate synchronous SPI transaction for each enabled channel.
The large kernel kfifo and mmap ring protect against userspace scheduling
latency, but they cannot fix missed conversion instants caused before samples
enter the IIO buffer. Therefore the current implementation is suitable for
bring-up and moderate sample rates, not a guaranteed 500 kSPS oscilloscope.

The lossless high-rate path should be implemented in the kernel as follows:

1. Configure the ADS8688A `AUTO_RST` channel sequence.
2. Run McSPI at up to 16 MHz with 32 bits per word.
3. Use the AM62 McSPI `cs_per_word` mode so every word produces the required
   CS rising/falling frame boundary.
4. Transfer large blocks (at least 160 bytes) so `spi-omap2-mcspi` selects DMA.
5. Chain two or more DMA-backed blocks and publish them to an IIO DMA buffer
   or a mmap-able driver ring.
6. Surface hardware overruns and sequence gaps to `multitoold`.

At 500 kSPS the raw traffic is 2 MB/s because every conversion occupies a
32-bit SPI frame. The memory bandwidth is easy for AM62; per-frame software
overhead and correct CS timing are the actual constraints.

## Safety and next work

- Confirm level-translator direction polarity and SWDIO pad switching on hardware.
- Measure actual TCK/SWCLK against `adapter speed` and calibrate the PRU delay loop.
- Add a kernel DDR capture transport for long PRU0 captures.
- Add a common hardware timestamp/cross-trigger signal between PRU captures
  and ADS8688A acquisitions.

## Native OpenOCD adapter and UI

The patched OpenOCD adapter `pb2_pru` supports both `jtag` and `swd`.
It uses a framed binary protocol over `/run/multitool/openocd.sock`.
`multitoold` remains the only RemoteProc owner, validates the runtime pin
profile and forwards bounded messages to PRU1. This keeps target, DAP and flash
algorithms in upstream OpenOCD while moving all time-critical signaling out of
Linux.

For JTAG the driver plugs into OpenOCD's standard bitbang queue, but buffers
logical transitions and TDO requests before sending them. For SWD it implements
the native `swd_driver` queue and returns OpenOCD ACK, read data and parity
status per transfer. WAIT and FAULT terminate the current PRU batch so OpenOCD
can perform its normal recovery/retry logic.

SWDIO uses distinct AM62 mux modes for PRU GPO and GPI. PRU1 first isolates the
external 74LVC1T45, changes the validated MAIN_PADCONFIG register, and then
enables the new direction. Pad writes are limited to aligned addresses inside
the AM62 MAIN_PADCONFIG window.

The local UI is a C++ Slint component rendered directly through LinuxKMS with
the software renderer. Its three pages call the same `multitoold` socket API
that a future Web backend will use; no hardware ownership is duplicated in
the UI. The `.slint` file and C++ application are also a normal desktop CMake
project, so layout work can be done on a PC.
