CXXFLAGS	= -std=c++17 -Igoogletest/googletest/include -I.. -g
CFLAGS		= -I.. -D__TEST__ -g
LFLAGS		= -Lout/lib -lgtest -lgtest_main -lpthread
LIBGTEST	= out/lib/libgtest.a
OBJS			= test.o JVSIO_c.o

test: ${LIBGTEST} ${OBJS}
	clang++ -o test ${OBJS} ${LFLAGS}

clean:
	rm -rf out *.o test

%.o: ../%.c ../*.h
	clang -c ${CFLAGS} -o $@ $<

%.o: %.cc *.h
	clang++ -c ${CXXFLAGS} -o $@ $<

${LIBGTEST}:
	(cd googletest && cmake . -B ../out && cd ../out && make)
