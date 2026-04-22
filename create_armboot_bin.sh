#!/bin/sh

##############################################################################################
#                                                                                            #
# Automated tool to create file ARMBOOT.BIN with BABELFISH included for use with BootMii     #
#                                                                                            #
# Eventually requires an unencrypted dump of boot2v4 as file "boot2v4.bin" in this directory #
#                                                                                            #
# Written (c) 2026 - nitr8                                                                   #
#                                                                                            #
##############################################################################################

gcc -s -Os -Wall -Wextra -o create_armboot_bin create_armboot_bin.c
exit 0

