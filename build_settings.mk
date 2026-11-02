# HLSDK linux/Makefile: x86, x87 floating point, no GNU unique symbols,
# no null-check deletion and no strict-aliasing assumptions. Keep the
# additional stack realignment required by the original Microsoft DLLs.
COMMON := -m32 -march=pentium-m -mfpmath=387 -O1 -g -fPIC \
          -fno-delete-null-pointer-checks -fno-gnu-unique -fno-strict-aliasing \
          -mstackrealign -mincoming-stack-boundary=2 -fno-omit-frame-pointer
