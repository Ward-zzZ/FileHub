CC=g++
CFLAGS=-I /usr/include/fastdfs/ -I /usr/include/fastcommon/
LIBS=-lfdfsclient

uploadfile_test: fdfs_api.cpp uploadFile_test.cpp
	$(CC) -o uploadfile_test fdfs_api.cpp uploadFile_test.cpp $(CFLAGS) $(LIBS)
