#! /bin/sh
#
#              © 2022-2025 Nokia
#              Licensed under the BSD 3-Clause Clear License
#              SPDX-License-Identifier: BSD-3-Clause-Clear
#
sudo echo performance  | sudo tee /sys/devices/system/cpu/cpufreq/policy15/scaling_governor
sudo echo performance  | sudo tee /sys/devices/system/cpu/cpufreq/policy14/scaling_governor
sudo echo performance  | sudo tee /sys/devices/system/cpu/cpufreq/policy13/scaling_governor
sudo echo performance  | sudo tee /sys/devices/system/cpu/cpufreq/policy12/scaling_governor
sudo echo performance  | sudo tee /sys/devices/system/cpu/cpufreq/policy11/scaling_governor
sudo echo performance  | sudo tee /sys/devices/system/cpu/cpufreq/policy10/scaling_governor
sudo echo performance  | sudo tee /sys/devices/system/cpu/cpufreq/policy9/scaling_governor
sudo echo performance  | sudo tee /sys/devices/system/cpu/cpufreq/policy8/scaling_governor
