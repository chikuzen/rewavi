#define VERSION_MAJOR               0
#define VERSION_MINOR               04
#define VERSION_REVISION            0
#define VERSION_BUILD               0

#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

#define VERSION STRINGIZE(VERSION_MAJOR) "." STRINGIZE(VERSION_MINOR)
