%{

#include	<u.h>
#include	<libc.h>
#include	<bio.h>
#include 	<mp.h>

typedef struct Sym Sym;
typedef struct Node Node;

enum {
	FSET	= 1,
	FUSE	= 2,
	FARG	= 4,
	FLOC	= 8,
};

struct Sym
{
	Sym*	l;
	int	f;
	char	n[];
};

struct Node
{
	int	c;
	Node*	l;
	Node*	r;
	Sym*	s;
	mpint*	m;
	int	n;
};

#pragma	varargck type "N" Node*

int	ntmp;
Node	*ftmps, *atmps;
Node	*modulo;

Node*	new(int, Node*, Node*);
Sym*	sym(char*);

Biobuf	bin;
int	goteof;
int	lineno;
int	clevel;
char*	filename;

int	getch(void);
void	ungetc(void);
void	yyerror(char*);
int	yyparse(void);
void	diag(Node*, char*, ...);
void	com(Node*);
void	fcom(Node*,Node*,Node*);

int	yylex(void);

#pragma varargck argpos cprint 1
#pragma varargck argpos diag 2

%}

%union
{
	Sym*	sval;
	Node*	node;
	long	lval;
}

%type	<node>	name num args expr bool block elif stmnt stmnts

%left	'{' '}' ';'
%right	'=' ','
%right	'?' ':'
%left	EQ NEQ '<' '>'
%left	LSH RSH
%left	'+' '-'
%left	'/' '%'
%left	'*'
%left	'^'
%right	'('

%token	<lval>	MOD IF ELSE WHILE BREAK
%token	<sval>	NAME NUM

%%

prog:
	prog func
|	func

func:
	name args stmnt
	{
		fcom($1, $2, $3);
	}

args:
	'(' expr ')'
	{
		$$ = $2;
	}
|	'(' ')'
	{
		$$ = nil;
	}

name:
	NAME
	{
		$$ = new(NAME,nil,nil);
		$$->s = $1;
	}
num:
	NUM
	{
		$$ = new(NUM,nil,nil);
		$$->s = $1;
	}

elif:
	ELSE IF '(' bool ')' stmnt
	{
		$$ = new('?', $4, new(':', $6, nil));
	}
|	ELSE IF '(' bool ')' stmnt elif
	{
		$$ = new('?', $4, new(':', $6, $7));
	}
|	ELSE stmnt
	{
		$$ = $2;
	}

sem:
	sem ';'
|	';'

stmnt:
	expr '=' expr sem
	{
		$$ = new('=', $1, $3);
	}
|	MOD args stmnt
	{
		$$ = new('m', $2, $3);
	}
|	IF '(' bool ')' stmnt
	{
		$$ = new('?', $3, new(':', $5, nil));
	}
|	IF '(' bool ')' stmnt elif
	{
		$$ = new('?', $3, new(':', $5, $6));
	}
|	WHILE '(' bool ')' stmnt
	{
		$$ = new('@', new('?', $3, new(':', $5, new('b', nil, nil))), nil);
	}
|	BREAK sem
	{
		$$ = new('b', nil, nil);
	}
|	expr sem
	{
		if($1->c == NAME)
			$$ = new('e', $1, nil);
		else
			$$ = $1;
	}
|	block

block:
	'{' stmnts '}'
	{
		$$ = $2;
	}

stmnts:
	stmnts stmnt
	{
		$$ = new('\n', $1, $2);
	}
|	stmnt

expr:
	'(' expr ')'
	{
		$$ = $2;
	}
|	name
	{
		$$ = $1;
	}
|	num
	{
		$$ = $1;
	}
|	'-' expr
	{
		$$ = new(NUM, nil, nil);
		$$->s = sym("0");
		$$->s->f = 0;
		$$ = new('-', $$, $2);
	}
|	expr ',' expr
	{
		$$ = new(',', $1, $3);
	}
|	expr '^' expr
	{
		$$ = new('^', $1, $3);
	}
|	expr '*' expr
	{
		$$ = new('*', $1, $3);
	}
|	expr '/' expr
	{
		$$ = new('/', $1, $3);
	}
|	expr '%' expr
	{
		$$ = new('%', $1, $3);
	}
