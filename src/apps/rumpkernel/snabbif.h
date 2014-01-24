int rump_init(void);
int rump_init_server(const char *);

int snabbif_pull(const char *, void **, size_t *);
void snabbif_push(const char *, void *, size_t);
