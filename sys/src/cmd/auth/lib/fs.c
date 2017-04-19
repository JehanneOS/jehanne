#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

Fs fs[3] =
{
[Plan9]		{ "/mnt/keys", "plan 9 key", "/adm/keys.who", 0 },
[Securenet]	{ "/mnt/netkeys", "network access key", "/adm/netkeys.who", 0 },
};
