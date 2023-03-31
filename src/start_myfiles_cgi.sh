#!/bin/bash

PID=$(pidof myfiles_cgi)
if [ -n "$PID" ]; then
  kill "$PID"
fi

# Compile reg_cgi
g++ -std=c++17 myfiles_cgi.cpp make_log.cpp mysql_util.cpp cgi_util.cpp -o myfiles_cgi -lfcgi -lmysqlclient -lredis++

# Launch reg_cgi using spawn-fcgi
spawn-fcgi -a 127.0.0.1 -p 10002 -f /home/ward/FileHub/src/myfiles_cgi
