.. Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
.. SPDX-License-Identifier: BSD-3-Clause

.. zephyr:board:: qcc744m_evk

Overview
********

The Qualcomm® QCC744M EVK is available to evaluate the features and performance
of the respective Qualcomm® QCC744M tri-radio module. It allows you to prototype
demanding IoT applications requiring multiple connectivity options.

The QCC744M EVK enables developers, OEMs, and ODMs to design and test
industrial-grade IoT multi-radio solutions (Wi-Fi 6, qualified against
Bluetooth® Core 5.4 technology, Thread, and ZigBee-ready) using standard
interfaces and hostless mode.

The EVK consist of a main board with standard connectors that integrate the
respective QCC744M tri-radio module.

For more information, check `Qualcomm® QCC74xM Evaluation Kits`_.

Hardware
********

- Processor: 32-bit RISC-V @ 325MHz
- Onboard Memory: 128KB ROM, 484KB SRAM
- SiP Memory: 8MB pSRAM
- Wi-Fi Standard: 1x1 2.4GHz 802.11 b/g/n/ac/ax (Wi-Fi 6)
- Bluetooth Standard: Bluetooth Low Energy v5.4
- 802.15.4 Standard: Thread and ZigBee-ready
- Matter support: Matter over Wi-Fi and Thread supported, with BLE commissioning
- Operating Temperature: -40C to +85C
- EVK Input Power: USB or 5V
- Module Antenna Config: PCB antenna or RF Connector
- Module onboard Flash: 8MB QSPI Flash
- Ethernet: RMII (via GPIO)
- Audio Codec: 1x DAC (Speaker) / 1x ADC (Mic) via GPIO
- Video Codec: MJPEG Encoding
- Camera Interface: DVP (via GPIO)
- Display Interface: DBI (via GPIO)
- GPIO: 35 (7x7 QFN)
- SDIO: SDIO 2.0 Secondary, SD Card Interface
- UART: 2x
- ISO 11898 (CAN Bus): 1x
- SPI: Primary/Secondary
- I2C: 2x
- I2S: Primary/Secondary
- PWM: 1x PWM (4 channels)
- QSPI: XiP QSPI (Flash)
- Timers: RTC, 2x 32-bit, 1x 16-bit
- IR Controller: Receiver
- ADC: General Purpose 12/14/16 bits
- DAC: General Purpose 12-bit 
- JTAG: via GPIO

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

Detailed information about board connections can be found in
`QCC744M Evaluation Kit Reference Schematic`_.

Programming and Debugging
*************************

Flashing
========

Build and flash applications as usual (see :ref:`build_an_application` and
:ref:`application_run` for more details).

Here is an example for the :zephyr:code-sample:`hello_world` application.

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: qcc744m_evk
   :goals: build flash
   :flash-args: --port <tty_device>

Run terminal program to listen for output (speed: 115200, stop bits: 1).

Replace :code:`<tty_device>` with the port where the QCC744M EVK can be found.
For example, under Linux, :code:`/dev/ttyACM0`.

Debugging
=========

The board has a High-Speed USB Converter Chip CH347 that can be used for
debugging. A custom OpenOCD is provided in
``modules/hal/qcom/tools/openocd/openocd``.

References
**********

.. target-notes::

.. _`Qualcomm® QCC74xM Evaluation Kits`: https://www.qualcomm.com/developer/hardware/qcc74xm-evk
.. _`QCC744M Evaluation Kit Reference Schematic`: https://docs.qualcomm.com/bundle/publicresource/80-78831-41_REV_AC_QCC744M_Evaluation_Kit_Reference_Schematic.pdf
