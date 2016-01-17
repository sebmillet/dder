#!/bin/sh

../generic_multiple.sh "-password abcde -verbose -width 8" 5 "dder test RSA HEX and password to decrypt PEM" "rsa" "tmp-o" "exp" ".pem" ".txt" $1

