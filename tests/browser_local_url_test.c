#include "browser_local_url.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *accepted[] = {
        "http://127.0.0.1/", "127.0.0.1", "localhost", "LOCALHOST/", "HTTP://LocalHost:80/",
        "http://172.31.42.1/", "http://192.168.1.43", "192.168.1.43:80/",
        " http://localhost/?music=1#songs \r\n", "http://localhost#music"
    };
    const char *rejected[] = {
        "", "http://", "https://localhost/", "ftp://localhost/", "http://localhost:8080/",
        "http://localhost:800/", "http://localhost:80evil", "http://localhost.evil/",
        "http://127.0.0.1@evil/", "http://evil@localhost/", "http://localhost/api/status",
        "http://localhost//", "http://localhost/%2F", "http://0.0.0.0/", "http://example.com/",
        "http://192.168.1.44/", "http://172.31.42.10/", "http://local host/",
        "http://localhost\\evil/", "http://%6cocalhost/", "https://172.31.42.1/"
    };
    for (unsigned i = 0; i < sizeof(accepted)/sizeof(*accepted); ++i)
        assert(browser_is_local_music_url(accepted[i], "172.31.42.1", "192.168.1.43"));
    for (unsigned i = 0; i < sizeof(rejected)/sizeof(*rejected); ++i)
        assert(!browser_is_local_music_url(rejected[i], "172.31.42.1", "192.168.1.43"));
    assert(!browser_is_local_music_url(NULL, NULL, NULL));
    assert(browser_is_local_music_url("localhost", NULL, NULL));
    assert(!browser_is_local_music_url("0.0.0.0", "0.0.0.0", ""));
    char long_url[450]; memset(long_url, 'x', sizeof(long_url));
    memcpy(long_url, "localhost/?", 11); long_url[sizeof(long_url)-1] = 0;
    assert(!browser_is_local_music_url(long_url, NULL, NULL));
    puts("local music URL routing: PASS (35 boundary cases)");
    return 0;
}
