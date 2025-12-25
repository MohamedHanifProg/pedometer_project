Pedometer System – Microchip Curiosity Nano
Overview

This project implements a fully functional pedometer using the Microchip Curiosity Nano development board.
The system tracks user steps in real time using accelerometer data, displays activity on an OLED screen, and provides an interactive user interface including a clock, step history, and animated graphics.

The project demonstrates low-level driver development, sensor data processing, and real-time embedded system design.

Features

Real-time step counting using onboard accelerometer

Threshold-based step detection algorithm with noise filtering

Interactive OLED user interface

Step history tracking with smoothed graphical visualization

Clock display with date and time configuration

Animated foot icons and activity indication

Menu-driven navigation system

Hardware

Microchip Curiosity Nano Development Board

Onboard accelerometer sensor

OLED display

I2C and SPI communication interfaces

Software Architecture

The project is structured in a modular way to separate hardware drivers, application logic, and user interface components.

Key Modules

Accelerometer Driver (I2C)

Custom low-level I2C driver

Sensor configuration and data acquisition

OLED Display Driver (SPI)

Custom SPI driver

Graphics rendering and text display

Step Detection Algorithm

Threshold-based filtering

Motion noise reduction

Real-time step recognition

User Interface

Menu navigation

Clock and date/time settings

Step counter and history graph

Animated icons

Data Management

Step history storage

Smoothed graph generation for visual clarity

Step Detection Algorithm

Reads acceleration data from the accelerometer

Applies threshold-based filtering to detect valid steps

Ignores minor movements and noise

Updates step count only on valid step events

Detects user activity state (active / inactive)

Display & User Interface

OLED display shows:

Current step count

Animated walking icons

Real-time activity indication

Historical step graph

Clock and date

Interactive menu allows:

Navigation between screens

Viewing step history

Adjusting date and time

Communication Protocols

I2C

Used for accelerometer communication

Custom driver implementation

SPI

Used for OLED display communication

High-speed screen updates

Project Highlights

Low-level embedded driver development (I2C & SPI)

Real-time sensor data processing

Efficient graphical visualization on limited hardware

Clean modular firmware design

User-friendly embedded UI

Possible Future Improvements

Power optimization for battery operation

Non-volatile memory storage for long-term step history

Bluetooth connectivity for mobile integration

Adaptive step detection using dynamic thresholds

Author

Developed as an embedded systems project demonstrating real-time firmware design, hardware interfacing, and user interface development.