|	expr '+' expr
	{
		$$ = new('+', $1, $3);
	}
|	expr '-' expr
	{
		$$ = new('-', $1, $3);
	}
|	bool '?' expr ':' expr
	{
		$$ = new('?', $1, new(':', $3, $5));
	}
|	name args
	{
		$$ = new('e', $1, $2);
	}
|	expr LSH expr
	{
		$$ = new(LSH, $1, $3);
	}
|	expr RSH expr
	{
		$$ = new(RSH, $1, $3);
	}

bool:
	'(' bool ')'
	{
		$$ = $2;
	}
|	'!' bool
	{
		$$ = new('!', $2, nil);
	}
|	expr EQ expr
	{
		$$ = new(EQ, $1, $3);
	}
|	expr NEQ expr
	{
		$$ = new('!', new(EQ, $1, $3), nil);
	}
|	expr '>' expr
	{
		$$ = new('>', $1, $3);
	}
|	expr '<' expr
	{
		$$ = new('<', $1, $3);
	}

%%

int
yylex(void)
{
	static char buf[200];
	char *p;
	int c;

Loop:
	c = getch();
	switch(c){
	case -1:
		return -1;
	case ' ':
	case '\t':
	case '\n':
		goto Loop;
	case '#':
		while((c = getch()) > 0)
			if(c == '\n')
				break;
		goto Loop;
	}

	switch(c){
	case '?': case ':':
	case '+': case '-':
	case '*': case '^':
	case '/': case '%':
	case '{': case '}':
	case '(': case ')':
	case ',': case ';':
		return c;
	case '<':
		if(getch() == '<') return LSH;
		ungetc();
		return '<';
	case '>':
		if(getch() == '>') return RSH;
		ungetc();
		return '>';
	case '=':
		if(getch() == '=') return EQ;
		ungetc();
		return '=';
	case '!':
		if(getch() == '=') return NEQ;
		ungetc();
		return '!';
	}

	ungetc();
	p = buf;
	for(;;){
		c = getch();
		if((c >= Runeself)
		|| (c == '_')
		|| (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| (c >= '0' && c <= '9')){
			*p++ = c;
			continue;
		}
		ungetc();
		break;
	}
	*p = '\0';

	if(strcmp(buf, "mod") == 0)
		return MOD;
	if(strcmp(buf, "if") == 0)
		return IF;
	if(strcmp(buf, "else") == 0)
		return ELSE;
	if(strcmp(buf, "while") == 0)
		return WHILE;
	if(strcmp(buf, "break") == 0)
		return BREAK;

	yylval.sval = sym(buf);
	yylval.sval->f = 0;
	return (buf[0] >= '0' && buf[0] <= '9') ? NUM : NAME;
}


int
getch(void)
{
	int c;

	c = Bgetc(&bin);
	if(c == Beof){
		goteof = 1;
		return -1;
	}
	if(c == '\n')
		lineno++;
	return c;
}

void
ungetc(void)
{
	Bungetc(&bin);
}

Node*
new(int c, Node *l, Node *r)
{
	Node *n;

	n = malloc(sizeof(Node));
	n->c = c;
	n->l = l;
	n->r = r;
	n->s = nil;
	n->m = nil;
	n->n = lineno;
	return n;
}

Sym*
sym(char *n)
{
	static Sym *tab[128];
	Sym *s;
	uint32_t h, t;
	int i;

	h = 0;
	for(i=0; n[i] != '\0'; i++){
		t = h & 0xf8000000;
		h <<= 5;
		h ^= t>>27;
		h ^= (uint32_t)n[i];
	}
	h %= nelem(tab);
	for(s = tab[h]; s != nil; s = s->l)
		if(strcmp(s->n, n) == 0)
			return s;
	s = malloc(sizeof(Sym)+i+1);
	memmove(s->n, n, i+1);
	s->f = 0;
	s->l = tab[h];
	tab[h] = s;
	return s;
}

void
yyerror(char *s)
{
	fprint(2, "%s:%d: %s\n", filename, lineno, s);
	exits(s);
}
void
cprint(char *fmt, ...)
{
	static char buf[1024], tabs[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	char *p, *x;
	va_list a;

	va_start(a, fmt);
	vsnprint(buf, sizeof(buf), fmt, a);
	va_end(a);

	p = buf;
	while((x = strchr(p, '\n')) != nil){
		x++;
		write(1, p, x-p);
		p = &tabs[sizeof(tabs)-1 - clevel];
		if(*p != '\0')
			write(1, p, strlen(p));
		p = x;
	}
	if(*p != '\0')
		write(1, p, strlen(p));
}

Node*
alloctmp(void)
{
	Node *t;

	t = ftmps;
	if(t != nil)
		ftmps = t->l;
	else {
		char n[16];

		snprint(n, sizeof(n), "tmp%d", ++ntmp);
		t = new(NAME, nil, nil);
		t->s = sym(n);

		cprint("mpint *");
	}
	cprint("%N = mpnew(0);\n", t);
	t->s->f &= ~(FSET|FUSE);
	t->l = atmps;
	atmps = t;
	return t;
}

int
isconst(Node *n)
{
	if(n->c == NUM)
		return 1;
	if(n->c == NAME){
		return 	n->s == sym("mpzero") ||
			n->s == sym("mpone") ||
			n->s == sym("mptwo");
	}
	return 0;
}

int
istmp(Node *n)
{
	Node *l;

	if(n->c == NAME){
		for(l = atmps; l != nil; l = l->l){
			if(l->s == n->s)
				return 1;
		}
	}
	return 0;
}


void
freetmp(Node *t)
{
	Node **ll, *l;

	if(t == nil)
		return;
	if(t->c == ','){
		freetmp(t->l);
		freetmp(t->r);
		return;
	}
	if(t->c != NAME)
		return;

	ll = &atmps;
	for(l = atmps; l != nil; l = l->l){
		if(l == t){
			cprint("mpfree(%N);\n", t);
			*ll = t->l;
			t->l = ftmps;
			ftmps = t;
			return;
		}
		ll = &l->l;
	}
}

int
symref(Node *n, Sym *s)
{
	if(n == nil)
		return 0;
	if(n->c == NAME && n->s == s)
		return 1;
	return symref(n->l, s) || symref(n->r, s);
}

void
nodeset(Node *n)
{
	if(n == nil)
		return;
	if(n->c == NAME){
		n->s->f |= FSET;
		return;
	}
	if(n->c == ','){
		nodeset(n->l);
		nodeset(n->r);
	}
}

int
complex(Node *n)
{
	if(n->c == NAME)
		return 0;
	if(n->c == NUM && n->m->sign > 0 && mpcmp(n->m, mptwo) <= 0)
		return 0;
	return 1;
}

void
bcom(Node *n, Node *t);

Node*
ccom(Node *f)
{
	Node *l, *r;

	if(f == nil)
		return nil;

	if(f->m != nil)
		return f;
	f->m = (void*)~0;

	switch(f->c){
	case NUM:
		f->m = strtomp(f->s->n, nil, 0, nil);
		if(f->m == nil)
			diag(f, "bad constant");
		goto out;

	case LSH:
	case RSH:
		break;

	case '+':
	case '-':
	case '*':
	case '/':
	case '%':
	case '^':
		if(modulo == nil || modulo->c == NUM)
			break;

		/* wet floor */
	default:
		return f;
	}

	f->l = l = ccom(f->l);
	f->r = r = ccom(f->r);
	if(l == nil || r == nil || l->c != NUM || r->c != NUM)
		return f;

	f->m = mpnew(0);
	switch(f->c){
	case LSH:
	case RSH:
		if(mpsignif(r->m) > 32)
			diag(f, "bad shift");
		if(f->c == LSH)
			mpleft(l->m, mptoi(r->m), f->m);
		else
			mpright(l->m, mptoi(r->m), f->m);
		goto out;

	case '+':
		mpadd(l->m, r->m, f->m);
		break;
	case '-':
		mpsub(l->m, r->m, f->m);
		break;
	case '*':
		mpmul(l->m, r->m, f->m);
		break;
	case '/':
		if(modulo != nil){
			mpinvert(r->m, modulo->m, f->m);
			mpmul(f->m, l->m, f->m);
		} else {
			mpdiv(l->m, r->m, f->m, nil);
		}
		break;
	case '%':
		mpmod(l->m, r->m, f->m);
		break;
	case '^':
		mpexp(l->m, r->m, modulo != nil ? modulo->m : nil, f->m);
		goto out;
	}
	if(modulo != nil)
		mpmod(f->m, modulo->m, f->m);

out:
	f->l = nil;
	f->r = nil;
	f->s = nil;
	f->c = NUM;
	return f;
}

Node*
ecom(Node *f, Node *t)
{
	Node *l, *r, *t2;

	if(f == nil)
		return nil;

	f = ccom(f);
	if(f->c == NUM){
		if(f->m->sign < 0){
			f->m->sign = 1;
			t = ecom(f, t);
			f->m->sign = -1;
			if(isconst(t))
				t = ecom(t, alloctmp());
			cprint("%N->sign = -1;\n", t);
			return t;
		}
		if(mpcmp(f->m, mpzero) == 0){
			f->c = NAME;
			f->s = sym("mpzero");
			f->s->f = FSET;
			return ecom(f, t);
		}
		if(mpcmp(f->m, mpone) == 0){
			f->c = NAME;
			f->s = sym("mpone");
			f->s->f = FSET;
			return ecom(f, t);
		}
		if(mpcmp(f->m, mptwo) == 0){
			f->c = NAME;
			f->s = sym("mptwo");
			f->s->f = FSET;
			return ecom(f, t);
		}
	}

	if(f->c == ','){
		if(t != nil)
			diag(f, "cannot assign list to %N", t);
		f->l = ecom(f->l, nil);
		f->r = ecom(f->r, nil);
		return f;
	}

	l = r = nil;
	if(f->c == NAME){
		if((f->s->f & FSET) == 0)
			diag(f, "name used but not set");
		f->s->f |= FUSE;
		if(t == nil)
			return f;
		if(f->s != t->s)
			cprint("mpassign(%N, %N);\n", f, t);
		goto out;
	}

	if(t == nil)
		t = alloctmp();

	if(f->c == '?'){
		bcom(f, t);
		goto out;
	}

	if(f->c == 'e'){
		r = ecom(f->r, nil);
		if(r == nil)
			cprint("%N(%N);\n", f->l, t);
		else
			cprint("%N(%N, %N);\n", f->l, r, t);
		goto out;
	}

	if(t->c != NAME)
		diag(f, "destination %N not a name", t);

	switch(f->c){
	case NUM:
		if(mpsignif(f->m) <= 32)
			cprint("uitomp(%udUL, %N);\n", mptoui(f->m), t);
		else if(mpsignif(f->m) <= 64)
			cprint("uvtomp(%lludULL, %N);\n", mptouv(f->m), t);
		else
			cprint("strtomp(\"%.16B\", nil, 16, %N);\n", f->m, t);
		goto out;
	case LSH:
	case RSH:
		r = ccom(f->r);
		if(r == nil || r->c != NUM || mpsignif(r->m) > 32)
			diag(f, "bad shift");
		l = f->l->c == NAME ? f->l : ecom(f->l, t);
		if(f->c == LSH)
			cprint("mpleft(%N, %d, %N);\n", l, mptoi(r->m), t);
		else
			cprint("mpright(%N, %d, %N);\n", l, mptoi(r->m), t);
		goto out;
	case '*':
	case '/':
		l = ecom(f->l, nil);
		r = ecom(f->r, nil);
		break;
	default:
		l = ccom(f->l);
		r = ccom(f->r);
		l = ecom(l, complex(l) && !symref(r, t->s) ? t : nil);
		r = ecom(r, complex(r) && l->s != t->s ? t : nil);
		break;
	}


	if(modulo != nil){
		switch(f->c){
		case '+':
			cprint("mpmodadd(%N, %N, %N, %N);\n", l, r, modulo, t);
			goto out;
		case '-':
			cprint("mpmodsub(%N, %N, %N, %N);\n", l, r, modulo, t);
			goto out;
		case '*':
		Modmul:
			if(l->s == sym("mptwo") || r->s == sym("mptwo"))
				cprint("mpmodadd(%N, %N, %N, %N); // 2*%N\n",
					r->s == sym("mptwo") ? l : r,
					r->s == sym("mptwo") ? l : r,
					modulo, t,
					r);
			else
				cprint("mpmodmul(%N, %N, %N, %N);\n", l, r, modulo, t);
			goto out;
		case '/':
			if(l->s == sym("mpone")){
				cprint("mpinvert(%N, %N, %N);\n", r, modulo, t);
				goto out;
			}
			t2 = alloctmp();
			cprint("mpinvert(%N, %N, %N);\n", r, modulo, t2);
			cprint("mpmodmul(%N, %N, %N, %N);\n", l, t2, modulo, t);
			freetmp(t2);
			goto out;
		case '^':
			if(r->s == sym("mptwo")){
				r = l;
				goto Modmul;
			}
			cprint("mpexp(%N, %N, %N, %N);\n", l, r, modulo, t);
			goto out;
		}
	}

	switch(f->c){
	case '+':
		cprint("mpadd(%N, %N, %N);\n", l, r, t);
		goto out;
	case '-':
		if(l->s == sym("mpzero")){
			r = ecom(r, t);
			cprint("%N->sign = -%N->sign;\n", t, t);
		} else
			cprint("mpsub(%N, %N, %N);\n", l, r, t);
		goto out;
	case '*':
	Mul:
		if(l->s == sym("mptwo") || r->s == sym("mptwo"))
			cprint("mpleft(%N, 1, %N);\n", r->s == sym("mptwo") ? l : r, t);
		else
			cprint("mpmul(%N, %N, %N);\n", l, r, t);
		goto out;
	case '/':
		cprint("mpdiv(%N, %N, %N, %N);\n", l, r, t, nil);
		goto out;
	case '%':
		cprint("mpmod(%N, %N, %N);\n", l, r, t);
		goto out;
	case '^':
		if(r->s == sym("mptwo")){
			r = l;
			goto Mul;
		}
		cprint("mpexp(%N, %N, nil, %N);\n", l, r, t);
		goto out;
	default:
		diag(f, "unknown operation");
	}

out:
	if(l != t)
		freetmp(l);
	if(r != t)
		freetmp(r);
	nodeset(t);
	return t;
}

void
bcom(Node *n, Node *t)
{
	Node *f, *l, *r;
	int neg = 0;

	l = r = nil;
	f = n->l;
Loop:
	switch(f->c){
	case '!':
		neg = !neg;
		f = f->l;
		goto Loop;
	case '>':
	case '<':
	case EQ:
		l = ecom(f->l, nil);
		r = ecom(f->r, nil);
		if(t != nil) {
			Node *b1, *b2;

			b1 = ecom(n->r->l, nil);
			b2 = ecom(n->r->r, nil);
			cprint("mpsel(");

			if(l->s == r->s)
				cprint("0");
			else {
				if(f->c == '>')
					cprint("-");
				cprint("mpcmp(%N, %N)", l, r);
			}
			if(f->c == EQ)
				neg = !neg;
			else
				cprint(" >> (sizeof(int)*8-1)");

			cprint(", %N, %N, %N);\n", neg ? b2 : b1, neg ? b1 : b2, t);
			freetmp(b1);
			freetmp(b2);
		} else {
			cprint("if(");

			if(l->s == r->s)
				cprint("0");
			else
				cprint("mpcmp(%N, %N)", l, r);
			if(f->c == EQ)
				cprint(neg ? " != 0" : " == 0");
			else if(f->c == '>')
				cprint(neg ? " <= 0" : " > 0");
			else
				cprint(neg ? " >= 0" : " < 0");

			cprint(")");
			com(n->r);
		}
		break;
	default:
		diag(n, "saw %N in boolean expression", f);
	}
	freetmp(l);
	freetmp(r);
}

void
com(Node *n)
{
	Node *l, *r;

Loop:
	if(n != nil)
	switch(n->c){
	case '\n':
		com(n->l);
		n = n->r;
		goto Loop;
	case '?':
		bcom(n, nil);
		break;
	case 'b':
		for(l = atmps; l != nil; l = l->l)
			cprint("mpfree(%N);\n", l);
		cprint("break;\n");
		break;
	case '@':
		cprint("for(;;)");
	case ':':
		clevel++;
		cprint("{\n");
		l = ftmps;
		r = atmps;
		if(n->c == '@')
			atmps = nil;
		ftmps = nil;
		com(n->l);
		if(n->r != nil){
			cprint("}else{\n");
			ftmps = nil;
			com(n->r);
		}
		ftmps = l;
		atmps = r;
		clevel--;
		cprint("}\n");
		break;
	case 'm':
		l = modulo;
		modulo = ecom(n->l, nil);
		com(n->r);
		freetmp(modulo);
		modulo = l;
		break;
	case 'e':
		if(n->r == nil)
			cprint("%N();\n", n->l);
		else {
			r = ecom(n->r, nil);
			cprint("%N(%N);\n", n->l, r);
			freetmp(r);
		}
		break;
	case '=':
		ecom(n->r, n->l);
		break;
	}
}

Node*
flocs(Node *n, Node *r)
{
Loop:
	if(n != nil)
	switch(n->c){
	default:
		r = flocs(n->l, r);
		r = flocs(n->r, r);
		n = n->r;
		goto Loop;
	case '=':
		n = n->l;
		if(n == nil)
			diag(n, "lhs is nil");
		while(n->c == ','){
			n->c = '=';
			r = flocs(n, r);
			n->c = ',';
			n = n->r;
			if(n == nil)
				return r;
		}
		if(n->c == NAME && (n->s->f & (FARG|FLOC)) == 0){
			n->s->f = FLOC;
			return new(',', n, r);
		}
		break;
	}
	return r;
}

void
fcom(Node *f, Node *a, Node *b)
{
	Node *a0, *l0, *l;

	ntmp = 0;
	ftmps = atmps = modulo = nil;
	clevel = 1;
	cprint("void %N(", f);
	a0 = a;
	while(a != nil){
		if(a != a0)
			cprint(", ");
		l = a->c == NAME ? a : a->l;
		l->s->f = FARG|FSET;
		cprint("mpint *%N", l);
		a = a->r;
	}
	cprint("){\n");
	l0 = flocs(b, nil);
	for(a = l0; a != nil; a = a->r)
		cprint("mpint *%N = mpnew(0);\n", a->l);
	com(b);
	for(a = l0; a != nil; a = a->r)
		cprint("mpfree(%N);\n", a->l);
	clevel = 0;
	cprint("}\n");
}

void
diag(Node *n, char *fmt, ...)
{
	static char buf[1024];
	va_list a;

	va_start(a, fmt);
	vsnprint(buf, sizeof(buf), fmt, a);
	va_end(a);

	fprint(2, "%s:%d: for %N; %s\n", filename, n->n, n, buf);
	exits("error");
}

int
Nfmt(Fmt *f)
{
	Node *n = va_arg(f->args, Node*);

	if(n == nil)
		return fmtprint(f, "nil");

	if(n->c == ',')
		return fmtprint(f, "%N, %N", n->l, n->r);

	switch(n->c){
	case NUM:
		if(n->m != nil)
			return fmtprint(f, "%B", n->m);
		/* wet floor */
	case NAME:
		return fmtprint(f, "%s", n->s->n);
	case EQ:
		return fmtprint(f, "==");
	case IF:
		return fmtprint(f, "if");
	case ELSE:
		return fmtprint(f, "else");
	case MOD:
		return fmtprint(f, "mod");
	default:
		return fmtprint(f, "%c", (char)n->c);
	}
}

void
parse(int fd, char *file)
{
	Binit(&bin, fd, OREAD);
	filename = file;
	clevel = 0;
	lineno = 1;
	goteof = 0;
	while(!goteof)
		yyparse();
	Bterm(&bin);
}

void
usage(void)
{
	fprint(2, "%s [file ...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	fmtinstall('N', Nfmt);
	fmtinstall('B', mpfmt);

	ARGBEGIN {
	default:
		usage();
	} ARGEND;

	if(argc == 0){
		parse(0, "<stdin>");
		exits(nil);
	}
	while(*argv != nil){
		int fd;

		if((fd = open(*argv, OREAD)) < 0){
			fprint(2, "%s: %r\n", *argv);
			exits("error");
		}
		parse(fd, *argv);
		close(fd);
		argv++;
	}
	exits(nil);
}
