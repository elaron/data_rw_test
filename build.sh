#!/bin/sh

gcc -lpthread -D RANDOM_IDX_SHOW -D_GNU_SOURCE -ggdb3 data_rw_test.c
