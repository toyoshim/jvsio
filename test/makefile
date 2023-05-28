CXXFLAGS	= -std=c++17 -Igoogletest/googletest/include -I.. -g
CFLAGS		= -I.. -D__TEST__ -g
LFLAGS		= -Lout/lib -lgtest -lgtest_main -lpthread
LIBGTEST	= out/lib/libgtest.a

node_test: ${LIBGTEST} node_test.o jvsio_node.o
	clang++ -o $@ node_test.o jvsio_node.o ${LFLAGS}

host_test: ${LIBGTEST} host_test.o jvsio_host.o
	clang++ -o $@ host_test.o jvsio_host.o ${LFLAGS}

dist-clean:
	rm -rf out *.o test host_test

clean:
	rm -rf *.o node_test host_test

%.o: ../%.c ../*.h
	clang -c ${CFLAGS} -o $@ $<

%.o: %.cc *.h
	clang++ -c ${CXXFLAGS} -o $@ $<

${LIBGTEST}:
	(cd googletest && cmake . -B ../out && cd ../out && make)
