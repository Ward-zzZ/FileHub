#!/bin/bash

PID=$(pidof login_cgi)
if [ -n "$PID" ]; then
  echo "Killing existing login_cgi process (PID: $PID)"
  kill "$PID"
fi

# Compile login_cgi
g++ -std=c++17 -g login_cgi.cpp make_log.cpp mysql_util.cpp cgi_util.cpp -o login_cgi -lfcgi -lmysqlclient -lredis++

# Launch login_cgi using spawn-fcgi
spawn-fcgi -a 127.0.0.1 -p 10001 -f /home/ward/FileHub/src/login_cgi
