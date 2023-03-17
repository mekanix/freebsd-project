all:
	${MAKE} ${MAKEFLAGS} -C kernel
	${MAKE} ${MAKEFLAGS} -C program

clean:
	${MAKE} ${MAKEFLAGS} -C kernel clean
	${MAKE} ${MAKEFLAGS} -C program clean
