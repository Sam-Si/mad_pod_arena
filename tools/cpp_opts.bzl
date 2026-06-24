COMMON_COPTS = [
    "-std=c++17",
    "-O3",
    "-DNDEBUG",
    "-fno-math-errno",
    "-fomit-frame-pointer",
]

THREAD_COPTS = COMMON_COPTS + [
    "-pthread",
]

THREAD_LINKOPTS = [
    "-pthread",
]
