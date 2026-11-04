# tinynet 只依賴標準函式庫與 pthread，刻意不引入第三方套件，方便在任何 Linux / WSL 上建置。
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
