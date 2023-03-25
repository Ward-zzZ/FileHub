#!/bin/bash

PID=$(pidof reg_cgi)
if [ -n "$PID" ]; then
  kill "$PID"
fi

# Compile reg_cgi
g++ -o reg_cgi reg_cgi.cpp make_log.cpp -lfcgi -lmysqlclient

# Launch reg_cgi using spawn-fcgi
spawn-fcgi -a 127.0.0.1 -p 10000 -f /home/ward/FileHub/src/reg_cgi
