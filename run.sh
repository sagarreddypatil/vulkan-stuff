#!/bin/bash

set -e

cmake --build build
cd build
mangohud ./triangle