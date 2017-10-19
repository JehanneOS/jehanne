#include <u.h>
#include <lib9.h>
#include <bio.h>
#include <ndb.h>

extern char* secureidcheck(char *user, char *response);

Ndb *db;

void
main(int argc, char **argv)
{
	Ndb *db2;

	if(argc!=2){
		fprint(2, "usage: %s pinsecurid\n", argv[0]);
		exits("usage");
	}

	db = ndbopen("/lib/ndb/auth");
	if(db == 0)
		syslog(0, "secstore", "no /lib/ndb/auth");
	db2 = ndbopen(0);
	if(db2 == 0)
		syslog(0, "secstore", "no /lib/ndb/local");
	db = ndbcat(db, db2);

	print("%s=%s\n", ENV_USER, getenv(ENV_USER));
	print("%s\n", secureidcheck(getenv(ENV_USER), argv[1]));
	exits(0);
}
