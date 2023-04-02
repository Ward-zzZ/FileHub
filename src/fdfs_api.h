#ifndef _FDFS_API_H
#define _FDFS_API_H

const char *const FDFSAPI_LOG_MODULE = "cgi";
const char *const FDFSAPI_LOG_PROC = "fdfs_api";

int fdfs_upload_file(const char *conf_file, const char *myfile, char *file_id);

#endif
