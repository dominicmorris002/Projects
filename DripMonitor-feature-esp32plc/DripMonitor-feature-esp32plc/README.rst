.. _drip_monitor:

DripMonitor
###########

Overview
********

A smart irrigation monitoring system for the Pinnacle 100 DVK board.

Requirements
************

* Zephyr RTOS v3.7.1
* Pinnacle 100 DVK board

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :host-os: unix
   :board: qemu_x86
   :goals: run
   :compact:

To build for another board, change "qemu_x86" above to that board's name.

Sample Output
=============

.. code-block:: console

    Hello World! x86

Exit QEMU by pressing :kbd:`CTRL+A` :kbd:`x`.
