#!/bin/bash

PID=$(pidof reg_cgi)
if [ -n "$PID" ]; then
  echo "Killing existing reg_cgi process (PID: $PID)"
  kill "$PID"
fi

# Compile reg_cgi
g++ -std=c++17 -g reg_cgi.cpp make_log.cpp mysql_util.cpp cgi_util.cpp -o reg_cgi -lfcgi -lmysqlclient -lredis++

# Launch reg_cgi using spawn-fcgi
spawn-fcgi -a 127.0.0.1 -p 10000 -f /home/ward/FileHub/src/reg_cgi
