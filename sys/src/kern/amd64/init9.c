extern void startboot(char*, char**);
typedef union NativeTypes
{
	char c;
	unsigned char uc;
	short s;
	unsigned short us;
	int i;
	unsigned int ui;
	long l;
	unsigned long ul;
	void* p;
} NativeTypes;
extern volatile NativeTypes* _sysargs;

void
main(char* argv0)
{
	/* TODO: why do we need this on GCC?
	 */
	char **argv;
	argv = &argv0;

	/* since crt0.s has not been included, we initialize _sysargs
	 * manually, keeping it on the stack
	 */
	NativeTypes __sysargs[6] = {
		{ .l = -1 },
		{ .l = -1 },
		{ .l = -1 },
		{ .l = -1 },
		{ .l = -1 },
		{ .l = -1 }
	};
	_sysargs = __sysargs;

	startboot(argv0, argv);
	_sysargs = (void*)0;
}
