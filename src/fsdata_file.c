/**
 * @file fsdata_file.c
 * @author Arijit Sadhu (arijitsadhu@users.noreply.github.com)
 * @brief Adds the automatic redirect to home page in wifi access point mode
 * @version 0.1
 * @date 2024-03-05
 *
 * @copyright Copyright (c) 2024 Arijit Sadhu
 *
 */

#include "tmp_fsdata.c"

static const unsigned char data_404_html[] = "/404.html\0HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1\r\nServer: lwIP/pre-0.6 (http://www.sics.se/~adam/lwip/)\r\nContent-type: text/html\r\n\r\n";

static const unsigned char data_302_html[] = "/302.html\0HTTP/1.1 302 Found\r\nLocation: /\r\nServer: lwIP/pre-0.6 (http://www.sics.se/~adam/lwip/)\r\nContent-type: text/html\r\n\r\n";

const struct fsdata_file file_404_html[] = { { FS_ROOT, data_404_html, data_404_html + 10, sizeof(data_404_html) - 10, FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT } };

const struct fsdata_file file_302_html[] = { { file_404_html, data_302_html, data_302_html + 10, sizeof(data_302_html) - 10, FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT } };

#undef FS_ROOT

#define FS_ROOT file_302_html

#define NUMFILES FS_NUMFILES

#undef FS_NUMFILES

#define FS_NUMFILES (NUMFILES + 1)
