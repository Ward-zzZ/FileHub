#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include "fdfs_client.h"
#include "logger.h"
#include "make_log.h"
#include "fdfs_api.h"

int fdfs_upload_file(const char *conf_file, const char *myfile, char *file_id) {
    char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
    ConnectionInfo *pTrackerServer;
    int result;
    int store_path_index;
    ConnectionInfo storageServer;

    // 通过客户端配置文件初始化一些数据
    if ((result = fdfs_client_init(conf_file)) != 0) {
        LOG_ERROR(FDFSAPI_LOG_MODULE, FDFSAPI_LOG_PROC, "fdfs_client_init error - %d", result);
        return result;
    }

    // 通过从配置文件中读出的数据, 连接追踪器tracker
    // 通过得到的地址可以访问追踪器
    pTrackerServer = tracker_get_connection();
    if (pTrackerServer == NULL) {
        fdfs_client_destroy();
        return errno != 0 ? errno : ECONNREFUSED;
    }

    *group_name = '\0';

    // 通过tracker得到存储节点信息
    if ((result = tracker_query_storage_store(pTrackerServer, \
                    &storageServer, group_name, &store_path_index)) != 0) {
        fdfs_client_destroy();
        LOG_ERROR(FDFSAPI_LOG_MODULE, FDFSAPI_LOG_PROC, "tracker_query_storage fail, error no: %d, error info: %s\n",
                  result, STRERROR(result));
        return result;
    }

    // 文件上传
    result = storage_upload_by_filename1(pTrackerServer, \
            &storageServer, store_path_index, \
            myfile, NULL, \
            NULL, 0, group_name, file_id);
    if (result == 0) {
        LOG_INFO(FDFSAPI_LOG_MODULE, FDFSAPI_LOG_PROC, "filename:%s,fileID: %s", myfile, file_id);
    } else {
        LOG_ERROR(FDFSAPI_LOG_MODULE, FDFSAPI_LOG_PROC, "upload file fail, error no: %d, error info: %s\n",
                  result, STRERROR(result));
    }

    tracker_disconnect_server_ex(pTrackerServer, true);
    fdfs_client_destroy();

    return result;
}

