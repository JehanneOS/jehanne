DSApriv*getdsakey(int argc, char **argv, int needprivate, Attr **pa);
RSApriv*getrsakey(int argc, char **argv, int needprivate, Attr **pa);
uint8_t*	put4(uint8_t *p, uint32_t n);
uint8_t*	putmp2(uint8_t *p, mpint *b);
uint8_t*	putn(uint8_t *p, void *v, uint32_t n);
uint8_t*	putstr(uint8_t *p, char *s);
