SHELL=/bin/bash

all: formatfs

formatfs:
	gcc -g -o formatfs formatfs.c softwaredisk.c filesystem.c

test0: formatfs
	gcc -g -o testfs0 ./tests/testfs0.c filesystem.c softwaredisk.c
test1: formatfs
	gcc -g -o testfs1 ./tests/testfs1.c filesystem.c softwaredisk.c
test2: formatfs
	gcc -g -o testfs2 ./tests/testfs2.c filesystem.c softwaredisk.c
test3: formatfs
	gcc -g -o testfs3 ./tests/testfs3.c filesystem.c softwaredisk.c
test4: formatfs
	gcc -g -o testfs4a ./tests/testfs4a.c filesystem.c softwaredisk.c
	gcc -g -o testfs4b ./tests/testfs4b.c filesystem.c softwaredisk.c
test5: formatfs
	gcc -g -o testfs5a ./tests/testfs5a.c filesystem.c softwaredisk.c
	gcc -g -o testfs5b ./tests/testfs5b.c filesystem.c softwaredisk.c

.PHONY: clean
clean:
	rm -f formatfs
	rm -f testfs*