#!/bin/bash

PID=$(pidof upload_cgi)
if [ -n "$PID" ]; then
  echo "Killing existing upload_cgi process (PID: $PID)"
  kill "$PID"
fi

g++ -std=c++17 -g upload_cgi.cpp make_log.cpp mysql_util.cpp cgi_util.cpp  -o upload_cgi -lfcgi -lmysqlclient -lredis++ -lfastcommon -lm

spawn-fcgi -a 127.0.0.1 -p 10002 -f /home/ward/FileHub/src/upload_cgi
